#include "ReceiveSubcommands-Impl.hh"

using namespace std;

asio::awaitable<void> on_invalid(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_UnusedHeader>(0xFFFF);
  if ((c->version() == Version::DC_NTE) || c->version() == Version::DC_11_2000) {
    c->log.error_f("Unrecognized DC NTE/prototype subcommand: {:02X}", cmd.subcommand);
    forward_subcommand(c, msg);
  } else if (command_is_private(msg.command)) {
    c->log.error_f("Invalid subcommand: {:02X} (private to {})", cmd.subcommand, msg.flag);
  } else {
    c->log.error_f("Invalid subcommand: {:02X} (public)", cmd.subcommand);
  }
  co_return;
}

asio::awaitable<void> on_debug_info(shared_ptr<Client>, SubcommandMessage&) {
  co_return;
}

asio::awaitable<void> on_forward_check_game_loading(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (l->is_game() && l->any_client_loading()) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_forward_check_game_quest(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (l->is_game() && l->quest) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_forward_check_client(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_ClientIDHeader>(0xFFFF);
  if (cmd.client_id == c->lobby_client_id) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_forward_check_game(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (l->is_game()) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_forward_check_lobby(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_forward_check_lobby_client(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_ClientIDHeader>(0xFFFF);
  auto l = c->require_lobby();
  if (!l->is_game() && (cmd.client_id == c->lobby_client_id)) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_forward_check_game_client(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_ClientIDHeader>(0xFFFF);
  auto l = c->require_lobby();
  if (l->is_game() && (cmd.client_id == c->lobby_client_id)) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_forward_check_ep3_lobby(shared_ptr<Client> c, SubcommandMessage& msg) {
  msg.check_size_t<G_UnusedHeader>(0xFFFF);
  auto l = c->require_lobby();
  if (!l->is_game() && l->is_ep3()) {
    forward_subcommand(c, msg);
  }
  co_return;
}
