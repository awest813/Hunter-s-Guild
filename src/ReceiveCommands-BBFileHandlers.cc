#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_E0_BB(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  send_system_file_bb(c);
  co_return;
}

asio::awaitable<void> on_E3_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_PlayerPreviewRequest_BB_E3>(msg.data);

  if (c->bb_connection_phase != 0x00) {
    c->unload_character(false);
    c->bb_character_index = cmd.character_index;
    c->bb_bank_character_index = cmd.character_index;
    send_approve_player_choice_bb(c);

  } else {
    if (!c->login) {
      c->channel->disconnect();
      co_return;
    }

    auto send_preview = [&c](size_t index) -> void {
      c->unload_character(false);
      c->bb_character_index = index;
      c->bb_bank_character_index = index;
      try {
        auto preview = c->character_file()->to_preview();
        send_player_preview_bb(c, c->bb_character_index, &preview);
      } catch (const exception& e) {
        // Player doesn't exist
        c->log.info_f("Can\'t load character data: {}", e.what());
        send_player_preview_bb(c, c->bb_character_index, nullptr);
      }
      c->unload_character(false);
    };

    if (msg.flag == 0) {
      if (cmd.character_index < 0 || cmd.character_index >= 0x80) {
        throw runtime_error("client requested invalid character slot");
      }
      send_preview(cmd.character_index);
    } else if (cmd.character_index == 0) {
      if (msg.flag >= 0x80) {
        throw runtime_error("client requested too many character slots");
      }
      for (size_t z = 0; z < msg.flag; z++) {
        send_preview(z);
      }
    }
  }
}

asio::awaitable<void> on_E8_BB(shared_ptr<Client> c, Channel::Message& msg) {
  constexpr size_t max_count = sizeof(PSOBBGuildCardFile::entries) / sizeof(PSOBBGuildCardFile::Entry);
  constexpr size_t max_blocked = sizeof(PSOBBGuildCardFile::blocked) / sizeof(GuildCardBB);
  auto gcf = c->guild_card_file();
  bool should_save = false;
  switch (msg.command) {
    case 0x01E8: { // Check guild card file checksum
      const auto& cmd = check_size_t<C_GuildCardChecksum_01E8>(msg.data);
      uint32_t checksum = gcf->checksum();
      c->log.info_f("(Guild card file) Server checksum = {:08X}, client checksum = {:08X}", checksum, cmd.checksum);
      S_GuildCardChecksumResponse_BB_02E8 response = {(cmd.checksum != checksum), 0};
      send_command_t(c, 0x02E8, 0x00000000, response);
      break;
    }
    case 0x03E8: // Download guild card file
      check_size_v(msg.data.size(), 0);
      send_guild_card_header_bb(c);
      break;
    case 0x04E8: { // Add or replace guild card
      auto& new_gc = check_size_t<GuildCardBB>(msg.data);
      for (size_t z = 0; z < max_count; z++) {
        auto& gcf_entry = gcf->entries[z];
        if (!gcf_entry.data.present || (gcf_entry.data.guild_card_number == new_gc.guild_card_number)) {
          gcf_entry.data = new_gc;
          gcf_entry.unknown_a1.clear(0);
          c->log.info_f("Added or replaced guild card {} at position {}", new_gc.guild_card_number, z);
          should_save = true;
          break;
        }
      }
      break;
    }
    case 0x05E8: { // Delete guild card
      auto& cmd = check_size_t<C_DeleteGuildCard_BB_05E8_08E8>(msg.data);
      for (size_t z = 0; z < max_count; z++) {
        auto& gcf_entry = gcf->entries[z];
        if (gcf_entry.data.guild_card_number == cmd.guild_card_number) {
          c->log.info_f("Deleted guild card {} at position {}", cmd.guild_card_number, z);
          for (z = 0; z < max_count - 1; z++) {
            gcf_entry = gcf->entries[z + 1];
          }
          gcf->entries[max_count - 1].clear();
          should_save = true;
          break;
        }
      }
      break;
    }
    case 0x06E8: { // Update guild card
      auto& new_gc = check_size_t<GuildCardBB>(msg.data);
      for (size_t z = 0; z < max_count; z++) {
        auto& gcf_entry = gcf->entries[z];
        if (gcf_entry.data.guild_card_number == new_gc.guild_card_number) {
          gcf_entry.data = new_gc;
          c->log.info_f("Updated guild card {} at position {}", new_gc.guild_card_number, z);
          should_save = true;
        }
      }
      if (c->login && new_gc.guild_card_number == c->login->account->account_id) {
        c->character_file(true, false)->guild_card.description = new_gc.description;
        c->log.info_f("Updated character's guild card");
      }
      break;
    }
    case 0x07E8: { // Add blocked user
      auto& new_gc = check_size_t<GuildCardBB>(msg.data);
      for (size_t z = 0; z < max_blocked; z++) {
        auto& gcf_blocked = gcf->blocked[z];
        if (!gcf_blocked.present) {
          gcf_blocked = new_gc;
          c->log.info_f("Added blocked guild card {} at position {}", new_gc.guild_card_number, z);
          should_save = true;
          break;
        }
      }
      break;
    }
    case 0x08E8: { // Delete blocked user
      auto& cmd = check_size_t<C_DeleteGuildCard_BB_05E8_08E8>(msg.data);
      for (size_t z = 0; z < max_blocked; z++) {
        auto& gcf_blocked = gcf->blocked[z];
        if (gcf_blocked.guild_card_number == cmd.guild_card_number) {
          c->log.info_f("Deleted blocked guild card {} at position {}", cmd.guild_card_number, z);
          for (z = 0; z < max_blocked - 1; z++) {
            gcf_blocked = gcf->blocked[z + 1];
          }
          gcf->blocked[max_blocked - 1].clear();
          should_save = true;
          break;
        }
      }
      break;
    }
    case 0x09E8: { // Write comment
      auto& cmd = check_size_t<C_WriteGuildCardComment_BB_09E8>(msg.data);
      for (size_t z = 0; z < max_count; z++) {
        auto& gcf_entry = gcf->entries[z];
        if (gcf_entry.data.guild_card_number == cmd.guild_card_number) {
          gcf_entry.comment = cmd.comment;
          c->log.info_f("Updated comment on guild card {} at position {}", cmd.guild_card_number, z);
          should_save = true;
          break;
        }
      }
      break;
    }
    case 0x0AE8: { // Swap guild card positions in list
      auto& cmd = check_size_t<C_SwapGuildCardPositions_BB_0AE8>(msg.data);
      size_t index1 = max_count;
      size_t index2 = max_count;
      for (size_t z = 0; z < max_count; z++) {
        auto& gcf_entry = gcf->entries[z];
        if (gcf_entry.data.guild_card_number == cmd.guild_card_number1) {
          if (index1 >= max_count) {
            index1 = z;
          } else {
            throw runtime_error("guild card 1 appears multiple times in file");
          }
        }
        if (gcf_entry.data.guild_card_number == cmd.guild_card_number2) {
          if (index2 >= max_count) {
            index2 = z;
          } else {
            throw runtime_error("guild card 2 appears multiple times in file");
          }
        }
      }
      if ((index1 >= max_count) || (index2 >= max_count)) {
        throw runtime_error("player does not have both requested guild cards");
      }

      if (index1 != index2) {
        PSOBBGuildCardFile::Entry displaced_entry = gcf->entries[index1];
        gcf->entries[index1] = gcf->entries[index2];
        gcf->entries[index2] = displaced_entry;
        c->log.info_f("Swapped positions of guild cards {} and {}", cmd.guild_card_number1, cmd.guild_card_number2);
        should_save = true;
      }
      break;
    }
    default:
      throw invalid_argument("invalid command");
  }
  if (should_save) {
    c->save_guild_card_file();
  }
  co_return;
}

asio::awaitable<void> on_DC_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_GuildCardDataRequest_BB_03DC>(msg.data);
  if (cmd.cont) {
    send_guild_card_chunk_bb(c, cmd.chunk_index);
  }
  co_return;
}

asio::awaitable<void> on_EB_BB(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  if (msg.command == 0x04EB) {
    send_stream_file_index_bb(c);
  } else if (msg.command == 0x03EB) {
    send_stream_file_chunk_bb(c, msg.flag);
  } else {
    throw invalid_argument("unimplemented command");
  }
  co_return;
}

asio::awaitable<void> on_EC_BB(shared_ptr<Client> c, Channel::Message& msg) {
  (void)c;
  msg.check_size_t<C_LeaveCharacterSelect_BB_00EC>();
  co_return;
}
