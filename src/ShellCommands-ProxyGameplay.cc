#include "ShellCommands.hh"

#include <phosg/Random.hh>
#include <phosg/Strings.hh>

#include "ReceiveCommands.hh"
#include "SendCommands.hh"

using namespace std;

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
