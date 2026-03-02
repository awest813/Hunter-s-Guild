#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

static constexpr const char* QUEST_BARRIER_DISCONNECT_HOOK_NAME = "quest_barrier";

void on_quest_loaded(shared_ptr<Lobby> l) {
  if (!l->quest) {
    throw logic_error("on_quest_loaded called without a quest loaded");
  }
  auto leader_c = l->clients.at(l->leader_id);
  if (!leader_c) {
    throw std::logic_error("lobby has no leader");
  }

  // Replace the free-play map with the quest's map
  l->load_maps();

  auto s = l->require_server_state();

  // Delete all floor items
  for (auto& m : l->floor_item_managers) {
    m.clear();
  }

  for (auto& lc : l->clients) {
    if (!lc) {
      continue;
    }

    if (lc->version() == Version::BB_V4) {
      send_rare_enemy_index_list(lc, l->map_state->bb_rare_enemy_indexes);
    }

    lc->delete_overlay();

    if ((l->quest->meta.challenge_template_index >= 0) && !is_v4(leader_c->version())) {
      // If the leader is BB, they will send an 02DF command that will create the overlays later; on other versions, we
      // do it at quest start time (now) instead, hence the version check above.
      if (is_v4(lc->version())) {
        lc->change_bank(lc->bb_character_index);
      }
      lc->create_challenge_overlay(lc->version(), l->quest->meta.challenge_template_index, s->level_table(lc->version()));
      lc->log.info_f("Created challenge overlay");
      l->assign_inventory_and_bank_item_ids(lc, true);

    } else if (l->quest->meta.battle_rules) {
      if (is_v4(lc->version())) {
        lc->change_bank(lc->bb_character_index);
      }
      lc->create_battle_overlay(l->quest->meta.battle_rules, s->level_table(lc->version()));
      lc->log.info_f("Created battle overlay");
    }
  }
}

void set_lobby_quest(shared_ptr<Lobby> l, shared_ptr<const Quest> q, bool substitute_v3_for_ep3) {
  if (!l->is_game()) {
    throw logic_error("non-game lobby cannot accept a quest");
  }
  if (l->quest) {
    throw runtime_error("lobby already has an assigned quest");
  }

  // Only allow loading battle/challenge quests if the game mode is correct
  if ((q->meta.challenge_template_index >= 0) != (l->mode == GameMode::CHALLENGE)) {
    throw runtime_error("incorrect game mode");
  }
  if ((q->meta.battle_rules != nullptr) != (l->mode == GameMode::BATTLE)) {
    throw runtime_error("incorrect game mode");
  }

  auto s = l->require_server_state();

  if (q->meta.joinable) {
    l->set_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS);
  } else {
    l->set_flag(Lobby::Flag::QUEST_IN_PROGRESS);
  }
  l->clear_flag(Lobby::Flag::QUEST_SELECTION_IN_PROGRESS);
  l->clear_flag(Lobby::Flag::PERSISTENT);

  l->quest = q;
  if (l->episode != Episode::EP3) {
    l->episode = q->meta.episode;
  }
  if (l->quest->meta.allowed_drop_modes) {
    l->allowed_drop_modes = l->quest->meta.allowed_drop_modes;
    l->drop_mode = l->quest->meta.default_drop_mode;
  }
  if (l->quest->meta.challenge_difficulty != Difficulty::UNKNOWN) {
    l->difficulty = l->quest->meta.challenge_difficulty;
  }
  l->create_item_creator();

  size_t num_clients_with_loading_flag = 0;
  for (size_t client_id = 0; client_id < l->max_clients; client_id++) {
    auto lc = l->clients[client_id];
    if (!lc) {
      continue;
    }

    Version effective_version = (substitute_v3_for_ep3 && is_ep3(lc->version())) ? Version::GC_V3 : lc->version();

    auto vq = q->version(effective_version, lc->language());
    if (!vq) {
      send_lobby_message_box(lc, "$C6Quest does not exist\nfor this game version.");
      lc->channel->disconnect();
      break;
    }
    lc->log.info_f("Sending {} version of quest \"{}\"", name_for_language(vq->meta.language), vq->meta.name);

    string bin_filename = vq->bin_filename();
    string dat_filename = vq->dat_filename();
    string xb_filename = vq->xb_filename();
    send_open_quest_file(
        lc, bin_filename, bin_filename, xb_filename, vq->meta.quest_number, QuestFileType::ONLINE, vq->bin_contents);
    send_open_quest_file(
        lc, dat_filename, dat_filename, xb_filename, vq->meta.quest_number, QuestFileType::ONLINE, vq->dat_contents);

    // There is no such thing as command AC (quest barrier) on PSO V1 and V2; quests just start immediately when
    // they're done downloading. (This is also the case on GC Trial Edition.) There are also no chunk acknowledgements
    // (C->S 13 commands) like there are on v3 and later. So, for pre-V3 clients, we can just not set the loading flag,
    // since we won't be told when to clear it later.
    // TODO: For V3 and V4 clients, the quest doesn't finish loading here, so technically we should queue up commands
    // sent by pre-V3 clients, since they might start the quest before V3/V4 clients do. We can probably use a method
    // similar to game_join_command_queue.
    if (!is_v1_or_v2(lc->version())) {
      num_clients_with_loading_flag++;
      lc->set_flag(Client::Flag::LOADING_QUEST);
      lc->log.info_f("LOADING_QUEST flag set");
      lc->disconnect_hooks.emplace(QUEST_BARRIER_DISCONNECT_HOOK_NAME, [l]() -> void {
        send_quest_barrier_if_all_clients_ready(l);
      });
    } else {
      lc->log.info_f("LOADING_QUEST flag skipped");
    }
  }

  if (num_clients_with_loading_flag == 0) {
    l->log.info_f("No clients require the LOADING_QUEST flag; starting quest now");
    on_quest_loaded(l);
  }
}
