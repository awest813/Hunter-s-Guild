#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_84(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_LobbySelection_84>(msg.data);
  auto s = c->require_server_state();

  if (c->check_flag(Client::Flag::AWAITING_ENABLE_B2_QUEST)) {
    co_await start_login_server_procedure(c);
    c->clear_flag(Client::Flag::AWAITING_ENABLE_B2_QUEST);
    co_return;
  }

  if (cmd.menu_id != MenuID::LOBBY) {
    send_message_box(c, "Incorrect menu ID");
    co_return;
  }

  if (!c->lobby.lock()) {
    // If the client isn't in any lobby, then they just left a game. Add them to the lobby they requested, but fall
    // back to another lobby if it's full.
    c->preferred_lobby_id = cmd.item_id;
    s->add_client_to_available_lobby(c);

  } else {
    // If the client already is in a lobby, then they're using the lobby teleporter; add them to the lobby they
    // requested or send a failure message.
    auto new_lobby = s->find_lobby(cmd.item_id);
    if (!new_lobby) {
      send_lobby_message_box(c, "$C6Can't change lobby\n\n$C7The lobby does not\nexist.");
      co_return;
    }

    if (new_lobby->is_game()) {
      send_lobby_message_box(c, "$C6Can't change lobby\n\n$C7The specified lobby\nis a game.");
      co_return;
    }

    if (new_lobby->is_ep3() && !is_ep3(c->version())) {
      send_lobby_message_box(c, "$C6Can't change lobby\n\n$C7The lobby is for\nEpisode 3 only.");
      co_return;
    }

    if (!s->change_client_lobby(c, new_lobby)) {
      send_lobby_message_box(c, "$C6Can\'t change lobby\n\n$C7The lobby is full.");
    }
  }
}

asio::awaitable<void> on_08_E6(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  send_game_menu(c, (msg.command == 0xE6), false);
  co_return;
}

asio::awaitable<void> on_1F(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  auto s = c->require_server_state();
  send_menu(c, s->information_menu(c->version()), true);
  co_return;
}

asio::awaitable<void> on_A0(shared_ptr<Client> c, Channel::Message&) {
  // The client sends data in this command, but none of it is important. We intentionally don't call check_size here,
  // but just ignore the data.

  // Delete the player from the lobby they're in (but only visible to themself). This makes it safe to allow the
  // player to choose download quests from the main menu again - if we didn't do this, they could move in the lobby
  // after canceling the download quests menu, which looks really bad.
  send_self_leave_notification(c);

  // Sending a blank message box here works around the bug where the log window contents appear prepended to the next
  // large message box. But, we don't have to do this if we're not going to show the welcome message or information
  // menu (that is, if the client will not send a close confirmation).
  if (!c->check_flag(Client::Flag::NO_D6)) {
    send_message_box(c, "");
  }

  return start_login_server_procedure(c);
}

asio::awaitable<void> on_A1(shared_ptr<Client> c, Channel::Message& msg) {
  // newserv doesn't have blocks; treat block change the same as ship change
  return on_A0(c, msg);
}

asio::awaitable<void> on_8E_DCNTE(shared_ptr<Client> c, Channel::Message& msg) {
  if (c->version() == Version::DC_NTE) {
    return on_A0(c, msg);
  } else {
    throw runtime_error("non-DCNTE client sent 8E");
  }
}

asio::awaitable<void> on_8F_DCNTE(shared_ptr<Client> c, Channel::Message& msg) {
  if (c->version() == Version::DC_NTE) {
    return on_A1(c, msg);
  } else {
    throw runtime_error("non-DCNTE client sent 8F");
  }
}

asio::awaitable<void> on_A2(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  auto s = c->require_server_state();

  auto l = c->lobby.lock();
  if (!l || !l->is_game()) {
    send_lobby_message_box(c, "$C7Quests are not available\nin lobbies.");
    co_return;
  }

  if (is_ep3(c->version())) {
    send_lobby_message_box(c, "$C7Episode 3 does not\nprovide online quests\nvia this interface.");
    co_return;
  }

  QuestMenuType menu_type;
  if ((c->version() == Version::BB_V4) && msg.flag) {
    menu_type = QuestMenuType::GOVERNMENT;
  } else {
    switch (l->mode) {
      case GameMode::NORMAL:
        menu_type = QuestMenuType::NORMAL;
        break;
      case GameMode::BATTLE:
        menu_type = QuestMenuType::BATTLE;
        break;
      case GameMode::CHALLENGE:
        menu_type = QuestMenuType::CHALLENGE;
        break;
      case GameMode::SOLO:
        menu_type = QuestMenuType::SOLO;
        break;
      default:
        throw logic_error("invalid game mode");
    }
  }

  send_quest_categories_menu(c, menu_type, l->episode);
  l->set_flag(Lobby::Flag::QUEST_SELECTION_IN_PROGRESS);
}

asio::awaitable<void> on_A9(shared_ptr<Client> c, Channel::Message&) {
  auto l = c->require_lobby();
  if (l->is_game() && (c->lobby_client_id == l->leader_id)) {
    l->clear_flag(Lobby::Flag::QUEST_SELECTION_IN_PROGRESS);
  }
  co_return;
}
