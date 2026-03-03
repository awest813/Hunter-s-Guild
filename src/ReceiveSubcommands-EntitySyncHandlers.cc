#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

uint8_t translate_subcommand_number(Version to_version, Version from_version, uint8_t subcommand);

template <typename CmdT, bool ForwardIfMissing = false, size_t EntityIDOffset = offsetof(G_EntityIDHeader, entity_id)>
static asio::awaitable<void> forward_subcommand_with_entity_id_transcode_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  // I'm lazy and this should never happen for item commands (since all players need to stay in sync)
  if (command_is_private(msg.command)) {
    throw runtime_error("entity subcommand sent via private command");
  }

  auto& cmd = msg.check_size_t<CmdT>();

  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("command cannot be used outside of a game");
  }

  le_uint16_t& cmd_entity_id = *reinterpret_cast<le_uint16_t*>(reinterpret_cast<uint8_t*>(&cmd) + EntityIDOffset);

  shared_ptr<const MapState::EnemyState> ene_st;
  shared_ptr<const MapState::ObjectState> obj_st;
  if ((cmd_entity_id >= 0x1000) && (cmd_entity_id < 0x4000)) {
    ene_st = l->map_state->enemy_state_for_index(c->version(), cmd_entity_id - 0x1000);
  } else if ((cmd_entity_id >= 0x4000) && (cmd_entity_id < 0xFFFF)) {
    obj_st = l->map_state->object_state_for_index(c->version(), cmd_entity_id - 0x4000);
  }

  for (auto& lc : l->clients) {
    if (!lc || lc == c) {
      continue;
    }
    if (c->version() != lc->version()) {
      cmd.header.subcommand = translate_subcommand_number(lc->version(), c->version(), cmd.header.subcommand);
      if (cmd.header.subcommand) {
        bool should_forward = true;
        if (ene_st) {
          cmd_entity_id = 0x1000 | l->map_state->index_for_enemy_state(lc->version(), ene_st);
          should_forward = ForwardIfMissing || (cmd_entity_id != 0xFFFF);
        } else if (obj_st) {
          cmd_entity_id = 0x4000 | l->map_state->index_for_object_state(lc->version(), obj_st);
          should_forward = ForwardIfMissing || (cmd_entity_id != 0xFFFF);
        }
        if (should_forward) {
          send_command_t(lc, msg.command, msg.flag, cmd);
        }
      } else {
        lc->log.info_f("Subcommand cannot be translated to client\'s version");
      }
    } else {
      send_command_t(lc, msg.command, msg.flag, cmd);
    }
  }
  co_return;
}

asio::awaitable<void> on_switch_state_changed(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_WriteSwitchFlag_6x05>();

  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  if (!l->quest &&
      (cmd.flags & 1) &&
      (cmd.header.entity_id != 0xFFFF) &&
      (cmd.switch_flag_num < 0x100) &&
      c->check_flag(Client::Flag::SWITCH_ASSIST_ENABLED)) {
    auto sw_obj_st = l->map_state->object_state_for_index(c->version(), cmd.switch_flag_floor, cmd.header.entity_id - 0x4000);
    c->log.info_f("Switch assist triggered by K-{:03X} setting SW-{:02X}-{:02X}",
        sw_obj_st->k_id, cmd.switch_flag_floor, cmd.switch_flag_num);
    for (auto obj_st : l->map_state->door_states_for_switch_flag(c->version(), cmd.switch_flag_floor, cmd.switch_flag_num)) {
      if (obj_st->game_flags & 0x0001) {
        c->log.info_f("K-{:03X} is already unlocked", obj_st->k_id);
        continue;
      }
      if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
        send_text_message_fmt(c, "$C5SWA K-{:03X} {:02X} {:02X}",
            obj_st->k_id, cmd.switch_flag_floor, cmd.switch_flag_num);
      }
      obj_st->game_flags |= 1;

      for (auto lc : l->clients) {
        if (!lc) {
          continue;
        }
        uint16_t object_index = l->map_state->index_for_object_state(lc->version(), obj_st);
        lc->log.info_f("Switch assist: door object K-{:03X} has index {:04X} on version {}",
            obj_st->k_id, object_index, phosg::name_for_enum(lc->version()));
        if (object_index != 0xFFFF) {
          G_UpdateObjectState_6x0B cmd0B;
          cmd0B.header.subcommand = 0x0B;
          cmd0B.header.size = sizeof(cmd0B) / 4;
          cmd0B.header.entity_id = object_index | 0x4000;
          cmd0B.flags = obj_st->game_flags;
          cmd0B.object_index = object_index;
          send_command_t(l, 0x60, 0x00, cmd0B);
        }
      }
    }
  }

  if (cmd.header.entity_id != 0xFFFF && c->check_flag(Client::Flag::DEBUG_ENABLED)) {
    const auto& obj_st = l->map_state->object_state_for_index(
        c->version(), cmd.switch_flag_floor, cmd.header.entity_id - 0x4000);
    auto s = c->require_server_state();
    uint8_t area = l->area_for_floor(c->version(), c->floor);
    auto type_name = obj_st->type_name(c->version(), area);
    send_text_message_fmt(c, "$C5K-{:03X} A {}", obj_st->k_id, type_name);
  }

  // Apparently sometimes 6x05 is sent with an invalid switch flag number. The client seems to just ignore the command
  // in that case, so we go ahead and forward it (in case the client's object update function is meaningful somehow)
  // and just don't update our view of the switch flags.
  if (l->switch_flags && (cmd.switch_flag_num < 0x100)) {
    if (cmd.flags & 1) {
      l->switch_flags->set(cmd.switch_flag_floor, cmd.switch_flag_num);
      if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
        send_text_message_fmt(c, "$C5SW-{:02X}-{:02X} ON", cmd.switch_flag_floor, cmd.switch_flag_num);
      }
    } else {
      l->switch_flags->clear(cmd.switch_flag_floor, cmd.switch_flag_num);
      if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
        send_text_message_fmt(c, "$C5SW-{:02X}-{:02X} OFF", cmd.switch_flag_floor, cmd.switch_flag_num);
      }
    }
  }

  co_await forward_subcommand_with_entity_id_transcode_t<G_WriteSwitchFlag_6x05, true>(c, msg);
  co_return;
}

asio::awaitable<void> on_set_entity_set_flag(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  const auto& cmd = msg.check_size_t<G_SetEntitySetFlags_6x76>();
  if (cmd.header.entity_id >= 0x4000) {
    try {
      auto obj_st = l->map_state->object_state_for_index(c->version(), cmd.floor, cmd.header.entity_id - 0x4000);
      obj_st->set_flags |= cmd.flags;
      l->log.info_f("Client set set flags {:04X} on K-{:03X} (flags are now {:04X})",
          cmd.flags, obj_st->k_id, cmd.flags);
    } catch (const out_of_range&) {
      l->log.warning_f("Flag update refers to missing object");
    }

  } else if (cmd.header.entity_id >= 0x1000) {
    int32_t room = -1;
    int32_t wave_number = -1;
    try {
      size_t enemy_index = cmd.header.entity_id - 0x1000;
      auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.floor, enemy_index);
      if (ene_st->super_ene->child_index > 0) {
        if (ene_st->super_ene->child_index > enemy_index) {
          throw logic_error("enemy\'s child index is greater than enemy\'s absolute index");
        }
        size_t parent_index = enemy_index - ene_st->super_ene->child_index;
        l->log.info_f("Client set set flags {:04X} on E-{:03X} but it is a child ({}); redirecting to E-{:X}",
            cmd.flags, ene_st->e_id, ene_st->super_ene->child_index, parent_index);
        ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.floor, parent_index);
      }
      ene_st->set_flags |= cmd.flags;
      const auto* set_entry = ene_st->super_ene->version(c->version()).set_entry;
      if (!set_entry) {
        // We should not have been able to look up this enemy if it didn't exist on this version
        throw logic_error("enemy does not exist on this game version");
      }
      room = set_entry->room;
      wave_number = set_entry->wave_number;
      l->log.info_f("Client set set flags {:04X} on E-{:03X} (flags are now {:04X})", cmd.flags, ene_st->e_id, cmd.flags);
    } catch (const out_of_range&) {
      l->log.warning_f("Flag update refers to missing enemy");
    }

    if ((room >= 0) && (wave_number >= 0)) {
      // When all enemies in a wave event have (set_flags & 8), which means they are defeated, set event_flags =
      // (event_flags | 0x18) & (~4), which means it is done and should not trigger again
      bool all_enemies_defeated = true;
      l->log.info_f("Checking for defeated enemies with room={:04X} wave_number={:04X}", room, wave_number);
      for (auto ene_st : l->map_state->enemy_states_for_floor_room_wave(c->version(), cmd.floor, room, wave_number)) {
        if (ene_st->super_ene->child_index) {
          l->log.info_f("E-{:03X} is a child of another enemy", ene_st->e_id);
        } else if (!(ene_st->set_flags & 8)) {
          l->log.info_f("E-{:03X} is not defeated; cannot advance event to finished state", ene_st->e_id);
          all_enemies_defeated = false;
          break;
        } else {
          l->log.info_f("E-{:03X} is defeated", ene_st->e_id);
        }
      }
      if (all_enemies_defeated) {
        l->log.info_f("All enemies defeated; setting events with room={:04X} wave_number={:04X} to finished state",
            room, wave_number);
        for (auto ev_st : l->map_state->event_states_for_floor_room_wave(c->version(), cmd.floor, room, wave_number)) {
          ev_st->flags = (ev_st->flags | 0x18) & (~4);
          l->log.info_f("Set flags on W-{:03X} to {:04X}", ev_st->w_id, ev_st->flags);

          const auto& ev_ver = ev_st->super_ev->version(c->version());
          phosg::StringReader actions_r(ev_ver.action_stream, ev_ver.action_stream_size);
          while (!actions_r.eof()) {
            uint8_t opcode = actions_r.get_u8();
            switch (opcode) {
              case 0x00: // nop
                l->log.info_f("(W-{:03X} script) nop", ev_st->w_id);
                break;
              case 0x01: // stop
                l->log.info_f("(W-{:03X} script) stop", ev_st->w_id);
                actions_r.go(actions_r.size());
                break;
              case 0x08: { // construct_objects
                uint16_t room = actions_r.get_u16l();
                uint16_t group = actions_r.get_u16l();
                l->log.info_f("(W-{:03X} script) construct_objects {:04X} {:04X}", ev_st->w_id, room, group);
                auto obj_sts = l->map_state->object_states_for_floor_room_group(
                    c->version(), ev_st->super_ev->floor, room, group);
                for (auto obj_st : obj_sts) {
                  if (!(obj_st->set_flags & 0x0A)) {
                    l->log.info_f("(W-{:03X} script)   Setting flags 0010 on object K-{:03X}", ev_st->w_id, obj_st->k_id);
                    obj_st->set_flags |= 0x10;
                  }
                }
                break;
              }
              case 0x09: // construct_enemies
              case 0x0D: { // construct_enemies_stop
                uint16_t room = actions_r.get_u16l();
                uint16_t wave_number = actions_r.get_u16l();
                l->log.info_f("(W-{:03X} script) construct_enemies {:04X} {:04X}", ev_st->w_id, room, wave_number);
                auto ene_sts = l->map_state->enemy_states_for_floor_room_wave(
                    c->version(), ev_st->super_ev->floor, room, wave_number);
                for (auto ene_st : ene_sts) {
                  if (!ene_st->super_ene->child_index && !(ene_st->set_flags & 0x0A)) {
                    l->log.info_f("(W-{:03X} script)   Setting flags 0002 on enemy set E-{:X}", ev_st->w_id, ene_st->e_id);
                    ene_st->set_flags |= 0x0002;
                  }
                }
                if (opcode == 0x0D) {
                  actions_r.go(actions_r.size());
                }
                break;
              }
              case 0x0A: // set_switch_flag
              case 0x0B: { // clear_switch_flag
                // These opcodes cause the client to send 6x05 commands, so we don't have to do anything here.
                uint16_t switch_flag_num = actions_r.get_u16l();
                l->log.info_f("(W-{:03X} script) {}able_switch_flag {:04X}",
                    ev_st->w_id, (opcode & 1) ? "dis" : "en", switch_flag_num);
                break;
              }
              case 0x0C: { // trigger_event
                // This opcode causes the client to send a 6x67 command, so we don't have to do anything here.
                uint32_t event_id = actions_r.get_u32l();
                l->log.info_f("(W-{:03X} script) trigger_event {:08X}", ev_st->w_id, event_id);
                break;
              }
              default:
                l->log.warning_f("(W-{:03X}) Invalid opcode {:02X} at offset {:X} in event action stream",
                    ev_st->w_id, opcode, actions_r.where() - 1);
                actions_r.go(actions_r.size());
            }
          }
        }
      }
    }
  }

  co_await forward_subcommand_with_entity_id_transcode_t<G_SetEntitySetFlags_6x76>(c, msg);
}

asio::awaitable<void> on_incr_enemy_damage(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_IncrementEnemyDamage_Extension_6xE4>();

  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }
  if (cmd.header.entity_id < 0x1000 || cmd.header.entity_id >= 0x4000) {
    throw runtime_error("6xE4 received for non-enemy entity");
  }
  auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.header.entity_id & 0x0FFF);

  c->log.info_f("E-{:03X} damage incremented by {} with factor {}; before hit, damage was {} (cmd) or {} (ene_st) and HP was {}/{}",
      ene_st->e_id,
      cmd.hit_amount.load(),
      cmd.factor.load(),
      ene_st->total_damage,
      cmd.total_damage_before_hit.load(),
      cmd.current_hp_before_hit.load(),
      cmd.max_hp.load());
  ene_st->total_damage = std::min<uint32_t>(ene_st->total_damage + cmd.hit_amount, cmd.max_hp);
  if (ene_st->alias_target_ene_st) {
    ene_st->alias_target_ene_st->total_damage = std::min<uint32_t>(
        ene_st->alias_target_ene_st->total_damage + cmd.hit_amount, cmd.max_hp);
  }

  co_await forward_subcommand_with_entity_id_transcode_t<G_IncrementEnemyDamage_Extension_6xE4>(c, msg);
}

asio::awaitable<void> on_set_enemy_low_game_flags_ultimate(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_SetEnemyLowGameFlagsUltimate_6x9C>();

  if (command_is_private(msg.command) ||
      (cmd.header.entity_id < 0x1000) ||
      (cmd.header.entity_id >= 0x4000) ||
      (cmd.low_game_flags & 0xFFFFFFC0) ||
      (c->lobby_client_id > 3)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game() || (l->difficulty != Difficulty::ULTIMATE)) {
    co_return;
  }

  auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.header.entity_id - 0x1000);
  if (!(ene_st->game_flags & cmd.low_game_flags)) {
    ene_st->game_flags |= cmd.low_game_flags;
    l->log.info_f("E-{:03X} updated to game_flags={:08X}", ene_st->e_id, ene_st->game_flags);
  }
  if (ene_st->alias_target_ene_st && !(ene_st->alias_target_ene_st->game_flags & cmd.low_game_flags)) {
    ene_st->alias_target_ene_st->game_flags |= cmd.low_game_flags;
    l->log.info_f("Alias E-{:03X} updated to game_flags={:08X}",
        ene_st->alias_target_ene_st->e_id, ene_st->alias_target_ene_st->game_flags);
  }

  co_await forward_subcommand_with_entity_id_transcode_t<G_SetEnemyLowGameFlagsUltimate_6x9C>(c, msg);
}

asio::awaitable<void> on_set_entity_pos_and_angle_6x17(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_SetEntityPositionAndAngle_6x17>();

  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  // 6x17 is used to transport players to the other part of the Vol Opt boss arena, so phase 2 can begin. We only allow
  // 6x17 in the Monitor Room (Vol Opt arena).
  if (l->area_for_floor(c->version(), c->floor) != 0x0D) {
    throw runtime_error("client sent 6x17 command in area other than Vol Opt");
  }

  // If the target is on a different floor or does not exist, just drop the command instead of raising; this could have
  // been due to a data race
  if (cmd.header.entity_id < 0x1000) {
    auto target = l->clients.at(cmd.header.entity_id);
    if (!target || target->floor != c->floor) {
      co_return;
    }
    target->pos = cmd.pos;
  }

  co_await forward_subcommand_with_entity_id_transcode_t<G_SetEntityPositionAndAngle_6x17>(c, msg);
}

asio::awaitable<void> on_set_boss_warp_flags_6x6A(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_SetBossWarpFlags_6x6A>();

  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }
  if (cmd.header.entity_id < 0x4000) {
    throw runtime_error("6x6A sent for non-object entity");
  }

  auto obj_st = l->map_state->object_state_for_index(c->version(), cmd.header.entity_id - 0x4000);
  if (!obj_st->super_obj) {
    throw runtime_error("missing object for 6x6A command");
  }
  auto set_entry = obj_st->super_obj->version(c->version()).set_entry;
  if (!set_entry) {
    throw runtime_error("missing set entry for 6x6A command");
  }
  if (set_entry->base_type != 0x0019 && set_entry->base_type != 0x0055) {
    throw runtime_error("incorrect object type for 6x6A command");
  }

  co_await forward_subcommand_with_entity_id_transcode_t<G_SetBossWarpFlags_6x6A>(c, msg);
}
