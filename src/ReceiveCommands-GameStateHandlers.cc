#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

bool add_next_game_client(shared_ptr<Lobby> l);
extern const char* ADD_NEXT_CLIENT_DISCONNECT_HOOK_NAME;

void on_joinable_quest_loaded(shared_ptr<Client> c) {
  auto l = c->require_lobby();
  if (!l->is_game() || !l->quest) {
    throw runtime_error("joinable quest load completed in non-game");
  }
  auto leader_c = l->clients.at(l->leader_id);
  if (!leader_c) {
    throw logic_error("lobby leader is missing");
  }

  // On BB, ask the leader to send the quest state to the joining player (and we'll need to use the game join command
  // queue to avoid any item ID races). On other versions, the server will have to generate the state commands; this
  // happens when the response to the ping (1D) is received, so we don't need the game join command queue in that case.
  if (leader_c->version() == Version::BB_V4) {
    send_command(leader_c, 0xDD, c->lobby_client_id);
    c->log.info_f("Creating game join command queue");
    c->game_join_command_queue = make_unique<deque<Client::JoinCommand>>();
  } else {
    c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_ITEM_STATE);
    c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_ENEMY_AND_SET_STATE);
    c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_OBJECT_STATE);
    c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_FLAG_STATE);
    c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_PLAYER_STATES);
  }
  send_command(c, 0x1D, 0x00);

  if (!is_v1_or_v2(c->version())) {
    send_command(c, 0xAC, 0x00);
  }
}

asio::awaitable<void> on_6F(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  auto l = c->lobby.lock();
  if (!l) {
    if (!c->check_flag(Client::Flag::AWAITING_ENABLE_B2_QUEST)) {
      throw runtime_error("client is not in any lobby and is not awaiting a patch enabler quest");
    }
    auto s = c->require_server_state();
    shared_ptr<const Quest> q;
    try {
      int64_t quest_num = s->enable_send_function_call_quest_numbers.at(c->specific_version);
      q = s->quest_index->get(quest_num);
    } catch (const out_of_range&) {
      throw std::logic_error("cannot find patch enable quest after it was previously found during login");
    }
    auto vq = q->version(is_ep3(c->version()) ? Version::GC_V3 : c->version(), Language::ENGLISH);
    if (!vq) {
      throw std::logic_error("cannot find patch enable quest version after it was previously found during login");
    }
    c->log.info_f("Sending {} version of quest \"{}\"", name_for_language(vq->meta.language), vq->meta.name);
    string bin_filename = vq->bin_filename();
    string dat_filename = vq->dat_filename();
    string xb_filename = vq->xb_filename();
    send_open_quest_file(c, bin_filename, bin_filename, xb_filename, vq->meta.quest_number, QuestFileType::ONLINE, vq->bin_contents);
    send_open_quest_file(c, dat_filename, dat_filename, xb_filename, vq->meta.quest_number, QuestFileType::ONLINE, vq->dat_contents);
    co_return;
  }
  // Now l is not null

  if (!l->is_game()) {
    throw runtime_error("client sent ready command outside of game");
  }

  // Episode 3 sends a 6F after a CAx21 (end battle) command, so we shouldn't reassign the item IDs again in that case
  // (even though item IDs really don't matter for Ep3)
  if (c->check_flag(Client::Flag::LOADING)) {
    c->clear_flag(Client::Flag::LOADING);
    c->log.info_f("LOADING flag cleared");

    // The client sends 6F when it has created its TObjPlayer and assigned its item IDs. For the leader, however, this
    // happens before any inbound commands are processed, so we already did it when the client was added to the lobby.
    // So, we only assign item IDs here if the client is not the leader.
    if ((msg.command == 0x006F) && (c->lobby_client_id != l->leader_id)) {
      l->assign_inventory_and_bank_item_ids(c, true);
    }
  }

  if (l->ep3_server && l->ep3_server->battle_finished) {
    auto s = l->require_server_state();
    l->log.info_f("Deleting Episode 3 server state");
    l->ep3_server.reset();
  }

  send_server_time(c);
  if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
    string variations_str;
    for (size_t z = 0; z < l->variations.entries.size(); z++) {
      const auto& e = l->variations.entries[z];
      variations_str += std::format(" {:X}{:X}", e.layout, e.entities);
    }
    char type_ch = dynamic_pointer_cast<DisabledRandomGenerator>(l->rand_crypt).get()
        ? 'X'
        : dynamic_pointer_cast<MT19937Generator>(l->rand_crypt).get()
        ? 'M'
        : dynamic_pointer_cast<PSOV2Encryption>(l->rand_crypt).get()
        ? 'L'
        : '?';
    send_text_message_fmt(c, "Rare seed: {:08X}/{:c}\nVariations:{}\n", l->random_seed, type_ch, variations_str);
  }

  bool should_resume_game = true;
  if (c->version() == Version::BB_V4) {
    send_set_exp_multiplier(l);
    send_update_team_reward_flags(c);
    send_all_nearby_team_metadatas_to_client(c, false);

    // On BB, send the joinable quest file as soon as the client is ready (6F). On other versions, we send joinable
    // quests in the 99 handler instead, since we need to wait for the client's save to complete. BB sends 016F when
    // the client is done loading a quest. In that case, we shouldn't send the quest to them again!
    if ((msg.command == 0x006F) && l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
      if (!l->quest) {
        throw runtime_error("JOINABLE_QUEST_IN_PROGRESS is set, but lobby has no quest");
      }
      auto vq = l->quest->version(c->version(), c->language());
      if (!vq) {
        throw runtime_error("JOINABLE_QUEST_IN_PROGRESS is set, but lobby has no quest for client version");
      }
      string bin_filename = vq->bin_filename();
      string dat_filename = vq->dat_filename();

      send_open_quest_file(c, bin_filename, bin_filename, "", vq->meta.quest_number, QuestFileType::ONLINE, vq->bin_contents);
      send_open_quest_file(c, dat_filename, dat_filename, "", vq->meta.quest_number, QuestFileType::ONLINE, vq->dat_contents);
      c->set_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST);
      c->log.info_f("LOADING_RUNNING_JOINABLE_QUEST flag set");
      should_resume_game = false;

    } else if ((msg.command == 0x016F) && c->check_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST)) {
      c->clear_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST);
      c->log.info_f("LOADING_RUNNING_JOINABLE_QUEST flag cleared");
    }
    send_rare_enemy_index_list(c, l->map_state->bb_rare_enemy_indexes);
  }

  // We should resume the game if:
  // - command is 016F and a joinable quest is in progress
  // - command is 006F and a joinable quest is NOT in progress
  if (should_resume_game) {
    send_resume_game(l, c);
  }

  // Handle initial commands for spectator teams
  auto watched_lobby = l->watched_lobby.lock();
  if (l->battle_player && l->check_flag(Lobby::Flag::START_BATTLE_PLAYER_IMMEDIATELY)) {
    l->battle_player->start();
  } else if (watched_lobby && watched_lobby->ep3_server) {
    if (!watched_lobby->ep3_server->battle_finished) {
      watched_lobby->ep3_server->send_commands_for_joining_spectator(c->channel);
    }
    send_ep3_update_game_metadata(watched_lobby);
  }

  // If there are more players to bring in, try to do so
  c->disconnect_hooks.erase(ADD_NEXT_CLIENT_DISCONNECT_HOOK_NAME);
  add_next_game_client(l);
}

asio::awaitable<void> on_99(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  // This is an odd place to send 6xB4x52, but there's a reason for it. If the client receives 6xB4x52 while it's
  // loading the battlefield, it won't set the spectator count or top-bar text. But the client doesn't send anything
  // when it's done loading the battlefield, so we have to have some other way of knowing when it's ready. We do this
  // by sending a B1 (server time) command immediately after the E8 (join spectator team) command, which allows us to
  // delay sending the 6xB4x52 until the server responds with a 99 command after loading is done.
  auto l = c->lobby.lock();
  if (l && l->is_game() && (l->episode == Episode::EP3) && l->check_flag(Lobby::Flag::IS_SPECTATOR_TEAM)) {
    auto watched_l = l->watched_lobby.lock();
    if (watched_l) {
      send_ep3_update_game_metadata(watched_l);
    }
  }

  // See the comment in on_6F about why we do this here, but only for non-BB versions.
  if (l && l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS) && (c->version() != Version::BB_V4)) {
    if (!l->quest) {
      throw runtime_error("JOINABLE_QUEST_IN_PROGRESS is set, but lobby has no quest");
    }
    auto vq = l->quest->version(c->version(), c->language());
    if (!vq) {
      throw runtime_error("JOINABLE_QUEST_IN_PROGRESS is set, but lobby has no quest for client version");
    }
    string bin_filename = vq->bin_filename();
    string dat_filename = vq->dat_filename();

    send_open_quest_file(c, bin_filename, bin_filename, "", vq->meta.quest_number, QuestFileType::ONLINE, vq->bin_contents);
    send_open_quest_file(c, dat_filename, dat_filename, "", vq->meta.quest_number, QuestFileType::ONLINE, vq->dat_contents);
    c->set_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST);
    c->log.info_f("LOADING_RUNNING_JOINABLE_QUEST flag set");

    // On v1 and v2, there is no confirmation when the client is done downloading the quest file, so just set the in-
    // quest state immediately. On v3 and later, we do this when we receive the AC command.
    // TODO: This might not work for GC NTE, since we wait for file chunk confirmations but there is no AC command.
    if (is_v1_or_v2(c->version())) {
      on_joinable_quest_loaded(c);
    }
  }

  // If enable_save_if_needed is waiting, unblock it.
  if (c->enable_save_promise) {
    c->enable_save_promise->set_value();
    c->enable_save_promise.reset();
  }
  co_return;
}
