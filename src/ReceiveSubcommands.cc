#include "ReceiveSubcommands.hh"
#include "ReceiveSubcommands-Impl.hh"

#include <math.h>
#include <string.h>

#include <memory>
#include <phosg/Random.hh>
#include <phosg/Strings.hh>
#include <phosg/Vector.hh>

#include "Client.hh"
#include "Compression.hh"
#include "GameServer.hh"
#include "HTTPServer.hh"
#include "Items.hh"
#include "Lobby.hh"
#include "Loggers.hh"
#include "Map.hh"
#include "PSOProtocol.hh"
#include "SendCommands.hh"
#include "StaticGameData.hh"
#include "Text.hh"

using namespace std;

// The functions in this file are called when a client sends a game command (60, 62, 6C, 6D, C9, or CB).

const SubcommandDefinition* def_for_subcommand(Version version, uint8_t subcommand) {
  static bool populated = false;
  static std::array<const SubcommandDefinition*, 0x100> nte_defs;
  static std::array<const SubcommandDefinition*, 0x100> proto_defs;
  static std::array<const SubcommandDefinition*, 0x100> final_defs;
  if (!populated) {
    nte_defs.fill(nullptr);
    proto_defs.fill(nullptr);
    final_defs.fill(nullptr);
    for (const auto& def : subcommand_definitions) {
      if (def.nte_subcommand != 0x00) {
        if (nte_defs[def.nte_subcommand]) {
          throw logic_error("multiple subcommand definitions map to the same NTE subcommand");
        }
        nte_defs[def.nte_subcommand] = &def;
      }
      if (def.proto_subcommand != 0x00) {
        if (proto_defs[def.proto_subcommand]) {
          throw logic_error("multiple subcommand definitions map to the same 11/2000 subcommand");
        }
        proto_defs[def.proto_subcommand] = &def;
      }
      if (def.final_subcommand != 0x00) {
        if (final_defs[def.final_subcommand]) {
          throw logic_error("multiple subcommand definitions map to the same final subcommand");
        }
        final_defs[def.final_subcommand] = &def;
      }
    }
    populated = true;
  }

  if (version == Version::DC_NTE) {
    return nte_defs[subcommand];
  } else if (version == Version::DC_11_2000) {
    return proto_defs[subcommand];
  } else {
    return final_defs[subcommand];
  }
}

uint8_t translate_subcommand_number(Version to_version, Version from_version, uint8_t subcommand) {
  const auto* def = def_for_subcommand(from_version, subcommand);
  if (!def) {
    return 0x00;
  } else if (to_version == Version::DC_NTE) {
    return def->nte_subcommand;
  } else if (to_version == Version::DC_11_2000) {
    return def->proto_subcommand;
  } else {
    return def->final_subcommand;
  }
}

bool command_is_private(uint8_t command) {
  return (command == 0x62) || (command == 0x6D);
}

asio::awaitable<void> on_warp(shared_ptr<Client>, SubcommandMessage& msg);
asio::awaitable<void> on_set_player_visible(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_change_floor_6x1F(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_change_floor_6x21(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_player_died(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_player_revivable(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_player_revived(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_received_condition(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_cast_technique_finished(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_npc_control(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_set_animation_state(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_play_sound_from_player(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_trigger_set_event(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_update_telepipe_state(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_update_enemy_state(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_activate_timed_switch(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_battle_scores(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_dragon_actions_6x12(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_gol_dragon_actions(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_change_hp_6x2F(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_change_hp_6x4A_4B_4C(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x20(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x24(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x3E(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x3F(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x40(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x41_6x42(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x55(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_movement_6x56(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_ep3_trade_card_counts(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_send_guild_card(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_symbol_chat(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_word_select(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_ep3_battle_subs(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_open_shop_bb_or_ep3_battle_subs(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_open_bank_bb_or_card_trade_counter_ep3(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_ep3_private_word_select_bb_bank_action(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_sort_inventory_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_set_quest_flag(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_sync_quest_register(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_steal_exp_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_enemy_exp_request_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_adjust_player_meseta_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_quest_exchange_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_wrap_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_photon_drop_exchange_for_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_photon_drop_exchange_for_s_rank_special_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_secret_lottery_ticket_exchange_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_photon_crystal_exchange_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_quest_F95E_result_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_quest_F95F_result_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_quest_F960_result_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_momoka_item_exchange_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_upgrade_weapon_attribute_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_write_quest_counter_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_medical_center_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_battle_restart_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_battle_level_up_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_request_challenge_grave_recovery_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_challenge_mode_retry_or_quit(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_challenge_update_records(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_update_battle_data_6x7D(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_level_up(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_quest_create_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_transfer_item_via_mail_message_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_exchange_item_for_team_points_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_destroy_inventory_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_destroy_floor_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_identify_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_accept_identify_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_sell_item_at_shop_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_buy_shop_item_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_player_drop_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_create_inventory_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_drop_partial_stack(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_drop_partial_stack_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_buy_shop_item(shared_ptr<Client> c, SubcommandMessage& msg);
void send_item_notification_if_needed(shared_ptr<Client> c, const ItemData& item, bool is_from_rare_table);
asio::awaitable<void> on_box_or_enemy_item_drop(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_pick_up_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_pick_up_item_request(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_equip_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_unequip_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_use_item(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_feed_mag(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_xbox_voice_chat_control(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_gc_nte_exclusive(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_charge_attack_bb(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_switch_state_changed(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_set_entity_set_flag(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_incr_enemy_damage(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_set_enemy_low_game_flags_ultimate(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_set_entity_pos_and_angle_6x17(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_set_boss_warp_flags_6x6A(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_sync_joining_player_compressed_state(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_sync_joining_player_quest_flags(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_update_object_state_6x0B(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_update_object_state_6x86(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_update_attackable_col_state(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_vol_opt_actions_6x16(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_vol_opt_actions_6x84(shared_ptr<Client> c, SubcommandMessage& msg);
shared_ptr<Client> get_sync_target(shared_ptr<Client> sender_c, uint8_t command, uint8_t flag, bool allow_if_not_loading);
asio::awaitable<void> on_sync_joining_player_disp_and_inventory(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_invalid(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_debug_info(shared_ptr<Client>, SubcommandMessage&);
asio::awaitable<void> on_forward_check_game_loading(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_forward_check_game_quest(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_forward_check_client(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_forward_check_game(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_forward_check_lobby(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_forward_check_lobby_client(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_forward_check_game_client(shared_ptr<Client> c, SubcommandMessage& msg);
asio::awaitable<void> on_forward_check_ep3_lobby(shared_ptr<Client> c, SubcommandMessage& msg);

void forward_subcommand(shared_ptr<Client> c, SubcommandMessage& msg) {
  // If the command is an Ep3-only command, make sure an Ep3 client sent it
  bool command_is_ep3 = (msg.command & 0xF0) == 0xC0;
  if (command_is_ep3 && !is_ep3(c->version())) {
    throw runtime_error("Episode 3 command sent by non-Episode 3 client");
  }

  auto l = c->lobby.lock();
  if (!l) {
    c->log.warning_f("Not in any lobby; dropping command");
    return;
  }

  auto& header = msg.check_size_t<G_UnusedHeader>(0xFFFF);
  const auto* def = def_for_subcommand(c->version(), header.subcommand);
  uint8_t def_flags = def ? def->flags : 0;

  string nte_data;
  string proto_data;
  string final_data;
  Version c_version = c->version();
  auto send_to_client = [&](shared_ptr<Client> lc) -> void {
    Version lc_version = lc->version();
    const void* data_to_send = nullptr;
    size_t size_to_send = 0;
    if ((!is_pre_v1(lc_version) && !is_pre_v1(c_version)) || (lc_version == c_version)) {
      data_to_send = msg.data;
      size_to_send = msg.size;
    } else if (lc->version() == Version::DC_NTE) {
      if (def && def->nte_subcommand) {
        if (nte_data.empty()) {
          nte_data.assign(reinterpret_cast<const char*>(msg.data), msg.size);
          nte_data[0] = def->nte_subcommand;
        }
        data_to_send = nte_data.data();
        size_to_send = nte_data.size();
      }
    } else if (lc->version() == Version::DC_11_2000) {
      if (def && def->proto_subcommand) {
        if (proto_data.empty()) {
          proto_data.assign(reinterpret_cast<const char*>(msg.data), msg.size);
          proto_data[0] = def->proto_subcommand;
        }
        data_to_send = proto_data.data();
        size_to_send = proto_data.size();
      }
    } else {
      if (def && def->final_subcommand) {
        if (final_data.empty()) {
          final_data.assign(reinterpret_cast<const char*>(msg.data), msg.size);
          final_data[0] = def->final_subcommand;
        }
        data_to_send = final_data.data();
        size_to_send = final_data.size();
      }
    }

    if (!data_to_send || !size_to_send) {
      lc->log.info_f("Command cannot be translated to client\'s version");
    } else {
      uint16_t command = msg.command;
      if ((command == 0xCB) && (lc->version() == Version::GC_EP3_NTE)) {
        command = 0xC9;
      }
      if (lc->game_join_command_queue) {
        lc->log.info_f("Client not ready to receive join commands; adding to queue");
        auto& cmd = lc->game_join_command_queue->emplace_back();
        cmd.command = command;
        cmd.flag = msg.flag;
        cmd.data.assign(reinterpret_cast<const char*>(data_to_send), size_to_send);
      } else {
        send_command(lc, command, msg.flag, data_to_send, size_to_send);
      }
    }
  };

  if (command_is_private(msg.command)) {
    if (msg.flag >= l->max_clients) {
      return;
    }
    auto target = l->clients[msg.flag];
    if (!target) {
      return;
    }
    send_to_client(target);

  } else {
    if (command_is_ep3) {
      for (auto& lc : l->clients) {
        if (!lc || (lc == c) || !is_ep3(lc->version())) {
          continue;
        }
        send_to_client(lc);
      }
      if ((msg.command == 0xCB) &&
          l->check_flag(Lobby::Flag::IS_SPECTATOR_TEAM) &&
          (def_flags & SDF::ALLOW_FORWARD_TO_WATCHED_LOBBY)) {
        auto watched_lobby = l->watched_lobby.lock();
        if (watched_lobby) {
          for (auto& lc : watched_lobby->clients) {
            if (lc && is_ep3(lc->version())) {
              send_to_client(lc);
            }
          }
        }
      }

    } else {
      for (auto& lc : l->clients) {
        if (lc && (lc != c)) {
          send_to_client(lc);
        }
      }
    }

    // Before battle, forward only chat commands to watcher lobbies; during battle, forward everything to watcher
    // lobbies. (This is necessary because if we forward everything before battle, the blocking menu subcommands cause
    // the battle setup menu to appear in the spectator room, which looks weird and is generally undesirable.)
    if ((l->ep3_server && (l->ep3_server->setup_phase != Episode3::SetupPhase::REGISTRATION)) ||
        (def_flags & SDF::ALWAYS_FORWARD_TO_WATCHERS)) {
      for (const auto& watcher_lobby : l->watcher_lobbies) {
        for (auto& target : watcher_lobby->clients) {
          if (target && is_ep3(target->version())) {
            send_to_client(target);
          }
        }
      }
    }

    if (l->battle_record && l->battle_record->battle_in_progress()) {
      auto type = ((msg.command & 0xF0) == 0xC0)
          ? Episode3::BattleRecord::Event::Type::EP3_GAME_COMMAND
          : Episode3::BattleRecord::Event::Type::GAME_COMMAND;
      l->battle_record->add_command(type, msg.data, msg.size);
    }
  }
}

static asio::awaitable<void> forward_subcommand_m(shared_ptr<Client> c, SubcommandMessage& msg) {
  forward_subcommand(c, msg);
  co_return;
}

template <typename CmdT, bool ForwardIfMissing = false, size_t EntityIDOffset = offsetof(G_EntityIDHeader, entity_id)>
asio::awaitable<void> forward_subcommand_with_entity_id_transcode_t(shared_ptr<Client> c, SubcommandMessage& msg) {
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

template <typename HeaderT>
asio::awaitable<void> forward_subcommand_with_entity_targets_transcode_and_track_hits_t(
    shared_ptr<Client> c, SubcommandMessage& msg) {
  // I'm lazy and this should never happen for item commands (since all players need to stay in sync)
  if (command_is_private(msg.command)) {
    throw runtime_error("entity subcommand sent via private command");
  }

  phosg::StringReader r(msg.data, msg.size);
  const auto& header = r.get<HeaderT>();
  if (header.target_count > 10) {
    throw runtime_error("invalid target count");
  }
  if (header.target_count > std::min<size_t>(header.header.size - sizeof(HeaderT) / 4, 10)) {
    throw runtime_error("invalid target list command");
  }
  const auto* targets = r.get_array<TargetEntry>(header.target_count);

  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("command cannot be used outside of a game");
  }

  struct TargetResolution {
    shared_ptr<const MapState::EnemyState> ene_st;
    shared_ptr<const MapState::ObjectState> obj_st;
    uint16_t entity_id;
  };
  vector<TargetResolution> resolutions;
  for (size_t z = 0; z < header.target_count; z++) {
    auto& res = resolutions.emplace_back(TargetResolution{nullptr, nullptr, targets[z].entity_id});
    if ((res.entity_id >= 0x1000) && (res.entity_id < 0x4000)) {
      auto ene_st = l->map_state->enemy_state_for_index(c->version(), res.entity_id - 0x1000);
      res.ene_st = ene_st;

      // Track hits for all resolved enemies
      c->log.info_f("Claiming last hit on E-{:03X}", ene_st->e_id);
      ene_st->set_last_hit_by_client_id(c->lobby_client_id);
      if (ene_st->alias_target_ene_st) {
        c->log.info_f("Claiming last hit on E-{:03X} (alias of E-{:03X})",
            ene_st->alias_target_ene_st->e_id, ene_st->e_id);
        ene_st->alias_target_ene_st->set_last_hit_by_client_id(c->lobby_client_id);
      }

    } else if ((res.entity_id >= 0x4000) && (res.entity_id < 0xFFFF)) {
      res.obj_st = l->map_state->object_state_for_index(c->version(), res.entity_id - 0x4000);
    }
  }

  for (auto& lc : l->clients) {
    if (!lc || lc == c) {
      continue;
    }
    if (c->version() != lc->version()) {
      HeaderT out_header = header;
      vector<TargetEntry> out_targets;
      out_header.header.subcommand = translate_subcommand_number(lc->version(), c->version(), header.header.subcommand);
      out_header.target_count = 0;
      if (out_header.header.subcommand) {
        for (size_t z = 0; z < header.target_count; z++) {
          uint16_t entity_id;
          const auto& res = resolutions[z];
          if (res.ene_st) {
            entity_id = 0x1000 | l->map_state->index_for_enemy_state(lc->version(), res.ene_st);
          } else if (res.obj_st) {
            entity_id = 0x4000 | l->map_state->index_for_object_state(lc->version(), res.obj_st);
          } else {
            entity_id = res.entity_id;
          }
          if (entity_id != 0xFFFF) {
            out_targets.emplace_back(TargetEntry{entity_id, targets[z].unknown_a2});
          }
        }
        size_t out_size = sizeof(HeaderT) + sizeof(TargetEntry) * out_targets.size();
        out_header.header.size = out_size >> 2;
        out_header.target_count = out_targets.size();
        send_command_t_vt(lc, msg.command, msg.flag, out_header, out_targets);
      } else {
        lc->log.info_f("Subcommand cannot be translated to client\'s version");
      }
    } else {
      send_command(lc, msg.command, msg.flag, msg.data, msg.size);
    }
  }
  co_return;
}

static void transcode_inventory_items(
    parray<PlayerInventoryItem, 30>& items,
    size_t num_items,
    Version from_version,
    Version to_version,
    shared_ptr<const ItemParameterTable> to_item_parameter_table) {
  if (num_items > 30) {
    throw runtime_error("invalid inventory item count");
  }
  if (from_version != to_version) {
    for (size_t z = 0; z < num_items; z++) {
      items[z].data.decode_for_version(from_version);
      items[z].data.encode_for_version(to_version, to_item_parameter_table);
    }
  }
  for (size_t z = num_items; z < 30; z++) {
    auto& item = items[z];
    item.present = 0;
    item.unknown_a1 = 0;
    item.flags = 0;
    item.data.clear();
  }
  if (is_v1(to_version)) {
    for (size_t z = 0; z < 30; z++) {
      auto& item = items[z];
      item.extension_data1 = 0x00;
      item.extension_data2 = 0x00;
    }
  } else {
    for (size_t z = 20; z < 30; z++) {
      items[z].extension_data1 = 0x00;
    }
    for (size_t z = 16; z < 30; z++) {
      items[z].extension_data2 = 0x00;
    }
  }
}

Parsed6x70Data::Parsed6x70Data(
    const G_SyncPlayerDispAndInventory_DCNTE_6x70& cmd,
    uint32_t guild_card_number,
    Version from_version,
    bool from_client_customization)
    : from_version(from_version),
      from_client_customization(from_client_customization),
      item_version(from_version),
      base(cmd.base),
      unknown_a5_nte(cmd.unknown_a5),
      unknown_a6_nte(cmd.unknown_a6),
      bonus_hp_from_materials(0),
      bonus_tp_from_materials(0),
      language(Language::JAPANESE),
      player_tag(0x00010000),
      guild_card_number(guild_card_number),
      unknown_a6(0),
      battle_team_number(0),
      telepipe(cmd.telepipe),
      death_flags(cmd.death_flags),
      hold_state(cmd.hold_state),
      area(cmd.area),
      game_flags(cmd.game_flags),
      game_flags_is_v3(false),
      visual(cmd.visual),
      stats(cmd.stats),
      num_items(cmd.num_items),
      items(cmd.items),
      floor(cmd.area),
      xb_user_id(this->default_xb_user_id()),
      xb_unknown_a16(0) {
  this->name = this->visual.name.decode(this->language);
}

Parsed6x70Data::Parsed6x70Data(
    const G_SyncPlayerDispAndInventory_DC112000_6x70& cmd,
    uint32_t guild_card_number,
    Language language,
    Version from_version,
    bool from_client_customization)
    : from_version(from_version),
      from_client_customization(from_client_customization),
      item_version(from_version),
      base(cmd.base),
      unknown_a5_nte(0),
      unknown_a6_nte(0),
      bonus_hp_from_materials(cmd.bonus_hp_from_materials),
      bonus_tp_from_materials(cmd.bonus_tp_from_materials),
      unknown_a5_112000(cmd.unknown_a5),
      language(language),
      player_tag(0x00010000),
      guild_card_number(guild_card_number),
      unknown_a6(0),
      battle_team_number(0),
      telepipe(cmd.telepipe),
      death_flags(cmd.death_flags),
      hold_state(cmd.hold_state),
      area(cmd.area),
      game_flags(cmd.game_flags),
      game_flags_is_v3(false),
      visual(cmd.visual),
      stats(cmd.stats),
      num_items(cmd.num_items),
      items(cmd.items),
      floor(cmd.area),
      xb_user_id(this->default_xb_user_id()),
      xb_unknown_a16(0) {
  this->name = this->visual.name.decode(this->language);
}

Parsed6x70Data::Parsed6x70Data(
    const G_SyncPlayerDispAndInventory_DC_PC_6x70& cmd,
    uint32_t guild_card_number,
    Version from_version,
    bool from_client_customization)
    : Parsed6x70Data(cmd.base, guild_card_number, from_version, from_client_customization) {
  this->stats = cmd.stats;
  this->num_items = cmd.num_items;
  this->items = cmd.items;
  this->floor = cmd.base.area;
  this->xb_user_id = this->default_xb_user_id();
  this->xb_unknown_a16 = 0;
  this->name = this->visual.name.decode(this->language);
}

Parsed6x70Data::Parsed6x70Data(
    const G_SyncPlayerDispAndInventory_GC_6x70& cmd,
    uint32_t guild_card_number,
    Version from_version,
    bool from_client_customization)
    : Parsed6x70Data(cmd.base, guild_card_number, from_version, from_client_customization) {
  this->game_flags_is_v3 = true;
  this->stats = cmd.stats;
  this->num_items = cmd.num_items;
  this->items = cmd.items;
  this->floor = cmd.floor;
  this->xb_user_id = this->default_xb_user_id();
  this->xb_unknown_a16 = 0;
  this->name = this->visual.name.decode(this->language);
}

Parsed6x70Data::Parsed6x70Data(
    const G_SyncPlayerDispAndInventory_XB_6x70& cmd,
    uint32_t guild_card_number,
    Version from_version,
    bool from_client_customization)
    : Parsed6x70Data(cmd.base, guild_card_number, from_version, from_client_customization) {
  this->game_flags_is_v3 = true;
  this->stats = cmd.stats;
  this->num_items = cmd.num_items;
  this->items = cmd.items;
  this->floor = cmd.floor;
  this->xb_user_id = (static_cast<uint64_t>(cmd.xb_user_id_high) << 32) | cmd.xb_user_id_low;
  this->xb_unknown_a16 = cmd.unknown_a16;
  this->name = this->visual.name.decode(this->language);
}

Parsed6x70Data::Parsed6x70Data(
    const G_SyncPlayerDispAndInventory_BB_6x70& cmd,
    uint32_t guild_card_number,
    Version from_version,
    bool from_client_customization)
    : Parsed6x70Data(cmd.base, guild_card_number, from_version, from_client_customization) {
  this->game_flags_is_v3 = true;
  this->stats = cmd.stats;
  this->num_items = cmd.num_items;
  this->items = cmd.items;
  this->floor = cmd.floor;
  this->xb_user_id = this->default_xb_user_id();
  this->xb_unknown_a16 = cmd.unknown_a16;
  this->name = cmd.name.decode(this->language);
  this->visual.name.encode(this->name, this->language);
}

G_SyncPlayerDispAndInventory_DCNTE_6x70 Parsed6x70Data::as_dc_nte(shared_ptr<ServerState> s) const {
  G_SyncPlayerDispAndInventory_DCNTE_6x70 ret;
  ret.base = this->base;
  ret.unknown_a5 = this->unknown_a5_nte;
  ret.unknown_a6 = this->unknown_a6;
  ret.telepipe = this->telepipe;
  ret.death_flags = this->death_flags;
  ret.hold_state = this->hold_state;
  ret.area = this->area;
  ret.game_flags = this->get_game_flags(false);
  ret.visual = this->visual;
  ret.stats = this->stats;
  ret.num_items = this->num_items;
  ret.items = this->items;

  transcode_inventory_items(
      ret.items,
      ret.num_items,
      this->item_version,
      Version::DC_NTE,
      s->item_parameter_table_for_encode(Version::DC_NTE));
  ret.visual.enforce_lobby_join_limits_for_version(Version::DC_NTE);

  uint32_t name_color = s->name_color_for_client(this->from_version, this->from_client_customization);
  if (name_color) {
    ret.visual.name_color = name_color;
    ret.visual.compute_name_color_checksum();
  }
  return ret;
}

G_SyncPlayerDispAndInventory_DC112000_6x70 Parsed6x70Data::as_dc_112000(shared_ptr<ServerState> s) const {
  G_SyncPlayerDispAndInventory_DC112000_6x70 ret;
  ret.base = this->base;
  ret.bonus_hp_from_materials = this->bonus_hp_from_materials;
  ret.bonus_tp_from_materials = this->bonus_tp_from_materials;
  ret.unknown_a5 = this->unknown_a5_112000;
  ret.telepipe = this->telepipe;
  ret.death_flags = this->death_flags;
  ret.hold_state = this->hold_state;
  ret.area = this->area;
  ret.game_flags = this->get_game_flags(false);
  ret.visual = this->visual;
  ret.stats = this->stats;
  ret.num_items = this->num_items;
  ret.items = this->items;

  transcode_inventory_items(
      ret.items,
      ret.num_items,
      this->item_version,
      Version::DC_11_2000,
      s->item_parameter_table_for_encode(Version::DC_11_2000));
  ret.visual.enforce_lobby_join_limits_for_version(Version::DC_11_2000);

  uint32_t name_color = s->name_color_for_client(this->from_version, this->from_client_customization);
  if (name_color) {
    ret.visual.name_color = name_color;
    ret.visual.compute_name_color_checksum();
  }

  return ret;
}

G_SyncPlayerDispAndInventory_DC_PC_6x70 Parsed6x70Data::as_dc_pc(shared_ptr<ServerState> s, Version to_version) const {
  G_SyncPlayerDispAndInventory_DC_PC_6x70 ret;
  ret.base = this->base_v1(false);
  ret.stats = this->stats;
  ret.num_items = this->num_items;
  ret.items = this->items;

  transcode_inventory_items(
      ret.items, ret.num_items, this->item_version, to_version, s->item_parameter_table_for_encode(to_version));
  ret.base.visual.enforce_lobby_join_limits_for_version(to_version);

  uint32_t name_color = s->name_color_for_client(this->from_version, this->from_client_customization);
  if (name_color) {
    ret.base.visual.name_color = name_color;
    ret.base.visual.compute_name_color_checksum();
  }

  return ret;
}

G_SyncPlayerDispAndInventory_GC_6x70 Parsed6x70Data::as_gc_gcnte(shared_ptr<ServerState> s, Version to_version) const {
  G_SyncPlayerDispAndInventory_GC_6x70 ret;
  ret.base = this->base_v1(!is_v1_or_v2(to_version));
  ret.stats = this->stats;
  ret.num_items = this->num_items;
  ret.items = this->items;
  ret.floor = this->floor;

  transcode_inventory_items(
      ret.items, ret.num_items, this->item_version, to_version, s->item_parameter_table_for_encode(to_version));
  ret.base.visual.enforce_lobby_join_limits_for_version(to_version);

  uint32_t name_color = s->name_color_for_client(this->from_version, this->from_client_customization);
  if (name_color) {
    ret.base.visual.name_color = name_color;
    if (is_v1_or_v2(to_version)) {
      ret.base.visual.compute_name_color_checksum();
    } else {
      ret.base.visual.name_color_checksum = 0;
    }
  }

  return ret;
}

G_SyncPlayerDispAndInventory_XB_6x70 Parsed6x70Data::as_xb(shared_ptr<ServerState> s) const {
  G_SyncPlayerDispAndInventory_XB_6x70 ret;
  ret.base = this->base_v1(true);
  ret.stats = this->stats;
  ret.num_items = this->num_items;
  ret.items = this->items;
  ret.floor = this->floor;
  ret.xb_user_id_high = this->xb_user_id >> 32;
  ret.xb_user_id_low = this->xb_user_id;
  ret.unknown_a16 = this->xb_unknown_a16;

  transcode_inventory_items(
      ret.items, ret.num_items, this->item_version, Version::XB_V3, s->item_parameter_table_for_encode(Version::XB_V3));
  ret.base.visual.enforce_lobby_join_limits_for_version(Version::XB_V3);

  uint32_t name_color = s->name_color_for_client(this->from_version, this->from_client_customization);
  if (name_color) {
    ret.base.visual.name_color = name_color;
    ret.base.visual.name_color_checksum = 0;
  }

  return ret;
}

G_SyncPlayerDispAndInventory_BB_6x70 Parsed6x70Data::as_bb(shared_ptr<ServerState> s, Language language) const {
  G_SyncPlayerDispAndInventory_BB_6x70 ret;
  ret.base = this->base_v1(true);
  ret.name.encode(this->name, language);
  ret.base.visual.name.encode(std::format("{:10}", this->guild_card_number), language);
  ret.stats = this->stats;
  ret.num_items = this->num_items;
  ret.items = this->items;
  ret.floor = this->floor;
  ret.xb_user_id_high = this->xb_user_id >> 32;
  ret.xb_user_id_low = this->xb_user_id;
  ret.unknown_a16 = this->xb_unknown_a16;

  transcode_inventory_items(
      ret.items, ret.num_items, this->item_version, Version::BB_V4, s->item_parameter_table_for_encode(Version::BB_V4));
  ret.base.visual.enforce_lobby_join_limits_for_version(Version::BB_V4);

  uint32_t name_color = s->name_color_for_client(this->from_version, this->from_client_customization);
  if (name_color) {
    ret.base.visual.name_color = name_color;
    ret.base.visual.name_color_checksum = 0;
  }

  return ret;
}

uint64_t Parsed6x70Data::default_xb_user_id() const {
  return (0xAE00000000000000 | this->guild_card_number);
}

void Parsed6x70Data::clear_v1_unused_item_fields() {
  for (size_t z = 0; z < min<uint32_t>(this->num_items, 30); z++) {
    auto& item = this->items[z];
    item.unknown_a1 = 0;
    item.extension_data1 = 0;
    item.extension_data2 = 0;
  }
}

void Parsed6x70Data::clear_dc_protos_unused_item_fields() {
  for (size_t z = 0; z < min<uint32_t>(this->num_items, 30); z++) {
    auto& item = this->items[z];
    item.unknown_a1 = 0;
    item.extension_data1 = 0;
    item.extension_data2 = 0;
    item.data.data2d = 0;
  }
}

Parsed6x70Data::Parsed6x70Data(
    const G_6x70_Base_V1& base,
    uint32_t guild_card_number,
    Version from_version,
    bool from_client_customization)
    : from_version(from_version),
      from_client_customization(from_client_customization),
      item_version(this->from_version),
      base(base.base),
      bonus_hp_from_materials(base.bonus_hp_from_materials),
      bonus_tp_from_materials(base.bonus_tp_from_materials),
      permanent_status_effect(base.permanent_status_effect),
      temporary_status_effect(base.temporary_status_effect),
      attack_status_effect(base.attack_status_effect),
      defense_status_effect(base.defense_status_effect),
      unused_status_effect(base.unused_status_effect),
      language(static_cast<Language>(base.language32.load())),
      player_tag(base.player_tag),
      guild_card_number(guild_card_number), // Ignore the client's GC#
      unknown_a6(base.unknown_a6),
      battle_team_number(base.battle_team_number),
      telepipe(base.telepipe),
      death_flags(base.death_flags),
      hold_state(base.hold_state),
      area(base.area),
      game_flags(base.game_flags),
      game_flags_is_v3(!is_v1_or_v2(from_version)),
      technique_levels_v1(base.technique_levels_v1),
      visual(base.visual) {}

G_6x70_Base_V1 Parsed6x70Data::base_v1(bool is_v3) const {
  G_6x70_Base_V1 ret;
  ret.base = this->base;
  ret.bonus_hp_from_materials = this->bonus_hp_from_materials;
  ret.bonus_tp_from_materials = this->bonus_tp_from_materials;
  ret.permanent_status_effect = this->permanent_status_effect;
  ret.temporary_status_effect = this->temporary_status_effect;
  ret.attack_status_effect = this->attack_status_effect;
  ret.defense_status_effect = this->defense_status_effect;
  ret.unused_status_effect = this->unused_status_effect;
  ret.language32 = static_cast<size_t>(this->language);
  ret.player_tag = this->player_tag;
  ret.guild_card_number = this->guild_card_number;
  ret.unknown_a6 = this->unknown_a6;
  ret.battle_team_number = this->battle_team_number;
  ret.telepipe = this->telepipe;
  ret.death_flags = this->death_flags;
  ret.hold_state = this->hold_state;
  ret.area = this->area;
  ret.game_flags = this->get_game_flags(is_v3);
  ret.technique_levels_v1 = this->technique_levels_v1;
  ret.visual = this->visual;
  return ret;
}

uint32_t Parsed6x70Data::convert_game_flags(uint32_t game_flags, bool to_v3) {
  // The format of game_flags for players was changed significantly between v2 and v3, and not accounting for this
  // results in odd effects like other characters not appearing when joining a game. Unfortunately, some bits were
  // deleted on v3 and other bits were added, so it doesn't suffice to simply store the most complete format of this
  // field - we have to be able to convert between the two.

  // Bits on v2: JIHCBAzy xwvutsrq ponmlkji hgfedcba
  // Bits on v3: JIHGFEDC BAzyxwvu srqponkj hgfedcba
  // The bits ilmt were removed in v3 and the bits to their left were shifted right. The bits DEFG were added in v3 and
  // do not exist on v2. Known meanings for these bits so far:
  //   o = is dead
  //   n = should play hit animation
  //   y = is near enemy
  //   H = is enemy?
  //   I = is object? (some entities have both H and I set though)
  //   J = is item

  if (to_v3) {
    return (game_flags & 0xE00000FF) |
        ((game_flags & 0x00000600) >> 1) |
        ((game_flags & 0x0007E000) >> 3) |
        ((game_flags & 0x1FF00000) >> 4);
  } else {
    return (game_flags & 0xE00000FF) |
        ((game_flags << 1) & 0x00000600) |
        ((game_flags << 3) & 0x0007E000) |
        ((game_flags << 4) & 0x1FF00000);
  }
}

uint32_t Parsed6x70Data::get_game_flags(bool is_v3) const {
  return (this->game_flags_is_v3 == is_v3)
      ? this->game_flags
      : Parsed6x70Data::convert_game_flags(this->game_flags, is_v3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Item commands

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// EXP/Drop Item commands

G_SpecializableItemDropRequest_6xA2 normalize_drop_request(const void* data, size_t size) {
  G_SpecializableItemDropRequest_6xA2 cmd;
  if (size == sizeof(G_SpecializableItemDropRequest_6xA2)) {
    cmd = check_size_t<G_SpecializableItemDropRequest_6xA2>(data, size);
  } else if (size == sizeof(G_StandardDropItemRequest_PC_V3_BB_6x60)) {
    const auto& in_cmd = check_size_t<G_StandardDropItemRequest_PC_V3_BB_6x60>(data, size);
    cmd.header = in_cmd.header;
    cmd.entity_index = in_cmd.entity_index;
    cmd.floor = in_cmd.floor;
    cmd.rt_index = in_cmd.rt_index;
    cmd.pos = in_cmd.pos;
    cmd.ignore_def = in_cmd.ignore_def;
    cmd.effective_area = in_cmd.effective_area;
  } else {
    const auto& in_cmd = check_size_t<G_StandardDropItemRequest_DC_6x60>(data, size);
    cmd.header = in_cmd.header;
    cmd.entity_index = in_cmd.entity_index;
    cmd.floor = in_cmd.floor;
    cmd.rt_index = in_cmd.rt_index;
    cmd.pos = in_cmd.pos;
    cmd.ignore_def = in_cmd.ignore_def;
    cmd.effective_area = in_cmd.floor;
  }
  return cmd;
}

DropReconcileResult reconcile_drop_request_with_map(
    shared_ptr<Client> c,
    G_SpecializableItemDropRequest_6xA2& cmd,
    Difficulty difficulty,
    uint8_t event,
    shared_ptr<MapState> map,
    bool mark_drop) {
  Version version = c->version();
  bool is_box = (cmd.rt_index == 0x30);

  DropReconcileResult res;
  res.effective_rt_index = 0xFF;
  res.should_drop = true;
  res.ignore_def = (cmd.ignore_def != 0);
  if (!map) {
    return res;
  }

  if (is_box) {
    res.obj_st = map->object_state_for_index(version, cmd.floor, cmd.entity_index);
    if (!res.obj_st->super_obj) {
      throw std::runtime_error("referenced object from drop request is a player trap");
    }
    const auto* set_entry = res.obj_st->super_obj->version(version).set_entry;
    if (!set_entry) {
      throw std::runtime_error("object set entry is missing");
    }
    string type_name = MapFile::name_for_object_type(set_entry->base_type, version);
    c->log.info_f("Drop check for K-{:03X} {} {}",
        res.obj_st->k_id,
        res.ignore_def ? 'G' : 'S',
        type_name);
    if (cmd.floor != res.obj_st->super_obj->floor) {
      c->log.warning_f("Floor {:02X} from command does not match object\'s expected floor {:02X}",
          cmd.floor, res.obj_st->super_obj->floor);
    }
    if (is_v1_or_v2(version) && (version != Version::GC_NTE)) {
      // V1/V2 don't have 6xA2, so we can't get ignore_def or the object parameters from the client on those versions
      cmd.param3 = set_entry->param3;
      cmd.param4 = set_entry->param4;
      cmd.param5 = set_entry->param5;
      cmd.param6 = set_entry->param6;
    }
    bool object_ignore_def = (set_entry->param1 > 0.0);
    if (res.ignore_def != object_ignore_def) {
      c->log.warning_f("ignore_def value {} from command does not match object\'s expected ignore_def {} (from p1={:g})",
          res.ignore_def ? "true" : "false", object_ignore_def ? "true" : "false", set_entry->param1);
    }
    if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
      string type_name = MapFile::name_for_object_type(set_entry->base_type, version);
      send_text_message_fmt(c, "$C5K-{:03X} {} {}", res.obj_st->k_id, res.ignore_def ? 'G' : 'S', type_name);
    }

  } else {
    res.ref_ene_st = map->enemy_state_for_index(version, cmd.floor, cmd.entity_index);
    res.target_ene_st = res.ref_ene_st->alias_target_ene_st ? res.ref_ene_st->alias_target_ene_st : res.ref_ene_st;
    uint8_t area = map->floor_to_area.at(res.target_ene_st->super_ene->floor);
    EnemyType type = res.target_ene_st->type(version, area, difficulty, event);
    c->log.info_f("Drop check for E-{:03X} (target E-{:03X}, type {})",
        res.ref_ene_st->e_id, res.target_ene_st->e_id, phosg::name_for_enum(type));
    res.effective_rt_index = type_definition_for_enemy(type).rt_index;
    bool mismatched_rt_index = false;
    if (cmd.rt_index != res.effective_rt_index) {
      // Special cases: BULCLAW => BULK and DARK_GUNNER => DEATH_GUNNER
      if (cmd.rt_index == 0x27 && type == EnemyType::BULCLAW) {
        c->log.info_f("E-{:03X} killed as BULK instead of BULCLAW", res.target_ene_st->e_id);
        res.effective_rt_index = 0x27;
      } else if (cmd.rt_index == 0x23 && type == EnemyType::DARK_GUNNER) {
        c->log.info_f("E-{:03X} killed as DEATH_GUNNER instead of DARK_GUNNER", res.target_ene_st->e_id);
        res.effective_rt_index = 0x23;
      } else {
        c->log.warning_f("rt_index {:02X} from command does not match entity\'s expected index {:02X}",
            cmd.rt_index, res.effective_rt_index);
        mismatched_rt_index = true;
      }
    }
    if (cmd.floor != res.target_ene_st->super_ene->floor) {
      c->log.warning_f("Floor {:02X} from command does not match entity\'s expected floor {:02X}",
          cmd.floor, res.target_ene_st->super_ene->floor);
    }
    if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
      std::string rt_index_str = mismatched_rt_index
          ? std::format(" $C4{:02X}->{:02X}$C5", cmd.rt_index, res.effective_rt_index)
          : std::format(" {:02X}", res.effective_rt_index);
      send_text_message_fmt(c, "$C5E-{:03X}{} {}", res.target_ene_st->e_id, rt_index_str, phosg::name_for_enum(type));
    }
  }

  if (mark_drop) {
    if (res.obj_st) {
      if (res.obj_st->item_drop_checked) {
        c->log.info_f("Drop check has already occurred for K-{:03X}; skipping it", res.obj_st->k_id);
        res.should_drop = false;
      } else {
        res.obj_st->item_drop_checked = true;
      }
    }
    if (res.target_ene_st) {
      if (res.target_ene_st->server_flags & MapState::EnemyState::Flag::ITEM_DROPPED) {
        c->log.info_f("Drop check has already occurred for E-{:03X}; skipping it", res.target_ene_st->e_id);
        res.should_drop = false;
      } else {
        res.target_ene_st->server_flags |= MapState::EnemyState::Flag::ITEM_DROPPED;
      }
    }
  }

  return res;
}

asio::awaitable<void> on_entity_drop_item_request(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto s = c->require_server_state();
  auto l = c->require_lobby();
  if (!l->is_game() || l->episode == Episode::EP3) {
    co_return;
  }

  // Note: We always call reconcile_drop_request_with_map, even in client drop mode, so that we can correctly mark
  // enemies and objects as having dropped their items in persistent games.
  G_SpecializableItemDropRequest_6xA2 cmd = normalize_drop_request(msg.data, msg.size);
  auto rec = reconcile_drop_request_with_map(c, cmd, l->difficulty, l->event, l->map_state, true);

  ServerDropMode drop_mode = l->drop_mode;
  bool forced_server_shared_for_bb_leader = false;
  switch (drop_mode) {
    case ServerDropMode::DISABLED:
      co_return;
    case ServerDropMode::CLIENT: {
      // If the leader is BB, use SERVER_SHARED instead
      // TODO: We should also use server drops if any clients have incompatible object lists, since they might generate
      // incorrect IDs for items and we can't override them
      auto leader = l->clients[l->leader_id];
      if (leader && leader->version() == Version::BB_V4) {
        drop_mode = ServerDropMode::SERVER_SHARED;
        forced_server_shared_for_bb_leader = true;
        break;
      } else {
        forward_subcommand(c, msg);
        co_return;
      }
    }
    case ServerDropMode::SERVER_SHARED:
    case ServerDropMode::SERVER_DUPLICATE:
    case ServerDropMode::SERVER_PRIVATE:
      break;
    default:
      throw logic_error("invalid drop mode");
  }
  if (forced_server_shared_for_bb_leader) {
    l->log.info_f(
        "triage-drop-mode: requested={} effective={} reason=bb_leader floor={:02X} area={:02X} source_client={:X}",
        phosg::name_for_enum(ServerDropMode::CLIENT),
        phosg::name_for_enum(drop_mode),
        cmd.floor,
        cmd.effective_area,
        c->lobby_client_id);
  }

  if (rec.should_drop) {
    auto generate_item = [&]() -> ItemCreator::DropResult {
      if (rec.obj_st) {
        if (rec.ignore_def) {
          l->log.info_f("Creating item from box {:04X} => K-{:03X} (area {:02X})",
              cmd.entity_index, rec.obj_st->k_id, cmd.effective_area);
          return l->item_creator->on_box_item_drop(cmd.effective_area);
        } else {
          l->log.info_f(
              "Creating item from box {:04X} => K-{:03X} (area {:02X}; specialized with {:g} {:08X} {:08X} {:08X})",
              cmd.entity_index, rec.obj_st->k_id, cmd.effective_area, cmd.param3, cmd.param4, cmd.param5, cmd.param6);
          return l->item_creator->on_specialized_box_item_drop(
              cmd.effective_area, cmd.param3, cmd.param4, cmd.param5, cmd.param6);
        }
      } else if (rec.target_ene_st) {
        l->log.info_f("Creating item from enemy {:04X} => E-{:03X} (area {:02X})",
            cmd.entity_index, rec.target_ene_st->e_id, cmd.effective_area);
        return l->item_creator->on_monster_item_drop(rec.effective_rt_index, cmd.effective_area);
      } else {
        throw runtime_error("neither object nor enemy were present");
      }
    };

    auto get_entity_index = [&](Version v) -> uint16_t {
      if (rec.obj_st) {
        return l->map_state->index_for_object_state(v, rec.obj_st);
      } else if (rec.ref_ene_st) {
        return l->map_state->index_for_enemy_state(v, rec.ref_ene_st);
      } else {
        return 0xFFFF;
      }
    };

    switch (drop_mode) {
      case ServerDropMode::DISABLED:
      case ServerDropMode::CLIENT:
        throw logic_error("unhandled simple drop mode");
      case ServerDropMode::SERVER_SHARED:
      case ServerDropMode::SERVER_DUPLICATE: {
        auto res = generate_item();
        if (res.item.empty()) {
          l->log.info_f("No item was created");
        } else {
          string name = s->describe_item(c->version(), res.item);
          l->log.info_f("Entity {:04X} (area {:02X}) created item {}", cmd.entity_index, cmd.effective_area, name);
          if (drop_mode == ServerDropMode::SERVER_DUPLICATE) {
            for (const auto& lc : l->clients) {
              if (lc && (rec.obj_st || (lc->floor == cmd.floor))) {
                res.item.id = l->generate_item_id(0xFF);
                l->log.info_f("Creating item {:08X} at {:02X}:{:g},{:g} for {}",
                    res.item.id, cmd.floor, cmd.pos.x, cmd.pos.z, lc->channel->name);
                l->add_item(cmd.floor, res.item, cmd.pos, rec.obj_st, rec.ref_ene_st, 0x1000 | (1 << lc->lobby_client_id));
                send_drop_item_to_channel(
                    s, lc->channel, res.item, rec.obj_st ? 2 : 1, cmd.floor, cmd.pos, get_entity_index(lc->version()));
                send_item_notification_if_needed(lc, res.item, res.is_from_rare_table);
              }
            }

          } else {
            res.item.id = l->generate_item_id(0xFF);
            l->log.info_f("Creating item {:08X} at {:02X}:{:g},{:g} for all clients",
                res.item.id, cmd.floor, cmd.pos.x, cmd.pos.z);
            l->add_item(cmd.floor, res.item, cmd.pos, rec.obj_st, rec.ref_ene_st, 0x100F);
            for (auto lc : l->clients) {
              if (lc) {
                send_drop_item_to_channel(
                    s, lc->channel, res.item, rec.obj_st ? 2 : 1, cmd.floor, cmd.pos, get_entity_index(lc->version()));
                send_item_notification_if_needed(lc, res.item, res.is_from_rare_table);
              }
            }
          }
        }
        break;
      }
      case ServerDropMode::SERVER_PRIVATE: {
        for (const auto& lc : l->clients) {
          if (lc && (rec.obj_st || (lc->floor == cmd.floor))) {
            auto res = generate_item();
            if (res.item.empty()) {
              l->log.info_f("No item was created for {}", lc->channel->name);
            } else {
              string name = s->describe_item(lc->version(), res.item);
              l->log.info_f("Entity {:04X} (area {:02X}) created item {}", cmd.entity_index, cmd.effective_area, name);
              res.item.id = l->generate_item_id(0xFF);
              l->log.info_f("Creating item {:08X} at {:02X}:{:g},{:g} for {}",
                  res.item.id, cmd.floor, cmd.pos.x, cmd.pos.z, lc->channel->name);
              l->add_item(cmd.floor, res.item, cmd.pos, rec.obj_st, rec.ref_ene_st, 0x1000 | (1 << lc->lobby_client_id));
              send_drop_item_to_channel(
                  s, lc->channel, res.item, rec.obj_st ? 2 : 1, cmd.floor, cmd.pos, get_entity_index(lc->version()));
              send_item_notification_if_needed(lc, res.item, res.is_from_rare_table);
            }
          }
        }
        break;
      }
      default:
        throw logic_error("invalid drop mode");
    }
  }
}

void send_max_level_notification_if_needed(shared_ptr<Client> c) {
  auto s = c->require_server_state();
  if (!s->notify_server_for_max_level_achieved) {
    return;
  }

  uint32_t max_level;
  if (is_v1(c->version())) {
    max_level = 99;
  } else if (!is_ep3(c->version())) {
    max_level = 199;
  } else {
    max_level = 998;
  }

  auto p = c->character_file();
  if (p->disp.stats.level == max_level) {
    string name = p->disp.name.decode(c->language());
    size_t level_for_str = max_level + 1;
    string message = std::format("$C6{}$C7\nGC: {}\nhas reached Level $C6{}",
        name, c->login->account->account_id, level_for_str);
    string bb_message = std::format("$C6{}$C7 (GC: {}) has reached Level $C6{}",
        name, c->login->account->account_id, level_for_str);
    for (auto& it : s->game_server->all_clients()) {
      if ((it != c) && it->login && !is_patch(it->version()) && it->lobby.lock()) {
        send_text_or_scrolling_message(it, message, bb_message);
      }
    }
  }
}

void assert_quest_item_create_allowed(shared_ptr<const Lobby> l, const ItemData& item) {
  // We always enforce these restrictions if the quest has any restrictions defined, even if the client has cheat mode
  // enabled or has debug enabled. If the client can cheat, there are much easier ways to create items (e.g. the $item
  // chat command) than spoofing these quest item creation commands, so they should just do that instead.

  if (!l->quest) {
    throw std::runtime_error("cannot create quest reward item with no quest loaded");
  }
  if (l->quest->meta.create_item_mask_entries.empty()) {
    l->log.warning_f("Player created quest item {}, but the loaded quest ({}) has no item creation masks", item.hex(), l->quest->meta.name);
    return;
  }

  for (const auto& mask : l->quest->meta.create_item_mask_entries) {
    if (mask.match(item)) {
      l->log.info_f("Player created quest item {} which matches create item mask {}", item.hex(), mask.str());
      return;
    }
  }
  l->log.warning_f("Player attempted to create quest item {}, but it does not match any create item mask", item.hex());
  l->log.info_f("Quest has {} create item masks:", l->quest->meta.create_item_mask_entries.size());
  for (const auto& mask : l->quest->meta.create_item_mask_entries) {
    l->log.info_f("  {}", mask.str());
  }
  throw std::runtime_error("invalid item creation from quest");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// This makes it easier to see which handlers exist on which prototypes via syntax highlighting
constexpr uint8_t NONE = 0x00;

const vector<SubcommandDefinition> subcommand_definitions{
    // {DC NTE, 11/2000, all other versions, handler}
    /* 6x00 */ {0x00, 0x00, 0x00, on_invalid},
    /* 6x01 */ {0x01, 0x01, 0x01, on_invalid},
    /* 6x02 */ {0x02, 0x02, 0x02, forward_subcommand_m},
    /* 6x03 */ {0x03, 0x03, 0x03, forward_subcommand_m},
    /* 6x04 */ {0x04, 0x04, 0x04, forward_subcommand_m},
    /* 6x05 */ {0x05, 0x05, 0x05, on_switch_state_changed},
    /* 6x06 */ {0x06, 0x06, 0x06, on_send_guild_card},
    /* 6x07 */ {0x07, 0x07, 0x07, on_symbol_chat, SDF::ALWAYS_FORWARD_TO_WATCHERS},
    /* 6x08 */ {0x08, 0x08, 0x08, on_invalid},
    /* 6x09 */ {0x09, 0x09, 0x09, on_invalid}, // See notes in CommandFormats.hh
    /* 6x0A */ {0x0A, 0x0A, 0x0A, on_update_enemy_state},
    /* 6x0B */ {0x0B, 0x0B, 0x0B, on_update_object_state_6x0B},
    /* 6x0C */ {0x0C, 0x0C, 0x0C, on_received_condition},
    /* 6x0D */ {NONE, NONE, 0x0D, on_forward_check_game},
    /* 6x0E */ {NONE, NONE, 0x0E, forward_subcommand_with_entity_id_transcode_t<G_ClearNegativeStatusEffects_6x0E>},
    /* 6x0F */ {NONE, NONE, 0x0F, on_invalid},
    /* 6x10 */ {0x0E, 0x0E, 0x10, forward_subcommand_with_entity_id_transcode_t<G_DragonBossActions_6x10_6x11>},
    /* 6x11 */ {0x0F, 0x0F, 0x11, forward_subcommand_with_entity_id_transcode_t<G_DragonBossActions_6x10_6x11>},
    /* 6x12 */ {0x10, 0x10, 0x12, on_dragon_actions_6x12},
    /* 6x13 */ {0x11, 0x11, 0x13, forward_subcommand_with_entity_id_transcode_t<G_DeRolLeBossActions_6x13>},
    /* 6x14 */ {0x12, 0x12, 0x14, forward_subcommand_with_entity_id_transcode_t<G_DeRolLeBossActionsWithTarget_6x14>},
    /* 6x15 */ {0x13, 0x13, 0x15, forward_subcommand_with_entity_id_transcode_t<G_VolOptBossActions_6x15>},
    /* 6x16 */ {0x14, 0x14, 0x16, on_vol_opt_actions_6x16},
    /* 6x17 */ {0x15, 0x15, 0x17, on_set_entity_pos_and_angle_6x17},
    /* 6x18 */ {0x16, 0x16, 0x18, forward_subcommand_with_entity_id_transcode_t<G_VolOpt2BossActions_6x18>},
    /* 6x19 */ {0x17, 0x17, 0x19, forward_subcommand_with_entity_id_transcode_t<G_DarkFalzActions_6x19>},
    /* 6x1A */ {NONE, NONE, 0x1A, on_invalid},
    /* 6x1B */ {NONE, 0x19, 0x1B, on_forward_check_game},
    /* 6x1C */ {NONE, 0x1A, 0x1C, on_forward_check_game},
    /* 6x1D */ {0x19, 0x1B, 0x1D, on_invalid},
    /* 6x1E */ {0x1A, 0x1C, 0x1E, on_invalid},
    /* 6x1F */ {0x1B, 0x1D, 0x1F, on_change_floor_6x1F},
    /* 6x20 */ {0x1C, 0x1E, 0x20, on_movement_6x20},
    /* 6x21 */ {0x1D, 0x1F, 0x21, on_change_floor_6x21},
    /* 6x22 */ {0x1E, 0x20, 0x22, on_forward_check_client},
    /* 6x23 */ {0x1F, 0x21, 0x23, on_set_player_visible},
    /* 6x24 */ {0x20, 0x22, 0x24, on_movement_6x24},
    /* 6x25 */ {0x21, 0x23, 0x25, on_equip_item},
    /* 6x26 */ {0x22, 0x24, 0x26, on_unequip_item}, // TODO: Why does BB allow this in the lobby?
    /* 6x27 */ {0x23, 0x25, 0x27, on_use_item},
    /* 6x28 */ {0x24, 0x26, 0x28, on_feed_mag},
    /* 6x29 */ {0x25, 0x27, 0x29, on_destroy_inventory_item},
    /* 6x2A */ {0x26, 0x28, 0x2A, on_player_drop_item},
    /* 6x2B */ {0x27, 0x29, 0x2B, on_create_inventory_item},
    /* 6x2C */ {0x28, 0x2A, 0x2C, on_forward_check_client},
    /* 6x2D */ {0x29, 0x2B, 0x2D, on_forward_check_client},
    /* 6x2E */ {0x2A, 0x2C, 0x2E, on_forward_check_client},
    /* 6x2F */ {0x2B, 0x2D, 0x2F, on_change_hp_6x2F},
    /* 6x30 */ {0x2C, 0x2E, 0x30, on_level_up},
    /* 6x31 */ {0x2D, 0x2F, 0x31, on_forward_check_game},
    /* 6x32 */ {NONE, NONE, 0x32, on_forward_check_game},
    /* 6x33 */ {0x2E, 0x30, 0x33, on_forward_check_game},
    /* 6x34 */ {0x2F, 0x31, 0x34, on_forward_check_game},
    /* 6x35 */ {0x30, NONE, 0x35, on_invalid},
    /* 6x36 */ {0x31, 0x32, 0x36, on_forward_check_game},
    /* 6x37 */ {0x32, 0x33, 0x37, on_forward_check_game},
    /* 6x38 */ {NONE, 0x34, 0x38, on_forward_check_game},
    /* NONE */ {0x33, 0x35, NONE, on_forward_check_game},
    /* 6x39 */ {NONE, 0x36, 0x39, on_forward_check_game},
    /* 6x3A */ {NONE, 0x37, 0x3A, on_forward_check_game},
    /* 6x3B */ {NONE, 0x38, 0x3B, forward_subcommand_m},
    /* 6x3C */ {0x34, 0x39, 0x3C, forward_subcommand_m},
    /* 6x3D */ {0x35, 0x3A, 0x3D, on_invalid},
    /* 6x3E */ {NONE, NONE, 0x3E, on_movement_6x3E},
    /* 6x3F */ {0x36, 0x3B, 0x3F, on_movement_6x3F},
    /* 6x40 */ {0x37, 0x3C, 0x40, on_movement_6x40},
    /* 6x41 */ {0x38, 0x3D, 0x41, on_movement_6x41_6x42},
    /* 6x42 */ {0x39, 0x3E, 0x42, on_movement_6x41_6x42},
    /* 6x43 */ {0x3A, 0x3F, 0x43, on_forward_check_game_client},
    /* 6x44 */ {0x3B, 0x40, 0x44, on_forward_check_game_client},
    /* 6x45 */ {0x3C, 0x41, 0x45, on_forward_check_game_client},
    /* 6x46 */ {NONE, 0x42, 0x46, forward_subcommand_with_entity_targets_transcode_and_track_hits_t<G_AttackFinished_Header_6x46>},
    /* 6x47 */ {0x3D, 0x43, 0x47, forward_subcommand_with_entity_targets_transcode_and_track_hits_t<G_CastTechnique_Header_6x47>},
    /* 6x48 */ {NONE, NONE, 0x48, on_cast_technique_finished},
    /* 6x49 */ {0x3E, 0x44, 0x49, forward_subcommand_with_entity_targets_transcode_and_track_hits_t<G_ExecutePhotonBlast_Header_6x49>},
    /* 6x4A */ {0x3F, 0x45, 0x4A, on_change_hp_6x4A_4B_4C},
    /* 6x4B */ {0x40, 0x46, 0x4B, on_change_hp_6x4A_4B_4C},
    /* 6x4C */ {0x41, 0x47, 0x4C, on_change_hp_6x4A_4B_4C},
    /* 6x4D */ {0x42, 0x48, 0x4D, on_player_died},
    /* 6x4E */ {NONE, NONE, 0x4E, on_player_revivable},
    /* 6x4F */ {0x43, 0x49, 0x4F, on_player_revived},
    /* 6x50 */ {0x44, 0x4A, 0x50, on_forward_check_game_client},
    /* 6x51 */ {0x45, 0x4B, 0x51, on_invalid},
    /* 6x52 */ {0x46, 0x4C, 0x52, on_set_animation_state},
    /* 6x53 */ {0x47, 0x4D, 0x53, on_forward_check_game},
    /* 6x54 */ {0x48, 0x4E, 0x54, forward_subcommand_m},
    /* 6x55 */ {0x49, 0x4F, 0x55, on_movement_6x55},
    /* 6x56 */ {0x4A, 0x50, 0x56, on_movement_6x56},
    /* 6x57 */ {NONE, 0x51, 0x57, on_forward_check_client},
    /* 6x58 */ {NONE, NONE, 0x58, on_forward_check_client},
    /* 6x59 */ {0x4B, 0x52, 0x59, on_pick_up_item},
    /* 6x5A */ {0x4C, 0x53, 0x5A, on_pick_up_item_request},
    /* 6x5B */ {0x4D, 0x54, 0x5B, forward_subcommand_m},
    /* 6x5C */ {0x4E, 0x55, 0x5C, on_destroy_floor_item},
    /* 6x5D */ {0x4F, 0x56, 0x5D, on_drop_partial_stack},
    /* 6x5E */ {0x50, 0x57, 0x5E, on_buy_shop_item},
    /* 6x5F */ {0x51, 0x58, 0x5F, on_box_or_enemy_item_drop},
    /* 6x60 */ {0x52, 0x59, 0x60, on_entity_drop_item_request},
    /* 6x61 */ {0x53, 0x5A, 0x61, on_forward_check_game},
    /* 6x62 */ {0x54, 0x5B, 0x62, on_forward_check_game},
    /* 6x63 */ {0x55, 0x5C, 0x63, on_destroy_floor_item},
    /* 6x64 */ {0x56, 0x5D, 0x64, on_forward_check_game},
    /* 6x65 */ {0x57, 0x5E, 0x65, on_forward_check_game},
    /* 6x66 */ {NONE, NONE, 0x66, on_forward_check_game},
    /* 6x67 */ {0x58, 0x5F, 0x67, on_trigger_set_event},
    /* 6x68 */ {0x59, 0x60, 0x68, on_update_telepipe_state},
    /* 6x69 */ {0x5A, 0x61, 0x69, on_npc_control},
    /* 6x6A */ {0x5B, 0x62, 0x6A, on_set_boss_warp_flags_6x6A},
    /* 6x6B */ {0x5C, 0x63, 0x6B, on_sync_joining_player_compressed_state},
    /* 6x6C */ {0x5D, 0x64, 0x6C, on_sync_joining_player_compressed_state},
    /* 6x6D */ {0x5E, 0x65, 0x6D, on_sync_joining_player_compressed_state},
    /* 6x6E */ {0x5F, 0x66, 0x6E, on_sync_joining_player_compressed_state},
    /* 6x6F */ {NONE, NONE, 0x6F, on_sync_joining_player_quest_flags},
    /* 6x70 */ {0x60, 0x67, 0x70, on_sync_joining_player_disp_and_inventory},
    /* 6x71 */ {NONE, NONE, 0x71, on_forward_check_game_loading},
    /* 6x72 */ {0x61, 0x68, 0x72, on_forward_check_game_loading},
    /* 6x73 */ {NONE, NONE, 0x73, on_forward_check_game_quest},
    /* 6x74 */ {0x62, 0x69, 0x74, on_word_select, SDF::ALWAYS_FORWARD_TO_WATCHERS},
    /* 6x75 */ {NONE, NONE, 0x75, on_set_quest_flag},
    /* 6x76 */ {NONE, NONE, 0x76, on_set_entity_set_flag},
    /* 6x77 */ {NONE, NONE, 0x77, on_sync_quest_register},
    /* 6x78 */ {NONE, NONE, 0x78, forward_subcommand_m},
    /* 6x79 */ {NONE, NONE, 0x79, on_forward_check_lobby},
    /* 6x7A */ {NONE, NONE, 0x7A, on_forward_check_game_client},
    /* 6x7B */ {NONE, NONE, 0x7B, forward_subcommand_m},
    /* 6x7C */ {NONE, NONE, 0x7C, on_challenge_update_records},
    /* 6x7D */ {NONE, NONE, 0x7D, on_update_battle_data_6x7D},
    /* 6x7E */ {NONE, NONE, 0x7E, forward_subcommand_m},
    /* 6x7F */ {NONE, NONE, 0x7F, on_battle_scores},
    /* 6x80 */ {NONE, NONE, 0x80, on_forward_check_game},
    /* 6x81 */ {NONE, NONE, 0x81, on_forward_check_game},
    /* 6x82 */ {NONE, NONE, 0x82, on_forward_check_game},
    /* 6x83 */ {NONE, NONE, 0x83, on_forward_check_game},
    /* 6x84 */ {NONE, NONE, 0x84, on_vol_opt_actions_6x84},
    /* 6x85 */ {NONE, NONE, 0x85, on_forward_check_game},
    /* 6x86 */ {NONE, NONE, 0x86, on_update_object_state_6x86},
    /* 6x87 */ {NONE, NONE, 0x87, on_forward_check_game},
    /* 6x88 */ {NONE, NONE, 0x88, on_forward_check_game},
    /* 6x89 */ {NONE, NONE, 0x89, forward_subcommand_with_entity_id_transcode_t<G_SetKillerEntityID_6x89, false, offsetof(G_SetKillerEntityID_6x89, killer_entity_id)>},
    /* 6x8A */ {NONE, NONE, 0x8A, on_forward_check_game},
    /* 6x8B */ {NONE, NONE, 0x8B, on_forward_check_game},
    /* 6x8C */ {NONE, NONE, 0x8C, on_forward_check_game},
    /* 6x8D */ {NONE, NONE, 0x8D, on_forward_check_game_client},
    /* 6x8E */ {NONE, NONE, 0x8E, on_forward_check_game},
    /* 6x8F */ {NONE, NONE, 0x8F, forward_subcommand_with_entity_id_transcode_t<G_AddBattleDamageScores_6x8F, false, offsetof(G_AddBattleDamageScores_6x8F, target_entity_id)>},
    /* 6x90 */ {NONE, NONE, 0x90, on_forward_check_game},
    /* 6x91 */ {NONE, NONE, 0x91, on_update_attackable_col_state},
    /* 6x92 */ {NONE, NONE, 0x92, on_forward_check_game},
    /* 6x93 */ {NONE, NONE, 0x93, on_activate_timed_switch},
    /* 6x94 */ {NONE, NONE, 0x94, on_warp},
    /* 6x95 */ {NONE, NONE, 0x95, on_forward_check_game},
    /* 6x96 */ {NONE, NONE, 0x96, on_forward_check_game},
    /* 6x97 */ {NONE, NONE, 0x97, on_challenge_mode_retry_or_quit},
    /* 6x98 */ {NONE, NONE, 0x98, on_forward_check_game},
    /* 6x99 */ {NONE, NONE, 0x99, on_forward_check_game},
    /* 6x9A */ {NONE, NONE, 0x9A, on_forward_check_game_client},
    /* 6x9B */ {NONE, NONE, 0x9B, on_forward_check_game},
    /* 6x9C */ {NONE, NONE, 0x9C, on_set_enemy_low_game_flags_ultimate},
    /* 6x9D */ {NONE, NONE, 0x9D, on_forward_check_game},
    /* 6x9E */ {NONE, NONE, 0x9E, forward_subcommand_m},
    /* 6x9F */ {NONE, NONE, 0x9F, forward_subcommand_with_entity_id_transcode_t<G_GalGryphonBossActions_6x9F>},
    /* 6xA0 */ {NONE, NONE, 0xA0, forward_subcommand_with_entity_id_transcode_t<G_GalGryphonBossActions_6xA0>},
    /* 6xA1 */ {NONE, NONE, 0xA1, on_forward_check_game},
    /* 6xA2 */ {NONE, NONE, 0xA2, on_entity_drop_item_request},
    /* 6xA3 */ {NONE, NONE, 0xA3, forward_subcommand_with_entity_id_transcode_t<G_OlgaFlowBossActions_6xA3>},
    /* 6xA4 */ {NONE, NONE, 0xA4, forward_subcommand_with_entity_id_transcode_t<G_OlgaFlowBossActions_6xA4_6xA5>},
    /* 6xA5 */ {NONE, NONE, 0xA5, forward_subcommand_with_entity_id_transcode_t<G_OlgaFlowBossActions_6xA4_6xA5>},
    /* 6xA6 */ {NONE, NONE, 0xA6, on_forward_check_game},
    /* 6xA7 */ {NONE, NONE, 0xA7, forward_subcommand_m},
    /* 6xA8 */ {NONE, NONE, 0xA8, on_gol_dragon_actions},
    /* 6xA9 */ {NONE, NONE, 0xA9, forward_subcommand_with_entity_id_transcode_t<G_BarbaRayBossActions_6xA9>},
    /* 6xAA */ {NONE, NONE, 0xAA, forward_subcommand_with_entity_id_transcode_t<G_BarbaRayBossActions_6xAA>},
    /* 6xAB */ {NONE, NONE, 0xAB, on_gc_nte_exclusive},
    /* 6xAC */ {NONE, NONE, 0xAC, on_gc_nte_exclusive},
    /* 6xAD */ {NONE, NONE, 0xAD, on_forward_check_game},
    /* 6xAE */ {NONE, NONE, 0xAE, on_forward_check_client},
    /* 6xAF */ {NONE, NONE, 0xAF, on_forward_check_lobby_client},
    /* 6xB0 */ {NONE, NONE, 0xB0, on_forward_check_lobby_client},
    /* 6xB1 */ {NONE, NONE, 0xB1, forward_subcommand_m},
    /* 6xB2 */ {NONE, NONE, 0xB2, on_play_sound_from_player},
    /* 6xB3 */ {NONE, NONE, 0xB3, on_xbox_voice_chat_control}, // Ep3 6xBx commands are handled via on_CA_Ep3 instead
    /* 6xB4 */ {NONE, NONE, 0xB4, on_xbox_voice_chat_control},
    /* 6xB5 */ {NONE, NONE, 0xB5, on_open_shop_bb_or_ep3_battle_subs},
    /* 6xB6 */ {NONE, NONE, 0xB6, on_invalid},
    /* 6xB7 */ {NONE, NONE, 0xB7, on_buy_shop_item_bb},
    /* 6xB8 */ {NONE, NONE, 0xB8, on_identify_item_bb},
    /* 6xB9 */ {NONE, NONE, 0xB9, on_invalid},
    /* 6xBA */ {NONE, NONE, 0xBA, on_accept_identify_item_bb},
    /* 6xBB */ {NONE, NONE, 0xBB, on_open_bank_bb_or_card_trade_counter_ep3},
    /* 6xBC */ {NONE, NONE, 0xBC, on_ep3_trade_card_counts},
    /* 6xBD */ {NONE, NONE, 0xBD, on_ep3_private_word_select_bb_bank_action, SDF::ALWAYS_FORWARD_TO_WATCHERS},
    /* 6xBE */ {NONE, NONE, 0xBE, forward_subcommand_m, SDF::ALWAYS_FORWARD_TO_WATCHERS | SDF::ALLOW_FORWARD_TO_WATCHED_LOBBY},
    /* 6xBF */ {NONE, NONE, 0xBF, on_forward_check_ep3_lobby},
    /* 6xC0 */ {NONE, NONE, 0xC0, on_sell_item_at_shop_bb},
    /* 6xC1 */ {NONE, NONE, 0xC1, forward_subcommand_m},
    /* 6xC2 */ {NONE, NONE, 0xC2, forward_subcommand_m},
    /* 6xC3 */ {NONE, NONE, 0xC3, on_drop_partial_stack_bb},
    /* 6xC4 */ {NONE, NONE, 0xC4, on_sort_inventory_bb},
    /* 6xC5 */ {NONE, NONE, 0xC5, on_medical_center_bb},
    /* 6xC6 */ {NONE, NONE, 0xC6, on_steal_exp_bb},
    /* 6xC7 */ {NONE, NONE, 0xC7, on_charge_attack_bb},
    /* 6xC8 */ {NONE, NONE, 0xC8, on_enemy_exp_request_bb},
    /* 6xC9 */ {NONE, NONE, 0xC9, on_adjust_player_meseta_bb},
    /* 6xCA */ {NONE, NONE, 0xCA, on_quest_create_item_bb},
    /* 6xCB */ {NONE, NONE, 0xCB, on_transfer_item_via_mail_message_bb},
    /* 6xCC */ {NONE, NONE, 0xCC, on_exchange_item_for_team_points_bb},
    /* 6xCD */ {NONE, NONE, 0xCD, forward_subcommand_m},
    /* 6xCE */ {NONE, NONE, 0xCE, forward_subcommand_m},
    /* 6xCF */ {NONE, NONE, 0xCF, on_battle_restart_bb},
    /* 6xD0 */ {NONE, NONE, 0xD0, on_battle_level_up_bb},
    /* 6xD1 */ {NONE, NONE, 0xD1, on_request_challenge_grave_recovery_item_bb},
    /* 6xD2 */ {NONE, NONE, 0xD2, on_write_quest_counter_bb},
    /* 6xD3 */ {NONE, NONE, 0xD3, on_invalid},
    /* 6xD4 */ {NONE, NONE, 0xD4, on_forward_check_game},
    /* 6xD5 */ {NONE, NONE, 0xD5, on_quest_exchange_item_bb},
    /* 6xD6 */ {NONE, NONE, 0xD6, on_wrap_item_bb},
    /* 6xD7 */ {NONE, NONE, 0xD7, on_photon_drop_exchange_for_item_bb},
    /* 6xD8 */ {NONE, NONE, 0xD8, on_photon_drop_exchange_for_s_rank_special_bb},
    /* 6xD9 */ {NONE, NONE, 0xD9, on_momoka_item_exchange_bb},
    /* 6xDA */ {NONE, NONE, 0xDA, on_upgrade_weapon_attribute_bb},
    /* 6xDB */ {NONE, NONE, 0xDB, on_invalid},
    /* 6xDC */ {NONE, NONE, 0xDC, on_forward_check_game},
    /* 6xDD */ {NONE, NONE, 0xDD, on_invalid},
    /* 6xDE */ {NONE, NONE, 0xDE, on_secret_lottery_ticket_exchange_bb},
    /* 6xDF */ {NONE, NONE, 0xDF, on_photon_crystal_exchange_bb},
    /* 6xE0 */ {NONE, NONE, 0xE0, on_quest_F95E_result_bb},
    /* 6xE1 */ {NONE, NONE, 0xE1, on_quest_F95F_result_bb},
    /* 6xE2 */ {NONE, NONE, 0xE2, on_quest_F960_result_bb},
    /* 6xE3 */ {NONE, NONE, 0xE3, on_invalid},
    /* 6xE4 */ {NONE, NONE, 0xE4, on_incr_enemy_damage}, // Extended subcommand; see CommandFormats.hh
    /* 6xE5 */ {NONE, NONE, 0xE5, on_invalid},
    /* 6xE6 */ {NONE, NONE, 0xE6, on_invalid},
    /* 6xE7 */ {NONE, NONE, 0xE7, on_invalid},
    /* 6xE8 */ {NONE, NONE, 0xE8, on_invalid},
    /* 6xE9 */ {NONE, NONE, 0xE9, on_invalid},
    /* 6xEA */ {NONE, NONE, 0xEA, on_invalid},
    /* 6xEB */ {NONE, NONE, 0xEB, on_invalid},
    /* 6xEC */ {NONE, NONE, 0xEC, on_invalid},
    /* 6xED */ {NONE, NONE, 0xED, on_invalid},
    /* 6xEE */ {NONE, NONE, 0xEE, on_invalid},
    /* 6xEF */ {NONE, NONE, 0xEF, on_invalid},
    /* 6xF0 */ {NONE, NONE, 0xF0, on_invalid},
    /* 6xF1 */ {NONE, NONE, 0xF1, on_invalid},
    /* 6xF2 */ {NONE, NONE, 0xF2, on_invalid},
    /* 6xF3 */ {NONE, NONE, 0xF3, on_invalid},
    /* 6xF4 */ {NONE, NONE, 0xF4, on_invalid},
    /* 6xF5 */ {NONE, NONE, 0xF5, on_invalid},
    /* 6xF6 */ {NONE, NONE, 0xF6, on_invalid},
    /* 6xF7 */ {NONE, NONE, 0xF7, on_invalid},
    /* 6xF8 */ {NONE, NONE, 0xF8, on_invalid},
    /* 6xF9 */ {NONE, NONE, 0xF9, on_invalid},
    /* 6xFA */ {NONE, NONE, 0xFA, on_invalid},
    /* 6xFB */ {NONE, NONE, 0xFB, on_invalid},
    /* 6xFC */ {NONE, NONE, 0xFC, on_invalid},
    /* 6xFD */ {NONE, NONE, 0xFD, on_invalid},
    /* 6xFE */ {NONE, NONE, 0xFE, on_invalid},
    /* 6xFF */ {NONE, NONE, 0xFF, on_debug_info}, // Extended subcommand with no format; used for debugging patches
};

asio::awaitable<void> on_subcommand_multi(shared_ptr<Client> c, Channel::Message& msg) {
  if (msg.data.empty()) {
    throw runtime_error("subcommand is empty");
  }

  size_t offset = 0;
  while (offset < msg.data.size()) {
    size_t cmd_size = 0;
    if (offset + sizeof(G_UnusedHeader) > msg.data.size()) {
      throw runtime_error("insufficient data remaining for next subcommand header");
    }
    const auto* header = reinterpret_cast<const G_UnusedHeader*>(msg.data.data() + offset);
    if (header->size != 0) {
      cmd_size = header->size << 2;
    } else {
      if (offset + sizeof(G_ExtendedHeaderT<G_UnusedHeader>) > msg.data.size()) {
        throw runtime_error("insufficient data remaining for next extended subcommand header");
      }
      const auto* ext_header = reinterpret_cast<const G_ExtendedHeaderT<G_UnusedHeader>*>(msg.data.data() + offset);
      cmd_size = ext_header->size;
      if (cmd_size < 8) {
        throw runtime_error("extended subcommand header has size < 8");
      }
      if (cmd_size & 3) {
        throw runtime_error("extended subcommand size is not a multiple of 4");
      }
    }
    if (cmd_size == 0) {
      throw runtime_error("invalid subcommand size");
    }
    void* cmd_data = msg.data.data() + offset;

    const auto* def = def_for_subcommand(c->version(), header->subcommand);
    SubcommandMessage sub_msg{.command = msg.command, .flag = msg.flag, .data = cmd_data, .size = cmd_size};
    co_await def->handler(c, sub_msg);
    offset += cmd_size;
  }
}
