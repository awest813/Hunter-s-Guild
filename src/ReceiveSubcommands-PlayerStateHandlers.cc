#include "ReceiveSubcommands-Impl.hh"

#include <algorithm>

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

asio::awaitable<void> on_warp(shared_ptr<Client>, SubcommandMessage& msg) {
  // Unconditionally block these. Players should use $warp instead.
  msg.check_size_t<G_InterLevelWarp_6x94>();
  co_return;
}

asio::awaitable<void> on_set_player_visible(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_SetPlayerVisibility_6x22_6x23>();

  if (cmd.header.client_id == c->lobby_client_id) {
    forward_subcommand(c, msg);

    auto l = c->lobby.lock();
    if (l) {
      if (!l->is_game()) {
        if (!is_v1(c->version())) {
          send_arrow_update(l);
        }
        if (l->check_flag(Lobby::Flag::IS_OVERFLOW)) {
          send_message_box(c, "$C6All lobbies are full.\n\n$C7You are in a private lobby. You can use the\nteleporter to join other lobbies if there is space\navailable.");
          send_lobby_message_box(c, "");
        }
        if (c->version() == Version::BB_V4) {
          send_update_team_reward_flags(c);
          send_all_nearby_team_metadatas_to_client(c, false);
        }
      } else if (c->check_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST) && (c->version() != Version::BB_V4)) {
        c->clear_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST);
        c->log.info_f("LOADING_RUNNING_JOINABLE_QUEST flag cleared");
      }
    }
  }
  co_return;
}

asio::awaitable<void> on_change_floor_6x1F(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (is_pre_v1(c->version())) {
    msg.check_size_t<G_SetPlayerFloor_DCNTE_6x1F>();
    // DC NTE and 11/2000 don't send 6F when they're done loading, so we clear the loading flag here instead.
    if (c->check_flag(Client::Flag::LOADING)) {
      c->clear_flag(Client::Flag::LOADING);
      c->log.info_f("LOADING flag cleared");
      send_resume_game(c->require_lobby(), c);
      c->require_lobby()->assign_inventory_and_bank_item_ids(c, true);
    }

  } else {
    const auto& cmd = msg.check_size_t<G_SetPlayerFloor_6x1F>();
    if (cmd.floor >= 0 && c->floor != static_cast<uint32_t>(cmd.floor)) {
      c->floor = cmd.floor;
    }
  }
  forward_subcommand(c, msg);
  co_return;
}

asio::awaitable<void> on_change_floor_6x21(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_InterLevelWarp_6x21>();
  if (cmd.floor >= 0 && c->floor != static_cast<uint32_t>(cmd.floor)) {
    c->floor = cmd.floor;
  }
  forward_subcommand(c, msg);
  co_return;
}

asio::awaitable<void> on_player_died(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_PlayerDied_6x4D>(0xFFFF);

  auto l = c->require_lobby();
  if (!l->is_game() || (cmd.header.client_id != c->lobby_client_id)) {
    co_return;
  }

  // Decrease MAG's synchro
  try {
    auto& inventory = c->character_file()->inventory;
    size_t mag_index = inventory.find_equipped_item(EquipSlot::MAG);
    auto& data = inventory.items[mag_index].data;
    data.data2[0] = max<int8_t>(static_cast<int8_t>(data.data2[0] - 5), 0);
  } catch (const out_of_range&) {
  }

  forward_subcommand(c, msg);
}

asio::awaitable<void> on_player_revivable(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_PlayerRevivable_6x4E>(0xFFFF);

  auto l = c->require_lobby();
  if (!l->is_game() || (cmd.header.client_id != c->lobby_client_id)) {
    co_return;
  }

  forward_subcommand(c, msg);

  // Revive if infinite HP is enabled
  bool player_cheats_enabled = l->check_flag(Lobby::Flag::CHEATS_ENABLED) ||
      (c->login->account->check_flag(Account::Flag::CHEAT_ANYWHERE));
  if (player_cheats_enabled && c->check_flag(Client::Flag::INFINITE_HP_ENABLED)) {
    G_UseMedicalCenter_6x31 v2_cmd = {0x31, 0x01, c->lobby_client_id};
    G_RevivePlayer_V3_BB_6xA1 v3_cmd = {0xA1, 0x01, c->lobby_client_id};
    static_assert(sizeof(v2_cmd) == sizeof(v3_cmd), "Command sizes do not match");

    const void* c_data = (!is_v1_or_v2(c->version()) || (c->version() == Version::GC_NTE))
        ? static_cast<const void*>(&v3_cmd)
        : static_cast<const void*>(&v2_cmd);
    // TODO: We might need to send different versions of the command here to different clients in certain crossplay
    // scenarios, so just using echo_to_lobby would not suffice. Figure out a way to handle this.
    co_await send_protected_command(c, c_data, sizeof(v3_cmd), true);
  }
}

asio::awaitable<void> on_player_revived(shared_ptr<Client> c, SubcommandMessage& msg) {
  msg.check_size_t<G_PlayerRevived_6x4F>(0xFFFF);

  auto l = c->require_lobby();
  if (l->is_game()) {
    forward_subcommand(c, msg);
    if ((l->check_flag(Lobby::Flag::CHEATS_ENABLED) || (c->login->account->check_flag(Account::Flag::CHEAT_ANYWHERE))) &&
        c->check_flag(Client::Flag::INFINITE_HP_ENABLED)) {
      co_await send_change_player_hp(l, c->lobby_client_id, PlayerHPChange::MAXIMIZE_HP, 0);
    }
  }
  co_return;
}

asio::awaitable<void> on_received_condition(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_ClientIDHeader>(0xFFFF);

  auto l = c->require_lobby();
  if (l->is_game()) {
    forward_subcommand(c, msg);
    if (cmd.client_id == c->lobby_client_id) {
      bool player_cheats_enabled = l->check_flag(Lobby::Flag::CHEATS_ENABLED) ||
          c->login->account->check_flag(Account::Flag::CHEAT_ANYWHERE);
      if (player_cheats_enabled && c->check_flag(Client::Flag::INFINITE_HP_ENABLED)) {
        co_await send_remove_negative_conditions(c);
      }
    }
  }
}

asio::awaitable<void> on_cast_technique_finished(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_CastTechniqueComplete_6x48>();

  auto l = c->require_lobby();
  if (l->is_game() && (cmd.header.client_id == c->lobby_client_id)) {
    forward_subcommand(c, msg);
    bool player_cheats_enabled = !is_v1(c->version()) &&
        (l->check_flag(Lobby::Flag::CHEATS_ENABLED) || (c->login->account->check_flag(Account::Flag::CHEAT_ANYWHERE)));
    if (player_cheats_enabled && c->check_flag(Client::Flag::INFINITE_TP_ENABLED)) {
      send_player_stats_change(c, PlayerStatsChange::ADD_TP, 255);
    }
  }
  co_return;
}

asio::awaitable<void> on_npc_control(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_NPCControl_6x69>();
  // Don't allow NPC control commands if there is a player in the relevant slot
  const auto& l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("cannot create or modify NPC in the lobby");
  }

  if ((cmd.command == 0 || cmd.command == 3) && ((cmd.param2 < 4) && l->clients[cmd.param2])) {
    throw runtime_error("cannot create NPC in existing player slot");
  }

  forward_subcommand(c, msg);
  co_return;
}

asio::awaitable<void> on_set_animation_state(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_SetAnimationState_6x52>();
  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }
  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (l->is_game()) {
    forward_subcommand(c, msg);
    co_return;
  }

  // The animation numbers were changed on V3. This is the most common one to see in the lobby (it occurs when a player
  // talks to the counter), so we take care to translate it specifically.
  bool c_is_v1_or_v2 = is_v1_or_v2(c->version());
  if (!((c_is_v1_or_v2 && (cmd.animation == 0x000A)) || (!c_is_v1_or_v2 && (cmd.animation == 0x0000)))) {
    forward_subcommand(c, msg);
    co_return;
  }

  G_SetAnimationState_6x52 other_cmd = cmd;
  other_cmd.animation = 0x000A - cmd.animation;
  for (auto lc : l->clients) {
    if (lc && (lc != c)) {
      auto& out_cmd = (is_v1_or_v2(lc->version()) != c_is_v1_or_v2) ? other_cmd : cmd;
      out_cmd.header.subcommand = translate_subcommand_number(lc->version(), Version::BB_V4, 0x52);
      send_command_t(lc, msg.command, msg.flag, out_cmd);
    }
  }
}

asio::awaitable<void> on_play_sound_from_player(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_PlaySoundFromPlayer_6xB2>();
  // This command can be used to play arbitrary sounds, but the client only ever sends it for the camera shutter sound,
  // so we only allow that one.
  if (cmd.sound_id == 0x00051720) {
    forward_subcommand(c, msg);
  }
  co_return;
}
