#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_forward_check_game(shared_ptr<Client> c, SubcommandMessage& msg);

asio::awaitable<void> on_medical_center_bb(shared_ptr<Client> c, SubcommandMessage&) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xC5 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xC5 command sent in non-game lobby");
  }

  c->character_file()->remove_meseta(10, false);
  co_return;
}

asio::awaitable<void> on_battle_restart_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xCF command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xCF command sent in non-game lobby");
  }
  if (l->episode == Episode::EP3) {
    throw runtime_error("6xCF command sent in Episode 3 game");
  }
  if (l->mode != GameMode::BATTLE) {
    throw runtime_error("6xCF command sent in non-battle game");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS)) {
    throw runtime_error("6xCF command sent during free play");
  }
  if (!l->quest) {
    throw runtime_error("6xCF command sent without quest loaded");
  }
  if (l->leader_id != c->lobby_client_id) {
    throw runtime_error("6xCF command sent by non-leader");
  }

  auto s = c->require_server_state();
  const auto& cmd = msg.check_size_t<G_StartBattle_BB_6xCF>();

  auto new_rules = make_shared<BattleRules>(cmd.rules);
  l->item_creator->set_restrictions(new_rules);

  for (auto& lc : l->clients) {
    if (lc) {
      lc->delete_overlay();
      if (is_v4(lc->version())) {
        lc->change_bank(lc->bb_character_index);
      }
      lc->create_battle_overlay(new_rules, s->level_table(c->version()));
    }
  }
  l->map_state->reset();
  co_return;
}

asio::awaitable<void> on_battle_level_up_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xD0 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xD0 command sent in non-game lobby");
  }
  if (l->mode != GameMode::BATTLE) {
    throw runtime_error("6xD0 command sent during free play");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS)) {
    throw runtime_error("6xD0 command sent during free play");
  }

  const auto& cmd = msg.check_size_t<G_BattleModeLevelUp_BB_6xD0>();
  auto lc = l->clients.at(cmd.header.client_id);
  if (lc) {
    auto s = c->require_server_state();
    auto lp = lc->character_file();
    uint32_t target_level = min<uint32_t>(lp->disp.stats.level + cmd.num_levels, 199);
    uint32_t before_exp = lp->disp.stats.experience;
    int32_t exp_delta = lp->disp.stats.experience - before_exp;
    if (exp_delta > 0) {
      s->level_table(lc->version())->advance_to_level(lp->disp.stats, target_level, lp->disp.visual.char_class);
      if (lc->version() == Version::BB_V4) {
        send_give_experience(lc, exp_delta, 0xFFFF);
        send_level_up(lc);
      }
    }
  }
  co_return;
}

asio::awaitable<void> on_request_challenge_grave_recovery_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xD1 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xD1 command sent in non-game lobby");
  }
  if (l->mode != GameMode::CHALLENGE) {
    throw runtime_error("6xD1 command sent in non-challenge game");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS)) {
    throw runtime_error("6xD1 command sent during free play");
  }

  const auto& cmd = msg.check_size_t<G_ChallengeModeGraveRecoveryItemRequest_BB_6xD1>();
  static const array<ItemData, 6> items = {
      ItemData(0x0300000000010000), // Monomate x1
      ItemData(0x0300010000010000), // Dimate x1
      ItemData(0x0300020000010000), // Trimate x1
      ItemData(0x0301000000010000), // Monofluid x1
      ItemData(0x0301010000010000), // Difluid x1
      ItemData(0x0301020000010000), // Trifluid x1
  };
  ItemData item = items.at(cmd.item_type);
  item.id = l->generate_item_id(cmd.header.client_id);
  l->add_item(cmd.floor, item, cmd.pos, nullptr, nullptr, 0x100F);
  send_drop_stacked_item_to_lobby(l, item, cmd.floor, cmd.pos);
  co_return;
}

asio::awaitable<void> on_challenge_mode_retry_or_quit(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_SelectChallengeModeFailureOption_6x97>();

  auto l = c->require_lobby();
  auto leader_c = l->clients.at(l->leader_id);
  if (leader_c != c) {
    throw runtime_error("6x97 sent by non-leader");
  }

  if (l->is_game() && (cmd.is_retry == 1) && l->quest && (l->quest->meta.challenge_template_index >= 0)) {
    auto s = l->require_server_state();

    for (auto& m : l->floor_item_managers) {
      m.clear();
    }

    // If the leader (c) is BB, they are expected to send 02DF later, which will recreate the overlays.
    if (!is_v4(c->version())) {
      for (auto lc : l->clients) {
        if (lc) {
          if (is_v4(lc->version())) {
            lc->change_bank(lc->bb_character_index);
          }
          lc->create_challenge_overlay(lc->version(), l->quest->meta.challenge_template_index, s->level_table(c->version()));
          lc->log.info_f("Created challenge overlay");
          l->assign_inventory_and_bank_item_ids(lc, true);
        }
      }
    }

    l->map_state->reset();
  }

  forward_subcommand(c, msg);
  co_return;
}

asio::awaitable<void> on_challenge_update_records(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->lobby.lock();
  if (!l) {
    c->log.warning_f("Not in any lobby; dropping command");
    co_return;
  }

  const auto& cmd = msg.check_size_t<G_SetChallengeRecordsBase_6x7C>(0xFFFF);
  if (cmd.client_id != c->lobby_client_id) {
    co_return;
  }

  auto p = c->character_file(true, false);
  Version c_version = c->version();
  switch (c_version) {
    case Version::DC_V2:
    case Version::GC_NTE: {
      const auto& cmd = msg.check_size_t<G_SetChallengeRecords_DC_6x7C>();
      p->challenge_records = cmd.records;
      break;
    }
    case Version::PC_V2: {
      const auto& cmd = msg.check_size_t<G_SetChallengeRecords_PC_6x7C>();
      p->challenge_records = cmd.records;
      break;
    }
    case Version::GC_V3:
    case Version::XB_V3: {
      const auto& cmd = msg.check_size_t<G_SetChallengeRecords_V3_6x7C>();
      p->challenge_records = cmd.records;
      break;
    }
    case Version::BB_V4: {
      const auto& cmd = msg.check_size_t<G_SetChallengeRecords_BB_6x7C>();
      p->challenge_records = cmd.records;
      break;
    }
    default:
      throw runtime_error("game version cannot send 6x7C");
  }

  string dc_data;
  string pc_data;
  string v3_data;
  string bb_data;
  auto send_to_client = [&](shared_ptr<Client> lc) -> void {
    Version lc_version = lc->version();
    const void* data_to_send = nullptr;
    size_t size_to_send = 0;
    if ((lc_version == c_version) || (is_v3(lc_version) && is_v3(c_version))) {
      data_to_send = msg.data;
      size_to_send = msg.size;
    } else if ((lc->version() == Version::DC_V2) || (lc->version() == Version::GC_NTE)) {
      if (dc_data.empty()) {
        dc_data.resize(sizeof(G_SetChallengeRecords_DC_6x7C));
        auto& dc_cmd = check_size_t<G_SetChallengeRecords_DC_6x7C>(dc_data);
        dc_cmd.header = cmd.header;
        dc_cmd.header.size = sizeof(G_SetChallengeRecords_DC_6x7C) >> 2;
        dc_cmd.client_id = cmd.client_id;
        dc_cmd.records = p->challenge_records;
      }
      data_to_send = dc_data.data();
      size_to_send = dc_data.size();
    } else if (lc->version() == Version::PC_V2) {
      if (pc_data.empty()) {
        pc_data.resize(sizeof(G_SetChallengeRecords_PC_6x7C));
        auto& pc_cmd = check_size_t<G_SetChallengeRecords_PC_6x7C>(pc_data);
        pc_cmd.header = cmd.header;
        pc_cmd.header.size = sizeof(G_SetChallengeRecords_PC_6x7C) >> 2;
        pc_cmd.client_id = cmd.client_id;
        pc_cmd.records = p->challenge_records;
      }
      data_to_send = pc_data.data();
      size_to_send = pc_data.size();
    } else if (is_v3(lc->version())) {
      if (v3_data.empty()) {
        v3_data.resize(sizeof(G_SetChallengeRecords_V3_6x7C));
        auto& v3_cmd = check_size_t<G_SetChallengeRecords_V3_6x7C>(v3_data);
        v3_cmd.header = cmd.header;
        v3_cmd.header.size = sizeof(G_SetChallengeRecords_V3_6x7C) >> 2;
        v3_cmd.client_id = cmd.client_id;
        v3_cmd.records = p->challenge_records;
      }
      data_to_send = v3_data.data();
      size_to_send = v3_data.size();
    } else if (is_v4(lc->version())) {
      if (bb_data.empty()) {
        bb_data.resize(sizeof(G_SetChallengeRecords_BB_6x7C));
        auto& bb_cmd = check_size_t<G_SetChallengeRecords_BB_6x7C>(bb_data);
        bb_cmd.header = cmd.header;
        bb_cmd.header.size = sizeof(G_SetChallengeRecords_BB_6x7C) >> 2;
        bb_cmd.client_id = cmd.client_id;
        bb_cmd.records = p->challenge_records;
      }
      data_to_send = bb_data.data();
      size_to_send = bb_data.size();
    }

    if (!data_to_send || !size_to_send) {
      lc->log.info_f("Command cannot be translated to client\'s version");
    } else {
      send_command(lc, msg.command, msg.flag, data_to_send, size_to_send);
    }
  };

  if (command_is_private(msg.command)) {
    if (msg.flag >= l->max_clients) {
      co_return;
    }
    auto target = l->clients[msg.flag];
    if (!target) {
      co_return;
    }
    send_to_client(target);

  } else {
    for (auto& lc : l->clients) {
      if (lc && (lc != c)) {
        send_to_client(lc);
      }
    }
  }
}

asio::awaitable<void> on_update_battle_data_6x7D(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->lobby.lock();
  if (!l) {
    c->log.warning_f("Not in any lobby; dropping command");
    co_return;
  }

  const auto& cmd = msg.check_size_t<G_SetBattleModeData_6x7D>(0xFFFF);
  if ((cmd.what == 3 || cmd.what == 4) && cmd.params[0] >= 4) {
    throw runtime_error("invalid client ID in 6x7D command");
  }

  co_await on_forward_check_game(c, msg);
}
