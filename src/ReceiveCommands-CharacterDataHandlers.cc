#include "ReceiveCommands.hh"

#include <phosg/Strings.hh>

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

asio::awaitable<void> on_61_98(shared_ptr<Client> c, Channel::Message& msg) {
  auto s = c->require_server_state();

  // 98 should only be sent when leaving a game, and we should leave the client in no lobby (they will send an 84 soon
  // afterward to choose a lobby).
  if (msg.command == 0x98) {
    // Clear all temporary state from the game
    c->delete_overlay();
    c->telepipe_lobby_id = 0;
    s->remove_client_from_lobby(c);
    c->clear_flag(Client::Flag::LOADING);
    c->clear_flag(Client::Flag::LOADING_QUEST);
    c->clear_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST);
    c->clear_flag(Client::Flag::LOADING_TOURNAMENT);
    c->clear_flag(Client::Flag::AT_BANK_COUNTER);
    c->clear_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_ITEM_STATE);
    c->clear_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_ENEMY_AND_SET_STATE);
    c->clear_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_OBJECT_STATE);
    c->clear_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_FLAG_STATE);
    c->clear_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_PLAYER_STATES);
  }

  auto player = c->character_file();

  switch (c->version()) {
    case Version::DC_NTE:
    case Version::DC_11_2000:
    case Version::DC_V1: {
      const auto& cmd = check_size_t<C_CharacterData_DCv1_61_98>(msg.data);
      c->v1_v2_last_reported_disp = make_unique<PlayerDispDataDCPCV3>(cmd.disp);
      player->inventory = cmd.inventory;
      player->disp = cmd.disp.to_bb(player->inventory.language, player->inventory.language);
      c->login->account->last_player_name = player->disp.name.decode(player->inventory.language);
      break;
    }
    case Version::DC_V2: {
      const auto& cmd = check_size_t<C_CharacterData_DCv2_61_98>(msg.data, 0xFFFF);
      c->v1_v2_last_reported_disp = make_unique<PlayerDispDataDCPCV3>(cmd.disp);
      player->inventory = cmd.inventory;
      player->disp = cmd.disp.to_bb(player->inventory.language, player->inventory.language);
      player->battle_records = cmd.records.battle;
      player->challenge_records = cmd.records.challenge;
      player->choice_search_config = cmd.choice_search_config;
      c->login->account->last_player_name = player->disp.name.decode(player->inventory.language);
      break;
    }
    case Version::PC_NTE:
    case Version::PC_V2: {
      const auto& cmd = check_size_t<C_CharacterData_PC_61_98>(msg.data, 0xFFFF);
      c->v1_v2_last_reported_disp = make_unique<PlayerDispDataDCPCV3>(cmd.disp);
      player->inventory = cmd.inventory;
      player->disp = cmd.disp.to_bb(player->inventory.language, player->inventory.language);
      player->battle_records = cmd.records.battle;
      player->challenge_records = cmd.records.challenge;
      player->choice_search_config = cmd.choice_search_config;
      c->import_blocked_senders(cmd.blocked_senders);
      if (cmd.auto_reply_enabled) {
        string auto_reply = msg.data.substr(sizeof(cmd));
        phosg::strip_trailing_zeroes(auto_reply);
        if (auto_reply.size() & 1) {
          auto_reply.push_back(0);
        }
        try {
          player->auto_reply.encode(tt_utf16_to_utf8(auto_reply), player->inventory.language);
        } catch (const runtime_error& e) {
          c->log.warning_f("Failed to decode auto-reply message: {}", e.what());
        }
        c->login->account->auto_reply_message = auto_reply;
      } else {
        player->auto_reply.clear();
        c->login->account->auto_reply_message.clear();
      }
      c->login->account->last_player_name = player->disp.name.decode(player->inventory.language);
      break;
    }
    case Version::GC_NTE: {
      const auto& cmd = check_size_t<C_CharacterData_GCNTE_61_98>(msg.data, 0xFFFF);
      c->v1_v2_last_reported_disp = make_unique<PlayerDispDataDCPCV3>(cmd.disp);

      auto s = c->require_server_state();
      player->inventory = cmd.inventory;
      player->disp = cmd.disp.to_bb(player->inventory.language, player->inventory.language);
      player->battle_records = cmd.records.battle;
      player->challenge_records = cmd.records.challenge;
      player->choice_search_config = cmd.choice_search_config;
      c->import_blocked_senders(cmd.blocked_senders);
      if (cmd.auto_reply_enabled) {
        string auto_reply = msg.data.substr(sizeof(cmd), 0xAC);
        phosg::strip_trailing_zeroes(auto_reply);
        try {
          string encoded = tt_decode_marked(auto_reply, player->inventory.language, false);
          player->auto_reply.encode(encoded, player->inventory.language);
        } catch (const runtime_error& e) {
          c->log.warning_f("Failed to decode auto-reply message: {}", e.what());
        }
        c->login->account->auto_reply_message = auto_reply;
      } else {
        player->auto_reply.clear();
        c->login->account->auto_reply_message.clear();
      }
      c->login->account->last_player_name = player->disp.name.decode(player->inventory.language);
      break;
    }

    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
    case Version::XB_V3: {
      const C_CharacterData_V3_61_98* cmd;
      if (msg.flag == 4) { // Episode 3
        if (!is_ep3(c->version())) {
          throw runtime_error("non-Episode 3 client sent Episode 3 player data");
        }
        const auto* cmd3 = &check_size_t<C_CharacterData_Ep3_61_98>(msg.data);
        c->ep3_config = make_shared<Episode3::PlayerConfig>(cmd3->ep3_config);
        c->ep3_config->decrypt();
        if (c->ep3_config->card_count_checksums_correct()) {
          c->log.info_f("Card count checksums are correct");
        } else {
          c->log.info_f("Card count checksums are incorrect");
        }
        cmd = reinterpret_cast<const C_CharacterData_V3_61_98*>(cmd3);
        if (specific_version_is_indeterminate(c->specific_version)) {
          c->specific_version = SPECIFIC_VERSION_GC_EP3_JP; // 3SJ0
        }
      } else {
        if (is_ep3(c->version())) {
          c->channel->version = Version::GC_EP3_NTE;
          c->log.info_f("Game version changed to GC_EP3_NTE");
          c->clear_flag(Client::Flag::ENCRYPTED_SEND_FUNCTION_CALL);
          if (specific_version_is_indeterminate(c->specific_version)) {
            c->specific_version = SPECIFIC_VERSION_GC_EP3_NTE;
          }
          c->convert_account_to_temporary_if_nte();
        }
        cmd = &check_size_t<C_CharacterData_V3_61_98>(msg.data, 0xFFFF);
      }

      player->inventory = cmd->inventory;
      player->disp = cmd->disp.to_bb(player->inventory.language, player->inventory.language);
      player->battle_records = cmd->records.battle;
      player->challenge_records = cmd->records.challenge;
      player->choice_search_config = cmd->choice_search_config;
      player->info_board.encode(cmd->info_board.decode(player->inventory.language), player->inventory.language);
      c->import_blocked_senders(cmd->blocked_senders);
      if (cmd->auto_reply_enabled) {
        string auto_reply = msg.data.substr(sizeof(cmd), 0xAC);
        phosg::strip_trailing_zeroes(auto_reply);
        try {
          string encoded = tt_decode_marked(auto_reply, player->inventory.language, false);
          player->auto_reply.encode(encoded, player->inventory.language);
        } catch (const runtime_error& e) {
          c->log.warning_f("Failed to decode auto-reply message: {}", e.what());
        }
        c->login->account->auto_reply_message = auto_reply;
      } else {
        player->auto_reply.clear();
        c->login->account->auto_reply_message.clear();
      }
      c->login->account->last_player_name = player->disp.name.decode(player->inventory.language);
      break;
    }
    case Version::BB_V4: {
      const auto& cmd = check_size_t<C_CharacterData_BB_61_98>(msg.data, 0xFFFF);
      // Note: we don't copy the inventory and disp here because we already have them (we sent the player data to the
      // client in the first place)
      player->battle_records = cmd.records.battle;
      player->challenge_records = cmd.records.challenge;
      player->choice_search_config = cmd.choice_search_config;
      player->info_board = cmd.info_board;
      c->import_blocked_senders(cmd.blocked_senders);
      if (cmd.auto_reply_enabled) {
        string auto_reply = msg.data.substr(sizeof(cmd), 0xAC);
        phosg::strip_trailing_zeroes(auto_reply);
        if (auto_reply.size() & 1) {
          auto_reply.push_back(0);
        }
        try {
          player->auto_reply.encode(tt_utf16_to_utf8(auto_reply), player->inventory.language);
        } catch (const runtime_error& e) {
          c->log.warning_f("Failed to decode auto-reply message: {}", e.what());
        }
        c->login->account->auto_reply_message = auto_reply;
      } else {
        player->auto_reply.clear();
        c->login->account->auto_reply_message.clear();
      }
      c->login->account->last_player_name = player->disp.name.decode(player->inventory.language);
      break;
    }
    default:
      throw logic_error("player data command not implemented for version");
  }
  player->inventory.decode_from_client(c->version());
  c->channel->language = player->inventory.language;
  c->login->account->save();

  c->update_channel_name();

  // If the player is BB and has just left a game, sync their save file to the client to make sure it's up to date
  if ((c->version() == Version::BB_V4) && (msg.command == 0x98)) {
    send_complete_player_bb(c);
  }

  if (c->character_data_ready_promise) {
    c->character_data_ready_promise->set_value(GetPlayerInfoResult{
        .character = player,
        .ep3_character = nullptr,
        .is_full_info = false,
    });
    c->character_data_ready_promise.reset();
  }
  co_return;
}

asio::awaitable<void> on_30(shared_ptr<Client> c, Channel::Message& msg) {
  if (!c->character_data_ready_promise) {
    co_return;
  }
  if (!c->channel->connected()) {
    throw runtime_error("Client has already disconnected");
  }

  shared_ptr<PSOBBCharacterFile> ch;
  shared_ptr<PSOGCEp3CharacterFile::Character> ep3_ch;
  if (is_ep3(c->version())) {
    ep3_ch = (c->version() == Version::GC_EP3_NTE)
        ? make_shared<PSOGCEp3CharacterFile::Character>(msg.check_size_t<PSOGCEp3NTECharacter>())
        : make_shared<PSOGCEp3CharacterFile::Character>(msg.check_size_t<PSOGCEp3CharacterFile::Character>());

  } else {
    switch (c->version()) {
      case Version::DC_V2:
        ch = PSOBBCharacterFile::create_from_file(msg.check_size_t<PSODCV2CharacterFile::Character>());
        ch->inventory.decode_from_client(c->version());
        break;
      case Version::GC_NTE:
        ch = PSOBBCharacterFile::create_from_file(msg.check_size_t<PSOGCNTECharacterFileCharacter>());
        ch->inventory.decode_from_client(c->version());
        break;
      case Version::GC_V3:
        ch = PSOBBCharacterFile::create_from_file(msg.check_size_t<PSOGCCharacterFile::Character>());
        // Note: We don't call ch->inventory.decode_from_client here because the data is sent in the game's native byte
        // order, which is already correct on GC (unlike for 61/98)
        break;
      case Version::XB_V3:
        ch = PSOBBCharacterFile::create_from_file(msg.check_size_t<PSOXBCharacterFile::Character>());
        ch->inventory.decode_from_client(c->version());
        break;
      case Version::GC_EP3_NTE:
      case Version::GC_EP3:
        throw logic_error("Episode 3 case not handled correctly");
      case Version::DC_NTE:
      case Version::DC_11_2000:
      case Version::DC_V1:
      case Version::PC_NTE:
      case Version::PC_V2:
      case Version::BB_V4:
      default:
        throw logic_error("extended player data command not implemented for version");
    }
    ch->disp.visual.version = 4;
    ch->disp.visual.name_color_checksum = 0x00000000;
  }

  c->character_data_ready_promise->set_value(GetPlayerInfoResult{
      .character = std::move(ch),
      .ep3_character = std::move(ep3_ch),
      .is_full_info = true,
  });
  c->character_data_ready_promise.reset();
}
