#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_entity_drop_item_request(shared_ptr<Client> c, SubcommandMessage& msg);

asio::awaitable<void> on_set_quest_flag(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  uint16_t flag_num, action;
  Difficulty difficulty;
  if (is_v1_or_v2(c->version()) && (c->version() != Version::GC_NTE)) {
    const auto& cmd = msg.check_size_t<G_UpdateQuestFlag_DC_PC_6x75>();
    flag_num = cmd.flag;
    action = cmd.action;
    difficulty = l->difficulty;
  } else {
    const auto& cmd = msg.check_size_t<G_UpdateQuestFlag_V3_BB_6x75>();
    flag_num = cmd.flag;
    action = cmd.action;
    difficulty = static_cast<Difficulty>(cmd.difficulty16.load());
  }

  // The client explicitly checks action for both 0 and 1 - any other value means no operation is performed.
  if ((flag_num >= 0x400) || (static_cast<size_t>(difficulty) > 3) || (action > 1)) {
    co_return;
  }
  bool should_set = (action == 0);

  if (l->quest_flags_known) {
    l->quest_flags_known->set(difficulty, flag_num);
  }
  if (should_set) {
    l->quest_flag_values->set(difficulty, flag_num);
  } else {
    l->quest_flag_values->clear(difficulty, flag_num);
  }

  if (c->version() == Version::BB_V4) {
    auto s = c->require_server_state();
    // TODO: Should we allow overlays here?
    auto p = c->character_file(true, false);
    if (should_set) {
      c->log.info_f("Setting quest flag {}:{:04X}", name_for_difficulty(difficulty), flag_num);
      p->quest_flags.set(difficulty, flag_num);
    } else {
      c->log.info_f("Clearing quest flag {}:{:04X}", name_for_difficulty(difficulty), flag_num);
      p->quest_flags.clear(difficulty, flag_num);
    }
  }

  forward_subcommand(c, msg);

  if (l->drop_mode != ServerDropMode::DISABLED) {
    EnemyType boss_enemy_type = EnemyType::NONE;
    uint8_t area = l->area_for_floor(c->version(), c->floor);
    if (area == 0x0E) {
      // On Normal, Dark Falz does not have a third phase, so send the drop request after the end of the second phase.
      // On all other difficulty levels, send it after the third phase.
      if ((difficulty == Difficulty::NORMAL) && (flag_num == 0x0035)) {
        boss_enemy_type = EnemyType::DARK_FALZ_2;
      } else if ((difficulty != Difficulty::NORMAL) && (flag_num == 0x0037)) {
        boss_enemy_type = EnemyType::DARK_FALZ_3;
      }
    } else if ((flag_num == 0x0057) && (area == 0x1F)) {
      boss_enemy_type = EnemyType::OLGA_FLOW_2;
    }

    if (boss_enemy_type != EnemyType::NONE) {
      l->log.info_f("Creating item from final boss ({})", phosg::name_for_enum(boss_enemy_type));
      uint16_t enemy_index = 0xFFFF;
      try {
        auto ene_st = l->map_state->enemy_state_for_floor_type(c->version(), c->floor, boss_enemy_type);
        if (ene_st->alias_target_ene_st) {
          ene_st = ene_st->alias_target_ene_st;
        }
        enemy_index = l->map_state->index_for_enemy_state(c->version(), ene_st);
        if (c->floor != ene_st->super_ene->floor) {
          l->log.warning_f("Floor {:02X} from client does not match entity\'s expected floor {:02X}",
              c->floor, ene_st->super_ene->floor);
        }
        l->log.info_f("Found enemy E-{:03X} at index {:04X} on floor {:X}", ene_st->e_id, enemy_index, ene_st->super_ene->floor);
      } catch (const out_of_range&) {
        l->log.warning_f("Could not find enemy on floor {:X}; unable to determine enemy type", c->floor);
        boss_enemy_type = EnemyType::NONE;
      }

      if (boss_enemy_type != EnemyType::NONE) {
        VectorXZF pos;
        switch (boss_enemy_type) {
          case EnemyType::DARK_FALZ_2:
            pos = {-58.0f, 31.0f};
            break;
          case EnemyType::DARK_FALZ_3:
            pos = {10160.0f, 0.0f};
            break;
          case EnemyType::OLGA_FLOW_2:
            pos = {-9999.0f, 0.0f};
            break;
          default:
            throw logic_error("invalid boss enemy type");
        }

        auto s = c->require_server_state();
        G_StandardDropItemRequest_PC_V3_BB_6x60 drop_req = {
            {
                {0x60, 0x06, 0x0000},
                static_cast<uint8_t>(c->floor),
                type_definition_for_enemy(boss_enemy_type).rt_index,
                enemy_index == 0xFFFF ? 0x0B4F : enemy_index,
                pos,
                2,
                0,
            },
            area, {}};
        SubcommandMessage drop_msg{0x62, l->leader_id, &drop_req, sizeof(drop_req)};
        co_await on_entity_drop_item_request(c, drop_msg);
      }
    }
  }
}

asio::awaitable<void> on_sync_quest_register(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  const auto& cmd = msg.check_size_t<G_SyncQuestRegister_6x77>();
  if (cmd.register_number >= 0x100) {
    throw runtime_error("invalid register number");
  }

  // If the lock status register is being written, change the game's flags to allow or forbid joining
  if (l->quest &&
      l->quest->meta.joinable &&
      (l->quest->meta.lock_status_register >= 0) &&
      (cmd.register_number == l->quest->meta.lock_status_register)) {
    // Lock if value is nonzero; unlock if value is zero
    if (cmd.value.as_int) {
      l->set_flag(Lobby::Flag::QUEST_IN_PROGRESS);
      l->clear_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS);
    } else {
      l->clear_flag(Lobby::Flag::QUEST_IN_PROGRESS);
      l->set_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS);
    }
  }

  bool should_forward = true;
  if (l->quest->meta.enable_schtserv_commands) {
    // We currently only implement one Schtserv server command here. There are likely many more which we don't support.

    if (cmd.register_number == 0xF0) {
      should_forward = false;
      c->schtserv_response_register = cmd.value.as_int;

    } else if ((cmd.register_number == 0xF1) && (cmd.value.as_int == 0x52455650)) {
      // PVER => respond with specific_version in schtserv's format
      should_forward = false;
      G_SyncQuestRegister_6x77 ret_cmd;
      ret_cmd.header.subcommand = 0x77;
      ret_cmd.header.size = sizeof(ret_cmd) / 4;
      ret_cmd.header.unused = 0;
      ret_cmd.register_number = c->schtserv_response_register;
      ret_cmd.value.as_int = is_v4(c->version()) ? 0x50 : c->sub_version;
      send_command_t(c, 0x60, 0x00, ret_cmd);
    }
  }

  if (should_forward) {
    forward_subcommand(c, msg);
  }
}
