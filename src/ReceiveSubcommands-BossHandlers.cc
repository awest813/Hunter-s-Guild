#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_battle_scores(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_BattleScores_6x7F>();

  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  G_BattleScoresBE_6x7F sw_cmd;
  sw_cmd.header.subcommand = 0x7F;
  sw_cmd.header.size = cmd.header.size;
  sw_cmd.header.unused = 0;
  for (size_t z = 0; z < 4; z++) {
    auto& sw_entry = sw_cmd.entries[z];
    const auto& entry = cmd.entries[z];
    sw_entry.client_id = entry.client_id;
    sw_entry.place = entry.place;
    sw_entry.score = entry.score;
  }
  bool sender_is_be = is_big_endian(c->version());
  for (auto lc : l->clients) {
    if (lc && (lc != c)) {
      if (is_big_endian(lc->version()) == sender_is_be) {
        send_command_t(lc, 0x60, 0x00, cmd);
      } else {
        send_command_t(lc, 0x60, 0x00, sw_cmd);
      }
    }
  }
}

asio::awaitable<void> on_dragon_actions_6x12(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_DragonBossActions_DC_PC_XB_BB_6x12>();

  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.header.entity_id - 0x1000);
  if (ene_st->super_ene->type != EnemyType::DRAGON) {
    throw runtime_error("6x12 command sent for incorrect enemy type");
  }
  if (ene_st->alias_target_ene_st) {
    throw runtime_error("DRAGON enemy is an alias");
  }

  G_DragonBossActions_GC_6x12 sw_cmd = {{cmd.header.subcommand, cmd.header.size, cmd.header.entity_id.load()},
      cmd.phase.load(), cmd.unknown_a3.load(), cmd.target_client_id.load(), cmd.x.load(), cmd.z.load()};
  bool sender_is_be = is_big_endian(c->version());
  for (auto lc : l->clients) {
    if (lc && (lc != c)) {
      if (is_big_endian(lc->version()) == sender_is_be) {
        cmd.header.entity_id = 0x1000 | l->map_state->index_for_enemy_state(lc->version(), ene_st);
        if (cmd.header.entity_id != 0xFFFF) {
          send_command_t(lc, 0x60, 0x00, cmd);
        }
      } else {
        sw_cmd.header.entity_id = 0x1000 | l->map_state->index_for_enemy_state(lc->version(), ene_st);
        if (sw_cmd.header.entity_id != 0xFFFF) {
          send_command_t(lc, 0x60, 0x00, sw_cmd);
        }
      }
    }
  }
}

asio::awaitable<void> on_gol_dragon_actions(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_GolDragonBossActions_XB_BB_6xA8>();

  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.header.entity_id - 0x1000);
  if (ene_st->super_ene->type != EnemyType::GOL_DRAGON) {
    throw runtime_error("6xA8 command sent for incorrect enemy type");
  }
  if (ene_st->alias_target_ene_st) {
    throw runtime_error("GOL_DRAGON enemy is an alias");
  }

  G_GolDragonBossActions_GC_6xA8 sw_cmd = {{cmd.header.subcommand, cmd.header.size, cmd.header.entity_id},
      cmd.phase.load(),
      cmd.unknown_a3.load(),
      cmd.target_client_id.load(),
      cmd.x.load(),
      cmd.z.load(),
      cmd.unknown_a5,
      0};
  bool sender_is_be = is_big_endian(c->version());
  for (auto lc : l->clients) {
    if (lc && (lc != c)) {
      if (is_big_endian(lc->version()) == sender_is_be) {
        cmd.header.entity_id = 0x1000 | l->map_state->index_for_enemy_state(lc->version(), ene_st);
        if (cmd.header.entity_id != 0xFFFF) {
          send_command_t(lc, 0x60, 0x00, cmd);
        }
      } else {
        sw_cmd.header.entity_id = 0x1000 | l->map_state->index_for_enemy_state(lc->version(), ene_st);
        if (sw_cmd.header.entity_id != 0xFFFF) {
          send_command_t(lc, 0x60, 0x00, sw_cmd);
        }
      }
    }
  }
}
