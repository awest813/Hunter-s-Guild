#include "ReceiveCommands.hh"

#include <phosg/Strings.hh>

#include "ChatCommands.hh"
#include "ReceiveSubcommands.hh"
#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

asio::awaitable<void> on_6x_C9_CB(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 4, 0xFFFF);
  if ((msg.data.size() > 0x400) && (msg.command != 0x6C) && (msg.command != 0x6D)) {
    throw runtime_error("non-extended game command data size is too large");
  }
  co_await on_subcommand_multi(c, msg);
}

asio::awaitable<void> on_06(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<SC_TextHeader_01_06_11_B0_EE>(msg.data, 0xFFFF);
  string text = msg.data.substr(sizeof(cmd));
  phosg::strip_trailing_zeroes(text);
  if (text.empty()) {
    co_return;
  }
  bool is_w = uses_utf16(c->version());
  if (is_w && (text.size() & 1)) {
    text.push_back(0);
  }

  auto l = c->lobby.lock();
  char private_flags = 0;
  if (is_ep3(c->version()) && l && l->is_ep3() && (text[0] != '\t')) {
    private_flags = text[0];
    text = text.substr(1);
  }

  try {
    text = tt_decode_marked(text, c->language(), is_w);
  } catch (const runtime_error& e) {
    c->log.warning_f("Failed to decode chat message: {}", e.what());
    send_text_message_fmt(c, "$C4Failed to decode\nchat message:\n{}", e.what());
    text.clear();
  }
  if (text.empty()) {
    co_return;
  }

  auto s = c->require_server_state();
  char command_sentinel = s->chat_command_sentinel
      ? s->chat_command_sentinel
      : ((c->version() == Version::DC_11_2000) ? '@' : '$');
  if ((text[0] == command_sentinel) && c->can_use_chat_commands()) {
    if (text[1] == command_sentinel) {
      text = text.substr(1);
    } else {
      co_await on_chat_command(c, text, true);
      co_return;
    }
  }

  if (!l || !c->can_chat) {
    co_return;
  }

  auto p = c->character_file();
  string from_name = p->disp.name.decode(c->language());
  static const string whisper_text = "(whisper)";
  for (size_t x = 0; x < l->max_clients; x++) {
    if (l->clients[x]) {
      bool should_hide_contents = (!(l->check_flag(Lobby::Flag::IS_SPECTATOR_TEAM))) && (private_flags & (1 << x));
      const string& effective_text = should_hide_contents ? whisper_text : text;
      try {
        send_chat_message(l->clients[x], c->login->account->account_id, from_name, effective_text, private_flags);
      } catch (const runtime_error& e) {
        l->clients[x]->log.warning_f("Failed to encode chat message: {}", e.what());
      }
    }
  }
  for (const auto& watcher_l : l->watcher_lobbies) {
    for (size_t x = 0; x < watcher_l->max_clients; x++) {
      if (watcher_l->clients[x]) {
        try {
          send_chat_message(watcher_l->clients[x], c->login->account->account_id, from_name, text, private_flags);
        } catch (const runtime_error& e) {
          watcher_l->clients[x]->log.warning_f("Failed to encode chat message: {}", e.what());
        }
      }
    }
  }

  if (l->battle_record && l->battle_record->battle_in_progress()) {
    try {
      auto prepared_message = prepare_chat_data(
          c->version(),
          c->language(),
          c->lobby_client_id,
          p->disp.name.decode(c->language()),
          text,
          private_flags);
      l->battle_record->add_chat_message(c->login->account->account_id, std::move(prepared_message));
    } catch (const runtime_error& e) {
      l->log.warning_f("Failed to encode chat message for battle record: {}", e.what());
    }
  }
}
