#include "SendCommands.hh"

#include <algorithm>
#include <ctime>
#include <stdexcept>

#include <phosg/Random.hh>
#include <phosg/Time.hh>

using namespace std;

extern const char* QUEST_BARRIER_DISCONNECT_HOOK_NAME;

bool send_quest_barrier_if_all_clients_ready(shared_ptr<Lobby> l) {
  if (!l || !l->is_game()) {
    return false;
  }

  // Check if any client is still loading
  size_t x;
  for (x = 0; x < l->max_clients; x++) {
    if (!l->clients[x]) {
      continue;
    }
    if (l->clients[x]->check_flag(Client::Flag::LOADING_QUEST)) {
      break;
    }
  }

  // If they're all done, start the quest
  if (x != l->max_clients) {
    return false;
  }

  for (auto& lc : l->clients) {
    if (lc) {
      if (!is_v1_or_v2(lc->version())) {
        send_command(lc, 0xAC, 0x00);
      }
      lc->disconnect_hooks.erase(QUEST_BARRIER_DISCONNECT_HOOK_NAME);
    }
  }
  return true;
}

bool send_ep3_start_tournament_deck_select_if_all_clients_ready(shared_ptr<Lobby> l) {
  if (!l || !l->is_game() || (l->episode != Episode::EP3) || !l->tournament_match) {
    return false;
  }
  auto tourn = l->tournament_match->tournament.lock();
  if (!tourn) {
    return false;
  }

  // Check if any client is still loading
  size_t x;
  for (x = 0; x < l->max_clients; x++) {
    if (!l->clients[x]) {
      continue;
    }
    if (l->clients[x]->check_flag(Client::Flag::LOADING_TOURNAMENT)) {
      break;
    }
  }

  // If they're all done, start deck selection
  if (x == l->max_clients) {
    if (!l->ep3_server) {
      l->create_ep3_server();
    }
    l->ep3_server->send_6xB6x41_to_all_clients();
    for (auto c : l->clients) {
      if (c) {
        send_ep3_set_tournament_player_decks(c);
      }
    }
    return true;
  } else {
    return false;
  }
}

void send_ep3_card_auction(shared_ptr<Lobby> l) {
  auto s = l->require_server_state();
  if ((s->ep3_card_auction_points == 0) ||
      (s->ep3_card_auction_min_size == 0) ||
      (s->ep3_card_auction_max_size == 0)) {
    throw runtime_error("card auctions are not configured on this server");
  }

  uint16_t num_cards;
  if (s->ep3_card_auction_min_size == s->ep3_card_auction_max_size) {
    num_cards = s->ep3_card_auction_min_size;
  } else {
    num_cards = s->ep3_card_auction_min_size +
        (phosg::random_object<uint16_t>() % (s->ep3_card_auction_max_size - s->ep3_card_auction_min_size + 1));
  }
  num_cards = min<uint16_t>(num_cards, 0x14);

  auto card_index = l->is_ep3_nte() ? s->ep3_card_index_trial : s->ep3_card_index;
  (void)card_index;

  uint64_t distribution_size = 0;
  for (const auto& e : s->ep3_card_auction_pool) {
    distribution_size += e.probability;
  }

  S_StartCardAuction_Ep3_EF cmd;
  cmd.points_available = s->ep3_card_auction_points;
  for (size_t z = 0; z < num_cards; z++) {
    uint64_t v = phosg::random_object<uint64_t>() % distribution_size;
    for (const auto& e : s->ep3_card_auction_pool) {
      if (v >= e.probability) {
        v -= e.probability;
      } else {
        cmd.entries[z].card_id = e.card_id;
        cmd.entries[z].min_price = e.min_price;
        break;
      }
    }
  }
  send_command_t(l, 0xEF, num_cards, cmd);
}

void send_ep3_disband_watcher_lobbies(shared_ptr<Lobby> primary_l) {
  for (auto watcher_l : primary_l->watcher_lobbies) {
    if (!watcher_l->is_ep3()) {
      throw logic_error("spectator team is not an Episode 3 lobby");
    }
    primary_l->log.info_f("Disbanding watcher lobby {:X}", watcher_l->lobby_id);
    send_command(watcher_l, 0xED, 0x00);
  }
}

void send_server_time(shared_ptr<Client> c) {
  // DC NTE and 11/2000 don't have this command
  if (is_pre_v1(c->version())) {
    return;
  }

  uint64_t t = phosg::now();

  time_t t_secs = t / 1000000;
  struct tm t_parsed;
#ifndef PHOSG_WINDOWS
  gmtime_r(&t_secs, &t_parsed);
#else
  gmtime_s(&t_parsed, &t_secs);
#endif

  string time_str(128, 0);
  size_t len = strftime(time_str.data(), time_str.size(), "%Y:%m:%d: %H:%M:%S.000", &t_parsed);
  if (len == 0) { // 128 should always be long enough
    throw logic_error("strftime buffer too short");
  }
  time_str.resize(len);

  S_ServerTime_B1 cmd;
  cmd.time_str.encode(time_str);
  cmd.time_flags_low = 0x01;
  cmd.time_flags_mid = 0x00;
  cmd.time_flags_high = 0x00;
  send_command_t(c, 0xB1, 0x00, cmd);
}

void send_change_event(shared_ptr<Client> c, uint8_t new_event) {
  // This command isn't supported on versions before V3 (including GC NTE), nor on the BB data server
  if (((c->version() != Version::BB_V4) || (c->bb_connection_phase >= 0x04)) &&
      !is_v1_or_v2(c->version()) &&
      !is_patch(c->version())) {
    send_command(c, 0xDA, new_event);
  }
}

void send_change_event(shared_ptr<Lobby> l, uint8_t new_event) {
  for (auto& c : l->clients) {
    if (!c) {
      continue;
    }
    send_change_event(c, new_event);
  }
}

void send_change_event(shared_ptr<ServerState> s, uint8_t new_event) {
  // TODO: Create a collection of all clients on the server (including those not in lobbies) and use that here instead
  for (auto& l : s->all_lobbies()) {
    send_change_event(l, new_event);
  }
}
