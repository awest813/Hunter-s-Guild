#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_10_tournaments(shared_ptr<Client> c, uint32_t menu_id, uint32_t item_id) {
  if (!is_ep3(c->version())) {
    throw runtime_error("non-Episode 3 client attempted to join tournament");
  }
  auto s = c->require_server_state();
  auto tourn = s->ep3_tournament_index->get_tournament(item_id);
  if (tourn) {
    send_ep3_tournament_entry_list(c, tourn, (menu_id == MenuID::TOURNAMENTS_FOR_SPEC));
  }
  co_return;
}

asio::awaitable<void> on_10_tournament_entries(
    shared_ptr<Client> c, uint32_t item_id, std::string&& team_name, std::string&& password) {
  if (!is_ep3(c->version())) {
    throw runtime_error("non-Episode 3 client attempted to join tournament");
  }
  if (c->ep3_tournament_team.lock()) {
    send_lobby_message_box(c, "$C7You are registered\nin a different\ntournament already");
    co_return;
  }
  if (team_name.empty()) {
    team_name = c->character_file()->disp.name.decode(c->language());
    team_name += std::format("/{:X}", c->login->account->account_id);
  }
  uint16_t tourn_num = item_id >> 16;
  uint16_t team_index = item_id & 0xFFFF;
  auto s = c->require_server_state();
  auto tourn = s->ep3_tournament_index->get_tournament(tourn_num);
  if (tourn) {
    auto team = tourn->get_team(team_index);
    if (team) {
      try {
        team->register_player(c, team_name, password);
        c->ep3_tournament_team = team;
        tourn->send_all_state_updates();
        string message = std::format("$C7You are registered in $C6{}$C7.\n\
\n\
After the tournament begins, start your matches\n\
by standing at any Battle Table along with your\n\
partner (if any) and opponent(s).",
            tourn->get_name());
        send_ep3_timed_message_box(c->channel, 240, message);

        s->ep3_tournament_index->save();

      } catch (const exception& e) {
        string message = std::format("Cannot join team:\n{}", e.what());
        send_lobby_message_box(c, message);
      }
    } else {
      send_lobby_message_box(c, "Team does not exist");
    }
  } else {
    send_lobby_message_box(c, "Tournament does\nnot exist");
  }
}
