#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_C1_PC(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_CreateGame_PC_C1>(msg.data);
  auto s = c->require_server_state();

  GameMode mode = GameMode::NORMAL;
  if (cmd.battle_mode) {
    mode = GameMode::BATTLE;
  } else if (cmd.challenge_mode) {
    mode = GameMode::CHALLENGE;
  }
  auto game = create_game_generic(s, c, cmd.name.decode(c->language()), cmd.password.decode(c->language()), Episode::EP1, mode, cmd.difficulty, true);
  if (game) {
    s->change_client_lobby(c, game);
    c->set_flag(Client::Flag::LOADING);
    c->log.info_f("LOADING flag set");
  }
  co_return;
}

asio::awaitable<void> on_0C_C1_E7_EC(shared_ptr<Client> c, Channel::Message& msg) {
  auto s = c->require_server_state();

  shared_ptr<Lobby> game;
  if (is_pre_v1(c->version())) {
    const auto& cmd = check_size_t<C_CreateGame_DCNTE>(msg.data);
    game = create_game_generic(
        s,
        c,
        cmd.name.decode(c->language()),
        cmd.password.decode(c->language()),
        Episode::EP1,
        GameMode::NORMAL,
        Difficulty::NORMAL,
        true);

  } else {
    const auto& cmd = check_size_t<C_CreateGame_DC_V3_0C_C1_Ep3_EC>(msg.data);

    // Only allow E7/EC from Ep3 clients
    bool client_is_ep3 = is_ep3(c->version());
    if (((msg.command & 0xF0) == 0xE0) != client_is_ep3) {
      throw runtime_error("invalid command");
    }

    Episode episode = Episode::NONE;
    bool allow_v1 = false;
    if (is_v1_or_v2(c->version()) && (c->version() != Version::GC_NTE)) {
      allow_v1 = (cmd.episode == 0);
      episode = Episode::EP1;
    } else if (client_is_ep3) {
      episode = Episode::EP3;
    } else { // XB/GC non-Ep3
      episode = cmd.episode == 2 ? Episode::EP2 : Episode::EP1;
    }

    GameMode mode = GameMode::NORMAL;
    bool spectators_forbidden = false;
    if (cmd.battle_mode) {
      mode = GameMode::BATTLE;
    }
    if (cmd.challenge_mode) {
      if (client_is_ep3) {
        spectators_forbidden = true;
      } else {
        mode = GameMode::CHALLENGE;
      }
    }

    shared_ptr<Lobby> watched_lobby;
    if (msg.command == 0xE7) {
      if (cmd.menu_id != MenuID::GAME) {
        throw runtime_error("incorrect menu ID");
      }
      watched_lobby = s->find_lobby(cmd.item_id);
      if (!watched_lobby) {
        send_lobby_message_box(c, "$C7This game no longer\nexists");
        co_return;
      }
      if (watched_lobby->check_flag(Lobby::Flag::SPECTATORS_FORBIDDEN)) {
        send_lobby_message_box(c, "$C7This game does not\nallow spectators");
        co_return;
      }
    }

    game = create_game_generic(s, c, cmd.name.decode(c->language()), cmd.password.decode(c->language()), episode, mode, cmd.difficulty, allow_v1, watched_lobby);
    if (game && (game->episode == Episode::EP3)) {
      game->ep3_ex_result_values = s->ep3_default_ex_values;
      if (spectators_forbidden) {
        game->set_flag(Lobby::Flag::SPECTATORS_FORBIDDEN);
      }
    }
  }

  if (game) {
    s->change_client_lobby(c, game);
    c->set_flag(Client::Flag::LOADING);
    c->log.info_f("LOADING flag set");

    // There is a bug in DC NTE and 11/2000 that causes them to assign item IDs twice when joining a game. If there are
    // other players in the game, this isn't an issue because the equivalent of the 6x6D command resets the next item
    // ID before the second assignment, so the item IDs stay in sync with the server. If there was no one else in the
    // game, however (as in this case, when it was just created), we need to artificially change the next item IDs
    // during the client's loading procedure.
    if (is_pre_v1(c->version())) {
      c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_ITEM_STATE);
    }
  }
}

asio::awaitable<void> on_C1_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_CreateGame_BB_C1>(msg.data);
  auto s = c->require_server_state();

  GameMode mode = GameMode::NORMAL;
  if (cmd.battle_mode) {
    mode = GameMode::BATTLE;
  }
  if (cmd.challenge_mode) {
    mode = GameMode::CHALLENGE;
  }
  if (cmd.solo_mode) {
    mode = GameMode::SOLO;
  }

  Episode episode;
  switch (cmd.episode) {
    case 1:
      episode = Episode::EP1;
      break;
    case 2:
      episode = Episode::EP2;
      break;
    case 3:
      episode = Episode::EP4;
      // Disallow battle/challenge in Ep4
      if (mode == GameMode::BATTLE) {
        send_lobby_message_box(c, "$C7Episode 4 does not\nsupport Battle Mode.");
        co_return;
      }
      if (mode == GameMode::CHALLENGE) {
        send_lobby_message_box(c, "$C7Episode 4 does not\nsupport Challenge Mode.");
        co_return;
      }
      break;
    default:
      throw runtime_error("invalid episode number");
  }

  auto game = create_game_generic(s, c, cmd.name.decode(c->language()), cmd.password.decode(c->language()), episode, mode, cmd.difficulty);
  if (game) {
    s->change_client_lobby(c, game);
    c->set_flag(Client::Flag::LOADING);
    c->log.info_f("LOADING flag set");
  }
}
