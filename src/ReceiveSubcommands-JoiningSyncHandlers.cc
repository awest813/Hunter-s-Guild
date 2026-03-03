#include "ReceiveSubcommands-Impl.hh"

#include <phosg/Strings.hh>

#include "Compression.hh"
#include "SendCommands.hh"

using namespace std;

shared_ptr<Client> get_sync_target(
    shared_ptr<Client> sender_c, uint8_t command, uint8_t flag, bool allow_if_not_loading) {
  if (!command_is_private(command)) {
    throw runtime_error("sync data sent via public command");
  }
  auto l = sender_c->require_lobby();
  if (l->is_game() && (allow_if_not_loading || l->any_client_loading()) && (flag < l->max_clients)) {
    return l->clients[flag];
  }
  return nullptr;
}

asio::awaitable<void> on_sync_joining_player_compressed_state(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto target = get_sync_target(c, msg.command, msg.flag, false); // Checks l->is_game
  if (!target) {
    co_return;
  }

  uint8_t orig_subcommand_number;
  size_t decompressed_size;
  size_t compressed_size;
  const void* compressed_data;
  if (is_pre_v1(c->version())) {
    const auto& cmd = msg.check_size_t<G_SyncGameStateHeader_DCNTE_6x6B_6x6C_6x6D_6x6E>(0xFFFF);
    orig_subcommand_number = cmd.header.basic_header.subcommand;
    decompressed_size = cmd.decompressed_size;
    compressed_size = msg.size - sizeof(cmd);
    compressed_data = reinterpret_cast<const char*>(msg.data) + sizeof(cmd);
  } else {
    const auto& cmd = msg.check_size_t<G_SyncGameStateHeader_6x6B_6x6C_6x6D_6x6E>(0xFFFF);
    if (cmd.compressed_size > msg.size - sizeof(cmd)) {
      throw runtime_error("compressed end offset is beyond end of command");
    }
    orig_subcommand_number = cmd.header.basic_header.subcommand;
    decompressed_size = cmd.decompressed_size;
    compressed_size = cmd.compressed_size;
    compressed_data = reinterpret_cast<const char*>(msg.data) + sizeof(cmd);
  }

  const auto* subcommand_def = def_for_subcommand(c->version(), orig_subcommand_number);
  if (!subcommand_def) {
    throw runtime_error("unknown sync subcommand");
  }

  string decompressed = bc0_decompress(compressed_data, compressed_size);
  if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
    c->log.info_f("Decompressed sync data ({:X} -> {:X} bytes; expected {:X}):",
        compressed_size, decompressed.size(), decompressed_size);
    phosg::print_data(stderr, decompressed);
  }

  // Assume all v1 and v2 versions are the same, and assume GC/XB are the same.
  // TODO: We should do this by checking if the supermaps are the same instead of hardcoding this here.
  auto collapse_version = +[](Version v) -> Version {
    // Collapse DC v1/v2 and PC into PC_V2
    if (is_v1_or_v2(v) && !is_pre_v1(v) && (v != Version::GC_NTE)) {
      return Version::PC_V2;
    }
    // Collapse GC and XB into GC_V3
    if (is_v3(v)) {
      return Version::GC_V3;
    }
    // All other versions can't be collapsed
    return v;
  };
  bool skip_recompress = collapse_version(c->version()) == collapse_version(target->version());

  switch (subcommand_def->final_subcommand) {
    case 0x6B: {
      auto l = c->require_lobby();
      l->map_state->import_enemy_states_from_sync(
          c->version(),
          reinterpret_cast<const SyncEnemyStateEntry*>(decompressed.data()),
          decompressed.size() / sizeof(SyncEnemyStateEntry));
      if (skip_recompress) {
        send_game_join_sync_command_compressed(
            target,
            compressed_data,
            compressed_size,
            decompressed_size,
            subcommand_def->nte_subcommand,
            subcommand_def->proto_subcommand,
            subcommand_def->final_subcommand);
      } else {
        send_game_enemy_state(target);
      }
      break;
    }

    case 0x6C: {
      auto l = c->require_lobby();
      l->map_state->import_object_states_from_sync(
          c->version(),
          reinterpret_cast<const SyncObjectStateEntry*>(decompressed.data()),
          decompressed.size() / sizeof(SyncObjectStateEntry));
      if (skip_recompress) {
        send_game_join_sync_command_compressed(
            target,
            compressed_data,
            compressed_size,
            decompressed_size,
            subcommand_def->nte_subcommand,
            subcommand_def->proto_subcommand,
            subcommand_def->final_subcommand);
      } else {
        send_game_object_state(target);
      }
      break;
    }

    case 0x6D: {
      if (decompressed.size() < sizeof(G_SyncItemState_6x6D_Decompressed)) {
        throw runtime_error(std::format(
            "decompressed 6x6D data (0x{:X} bytes) is too short for header (0x{:X} bytes)",
            decompressed.size(), sizeof(G_SyncItemState_6x6D_Decompressed)));
      }
      auto* decompressed_cmd = reinterpret_cast<G_SyncItemState_6x6D_Decompressed*>(decompressed.data());

      size_t num_floor_items = 0;
      for (size_t z = 0; z < decompressed_cmd->floor_item_count_per_floor.size(); z++) {
        num_floor_items += decompressed_cmd->floor_item_count_per_floor[z];
      }

      size_t required_size = sizeof(G_SyncItemState_6x6D_Decompressed) + num_floor_items * sizeof(FloorItem);
      if (decompressed.size() < required_size) {
        throw runtime_error(std::format(
            "decompressed 6x6D data (0x{:X} bytes) is too short for all floor items (0x{:X} bytes)",
            decompressed.size(), required_size));
      }

      auto l = c->require_lobby();
      size_t target_num_items = target->character_file()->inventory.num_items;
      for (size_t z = 0; z < 12; z++) {
        uint32_t client_next_id = decompressed_cmd->next_item_id_per_player[z];
        uint32_t server_next_id = l->next_item_id_for_client[z];
        if (client_next_id == server_next_id) {
          l->log.info_f("Next item ID for player {} ({:08X}) matches expected value", z, l->next_item_id_for_client[z]);
        } else if ((z == target->lobby_client_id) && (client_next_id == server_next_id - target_num_items)) {
          l->log.info_f("Next item ID for player {} ({:08X}) matches expected value before inventory item ID assignment ({:08X})", z, l->next_item_id_for_client[z], static_cast<uint32_t>(server_next_id - target_num_items));
        } else {
          l->log.warning_f("Next item ID for player {} ({:08X}) does not match expected value ({:08X})",
              z, decompressed_cmd->next_item_id_per_player[z], l->next_item_id_for_client[z]);
        }
      }

      // The leader's item state is never forwarded since the leader may be able to see items that the joining player
      // should not see. We always generate a new item state for the joining player instead.
      send_game_item_state(target);
      break;
    }
    case 0x6E: {
      phosg::StringReader r(decompressed);
      const auto& dec_header = r.get<G_SyncSetFlagState_6x6E_Decompressed>();
      if (dec_header.total_size != dec_header.entity_set_flags_size + dec_header.event_set_flags_size + dec_header.switch_flags_size) {
        throw runtime_error("incorrect size fields in 6x6E header");
      }

      auto l = c->require_lobby();
      phosg::StringReader set_flags_r = r.sub(r.where(), dec_header.entity_set_flags_size);
      const auto& set_flags_header = set_flags_r.get<G_SyncSetFlagState_6x6E_Decompressed::EntitySetFlags>();

      if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
        c->log.info_f("Set flags data:");
        phosg::print_data(stderr, r.getv(dec_header.entity_set_flags_size, false), dec_header.entity_set_flags_size);
      }

      const auto* object_set_flags = &set_flags_r.get<le_uint16_t>(
          true, set_flags_header.num_object_sets * sizeof(le_uint16_t));
      const auto* enemy_set_flags = &set_flags_r.get<le_uint16_t>(
          true, set_flags_header.num_enemy_sets * sizeof(le_uint16_t));
      size_t event_set_flags_count = dec_header.event_set_flags_size / sizeof(le_uint16_t);
      const auto* event_set_flags = &r.pget<le_uint16_t>(
          r.where() + dec_header.entity_set_flags_size, event_set_flags_count * sizeof(le_uint16_t));
      l->map_state->import_flag_states_from_sync(
          c->version(),
          object_set_flags,
          set_flags_header.num_object_sets,
          enemy_set_flags,
          set_flags_header.num_enemy_sets,
          event_set_flags,
          event_set_flags_count);

      size_t expected_switch_flag_num_floors = is_v1(c->version()) ? 0x10 : 0x12;
      size_t expected_switch_flags_size = expected_switch_flag_num_floors * 0x20;
      if (dec_header.switch_flags_size != expected_switch_flags_size) {
        l->log.warning_f("Switch flags size (0x{:X}) does not match expected size (0x{:X})",
            dec_header.switch_flags_size, expected_switch_flags_size);
      } else {
        l->log.info_f("Switch flags size matches expected size (0x{:X})", expected_switch_flags_size);
      }
      if (l->switch_flags) {
        phosg::StringReader switch_flags_r = r.sub(r.where() + dec_header.entity_set_flags_size + dec_header.event_set_flags_size);
        for (size_t floor = 0; floor < expected_switch_flag_num_floors; floor++) {
          // There is a bug in most (perhaps all) versions of the game, which causes this array to be too small. It
          // looks like Sega forgot to account for the header (G_SyncSetFlagState_6x6E_Decompressed) before compressing
          // the buffer, so the game cuts off the last 8 bytes of the switch flags. Since this only affects the last
          // floor, which rarely has any switches on it (or is even accessible by the player), it's not surprising that
          // no one noticed this. But it does mean we have to check switch_flags_r.eof() here.
          for (size_t z = 0; (z < 0x20) && !switch_flags_r.eof(); z++) {
            uint8_t& l_flags = l->switch_flags->array(floor).data[z];
            uint8_t r_flags = switch_flags_r.get_u8();
            if (l_flags != r_flags) {
              l->log.warning_f(
                  "Switch flags do not match at floor {:02X} byte {:02X} (expected {:02X}, received {:02X})",
                  floor, z, l_flags, r_flags);
              l_flags = r_flags;
            }
          }
        }
      }

      if (skip_recompress) {
        send_game_join_sync_command_compressed(
            target,
            compressed_data,
            compressed_size,
            decompressed_size,
            subcommand_def->nte_subcommand,
            subcommand_def->proto_subcommand,
            subcommand_def->final_subcommand);
      } else {
        send_game_set_state(target);
      }
      break;
    }

    default:
      throw logic_error("invalid compressed sync state subcommand");
  }
}

template <typename CmdT>
static asio::awaitable<void> on_sync_joining_player_quest_flags_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<CmdT>();

  if (!command_is_private(msg.command)) {
    co_return;
  }

  auto l = c->require_lobby();
  if (l->is_game() && l->any_client_loading() && (l->leader_id == c->lobby_client_id)) {
    l->quest_flags_known = nullptr; // All quest flags are now known
    l->quest_flag_values = make_unique<QuestFlags>(cmd.quest_flags);
    auto target = l->clients.at(msg.flag);
    if (target) {
      send_game_flag_state(target);
    }
  }
}

asio::awaitable<void> on_sync_joining_player_quest_flags(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (is_v1(c->version())) {
    co_await on_sync_joining_player_quest_flags_t<G_SetQuestFlags_DCv1_6x6F>(c, msg);
  } else if (!is_v4(c->version())) {
    co_await on_sync_joining_player_quest_flags_t<G_SetQuestFlags_V2_V3_6x6F>(c, msg);
  } else {
    co_await on_sync_joining_player_quest_flags_t<G_SetQuestFlags_BB_6x6F>(c, msg);
  }
}
