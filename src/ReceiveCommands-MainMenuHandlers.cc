#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

void send_main_menu(shared_ptr<Client> c);
void send_proxy_destinations_menu(shared_ptr<Client> c);
shared_ptr<const Menu> proxy_options_menu_for_client(shared_ptr<const Client> c);
asio::awaitable<void> send_auto_patches_if_needed(shared_ptr<Client> c);
asio::awaitable<void> enable_save_if_needed(shared_ptr<Client> c);

asio::awaitable<void> on_10_main_menu(shared_ptr<Client> c, uint32_t item_id) {
  auto s = c->require_server_state();

  switch (item_id) {
    case MainMenuItemID::GO_TO_LOBBY: {
      co_await send_auto_patches_if_needed(c);
      co_await enable_save_if_needed(c);
      send_lobby_list(c);
      if (is_pre_v1(c->version())) {
        co_await send_get_player_info(c);
      }
      if (!c->lobby.lock()) {
        s->add_client_to_available_lobby(c);
      }
      break;
    }

    case MainMenuItemID::INFORMATION: {
      send_menu(c, s->information_menu(c->version()));
      c->set_flag(Client::Flag::IN_INFORMATION_MENU);
      break;
    }

    case MainMenuItemID::PROXY_DESTINATIONS:
      send_proxy_destinations_menu(c);
      break;

    case MainMenuItemID::DOWNLOAD_QUESTS: {
      send_quest_categories_menu(c, QuestMenuType::DOWNLOAD, Episode::NONE);
      break;
    }

    case MainMenuItemID::PATCH_SWITCHES: {
      if (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL)) {
        throw runtime_error("client does not support send_function_call");
      }
      // We have to prepare the client for patches here, even though we don't send them from this mennu, because we
      // need to know the client's specific_version before sending the menu.
      co_await prepare_client_for_patches(c);
      send_menu(c, c->require_server_state()->function_code_index->patch_switches_menu(c->specific_version, s->auto_patches, c->login->account->auto_patches_enabled));
      break;
    }

    case MainMenuItemID::PROGRAMS: {
      if (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL)) {
        throw runtime_error("client does not support send_function_call");
      }
      co_await prepare_client_for_patches(c);
      send_menu(c, c->require_server_state()->dol_file_index->menu);
      break;
    }

    case MainMenuItemID::DISCONNECT:
      if (c->version() == Version::XB_V3) {
        // On XB (at least via Insignia) the server has to explicitly tell the client to disconnect using this command.
        send_command(c, 0x05, 0x00);
      }
      c->channel->disconnect();
      break;

    case MainMenuItemID::CLEAR_LICENSE: {
      auto conf_menu = make_shared<Menu>(MenuID::CLEAR_LICENSE_CONFIRMATION, s->name);
      conf_menu->items.emplace_back(ClearLicenseConfirmationMenuItemID::CANCEL, "Go back",
          "Go back to the\nmain menu", 0);
      conf_menu->items.emplace_back(ClearLicenseConfirmationMenuItemID::CLEAR_LICENSE, "Clear license",
          "Disconnect with an\ninvalid license error\nso you can enter a\ndifferent serial\nnumber, access key,\nor password",
          MenuItem::Flag::INVISIBLE_ON_DC_PROTOS | MenuItem::Flag::INVISIBLE_ON_PC_NTE | MenuItem::Flag::INVISIBLE_ON_XB | MenuItem::Flag::INVISIBLE_ON_BB);

      send_menu(c, conf_menu);
      send_ship_info(c, "Are you sure?");
      break;
    }

    default:
      send_message_box(c, "Incorrect menu item ID.");
      break;
  }
}

asio::awaitable<void> on_10_clear_license_confirmation(shared_ptr<Client> c, uint32_t item_id) {
  switch (item_id) {
    case ClearLicenseConfirmationMenuItemID::CANCEL:
      send_main_menu(c);
      break;
    case ClearLicenseConfirmationMenuItemID::CLEAR_LICENSE:
      send_command(c, 0x9A, 0x04);
      c->channel->disconnect();
  }
  co_return;
}

asio::awaitable<void> on_10_information(shared_ptr<Client> c, uint32_t item_id) {
  if (item_id == InformationMenuItemID::GO_BACK) {
    c->clear_flag(Client::Flag::IN_INFORMATION_MENU);
    send_main_menu(c);
  } else {
    try {
      auto contents = c->require_server_state()->information_contents_for_client(c);
      send_message_box(c, contents->at(item_id));
    } catch (const out_of_range&) {
      send_message_box(c, "$C6No such information exists.");
    }
  }
  co_return;
}

asio::awaitable<void> on_10_proxy_options(shared_ptr<Client> c, uint32_t item_id) {
  switch (item_id) {
    case ProxyOptionsMenuItemID::GO_BACK:
      send_proxy_destinations_menu(c);
      co_return;
    case ProxyOptionsMenuItemID::CHAT_COMMANDS:
      if (c->can_use_chat_commands()) {
        c->toggle_flag(Client::Flag::PROXY_CHAT_COMMANDS_ENABLED);
      } else {
        c->clear_flag(Client::Flag::PROXY_CHAT_COMMANDS_ENABLED);
      }
      break;
    case ProxyOptionsMenuItemID::PLAYER_NOTIFICATIONS:
      c->toggle_flag(Client::Flag::PROXY_PLAYER_NOTIFICATIONS_ENABLED);
      break;
    case ProxyOptionsMenuItemID::DROP_NOTIFICATIONS:
      switch (c->get_drop_notification_mode()) {
        case Client::ItemDropNotificationMode::NOTHING:
          c->set_drop_notification_mode(Client::ItemDropNotificationMode::RARES_ONLY);
          break;
        case Client::ItemDropNotificationMode::RARES_ONLY:
          c->set_drop_notification_mode(Client::ItemDropNotificationMode::ALL_ITEMS);
          break;
        case Client::ItemDropNotificationMode::ALL_ITEMS:
          c->set_drop_notification_mode(Client::ItemDropNotificationMode::ALL_ITEMS_INCLUDING_MESETA);
          break;
        case Client::ItemDropNotificationMode::ALL_ITEMS_INCLUDING_MESETA:
          c->set_drop_notification_mode(Client::ItemDropNotificationMode::NOTHING);
          break;
      }
      break;
    case ProxyOptionsMenuItemID::INFINITE_HP:
      c->toggle_flag(Client::Flag::INFINITE_HP_ENABLED);
      break;
    case ProxyOptionsMenuItemID::INFINITE_TP:
      c->toggle_flag(Client::Flag::INFINITE_TP_ENABLED);
      break;
    case ProxyOptionsMenuItemID::FAST_KILLS:
      c->toggle_flag(Client::Flag::FAST_KILLS_ENABLED);
      break;
    case ProxyOptionsMenuItemID::SWITCH_ASSIST:
      c->toggle_flag(Client::Flag::SWITCH_ASSIST_ENABLED);
      break;
    case ProxyOptionsMenuItemID::EP3_INFINITE_MESETA:
      c->toggle_flag(Client::Flag::PROXY_EP3_INFINITE_MESETA_ENABLED);
      break;
    case ProxyOptionsMenuItemID::EP3_INFINITE_TIME:
      c->toggle_flag(Client::Flag::PROXY_EP3_INFINITE_TIME_ENABLED);
      break;
    case ProxyOptionsMenuItemID::EP3_UNMASK_WHISPERS:
      c->toggle_flag(Client::Flag::PROXY_EP3_UNMASK_WHISPERS);
      break;
    case ProxyOptionsMenuItemID::BLOCK_EVENTS:
      c->override_lobby_event = (c->override_lobby_event == 0xFF) ? 0x00 : 0xFF;
      break;
    case ProxyOptionsMenuItemID::BLOCK_PATCHES:
      c->toggle_flag(Client::Flag::PROXY_BLOCK_FUNCTION_CALLS);
      break;
    case ProxyOptionsMenuItemID::SAVE_FILES:
      c->toggle_flag(Client::Flag::PROXY_SAVE_FILES);
      break;
    default:
      send_message_box(c, "Incorrect menu item ID.");
      co_return;
  }
  send_menu(c, proxy_options_menu_for_client(c));
}

asio::awaitable<void> on_10_proxy_destinations(shared_ptr<Client> c, uint32_t item_id) {
  if (item_id == ProxyDestinationsMenuItemID::GO_BACK) {
    send_main_menu(c);

  } else if (item_id == ProxyDestinationsMenuItemID::OPTIONS) {
    send_menu(c, proxy_options_menu_for_client(c));

  } else {
    auto s = c->require_server_state();
    const pair<string, uint16_t>* dest = nullptr;
    try {
      dest = &s->proxy_destinations(c->version()).at(item_id);
    } catch (const out_of_range&) {
    }

    if (!dest) {
      send_message_box(c, "$C6No such destination exists.");
      c->channel->disconnect();
    } else {
      // Clear Check Tactics menu so client won't see newserv tournament state while logically on another server. There
      // is no such command on Trial Edition though, so only do this on Ep3 final.
      if (c->version() == Version::GC_EP3) {
        send_ep3_confirm_tournament_entry(c, nullptr);
      }

      co_await enable_save_if_needed(c);
      co_await start_proxy_session(c, dest->first, dest->second, false);
    }
  }
}

asio::awaitable<void> on_10_game_menu(shared_ptr<Client> c, uint32_t item_id, const std::string& password) {
  auto s = c->require_server_state();
  auto game = s->find_lobby(item_id);
  if (!game) {
    send_lobby_message_box(c, "$C7You cannot join this\ngame because it no\nlonger exists.");
    co_return;
  }
  switch (game->join_error_for_client(c, &password)) {
    case Lobby::JoinError::ALLOWED:
      if (!s->change_client_lobby(c, game)) {
        throw logic_error("client cannot join game after all preconditions satisfied");
      }
      if (game->is_game()) {
        c->set_flag(Client::Flag::LOADING);
        c->log.info_f("LOADING flag set");

        // If no one was in the game before, then there's no leader to send the game state - send it to the joining
        // player (who is now the leader)
        if (game->count_clients() == 1) {
          c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_ITEM_STATE);
          c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_ENEMY_AND_SET_STATE);
          c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_OBJECT_STATE);
          c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_FLAG_STATE);
        }
      }
      break;
    case Lobby::JoinError::FULL:
      send_lobby_message_box(c, "$C7You cannot join this\ngame because it is\nfull.");
      break;
    case Lobby::JoinError::VERSION_CONFLICT:
      send_lobby_message_box(c, "$C7You cannot join this\ngame because it is\nfor a different\nversion of PSO.");
      break;
    case Lobby::JoinError::QUEST_SELECTION_IN_PROGRESS:
      send_lobby_message_box(c, "$C7You cannot join this\ngame because the\nplayers are currently\nchoosing a quest.");
      break;
    case Lobby::JoinError::QUEST_IN_PROGRESS:
      send_lobby_message_box(c, "$C7You cannot join this\ngame because a\nquest is already\nin progress.");
      break;
    case Lobby::JoinError::BATTLE_IN_PROGRESS:
      send_lobby_message_box(c, "$C7You cannot join this\ngame because a\nbattle is already\nin progress.");
      break;
    case Lobby::JoinError::LOADING:
      send_lobby_message_box(c, "$C7You cannot join this\ngame because\nanother player is\ncurrently loading.\nTry again soon.");
      break;
    case Lobby::JoinError::SOLO:
      send_lobby_message_box(c, "$C7You cannot join this\ngame because it is\na Solo Mode game.");
      break;
    case Lobby::JoinError::INCORRECT_PASSWORD:
      send_lobby_message_box(c, "$C7Incorrect password.");
      break;
    case Lobby::JoinError::LEVEL_TOO_LOW: {
      string msg = std::format("$C7You must be level\n{} or above to\njoin this game.",
          static_cast<size_t>(game->min_level + 1));
      send_lobby_message_box(c, msg);
      break;
    }
    case Lobby::JoinError::LEVEL_TOO_HIGH: {
      string msg = std::format("$C7You must be level\n{} or below to\njoin this game.",
          static_cast<size_t>(game->max_level + 1));
      send_lobby_message_box(c, msg);
      break;
    }
    case Lobby::JoinError::NO_ACCESS_TO_QUEST:
      send_lobby_message_box(c, "$C7You don't have access\nto the quest in progress\nin this game, or there\nis no space for another\nplayer in the quest.");
      break;
    default:
      send_lobby_message_box(c, "$C7You cannot join this\ngame.");
      break;
  }
}
