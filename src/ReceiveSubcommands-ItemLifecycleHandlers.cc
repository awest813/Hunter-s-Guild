#include "ReceiveSubcommands-Impl.hh"

#include "GameServer.hh"
#include "HTTPServer.hh"
#include "Items.hh"
#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

uint8_t translate_subcommand_number(Version to_version, Version from_version, uint8_t subcommand);

template <typename CmdT>
static void forward_subcommand_with_item_transcode_t(shared_ptr<Client> c, uint8_t command, uint8_t flag, const CmdT& cmd) {
  // I'm lazy and this should never happen for item commands (since all players need to stay in sync)
  if (command_is_private(command)) {
    throw runtime_error("item subcommand sent via private command");
  }

  auto l = c->require_lobby();
  auto s = c->require_server_state();
  for (auto& lc : l->clients) {
    if (!lc || lc == c) {
      continue;
    }
    if (c->version() != lc->version()) {
      CmdT out_cmd = cmd;
      out_cmd.header.subcommand = translate_subcommand_number(lc->version(), c->version(), out_cmd.header.subcommand);
      if (out_cmd.header.subcommand) {
        out_cmd.item_data.decode_for_version(c->version());
        out_cmd.item_data.encode_for_version(lc->version(), s->item_parameter_table_for_encode(lc->version()));
        send_command_t(lc, command, flag, out_cmd);
      } else {
        lc->log.info_f("Subcommand cannot be translated to client\'s version");
      }
    } else {
      send_command_t(lc, command, flag, cmd);
    }
  }
}

asio::awaitable<void> on_player_drop_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_DropItem_6x2A>();

  if ((cmd.header.client_id != c->lobby_client_id)) {
    co_return;
  }

  auto s = c->require_server_state();
  auto l = c->require_lobby();
  auto p = c->character_file();
  auto item = p->remove_item(cmd.item_id, 0, *s->item_stack_limits(c->version()));
  l->add_item(cmd.floor, item, cmd.pos, nullptr, nullptr, 0x00F);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto s = c->require_server_state();
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} dropped item {:08X} ({}) at {}:({:g}, {:g})",
        cmd.header.client_id, cmd.item_id, name, cmd.floor, cmd.pos.x, cmd.pos.z);
    c->print_inventory();
  }

  forward_subcommand(c, msg);
}

template <typename CmdT>
static asio::awaitable<void> on_create_inventory_item_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<CmdT>();

  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  ItemData item = cmd.item_data;
  item.decode_for_version(c->version());
  l->on_item_id_generated_externally(item.id);

  // Players cannot send this on behalf of another player, but they can send it on behalf of an NPC; we don't track
  // items for NPCs so in that case we just mark the item ID as used and ignore it. This works for the most part,
  // because when NPCs use or equip items, we ignore the command since it has the wrong client ID.
  // TODO: This won't work if NPCs ever drop items that players can interact with. Presumably we would have to track
  // all NPCs' inventory items to handle that.
  auto s = c->require_server_state();
  if (cmd.header.client_id != c->lobby_client_id) {
    // Don't allow creating items in other players' inventories, only in NPCs'
    if (l->clients.at(cmd.header.client_id)) {
      co_return;
    }

    if (l->log.should_log(phosg::LogLevel::L_INFO)) {
      auto name = s->describe_item(c->version(), item);
      l->log.info_f("Player {} created inventory item {:08X} ({}) in inventory of NPC {:02X}; ignoring", c->lobby_client_id, item.id, name, cmd.header.client_id);
    }

  } else {
    c->character_file()->add_item(item, *s->item_stack_limits(c->version()));

    if (l->log.should_log(phosg::LogLevel::L_INFO)) {
      auto name = s->describe_item(c->version(), item);
      l->log.info_f("Player {} created inventory item {:08X} ({})", c->lobby_client_id, item.id, name);
      c->print_inventory();
    }
  }

  forward_subcommand_with_item_transcode_t(c, msg.command, msg.flag, cmd);
}

asio::awaitable<void> on_create_inventory_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (msg.size == sizeof(G_CreateInventoryItem_PC_V3_BB_6x2B)) {
    co_await on_create_inventory_item_t<G_CreateInventoryItem_PC_V3_BB_6x2B>(c, msg);
  } else if (msg.size == sizeof(G_CreateInventoryItem_DC_6x2B)) {
    co_await on_create_inventory_item_t<G_CreateInventoryItem_DC_6x2B>(c, msg);
  } else {
    throw runtime_error("invalid size for 6x2B command");
  }
  co_return;
}

template <typename CmdT>
static void on_drop_partial_stack_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<CmdT>();

  auto l = c->require_lobby();
  if (c->version() == Version::BB_V4) {
    throw runtime_error("6x5D command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6x5D command sent in non-game lobby");
  }
  // TODO: Should we check the client ID here too?

  // We don't delete anything from the inventory here; the client will send a 6x29 to do so following this command.

  ItemData item = cmd.item_data;
  item.decode_for_version(c->version());
  l->on_item_id_generated_externally(item.id);
  l->add_item(cmd.floor, item, cmd.pos, nullptr, nullptr, 0x00F);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto s = c->require_server_state();
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} split stack to create floor item {:08X} ({}) at {}:({:g},{:g})",
        cmd.header.client_id, item.id, name, cmd.floor, cmd.pos.x, cmd.pos.z);
    c->print_inventory();
  }

  forward_subcommand_with_item_transcode_t(c, msg.command, msg.flag, cmd);
}

asio::awaitable<void> on_drop_partial_stack(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (msg.size == sizeof(G_DropStackedItem_PC_V3_BB_6x5D)) {
    on_drop_partial_stack_t<G_DropStackedItem_PC_V3_BB_6x5D>(c, msg);
  } else if (msg.size == sizeof(G_DropStackedItem_DC_6x5D)) {
    on_drop_partial_stack_t<G_DropStackedItem_DC_6x5D>(c, msg);
  } else {
    throw runtime_error("invalid size for 6x5D command");
  }
  co_return;
}

asio::awaitable<void> on_drop_partial_stack_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_SplitStackedItem_BB_6xC3>();
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xC3 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xC3 command sent in non-game lobby");
  }
  if (cmd.header.client_id != c->lobby_client_id) {
    throw runtime_error("6xC3 command sent by incorrect client");
  }

  auto s = c->require_server_state();
  auto p = c->character_file();
  const auto& limits = *s->item_stack_limits(c->version());
  auto item = p->remove_item(cmd.item_id, cmd.amount, limits);

  // If a stack was split, the original item still exists, so the dropped item needs a new ID. remove_item signals this
  // by returning an item with an ID of 0xFFFFFFFF.
  if (item.id == 0xFFFFFFFF) {
    item.id = l->generate_item_id(c->lobby_client_id);
  }

  // PSOBB sends a 6x29 command after it receives the 6x5D, so we need to add the item back to the player's inventory
  // to correct for this (it will get removed again by the 6x29 handler)
  p->add_item(item, limits);

  l->add_item(cmd.floor, item, cmd.pos, nullptr, nullptr, 0x00F);
  send_drop_stacked_item_to_lobby(l, item, cmd.floor, cmd.pos);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto s = c->require_server_state();
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} split stack {:08X} (removed: {}) at {}:({:g}, {:g})",
        cmd.header.client_id, cmd.item_id, name, cmd.floor, cmd.pos.x, cmd.pos.z);
    c->print_inventory();
  }
  co_return;
}

asio::awaitable<void> on_buy_shop_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_BuyShopItem_6x5E>();
  auto l = c->require_lobby();
  if (c->version() == Version::BB_V4) {
    throw runtime_error("6x5E command sent by BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6x5E command sent in non-game lobby");
  }
  if (cmd.header.client_id != c->lobby_client_id) {
    throw runtime_error("6x5E command sent by incorrect client");
  }

  auto s = c->require_server_state();
  auto p = c->character_file();
  ItemData item = cmd.item_data;
  item.data2d = 0; // Clear the price field
  item.decode_for_version(c->version());
  l->on_item_id_generated_externally(item.id);
  p->add_item(item, *s->item_stack_limits(c->version()));

  size_t price = s->item_parameter_table(c->version())->price_for_item(item);
  p->remove_meseta(price, c->version() != Version::BB_V4);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} bought item {:08X} ({}) from shop ({} Meseta)",
        cmd.header.client_id, item.id, name, price);
    c->print_inventory();
  }

  forward_subcommand_with_item_transcode_t(c, msg.command, msg.flag, cmd);
  co_return;
}

void send_item_notification_if_needed(shared_ptr<Client> c, const ItemData& item, bool is_from_rare_table) {
  auto s = c->require_server_state();

  bool should_notify = false;
  bool should_include_rare_header = false;
  switch (c->get_drop_notification_mode()) {
    case Client::ItemDropNotificationMode::NOTHING:
      break;
    case Client::ItemDropNotificationMode::RARES_ONLY:
      should_notify = (is_from_rare_table || (item.data1[0] == 0x03)) &&
          s->item_parameter_table(c->version())->is_item_rare(item);
      should_include_rare_header = true;
      break;
    case Client::ItemDropNotificationMode::ALL_ITEMS:
      should_notify = (item.data1[0] != 0x04);
      break;
    case Client::ItemDropNotificationMode::ALL_ITEMS_INCLUDING_MESETA:
      should_notify = true;
      break;
  }

  if (should_notify) {
    string name = s->describe_item(c->version(), item, ItemNameIndex::Flag::INCLUDE_PSO_COLOR_ESCAPES);
    const char* rare_header = (should_include_rare_header ? "$C6Rare item dropped:\n" : "");
    send_text_message_fmt(c, "{}{}", rare_header, name);
  }
}

template <typename CmdT>
static void on_box_or_enemy_item_drop_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  // I'm lazy and this should never happen for item commands (since all players need to stay in sync)
  if (command_is_private(msg.command)) {
    throw runtime_error("item subcommand sent via private command");
  }

  const auto& cmd = msg.check_size_t<CmdT>();

  auto s = c->require_server_state();
  auto l = c->require_lobby();
  if (!l->is_game() || (c->lobby_client_id != l->leader_id)) {
    return;
  }
  if (c->version() == Version::BB_V4) {
    throw runtime_error("BB client sent 6x5F command");
  }

  bool should_notify = s->rare_notifs_enabled_for_client_drops && (l->drop_mode == ServerDropMode::CLIENT);

  shared_ptr<const MapState::EnemyState> ene_st;
  shared_ptr<const MapState::ObjectState> obj_st;
  string from_entity_str;
  if (cmd.item.source_type == 1) {
    ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.item.floor, cmd.item.entity_index);
    from_entity_str = std::format(" from E-{:03X}", ene_st->e_id);
  } else {
    obj_st = l->map_state->object_state_for_index(c->version(), cmd.item.floor, cmd.item.entity_index);
    from_entity_str = std::format(" from K-{:03X}", obj_st->k_id);
  }

  ItemData item = cmd.item.item;
  item.decode_for_version(c->version());
  l->on_item_id_generated_externally(item.id);
  l->add_item(cmd.item.floor, item, cmd.item.pos, obj_st, ene_st, should_notify ? 0x100F : 0x000F);

  auto name = s->describe_item(c->version(), item);
  l->log.info_f("Player {} (leader) created floor item {:08X} ({}){} at {}:({:g}, {:g})",
      l->leader_id,
      item.id,
      name,
      from_entity_str,
      cmd.item.floor,
      cmd.item.pos.x,
      cmd.item.pos.z);

  for (auto& lc : l->clients) {
    if (!lc) {
      continue;
    }
    if (lc != c) {
      uint16_t entity_index = 0xFFFF;
      if (ene_st) {
        entity_index = l->map_state->index_for_enemy_state(lc->version(), ene_st);
      } else if (obj_st) {
        entity_index = l->map_state->index_for_object_state(lc->version(), obj_st);
      }
      send_drop_item_to_channel(s, lc->channel, item, cmd.item.source_type, cmd.item.floor, cmd.item.pos, entity_index);
    }
    send_item_notification_if_needed(lc, item, true);
  }
}

asio::awaitable<void> on_box_or_enemy_item_drop(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (msg.size == sizeof(G_DropItem_DC_6x5F)) {
    on_box_or_enemy_item_drop_t<G_DropItem_DC_6x5F>(c, msg);
  } else if (msg.size == sizeof(G_DropItem_PC_V3_BB_6x5F)) {
    on_box_or_enemy_item_drop_t<G_DropItem_PC_V3_BB_6x5F>(c, msg);
  } else {
    throw runtime_error("invalid size for 6x5F command");
  }
  co_return;
}

static asio::awaitable<void> on_pick_up_item_generic(
    shared_ptr<Client> c, uint16_t client_id, uint16_t floor, uint32_t item_id, bool is_request) {
  auto l = c->require_lobby();
  if (!l->is_game() || (client_id != c->lobby_client_id)) {
    co_return;
  }

  if (!l->item_exists(floor, item_id)) {
    // This can happen if the network is slow, and the client tries to pick up the same item multiple times. Or
    // multiple clients could try to pick up the same item at approximately the same time; only one should get it.
    l->log.warning_f("Player {} requests to pick up {:08X}, but the item does not exist; dropping command", client_id, item_id);

  } else {
    // This is handled by the server on BB, and by the leader on other versions. However, the client's logic is to
    // simply always send a 6x59 command when it receives a 6x5A and the floor item exists, so we just implement that
    // logic here instead of forwarding the 6x5A to the leader.

    auto p = c->character_file();
    auto s = c->require_server_state();
    auto fi = l->remove_item(floor, item_id, c->lobby_client_id);
    if (!fi->visible_to_client(c->lobby_client_id)) {
      l->log.warning_f("Player {} requests to pick up {:08X}, but is it not visible to them; dropping command",
          client_id, item_id);
      l->add_item(floor, fi);
      co_return;
    }

    try {
      p->add_item(fi->data, *s->item_stack_limits(c->version()));
    } catch (const out_of_range&) {
      // Inventory is full; put the item back where it was
      l->log.warning_f("Player {} requests to pick up {:08X}, but their inventory is full; dropping command",
          client_id, item_id);
      l->add_item(floor, fi);
      co_return;
    }

    if (l->log.should_log(phosg::LogLevel::L_INFO)) {
      auto s = c->require_server_state();
      auto name = s->describe_item(c->version(), fi->data);
      l->log.info_f("Player {} picked up {:08X} ({})", client_id, item_id, name);
      c->print_inventory();
    }

    for (size_t z = 0; z < 12; z++) {
      auto lc = l->clients[z];
      if ((!lc) || (!is_request && (lc == c))) {
        continue;
      }
      if (fi->visible_to_client(z)) {
        send_pick_up_item_to_client(lc, client_id, item_id, floor);
      } else {
        send_create_inventory_item_to_client(lc, client_id, fi->data);
      }
    }

    if (!c->login->account->check_user_flag(Account::UserFlag::DISABLE_DROP_NOTIFICATION_BROADCAST) && (fi->flags & 0x1000)) {
      uint32_t pi = fi->data.primary_identifier();
      bool should_send_game_notif, should_send_global_notif;
      if (is_v1_or_v2(c->version()) && (c->version() != Version::GC_NTE)) {
        should_send_game_notif = s->notify_game_for_item_primary_identifiers_v1_v2.count(pi);
        should_send_global_notif = s->notify_server_for_item_primary_identifiers_v1_v2.count(pi);
      } else if (!is_v4(c->version())) {
        should_send_game_notif = s->notify_game_for_item_primary_identifiers_v3.count(pi);
        should_send_global_notif = s->notify_server_for_item_primary_identifiers_v3.count(pi);
      } else {
        should_send_game_notif = s->notify_game_for_item_primary_identifiers_v4.count(pi);
        should_send_global_notif = s->notify_server_for_item_primary_identifiers_v4.count(pi);
      }

      if (should_send_game_notif || should_send_global_notif) {
        string p_name = p->disp.name.decode();
        string desc_ingame = s->describe_item(c->version(), fi->data, ItemNameIndex::Flag::INCLUDE_PSO_COLOR_ESCAPES);
        string desc_http = s->describe_item(c->version(), fi->data);

        if (s->http_server) {
          auto message = make_shared<phosg::JSON>(phosg::JSON::dict({
              {"PlayerAccountID", c->login->account->account_id},
              {"PlayerName", p_name},
              {"PlayerVersion", phosg::name_for_enum(c->version())},
              {"GameName", l->name},
              {"GameDropMode", phosg::name_for_enum(l->drop_mode)},
              {"ItemData", fi->data.hex()},
              {"ItemDescription", desc_http},
              {"NotifyGame", should_send_game_notif},
              {"NotifyServer", should_send_global_notif},
          }));
          co_await s->http_server->send_rare_drop_notification(message);
        }

        string message = std::format("$C6{}$C7 found\n{}", p_name, desc_ingame);
        string bb_message = std::format("$C6{}$C7 has found {}", p_name, desc_ingame);
        if (should_send_global_notif) {
          for (auto& it : s->game_server->all_clients()) {
            if (it->login &&
                !is_patch(it->version()) &&
                !is_ep3(it->version()) &&
                it->lobby.lock()) {
              send_text_or_scrolling_message(it, message, bb_message);
            }
          }
        } else {
          send_text_or_scrolling_message(l, nullptr, message, bb_message);
        }
      }
    }
  }
}

asio::awaitable<void> on_pick_up_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_PickUpItem_6x59>();
  co_await on_pick_up_item_generic(c, cmd.client_id2, cmd.floor, cmd.item_id, false);
}

asio::awaitable<void> on_pick_up_item_request(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_PickUpItemRequest_6x5A>();
  co_await on_pick_up_item_generic(c, cmd.header.client_id, cmd.floor, cmd.item_id, true);
}

asio::awaitable<void> on_equip_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_EquipItem_6x25>();

  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }

  auto l = c->require_lobby();
  EquipSlot slot = static_cast<EquipSlot>(cmd.equip_slot.load());
  auto p = c->character_file();
  p->inventory.equip_item_id(cmd.item_id, slot);
  c->log.info_f("Equipped item {:08X}", cmd.item_id);

  forward_subcommand(c, msg);
}

asio::awaitable<void> on_unequip_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_UnequipItem_6x26>();

  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }

  auto l = c->require_lobby();
  auto p = c->character_file();
  p->inventory.unequip_item_id(cmd.item_id);
  c->log.info_f("Unequipped item {:08X}", cmd.item_id);

  forward_subcommand(c, msg);
}

asio::awaitable<void> on_use_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_UseItem_6x27>();

  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }

  auto l = c->require_lobby();
  auto s = c->require_server_state();
  auto p = c->character_file();
  size_t index = p->inventory.find_item(cmd.item_id);
  string name;
  {
    // Note: We manually downscope item here because player_use_item will likely move or delete the item, which will
    // break the reference, so we don't want to accidentally use it again after that.
    const auto& item = p->inventory.items[index].data;
    name = s->describe_item(c->version(), item);
  }
  player_use_item(c, index, l->rand_crypt);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    l->log.info_f("Player {} used item {}:{:08X} ({})", c->lobby_client_id, cmd.header.client_id, cmd.item_id, name);
    c->print_inventory();
  }

  forward_subcommand(c, msg);
}

asio::awaitable<void> on_feed_mag(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_FeedMag_6x28>();

  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }

  auto s = c->require_server_state();
  auto l = c->require_lobby();
  auto p = c->character_file();

  size_t mag_index = p->inventory.find_item(cmd.mag_item_id);
  size_t fed_index = p->inventory.find_item(cmd.fed_item_id);
  string mag_name, fed_name;
  {
    // Note: We downscope these because player_feed_mag will likely delete the items, which will break these references
    const auto& fed_item = p->inventory.items[fed_index].data;
    fed_name = s->describe_item(c->version(), fed_item);
    const auto& mag_item = p->inventory.items[mag_index].data;
    mag_name = s->describe_item(c->version(), mag_item);
  }
  player_feed_mag(c, mag_index, fed_index);

  // On BB, the player only sends a 6x28; on other versions, the player sends a 6x29 immediately after to destroy the
  // fed item. So on BB, we should remove the fed item here, but on other versions, we allow the following 6x29 command
  // to do that.
  if (c->version() == Version::BB_V4) {
    p->remove_item(cmd.fed_item_id, 1, *s->item_stack_limits(c->version()));
  }

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    l->log.info_f("Player {} fed item {}:{:08X} ({}) to mag {}:{:08X} ({})",
        c->lobby_client_id, cmd.header.client_id, cmd.fed_item_id, fed_name,
        cmd.header.client_id, cmd.mag_item_id, mag_name);
    c->print_inventory();
  }

  forward_subcommand(c, msg);
}
