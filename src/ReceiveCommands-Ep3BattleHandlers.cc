#include "ReceiveCommands.hh"

#include <phosg/Filesystem.hh>
#include <phosg/Random.hh>
#include <phosg/Time.hh>

#include "SendCommands.hh"

using namespace std;

extern const char* BATTLE_TABLE_DISCONNECT_HOOK_NAME;
extern const char* ADD_NEXT_CLIENT_DISCONNECT_HOOK_NAME;

asio::awaitable<void> on_B7_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  // If the client is not in any lobby, assume they're at the main menu and send the menu song (if any).
  auto s = c->require_server_state();
  auto l = c->lobby.lock();
  if (!l && (s->ep3_menu_song >= 0)) {
    send_ep3_change_music(c->channel, s->ep3_menu_song);
  }
  co_return;
}

asio::awaitable<void> on_BA_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& in_cmd = check_size_t<C_MesetaTransaction_Ep3_BA>(msg.data);
  auto s = c->require_server_state();
  auto l = c->lobby.lock();
  bool is_lobby = l && !l->is_game();

  uint32_t current_meseta, total_meseta_earned;
  if (s->ep3_infinite_meseta) {
    current_meseta = 1000000;
    total_meseta_earned = 1000000;
  } else if (is_lobby && s->ep3_jukebox_is_free) {
    current_meseta = c->login->account->ep3_current_meseta;
    total_meseta_earned = c->login->account->ep3_total_meseta_earned;
  } else {
    if (c->login->account->ep3_current_meseta < in_cmd.value) {
      S_MesetaTransaction_Ep3_BA out_cmd = {
          c->login->account->ep3_current_meseta, c->login->account->ep3_total_meseta_earned, in_cmd.request_token};
      send_command(c, msg.command, 0x04, &out_cmd, sizeof(out_cmd));
      co_return;
    }
    c->login->account->ep3_current_meseta -= in_cmd.value;
    if (s->allow_saving_accounts) {
      c->login->account->save();
    }
    current_meseta = c->login->account->ep3_current_meseta;
    total_meseta_earned = c->login->account->ep3_total_meseta_earned;
  }

  S_MesetaTransaction_Ep3_BA out_cmd = {current_meseta, total_meseta_earned, in_cmd.request_token};
  send_command(c, msg.command, 0x03, &out_cmd, sizeof(out_cmd));
  co_return;
}

bool add_next_game_client(shared_ptr<Lobby> l) {
  auto it = l->clients_to_add.begin();
  if (it == l->clients_to_add.end()) {
    return false;
  }
  size_t target_client_id = it->first;
  shared_ptr<Client> c = it->second.lock();
  l->clients_to_add.erase(it);

  auto tourn = l->tournament_match ? l->tournament_match->tournament.lock() : nullptr;

  // If the game is a tournament match and the client has disconnected before they could join the match, disband the
  // entire game
  if (!c && l->tournament_match) {
    l->log.info_f("Client in slot {} has disconnected before joining the game; disbanding it", target_client_id);
    send_command(l, 0xED, 0x00);
    return false;
  }

  if (l->clients.at(target_client_id) != nullptr) {
    throw logic_error("client id is already in use");
  }

  auto s = c->require_server_state();
  if (tourn) {
    G_SetStateFlags_Ep3_6xB4x03 state_cmd;
    state_cmd.state.turn_num = 1;
    state_cmd.state.battle_phase = Episode3::BattlePhase::INVALID_00;
    state_cmd.state.current_team_turn1 = 0xFF;
    state_cmd.state.current_team_turn2 = 0xFF;
    state_cmd.state.action_subphase = Episode3::ActionSubphase::ATTACK;
    state_cmd.state.setup_phase = Episode3::SetupPhase::REGISTRATION;
    state_cmd.state.registration_phase = Episode3::RegistrationPhase::AWAITING_NUM_PLAYERS;
    state_cmd.state.team_exp.clear(0);
    state_cmd.state.team_dice_bonus.clear(0);
    state_cmd.state.first_team_turn = 0xFF;
    state_cmd.state.tournament_flag = 0x01;
    state_cmd.state.client_sc_card_types.clear(Episode3::CardType::INVALID_FF);
    if ((c->version() != Version::GC_EP3_NTE) && !(s->ep3_behavior_flags & Episode3::BehaviorFlag::DISABLE_MASKING)) {
      uint8_t mask_key = (phosg::random_object<uint32_t>() % 0xFF) + 1;
      set_mask_for_ep3_game_command(&state_cmd, sizeof(state_cmd), mask_key);
    }
    send_command_t(c, 0xC9, 0x00, state_cmd);
  }

  s->change_client_lobby(c, l, true, target_client_id);
  c->set_flag(Client::Flag::LOADING);
  c->log.info_f("LOADING flag set");
  if (tourn) {
    c->set_flag(Client::Flag::LOADING_TOURNAMENT);
  }
  c->disconnect_hooks.emplace(ADD_NEXT_CLIENT_DISCONNECT_HOOK_NAME, [s, l]() -> void {
    add_next_game_client(l);
  });

  return true;
}

static bool start_ep3_battle_table_game_if_ready(shared_ptr<Lobby> l, int16_t table_number) {
  if (table_number < 0) {
    // Negative numbers are supposed to mean the client is not seated at a table, so it's an error for this function to
    // be called with a negative table number
    throw runtime_error("negative table number");
  }

  // Figure out which clients are at this table. If any client has declined, we never start a match, but we may start a
  // match even if all clients have not yet accepted (in case of a tournament match).
  Version game_version = Version::UNKNOWN;
  unordered_map<size_t, shared_ptr<Client>> table_clients;
  bool all_clients_accepted = true;
  for (const auto& c : l->clients) {
    if (!c || (c->card_battle_table_number != table_number)) {
      continue;
    }
    // Prevent match from starting unless all players are on the same version
    if (game_version == Version::UNKNOWN) {
      game_version = c->version();
    } else if (game_version != c->version()) {
      return false;
    }
    if (c->card_battle_table_seat_number >= 4) {
      throw runtime_error("invalid seat number");
    }
    // Apparently this can actually happen; just prevent them from starting a battle if multiple players are in the
    // same seat
    if (!table_clients.emplace(c->card_battle_table_seat_number, c).second) {
      return false;
    }
    if (c->card_battle_table_seat_state == 3) {
      return false;
    }
    if (c->card_battle_table_seat_state != 2) {
      all_clients_accepted = false;
    }
  }
  if (table_clients.size() > 4) {
    throw runtime_error("too many clients at battle table");
  }

  // Figure out if this is a tournament match setup
  unordered_set<shared_ptr<Episode3::Tournament::Match>> tourn_matches;
  for (const auto& it : table_clients) {
    auto team = it.second->ep3_tournament_team.lock();
    auto tourn = team ? team->tournament.lock() : nullptr;
    auto match = tourn ? tourn->next_match_for_team(team) : nullptr;
    // Note: We intentionally don't check for null here. This is to handle the case where a tournament-registered
    // player steps into a seat at a table where a non-tournament-registered player is already present - we should NOT
    // start any match until the non-tournament-registered player leaves,
    // or they both accept (and we start a non-tournament match).
    tourn_matches.emplace(match);
  }

  // Get the tournament. Invariant: both tourn_match and tourn are null, or neither are null.
  auto tourn_match = (tourn_matches.size() == 1) ? *tourn_matches.begin() : nullptr;
  auto tourn = tourn_match ? tourn_match->tournament.lock() : nullptr;
  if (!tourn || !tourn_match->preceding_a->winner_team || !tourn_match->preceding_b->winner_team) {
    tourn.reset();
    tourn_match.reset();
  }

  // If this is a tournament match setup, check if all required players are present and rearrange their client IDs to
  // match their team positions
  unordered_map<size_t, shared_ptr<Client>> game_clients;
  if (tourn_match) {
    unordered_map<size_t, uint32_t> required_account_ids;
    auto add_team_players = [&](shared_ptr<const Episode3::Tournament::Team> team, size_t base_index) -> void {
      size_t z = 0;
      for (const auto& player : team->players) {
        if (z >= 2) {
          throw logic_error("more than 2 players on team");
        }
        if (player.is_human()) {
          required_account_ids.emplace(base_index + z, player.account_id);
        }
        z++;
      }
    };
    add_team_players(tourn_match->preceding_a->winner_team, 0);
    add_team_players(tourn_match->preceding_b->winner_team, 2);

    // Only count the players if they're all the same version (GC_EP3 or GC_EP3_NTE)
    Version version = Version::UNKNOWN;
    for (const auto& it : required_account_ids) {
      size_t client_id = it.first;
      uint32_t account_id = it.second;
      for (const auto& [_, table_c] : table_clients) {
        if (table_c->login->account->account_id == account_id) {
          if (version == Version::UNKNOWN) {
            version = table_c->version();
          }
          if (version == table_c->version()) {
            game_clients.emplace(client_id, table_c);
          }
        }
      }
    }

    if (game_clients.size() != required_account_ids.size()) {
      // Not all tournament match participants are present, so we can't start the tournament match. (But they can still
      // use the battle table)
      tourn_match.reset();
      tourn.reset();
    } else {
      // If there is already a game for this match, don't allow a new one to start
      auto s = l->require_server_state();
      for (auto l : s->all_lobbies()) {
        if (l->tournament_match == tourn_match) {
          tourn_match.reset();
          tourn.reset();
        }
      }
    }
  }

  // In the non-tournament case (or if the tournament case was rejected above), only start the game if all players have
  // accepted. If they have, just put them in the clients map in seat order.
  if (!tourn_match) {
    if (!all_clients_accepted) {
      return false;
    }
    game_clients = std::move(table_clients);
  }

  // If there are no clients, do nothing (happens when the last player leaves a battle table without starting a game)
  if (game_clients.empty()) {
    return false;
  }

  // At this point, we've checked all the necessary conditions for a game to begin, but create_game_generic can still
  // return null if an internal precondition fails (though this should never happen for Episode 3 games).

  auto c = game_clients.begin()->second;
  auto s = c->require_server_state();
  string name = tourn ? tourn->get_name() : "<BattleTable>";
  auto game = create_game_generic(s, c, name, "", Episode::EP3);
  if (!game) {
    return false;
  }
  game->tournament_match = tourn_match;
  game->ep3_ex_result_values = (tourn_match && tourn && tourn->get_final_match() == tourn_match)
      ? s->ep3_tournament_final_round_ex_values
      : s->ep3_tournament_ex_values;
  game->clients_to_add.clear();
  for (const auto& it : game_clients) {
    game->clients_to_add.emplace(it.first, it.second);
  }

  // Remove all players from the battle table (but don't tell them about this)
  for (const auto& it : game_clients) {
    auto other_c = it.second;
    other_c->card_battle_table_number = -1;
    other_c->card_battle_table_seat_number = 0;
    other_c->disconnect_hooks.erase(BATTLE_TABLE_DISCONNECT_HOOK_NAME);
  }

  // If there's only one client in the match, skip the wait phase - they'll be added to the match immediately by
  // add_next_game_client anyway
  if (game_clients.empty()) {
    throw logic_error("no clients to add to battle table match");

  } else if (game_clients.size() != 1) {
    for (const auto& it : game_clients) {
      auto other_c = it.second;
      send_self_leave_notification(other_c);
      string message;
      if (tourn) {
        message = std::format(
            "$C7Waiting to begin match in tournament\n$C6{}$C7...\n\n(Hold B+X+START to abort)", tourn->get_name());
      } else {
        message = "$C7Waiting to begin battle table match...\n\n(Hold B+X+START to abort)";
      }
      send_message_box(other_c, message);
    }
  }

  // Add the first client to the game (the remaining clients will be added when the previous is done loading)
  add_next_game_client(game);

  return true;
}

static void on_ep3_battle_table_state_updated(shared_ptr<Lobby> l, int16_t table_number) {
  send_ep3_card_battle_table_state(l, table_number);
  start_ep3_battle_table_game_if_ready(l, table_number);
}

asio::awaitable<void> on_E4_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_CardBattleTableState_Ep3_E4>(msg.data);

  // This command can be received shortly after a proxy session closes if the player uses $exit while standing on a
  // battle table pad. In that situation, the player will not be in any lobby, so we just ignore the command.
  auto l = c->lobby.lock();
  if (!l) {
    co_return;
  }

  if (cmd.seat_number >= 4) {
    throw runtime_error("invalid seat number");
  }

  if (msg.flag) {
    if (l->is_game() || !l->is_ep3()) {
      throw runtime_error("battle table join command sent in non-CARD lobby");
    }
    c->card_battle_table_number = cmd.table_number;
    c->card_battle_table_seat_number = cmd.seat_number;
    c->card_battle_table_seat_state = 1;

  } else { // Leaving battle table
    c->card_battle_table_number = -1;
    c->card_battle_table_seat_number = 0;
    c->card_battle_table_seat_state = 0;
  }

  on_ep3_battle_table_state_updated(l, cmd.table_number);

  bool should_have_disconnect_hook = (c->card_battle_table_number != -1);

  if (should_have_disconnect_hook && !c->disconnect_hooks.count(BATTLE_TABLE_DISCONNECT_HOOK_NAME)) {
    c->disconnect_hooks.emplace(BATTLE_TABLE_DISCONNECT_HOOK_NAME, [l, c]() -> void {
      int16_t table_number = c->card_battle_table_number;
      c->card_battle_table_number = -1;
      c->card_battle_table_seat_number = 0;
      c->card_battle_table_seat_state = 0;
      if (table_number != -1) {
        on_ep3_battle_table_state_updated(l, table_number);
      }
    });
  } else if (!should_have_disconnect_hook) {
    c->disconnect_hooks.erase(BATTLE_TABLE_DISCONNECT_HOOK_NAME);
  }
  co_return;
}

asio::awaitable<void> on_E5_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_t<S_CardBattleTableConfirmation_Ep3_E5>(msg.data);
  auto l = c->require_lobby();
  if (l->is_game() || !l->is_ep3()) {
    throw runtime_error("battle table command sent in non-CARD lobby");
  }

  if (c->card_battle_table_number < 0) {
    throw runtime_error("invalid table number");
  }

  if (msg.flag) {
    c->card_battle_table_seat_state = 2;
  } else {
    c->card_battle_table_seat_state = 3;
  }
  on_ep3_battle_table_state_updated(l, c->card_battle_table_number);
  co_return;
}

asio::awaitable<void> on_DC_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  auto l = c->lobby.lock();
  if (!l) {
    co_return;
  }

  if (msg.flag != 0) {
    c->clear_flag(Client::Flag::LOADING_TOURNAMENT);
    l->set_flag(Lobby::Flag::BATTLE_IN_PROGRESS);
    send_command(c, 0xDC, 0x00);
    send_ep3_start_tournament_deck_select_if_all_clients_ready(l);
  } else {
    l->clear_flag(Lobby::Flag::BATTLE_IN_PROGRESS);
  }
  co_return;
}

static void on_tournament_bracket_updated(shared_ptr<ServerState> s, shared_ptr<const Episode3::Tournament> tourn) {
  tourn->send_all_state_updates();
  if (tourn->get_state() == Episode3::Tournament::State::COMPLETE) {
    auto team = tourn->get_winner_team();
    if (!team->has_any_human_players()) {
      send_ep3_text_message_fmt(s, "$C7A CPU team won\nthe tournament\n$C6{}", tourn->get_name());
    } else {
      send_ep3_text_message_fmt(s, "$C6{}$C7\nwon the tournament\n$C6{}", team->name, tourn->get_name());
    }
    s->ep3_tournament_index->delete_tournament(tourn->get_name());
  } else {
    s->ep3_tournament_index->save();
  }
}

asio::awaitable<void> on_CA_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  auto l = c->lobby.lock();
  if (!l) {
    // In rare cases (e.g. when two players end a tournament's match results screens at exactly the same time), the
    // client can send a server data command when it's not in any lobby at all. We just ignore such commands.
    co_return;
  }
  if (!l->is_game() || !l->is_ep3()) {
    throw runtime_error("Episode 3 server data request sent outside of Episode 3 game");
  }

  if (l->battle_player) {
    co_return;
  }

  auto s = l->require_server_state();

  const auto& header = check_size_t<G_CardServerDataCommandHeader>(msg.data, 0xFFFF);
  if (header.subcommand != 0xB3) {
    throw runtime_error("unknown Episode 3 server data request");
  }

  if (!l->ep3_server || l->ep3_server->battle_finished) {
    auto s = c->require_server_state();

    if (s->ep3_behavior_flags & Episode3::BehaviorFlag::ENABLE_RECORDING) {
      l->battle_record = make_shared<Episode3::BattleRecord>(s->ep3_behavior_flags);
      for (auto existing_c : l->clients) {
        if (existing_c) {
          auto existing_p = existing_c->character_file();
          PlayerLobbyDataDCGC lobby_data;
          lobby_data.name.encode(existing_p->disp.name.decode(existing_c->language()), c->language());
          lobby_data.player_tag = 0x00010000;
          lobby_data.guild_card_number = existing_c->login->account->account_id;
          l->battle_record->add_player(
              lobby_data,
              existing_p->inventory,
              existing_p->disp.to_dcpcv3<false>(c->language(), c->language()),
              c->ep3_config ? (c->ep3_config->online_clv_exp / 100) : 0);
        }
      }
    }

    l->create_ep3_server();
  }

  bool battle_finished_before = l->ep3_server->battle_finished;

  try {
    l->ep3_server->on_server_data_input(c, msg.data);
  } catch (const exception& e) {
    c->log.error_f("Episode 3 engine returned an error: {}", e.what());
    if (l->battle_record) {
      string filename = std::format("system/ep3/battle-records/exc.{}.mzrd", phosg::now());
      phosg::save_file(filename, l->battle_record->serialize());
      c->log.error_f("Saved partial battle record as {}", filename);
    }
    throw;
  }

  // If the battle has finished, finalize the recording and link it to all participating players and spectators
  if (!battle_finished_before && l->ep3_server->battle_finished && l->battle_record) {
    l->battle_record->set_battle_end_timestamp();
    unordered_set<shared_ptr<Lobby>> lobbies;
    lobbies.emplace(l);
    for (const auto& wl : l->watcher_lobbies) {
      lobbies.emplace(wl);
    }
    for (const auto& rl : lobbies) {
      for (const auto& rc : rl->clients) {
        if (rc) {
          rc->ep3_prev_battle_record = l->battle_record;
          if ((s->ep3_behavior_flags & Episode3::BehaviorFlag::ENABLE_STATUS_MESSAGES)) {
            send_text_message(rc, "$C7Recording complete");
          }
        }
      }
    }
    l->battle_record.reset();
  }

  if (l->tournament_match &&
      l->ep3_server->setup_phase == Episode3::SetupPhase::BATTLE_ENDED &&
      !l->ep3_server->tournament_match_result_sent) {
    int8_t winner_team_id = l->ep3_server->get_winner_team_id();
    if (winner_team_id == -1) {
      throw runtime_error("match complete, but winner team not specified");
    }

    auto tourn = l->tournament_match->tournament.lock();

    shared_ptr<Episode3::Tournament::Team> winner_team;
    shared_ptr<Episode3::Tournament::Team> loser_team;
    if (winner_team_id == 0) {
      winner_team = l->tournament_match->preceding_a->winner_team;
      loser_team = l->tournament_match->preceding_b->winner_team;
    } else if (winner_team_id == 1) {
      winner_team = l->tournament_match->preceding_b->winner_team;
      loser_team = l->tournament_match->preceding_a->winner_team;
    } else {
      throw logic_error("invalid winner team id");
    }
    l->tournament_match->set_winner_team(winner_team);

    uint32_t meseta_reward = 0;
    auto& round_rewards = loser_team->has_any_human_players()
        ? s->ep3_defeat_player_meseta_rewards
        : s->ep3_defeat_com_meseta_rewards;
    meseta_reward = (l->tournament_match->round_num - 1 < round_rewards.size())
        ? round_rewards[l->tournament_match->round_num - 1]
        : round_rewards.back();
    if (tourn && (l->tournament_match == tourn->get_final_match())) {
      meseta_reward += s->ep3_final_round_meseta_bonus;
    }
    for (const auto& player : winner_team->players) {
      if (player.is_human()) {
        auto winner_c = player.client.lock();
        if (winner_c) {
          winner_c->login->account->ep3_current_meseta += meseta_reward;
          winner_c->login->account->ep3_total_meseta_earned += meseta_reward;
          if (s->allow_saving_accounts) {
            winner_c->login->account->save();
          }
          send_ep3_rank_update(winner_c);
        }
      }
    }
    send_ep3_tournament_match_result(l, meseta_reward);

    if (tourn) {
      on_tournament_bracket_updated(s, tourn);
    }
    l->ep3_server->tournament_match_result_sent = true;
  }
}

asio::awaitable<void> on_E2_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  switch (msg.flag) {
    case 0x00: // Request tournament list
      send_ep3_tournament_list(c, false);
      break;
    case 0x01: { // Check tournament
      auto team = c->ep3_tournament_team.lock();
      if (team) {
        auto tourn = team->tournament.lock();
        if (tourn) {
          send_ep3_tournament_entry_list(c, tourn, false);
        } else {
          send_lobby_message_box(c, "$C7The tournament\nhas concluded.");
        }
      } else {
        send_lobby_message_box(c, "$C7You are not\nregistered in a\ntournament.");
      }
      break;
    }
    case 0x02: { // Cancel tournament entry
      auto team = c->ep3_tournament_team.lock();
      if (team) {
        auto tourn = team->tournament.lock();
        if (tourn) {
          if (tourn->get_state() != Episode3::Tournament::State::COMPLETE) {
            auto s = c->require_server_state();
            team->unregister_player(c->login->account->account_id);
            on_tournament_bracket_updated(s, tourn);
          }
          c->ep3_tournament_team.reset();
        }
      }
      if (c->version() != Version::GC_EP3_NTE) {
        send_ep3_confirm_tournament_entry(c, nullptr);
      }
      break;
    }
    case 0x03: // Create tournament spectator team (get battle list)
    case 0x04: // Join tournament spectator team (get team list)
      send_lobby_message_box(c, "$C7Use View Regular\nBattle for this");
      break;
    default:
      throw runtime_error("invalid tournament operation");
  }
  co_return;
}
