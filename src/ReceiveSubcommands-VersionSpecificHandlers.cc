#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_xbox_voice_chat_control(shared_ptr<Client> c, SubcommandMessage& msg) {
  // If sent by an XB client, should be forwarded to XB clients and no one else
  if (c->version() != Version::XB_V3) {
    co_return;
  }

  auto l = c->require_lobby();
  if (command_is_private(msg.command)) {
    if (msg.flag >= l->max_clients) {
      co_return;
    }
    auto target = l->clients[msg.flag];
    if (target && (target->version() == Version::XB_V3)) {
      send_command(target, msg.command, msg.flag, msg.data, msg.size);
    }
  } else {
    for (auto& lc : l->clients) {
      if (lc && (lc != c) && (lc->version() == Version::XB_V3)) {
        send_command(lc, msg.command, msg.flag, msg.data, msg.size);
      }
    }
  }
}

asio::awaitable<void> on_gc_nte_exclusive(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto can_participate = [&](Version vers) {
    return (!is_v1_or_v2(vers) || (vers == Version::GC_NTE));
  };
  if (!can_participate(c->version())) {
    co_return;
  }

  // Command should not be forwarded across the GC NTE boundary, but may be forwarded to other clients within that
  // boundary
  bool c_is_nte = (c->version() == Version::GC_NTE);

  auto l = c->require_lobby();
  if (command_is_private(msg.command)) {
    if (msg.flag >= l->max_clients) {
      co_return;
    }
    auto lc = l->clients[msg.flag];
    if (lc && can_participate(lc->version()) && ((lc->version() == Version::GC_NTE) == c_is_nte)) {
      send_command(lc, msg.command, msg.flag, msg.data, msg.size);
    }
  } else {
    for (auto& lc : l->clients) {
      if (lc && (lc != c) && can_participate(lc->version()) && ((lc->version() == Version::GC_NTE) == c_is_nte)) {
        send_command(lc, msg.command, msg.flag, msg.data, msg.size);
      }
    }
  }
}

asio::awaitable<void> on_charge_attack_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xC7 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xC7 command sent in non-game lobby");
  }

  const auto& cmd = msg.check_size_t<G_ChargeAttack_BB_6xC7>();
  auto& disp = c->character_file()->disp;
  if (cmd.meseta_amount > disp.stats.meseta) {
    disp.stats.meseta = 0;
  } else {
    disp.stats.meseta -= cmd.meseta_amount;
  }
  co_return;
}
