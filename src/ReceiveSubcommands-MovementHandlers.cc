#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

template <typename CmdT>
static asio::awaitable<void> on_change_hp_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<CmdT>(0xFFFF);

  auto l = c->require_lobby();
  if (!l->is_game() || (cmd.client_id != c->lobby_client_id)) {
    co_return;
  }

  forward_subcommand(c, msg);
  if ((l->check_flag(Lobby::Flag::CHEATS_ENABLED) || c->login->account->check_flag(Account::Flag::CHEAT_ANYWHERE)) &&
      c->check_flag(Client::Flag::INFINITE_HP_ENABLED)) {
    co_await send_change_player_hp(l, c->lobby_client_id, PlayerHPChange::MAXIMIZE_HP, 0);
  }
}

asio::awaitable<void> on_change_hp_6x2F(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_change_hp_t<G_ChangePlayerHP_6x2F>(c, msg);
}

asio::awaitable<void> on_change_hp_6x4A_4B_4C(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_change_hp_t<G_ClientIDHeader>(c, msg);
}

template <typename CmdT>
static asio::awaitable<void> on_movement_xz_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<CmdT>();
  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }
  c->pos.x = cmd.pos.x;
  c->pos.z = cmd.pos.z;
  forward_subcommand(c, msg);
}

template <typename CmdT>
static asio::awaitable<void> on_movement_xyz_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<CmdT>();
  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }
  c->pos = cmd.pos;
  forward_subcommand(c, msg);
}

template <typename CmdT>
static asio::awaitable<void> on_movement_xyz_with_floor_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<CmdT>();
  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }
  c->pos = cmd.pos;
  if (cmd.floor >= 0 && c->floor != static_cast<uint32_t>(cmd.floor)) {
    c->floor = cmd.floor;
  }
  forward_subcommand(c, msg);
}

asio::awaitable<void> on_movement_6x20(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xyz_with_floor_t<G_SetPosition_6x20>(c, msg);
}

asio::awaitable<void> on_movement_6x24(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xyz_t<G_TeleportPlayer_6x24>(c, msg);
}

asio::awaitable<void> on_movement_6x3E(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xyz_with_floor_t<G_StopAtPosition_6x3E>(c, msg);
}

asio::awaitable<void> on_movement_6x3F(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xyz_with_floor_t<G_SetPosition_6x3F>(c, msg);
}

asio::awaitable<void> on_movement_6x40(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xz_t<G_WalkToPosition_6x40>(c, msg);
}

asio::awaitable<void> on_movement_6x41_6x42(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xz_t<G_MoveToPosition_6x41_6x42>(c, msg);
}

asio::awaitable<void> on_movement_6x55(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xyz_t<G_IntraMapWarp_6x55>(c, msg);
}

asio::awaitable<void> on_movement_6x56(shared_ptr<Client> c, SubcommandMessage& msg) {
  co_await on_movement_xyz_t<G_SetPlayerPositionAndAngle_6x56>(c, msg);
}
