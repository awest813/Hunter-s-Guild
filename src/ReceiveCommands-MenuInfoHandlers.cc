#include "ReceiveCommands.hh"

#include <phosg/Strings.hh>

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

void send_main_menu(shared_ptr<Client> c);

asio::awaitable<void> on_D6_V3(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  if (c->check_flag(Client::Flag::IN_INFORMATION_MENU)) {
    auto s = c->require_server_state();
    send_menu(c, s->information_menu(c->version()));
  } else if (c->check_flag(Client::Flag::AT_WELCOME_MESSAGE)) {
    c->clear_flag(Client::Flag::AT_WELCOME_MESSAGE);
    send_main_menu(c);
  }
  co_return;
}

asio::awaitable<void> on_09(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_MenuItemInfoRequest_09>(msg.data);
  auto s = c->require_server_state();

  switch (cmd.menu_id) {
    case MenuID::QUEST_CATEGORIES_EP1_EP3_EP4:
    case MenuID::QUEST_CATEGORIES_EP2:
      // Don't send anything here. The quest filter menu already has short descriptions included with the entries,
      // which the client shows in the usual location on the screen.
      break;
    case MenuID::QUEST_EP1:
    case MenuID::QUEST_EP2: {
      bool is_download_quest = !c->lobby.lock();
      if (!s->quest_index) {
        send_quest_info(c, "$C7Quests are not available.", 0x00, is_download_quest);
      } else {
        auto q = s->quest_index->get(cmd.item_id);
        if (!q) {
          send_quest_info(c, "$C4Quest does not\nexist.", 0x00, is_download_quest);
        } else {
          auto vq = q->version(c->version(), c->language());
          if (!vq) {
            send_quest_info(c, "$C4Quest does not\nexist for this game\nversion.", 0x00, is_download_quest);
          } else {
            send_quest_info(c, vq->meta.long_description, vq->meta.description_flag, is_download_quest);
          }
        }
      }
      break;
    }
    case MenuID::QUEST_EP3: {
      auto vis_flag = (c->version() == Version::GC_EP3_NTE)
          ? Episode3::MapIndex::VisibilityFlag::ONLINE_TRIAL
          : Episode3::MapIndex::VisibilityFlag::ONLINE_FINAL;

      auto map = s->ep3_map_index->map_for_id(cmd.item_id);
      if (!map || !map->check_visibility_flag(vis_flag)) {
        send_quest_info(c, "$C4Map does not exist.", 0x00, true);
      } else {
        auto vm = map->version(c->language());
        send_quest_info(c, vm->map->description.decode(vm->language), 0x00, true);
      }
      break;
    }

    case MenuID::GAME: {
      auto game = s->find_lobby(cmd.item_id);
      if (!game) {
        send_ship_info(c, "$C4Game no longer\nexists.");
        break;
      }

      if (!game->is_game()) {
        send_ship_info(c, "$C4Incorrect game ID");

      } else if (is_ep3(c->version()) && game->is_ep3()) {
        send_ep3_game_details(c, game);

      } else {
        string info;
        if (c->last_game_info_requested != game->lobby_id) {
          // Send page 1 (players)
          c->last_game_info_requested = game->lobby_id;
          for (size_t x = 0; x < game->max_clients; x++) {
            const auto& game_c = game->clients[x];
            if (game_c.get()) {
              static_assert(NUM_VERSIONS == 14, "Don\'t forget to update the game player listing version tokens");
              static const array<const char*, NUM_VERSIONS> version_tokens = {
                  " $C4P2$C7", " $C4P4$C7", " $C5DCN$C7", " $C5DCP$C7", " $C2DC1$C7", " $C2DC2$C7", " $C5PCN$C7",
                  " $C2PC$C7", " $C5GCN$C7", " $C2GC$C7", " $C5Ep3N$C7", " $C2Ep3$C7", " $C2XB$C7", " $C2BB$C7"};
              const char* version_token = (game_c->version() != c->version())
                  ? version_tokens.at(static_cast<size_t>(game_c->version()))
                  : "";
              auto player = game_c->character_file();
              string name = escape_player_name(player->disp.name.decode(game_c->language()));
              info += std::format("{}{}\n  {} Lv{} {}\n",
                  name,
                  version_token,
                  name_for_char_class(player->disp.visual.char_class),
                  player->disp.stats.level + 1,
                  char_for_language(game_c->language()));
            }
          }
        }

        // If page 1 is blank (there are no players) or we sent page 1 last time, send page 2 (extended info)
        if (info.empty()) {
          c->last_game_info_requested = 0;
          uint8_t effective_section_id = game->effective_section_id();
          if (effective_section_id < 10) {
            info += std::format("Section ID: {}\n", name_for_section_id(effective_section_id));
          }
          if (game->max_level != 0xFFFFFFFF) {
            info += std::format("Req. level: {}-{}\n", game->min_level + 1, game->max_level + 1);
          } else if (game->min_level != 0) {
            info += std::format("Req. level: {}+\n", game->min_level + 1);
          }

          if (game->check_flag(Lobby::Flag::CHEATS_ENABLED)) {
            info += "$C6Cheats enabled$C7\n";
          }
          if (game->check_flag(Lobby::Flag::PERSISTENT)) {
            info += "$C6Persistence enabled$C7\n";
          }

          if (game->quest) {
            info += (game->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) ? "$C6Quest: " : "$C4Quest: ";
            info += remove_color(game->quest->name_for_language(c->language()));
            info += "\n";
          } else if (game->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
            info += "$C6Quest in progress\n";
          } else if (game->check_flag(Lobby::Flag::QUEST_IN_PROGRESS)) {
            info += "$C4Quest in progress\n";
          } else if (game->check_flag(Lobby::Flag::QUEST_SELECTION_IN_PROGRESS)) {
            info += "$C4Selecting quest\n";
          }

          switch (game->drop_mode) {
            case ServerDropMode::DISABLED:
              info += "$C6Drops disabled$C7\n";
              break;
            case ServerDropMode::CLIENT:
              info += "$C6Client drops$C7\n";
              break;
            case ServerDropMode::SERVER_SHARED:
              info += "$C6Server drops$C7\n";
              break;
            case ServerDropMode::SERVER_PRIVATE:
              info += "$C6Private drops$C7\n";
              break;
            case ServerDropMode::SERVER_DUPLICATE:
              info += "$C6Duplicate drops$C7\n";
              break;
          }
        }
        phosg::strip_trailing_whitespace(info);
        send_ship_info(c, info);
      }
      break;
    }

    case MenuID::TOURNAMENTS_FOR_SPEC:
    case MenuID::TOURNAMENTS: {
      if (!is_ep3(c->version())) {
        send_ship_info(c, "Incorrect menu ID");
        break;
      }
      auto tourn = s->ep3_tournament_index->get_tournament(cmd.item_id);
      if (tourn) {
        send_ep3_tournament_details(c, tourn);
      }
      break;
    }

    case MenuID::TOURNAMENT_ENTRIES: {
      if (!is_ep3(c->version())) {
        send_ship_info(c, "Incorrect menu ID");
        break;
      }
      uint16_t tourn_num = cmd.item_id >> 16;
      uint16_t team_index = cmd.item_id & 0xFFFF;
      auto tourn = s->ep3_tournament_index->get_tournament(tourn_num);
      if (tourn) {
        auto team = tourn->get_team(team_index);
        if (team) {
          string message;
          string team_name = escape_player_name(team->name);
          if (team_name.empty()) {
            message = "(No registrant)";
          } else if (team->max_players == 1) {
            message = std::format("$C6{}$C7\n{} {} ({})\nPlayers:",
                team_name,
                team->num_rounds_cleared,
                team->num_rounds_cleared == 1 ? "win" : "wins",
                team->is_active ? "active" : "defeated");
          } else {
            message = std::format("$C6{}$C7\n{} {} ({}){}\nPlayers:",
                team_name,
                team->num_rounds_cleared,
                team->num_rounds_cleared == 1 ? "win" : "wins",
                team->is_active ? "active" : "defeated",
                team->password.empty() ? "" : "\n$C4Locked$C7");
          }
          for (const auto& player : team->players) {
            if (player.is_human()) {
              if (player.player_name.empty()) {
                message += std::format("\n  $C6{:08X}$C7", player.account_id);
              } else {
                string player_name = escape_player_name(player.player_name);
                message += std::format("\n  $C6{}$C7 ({:08X})", player_name, player.account_id);
              }
            } else {
              string player_name = escape_player_name(player.com_deck->player_name);
              string deck_name = escape_player_name(player.com_deck->deck_name);
              message += std::format("\n  $C3{} \"{}\"$C7", player_name, deck_name);
            }
          }
          send_ship_info(c, message);
        } else {
          send_ship_info(c, "$C7No such team");
        }
      } else {
        send_ship_info(c, "$C7No such tournament");
      }
      break;
    }

    default:
      if (!c->last_menu_sent || c->last_menu_sent->menu_id != cmd.menu_id) {
        send_ship_info(c, "Incorrect menu ID");
      } else {
        for (const auto& item : c->last_menu_sent->items) {
          if (item.item_id == cmd.item_id) {
            if (item.get_description != nullptr) {
              send_ship_info(c, item.get_description());
            } else {
              send_ship_info(c, item.description);
            }
            co_return;
          }
        }
        send_ship_info(c, "$C4Incorrect menu\nitem ID");
      }
      break;
  }
  co_return;
}
