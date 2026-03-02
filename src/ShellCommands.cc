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
