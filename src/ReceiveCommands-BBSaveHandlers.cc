#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_E5_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<SC_PlayerPreview_CreateCharacter_BB_00E5>(msg.data);

  if (!c->login) {
    send_message_box(c, "$C6You are not logged in.");
    co_return;
  }

  if (c->character_file(false).get()) {
    throw runtime_error("player already exists");
  }

  c->bb_character_index = -1;
  c->bb_bank_character_index = -1;
  c->system_file(); // Ensure system file is loaded
  c->bb_character_index = cmd.character_index;
  c->bb_bank_character_index = cmd.character_index;

  bool should_send_approve = true;
  if (c->bb_connection_phase == 0x03) { // Dressing room
    try {
      c->character_file()->disp.apply_dressing_room(cmd.preview);
    } catch (const exception& e) {
      send_message_box(c, std::format("$C6Character could not be modified:\n{}", e.what()));
      should_send_approve = false;
    }
  } else {
    try {
      auto s = c->require_server_state();
      c->create_character_file(c->login->account->account_id, c->language(), cmd.preview, s->level_table(c->version()));
    } catch (const exception& e) {
      send_message_box(c, std::format("$C6New character could not be created:\n{}", e.what()));
      should_send_approve = false;
    }
  }

  if (should_send_approve) {
    send_approve_player_choice_bb(c);
  }
}

asio::awaitable<void> on_ED_BB(shared_ptr<Client> c, Channel::Message& msg) {
  auto sys = c->system_file();
  switch (msg.command) {
    case 0x01ED: {
      const auto& cmd = check_size_t<C_UpdateOptionFlags_BB_01ED>(msg.data);
      c->character_file(true, false)->option_flags = cmd.option_flags;
      break;
    }
    case 0x02ED: {
      const auto& cmd = check_size_t<C_UpdateSymbolChats_BB_02ED>(msg.data);
      c->character_file(true, false)->symbol_chats = cmd.symbol_chats;
      break;
    }
    case 0x03ED: {
      const auto& cmd = check_size_t<C_UpdateChatShortcuts_BB_03ED>(msg.data);
      c->character_file(true, false)->shortcuts = cmd.chat_shortcuts;
      break;
    }
    case 0x04ED: {
      const auto& cmd = check_size_t<C_UpdateKeyConfig_BB_04ED>(msg.data);
      sys->key_config = cmd.key_config;
      c->save_system_file();
      break;
    }
    case 0x05ED: {
      const auto& cmd = check_size_t<C_UpdatePadConfig_BB_05ED>(msg.data);
      sys->joystick_config = cmd.pad_config;
      c->save_system_file();
      break;
    }
    case 0x06ED: {
      const auto& cmd = check_size_t<C_UpdateTechMenu_BB_06ED>(msg.data);
      c->character_file(true, false)->tech_menu_shortcut_entries = cmd.tech_menu;
      break;
    }
    case 0x07ED: {
      const auto& cmd = check_size_t<C_UpdateCustomizeMenu_BB_07ED>(msg.data);
      c->character_file()->disp.config = cmd.customize;
      break;
    }
    case 0x08ED: {
      const auto& cmd = check_size_t<C_UpdateChallengeRecords_BB_08ED>(msg.data);
      c->character_file(true, false)->challenge_records = cmd.records;
      break;
    }
    default:
      throw invalid_argument("unknown account command");
  }
  co_return;
}

asio::awaitable<void> on_E7_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<SC_SyncSaveFiles_BB_E7>(msg.data);

  // TODO: In the future, we shouldn't need to trust any of the client's data here. We should instead verify our copy
  // of the player against what the client sent, and alert on anything that's out of sync.
  auto p = c->character_file();
  p->challenge_records = cmd.char_file.challenge_records;
  p->battle_records = cmd.char_file.battle_records;
  p->death_count = cmd.char_file.death_count;
  *c->system_file() = cmd.system_file;
  co_return;
}

asio::awaitable<void> on_E2_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<S_SyncSystemFile_BB_E2>(msg.data);
  *c->system_file() = cmd.system_file;
  c->save_system_file();

  S_SystemFileCreated_00E1_BB out_cmd = {1};
  send_command_t(c, 0x00E1, 0x00000000, out_cmd);
  co_return;
}
