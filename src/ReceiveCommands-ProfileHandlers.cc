#include "ReceiveCommands.hh"

#include <phosg/Strings.hh>

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_D8(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  send_info_board(c);
  co_return;
}

asio::awaitable<void> on_D9(shared_ptr<Client> c, Channel::Message& msg) {
  phosg::strip_trailing_zeroes(msg.data);
  bool is_w = uses_utf16(c->version());
  if (is_w && (msg.data.size() & 1)) {
    msg.data.push_back(0);
  }
  try {
    c->character_file(true, false)->info_board.encode(tt_decode_marked(msg.data, c->language(), is_w), c->language());
  } catch (const runtime_error& e) {
    c->log.warning_f("Failed to decode info board message: {}", e.what());
  }
  co_return;
}

asio::awaitable<void> on_C7(shared_ptr<Client> c, Channel::Message& msg) {
  phosg::strip_trailing_zeroes(msg.data);
  bool is_w = uses_utf16(c->version());
  if (is_w && (msg.data.size() & 1)) {
    msg.data.push_back(0);
  }

  string message = tt_decode_marked(msg.data, c->language(), is_w);
  c->character_file(true, false)->auto_reply.encode(message, c->language());
  c->login->account->auto_reply_message = message;
  c->login->account->save();
  co_return;
}

asio::awaitable<void> on_C8(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  c->character_file(true, false)->auto_reply.clear();
  c->login->account->auto_reply_message.clear();
  c->login->account->save();
  co_return;
}

asio::awaitable<void> on_C6(shared_ptr<Client> c, Channel::Message& msg) {
  c->blocked_senders.clear();
  if (c->version() == Version::BB_V4) {
    const auto& cmd = check_size_t<C_SetBlockedSenders_BB_C6>(msg.data);
    c->import_blocked_senders(cmd.blocked_senders);
  } else {
    const auto& cmd = check_size_t<C_SetBlockedSenders_V3_C6>(msg.data);
    c->import_blocked_senders(cmd.blocked_senders);
  }
  co_return;
}

asio::awaitable<void> on_C9_XB(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  c->log.warning_f("Ignoring connection status change command ({:02X})", msg.flag);
  co_return;
}
