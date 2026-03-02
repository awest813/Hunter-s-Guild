#include "ShellCommands.hh"

#include <stdio.h>
#include <string.h>

#include <phosg/Random.hh>
#include <phosg/Strings.hh>

#include "ChatCommands.hh"
#include "GameServer.hh"
#include "ReceiveCommands.hh"
#include "ReplaySession.hh"
#include "SendCommands.hh"
#include "ServerState.hh"
#include "StaticGameData.hh"

using namespace std;

vector<const ShellCommand*>& ShellCommand::commands_by_order() {
  static auto* commands = new vector<const ShellCommand*>();
  return *commands;
}

unordered_map<string, const ShellCommand*>& ShellCommand::commands_by_name() {
  static auto* commands = new unordered_map<string, const ShellCommand*>();
  return *commands;
}

exit_shell::exit_shell() : runtime_error("shell exited") {}

shared_ptr<Client> ShellCommand::Args::get_client() const {
  if (!s->game_server) {
    throw logic_error("game server is missing");
  }

  shared_ptr<Client> c;
  if (this->session_name.empty()) {
    return this->s->game_server->get_client();
  } else {
    auto clients = this->s->game_server->get_clients_by_identifier(this->session_name);
    if (clients.empty()) {
      throw runtime_error("no such client");
    }
    if (clients.size() > 1) {
      throw runtime_error("multiple clients found");
    }
    return clients[0];
  }
}

shared_ptr<Client> ShellCommand::Args::get_proxy_client() const {
  auto c = this->get_client();
  if (!c->proxy_session) {
    throw runtime_error("client is not in a proxy session");
  }
  return c;
}

ShellCommand::ShellCommand(const char* name, const char* help_text, asio::awaitable<deque<string>> (*run)(Args&))
    : name(name), help_text(help_text), run(run) {
  ShellCommand::commands_by_order().emplace_back(this);
  ShellCommand::commands_by_name().emplace(this->name, this);
}

asio::awaitable<deque<string>> ShellCommand::dispatch_str(shared_ptr<ServerState> s, const string& command) {
  size_t command_end = phosg::skip_non_whitespace(command, 0);
  size_t args_begin = phosg::skip_whitespace(command, command_end);
  Args args;
  args.s = s;
  args.command = command.substr(0, command_end);
  args.args = command.substr(args_begin);
  co_return co_await ShellCommand::dispatch(args);
}

asio::awaitable<deque<string>> ShellCommand::dispatch(Args& args) {
  const ShellCommand* def = nullptr;
  try {
    def = commands_by_name().at(args.command);
  } catch (const out_of_range&) {
  }
  if (!def) {
    throw runtime_error("no such command; try 'help'");
  } else {
    return def->run(args);
  }
}

static asio::awaitable<deque<string>> empty_handler(ShellCommand::Args&) {
  co_return deque<string>();
}

ShellCommand c_nop1("", nullptr, empty_handler);
ShellCommand c_nop2("//", nullptr, empty_handler);
ShellCommand c_nop3("#", nullptr, empty_handler);

ShellCommand c_help(
    "help", "help\n\
    You\'re reading it now.",
    +[](ShellCommand::Args&) -> asio::awaitable<deque<string>> {
      deque<string> ret({"Commands:"});
      for (const auto& def : ShellCommand::commands_by_order()) {
        if (def->help_text) {
          // TODO: It's not great that we copy the text here.
          auto& s = ret.emplace_back("  ");
          s += def->help_text;
        }
      }
      co_return ret;
    });
ShellCommand c_exit(
    "exit", "exit (or ctrl+d)\n\
    Shut down the server.",
    +[](ShellCommand::Args&) -> asio::awaitable<deque<string>> {
      throw exit_shell();
    });
ShellCommand c_on(
    "on", "on SESSION COMMAND [ARGS...]\n\
    Run a command on a specific game server client or proxy server session.\n\
    Without this prefix, commands that affect a single client or session will\n\
    work only if there's exactly one connected client or open session. SESSION\n\
    may be a client ID (e.g. C-3), a player name, an account ID, an Xbox\n\
    gamertag, or a BB account username. For proxy commands, SESSION should be\n\
    the session ID, which generally is the same as the player\'s account ID\n\
    and appears after \"LinkedSession:\" in the log output.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      size_t session_name_end = phosg::skip_non_whitespace(args.args, 0);
      size_t command_begin = phosg::skip_whitespace(args.args, session_name_end);
      size_t command_end = phosg::skip_non_whitespace(args.args, command_begin);
      size_t args_begin = phosg::skip_whitespace(args.args, command_end);
      args.session_name = args.args.substr(0, session_name_end);
      args.command = args.args.substr(command_begin, command_end - command_begin);
      args.args = args.args.substr(args_begin);
      return ShellCommand::dispatch(args);
    });

ShellCommand c_reload(
    "reload", "reload ITEM [ITEM...]\n\
    Reload various parts of the server configuration. The items are:\n\
      accounts - reindex user accounts\n\
      battle-params - reload the BB enemy stats files\n\
      bb-keys - reload BB private keys\n\
      caches - clear all cached files\n\
      config - reload most fields from config.json\n\
      dol-files - reindex all DOL files\n\
      drop-tables - reload drop tables\n\
      ep3-cards - reload Episode 3 card definitions\n\
      ep3-maps - reload Episode 3 maps (not download quests)\n\
      ep3-tournaments - reload Episode 3 tournament state\n\
      functions - recompile all client-side patches and functions\n\
      item-definitions - reload item definitions files\n\
      item-name-index - regenerate item name list\n\
      level-tables - reload the player stats tables\n\
      patch-files - reindex the PC and BB patch directories\n\
      quests - reindex all quests (including Episode3 download quests)\n\
      set-tables - reload set data tables\n\
      teams - reindex all BB teams\n\
      text-index - reload in-game text\n\
      word-select - regenerate the Word Select translation table\n\
      all - do all of the above\n\
    Reloading will not affect items that are in use; for example, if an Episode\n\
    3 battle is in progress, it will continue to use the previous map and card\n\
    definitions until the battle ends. Similarly, BB clients are not forced to\n\
    disconnect or reload the battle parameters, so if these are changed without\n\
    restarting, clients may see (for example) EXP messages inconsistent with\n\
    the amounts of EXP actually received.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto types = phosg::split(args.args, ' ');
      for (const auto& type : types) {
        if (type == "all") {
          args.s->load_all(true);
        } else if (type == "bb-keys") {
          args.s->load_bb_private_keys();
        } else if (type == "accounts") {
          args.s->load_accounts();
        } else if (type == "maps") {
          args.s->load_maps();
        } else if (type == "caches") {
          args.s->clear_file_caches();
        } else if (type == "patch-files") {
          args.s->load_patch_indexes();
        } else if (type == "ep3-cards") {
          args.s->load_ep3_cards();
        } else if (type == "ep3-maps") {
          args.s->load_ep3_maps();
        } else if (type == "ep3-tournaments") {
          args.s->load_ep3_tournament_state();
        } else if (type == "functions") {
          args.s->compile_functions();
        } else if (type == "dol-files") {
          args.s->load_dol_files();
        } else if (type == "set-tables") {
          args.s->load_set_data_tables();
        } else if (type == "battle-params") {
          args.s->load_battle_params();
        } else if (type == "level-tables") {
          args.s->load_level_tables();
        } else if (type == "text-index") {
          args.s->load_text_index();
        } else if (type == "word-select") {
          args.s->load_word_select_table();
        } else if (type == "item-definitions") {
          args.s->load_item_definitions();
        } else if (type == "item-name-index") {
          args.s->load_item_name_indexes();
        } else if (type == "drop-tables") {
          args.s->load_drop_tables();
        } else if (type == "config") {
          args.s->load_config_early();
          args.s->load_config_late();
        } else if (type == "teams") {
          args.s->load_teams();
        } else if (type == "quests") {
          args.s->load_quest_index();
        } else {
          throw runtime_error("invalid data type: " + type);
        }
      }

      co_return deque<string>{};
    });

ShellCommand c_cc(
    "cc", "cc COMMAND\n\
    Execute a chat command as if a client had sent it in-game. The command\n\
    should be specified exactly as it would be typed in-game; for example:\n\
      cc $itemnotifs rare\n\
    This command cannot send chat messages to other players or to the server\n\
    (in proxy sessions); it can only execute chat commands. Chat commands run\n\
    via this command are exempt from permission checks, so commands that\n\
    require cheat mode or debug mode are always available via cc even if the\n\
    player cannot normamlly use them.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto c = args.get_client();
      co_await on_chat_command(c, args.args, false);
      co_return deque<string>{};
    });

asio::awaitable<deque<string>> f_sc_ss(ShellCommand::Args& args) {
  string data = phosg::parse_data_string(args.args, nullptr, phosg::ParseDataFlags::ALLOW_FILES);
  if (data.size() == 0) {
    throw invalid_argument("no data given");
  }
  data.resize((data.size() + 3) & (~3));

  auto c = args.get_client();
  if (args.command[1] == 's') {
    if (c->proxy_session) {
      send_command_with_header(c->proxy_session->server_channel, data.data(), data.size());
    } else {
      co_await on_command_with_header(c, data);
    }
  } else {
    send_command_with_header(c->channel, data.data(), data.size());
  }

  co_return deque<string>{};
}

ShellCommand c_sc("sc", "sc DATA\n\
    Send a network command to the client.",
    f_sc_ss);
ShellCommand c_ss("ss", "ss DATA\n\
    Send a network command to the server.",
    f_sc_ss);

ShellCommand c_show_slots(
    "show-slots", "show-slots\n\
    Show the player names, Guild Card numbers, and client IDs of all players in\n\
    the current lobby or game.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto c = args.get_proxy_client();

      deque<string> ret;
      for (size_t z = 0; z < c->proxy_session->lobby_players.size(); z++) {
        const auto& player = c->proxy_session->lobby_players[z];
        if (player.guild_card_number) {
          ret.emplace_back(format("  {}: {} => {} ({}, {}, {})",
              z, player.guild_card_number, player.name,
              char_for_language(player.language),
              name_for_char_class(player.char_class),
              name_for_section_id(player.section_id)));
        } else {
          ret.emplace_back(format("  {}: (no player)", z));
        }
      }
      co_return ret;
    });

asio::awaitable<deque<string>> fn_chat(ShellCommand::Args& args) {
  auto c = args.get_client();
  bool is_dchat = (args.command == "dchat");

  if (c->proxy_session) {
    if (!is_dchat && uses_utf16(c->version())) {
      send_chat_message_from_client(c->proxy_session->server_channel, args.args, 0);
    } else {
      string data(8, '\0');
      data.push_back('\x09');
      data.push_back('E');
      if (is_dchat) {
        data += phosg::parse_data_string(args.args, nullptr, phosg::ParseDataFlags::ALLOW_FILES);
      } else {
        data += args.args;
        data.push_back('\0');
      }
      data.resize((data.size() + 3) & (~3));
      c->proxy_session->server_channel->send(0x06, 0x00, data);
    }
  } else if (c->login) {
    string text = is_dchat ? phosg::parse_data_string(args.args, nullptr, phosg::ParseDataFlags::ALLOW_FILES) : args.args;
    auto l = c->require_lobby();
    for (auto& lc : l->clients) {
      if (lc) {
        send_chat_message(lc, c->login->account->account_id, c->character_file()->disp.name.decode(c->language()), text, 0);
      }
    }
  }

  co_return deque<string>{};
}
ShellCommand c_c("c", "c TEXT", fn_chat);
ShellCommand c_chat("chat", "chat TEXT\n\
    Send a chat message to the server.",
    fn_chat);
ShellCommand c_dchat("dchat", "dchat DATA\n\
    Send a chat message to the server with arbitrary data in it.",
    fn_chat);

asio::awaitable<deque<string>> fn_wchat(ShellCommand::Args& args) {
  auto c = args.get_client();
  if (!is_ep3(c->version())) {
    throw runtime_error("wchat can only be used on Episode 3");
  }
  if (c->proxy_session) {
    string data(8, '\0');
    data.push_back('\x40'); // private_flags: visible to all
    data.push_back('\x09');
    data.push_back('E');
    data += args.args;
    data.push_back('\0');
    data.resize((data.size() + 3) & (~3));
    c->proxy_session->server_channel->send(0x06, 0x00, data);
  } else if (c->login) {
    auto l = c->require_lobby();
    for (auto& lc : l->clients) {
      if (lc) {
        send_chat_message(
            lc, c->login->account->account_id, c->character_file()->disp.name.decode(c->language()), args.args, 0x40);
      }
    }
  }
  co_return deque<string>{};
}
ShellCommand c_wc("wc", "wc TEXT", fn_wchat);
ShellCommand c_wchat("wchat", "wchat TEXT\n\
    Send a chat message with private_flags on Episode 3.",
    fn_wchat);

ShellCommand c_marker(
    "marker", "marker COLOR-ID\n\
    Change your lobby marker color.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto c = args.get_proxy_client();
      c->proxy_session->server_channel->send(0x89, stoul(args.args));
      co_return deque<string>{};
    });

asio::awaitable<deque<string>> fn_warp(ShellCommand::Args& args) {
  auto c = args.get_proxy_client();
  uint8_t floor = stoul(args.args);
  send_warp(c->channel, c->lobby_client_id, floor, true);
  if (args.command == "warpall") {
    send_warp(c->proxy_session->server_channel, c->lobby_client_id, floor, false);
  }
  co_return deque<string>{};
}
ShellCommand c_warp("warp", "warp FLOOR-ID", fn_warp);
ShellCommand c_warpme("warpme", "warpme FLOOR-ID\n\
    Send yourself to a specific floor.",
    fn_warp);
ShellCommand c_warpall("warpall", "warpall FLOOR-ID\n\
    Send everyone to a specific floor.",
    fn_warp);

asio::awaitable<deque<string>> fn_info_board(ShellCommand::Args& args) {
  auto c = args.get_proxy_client();

  string data;
  if (args.command == "info-board-data") {
    data += phosg::parse_data_string(args.args, nullptr, phosg::ParseDataFlags::ALLOW_FILES);
  } else {
    data += add_color(args.args);
  }
  data.push_back('\0');
  data.resize((data.size() + 3) & (~3));

  c->proxy_session->server_channel->send(0xD9, 0x00, data);
  co_return deque<string>{};
}
ShellCommand c_info_board("info-board", "info-board TEXT\n\
    Set your info board contents. This will affect the current session only,\n\
    and will not be saved for future sessions. Escape codes (e.g. $C4) can be\n\
    used.",
    fn_info_board);
ShellCommand c_info_board_data("info-board-data", "info-board-data DATA\n\
    Set your info board contents with arbitrary data. Like the above, affects\n\
    the current session only.",
    fn_info_board);

ShellCommand c_create_item(
    "create-item", "create-item DATA\n\
    Create an item as if the client had run the $item command.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto c = args.get_proxy_client();

      if (c->version() == Version::BB_V4) {
        throw runtime_error("proxy session is BB");
      }
      if (!c->proxy_session->is_in_game) {
        throw runtime_error("proxy session is not in a game");
      }
      if (c->lobby_client_id != c->proxy_session->leader_client_id) {
        throw runtime_error("proxy session is not game leader");
      }

      ItemData item = args.s->parse_item_description(c->version(), args.args);
      item.id = phosg::random_object<uint32_t>() | 0x80000000;

      send_drop_stacked_item_to_channel(args.s, c->channel, item, c->floor, c->pos);
      send_drop_stacked_item_to_channel(args.s, c->proxy_session->server_channel, item, c->floor, c->pos);

      string name = args.s->describe_item(c->version(), item, ItemNameIndex::Flag::INCLUDE_PSO_COLOR_ESCAPES);
      send_text_message(c->channel, "$C7Item created:\n" + name);
      co_return deque<string>{};
    });

ShellCommand c_replay_log(
    "replay-log", nullptr,
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      if (args.s->allow_saving_accounts) {
        throw runtime_error("Replays cannot be run when account saving is enabled");
      }
      auto log_f = phosg::fopen_shared(args.args, "rt");
      auto replay_session = make_shared<ReplaySession>(args.s, log_f.get(), true);
      co_await replay_session->run();
      co_return deque<string>{};
    });
