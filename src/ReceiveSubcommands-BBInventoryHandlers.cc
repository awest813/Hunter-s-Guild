#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

void send_max_level_notification_if_needed(shared_ptr<Client> c);
void assert_quest_item_create_allowed(shared_ptr<const Lobby> l, const ItemData& item);

asio::awaitable<void> on_level_up(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  // On the DC prototypes, this command doesn't include any stats - it just increments the player's level by 1.
  auto p = c->character_file();
  if (is_pre_v1(c->version())) {
    msg.check_size_t<G_ChangePlayerLevel_DCNTE_6x30>();
    auto s = c->require_server_state();
    auto level_table = s->level_table(c->version());
    const auto& level_incrs = level_table->stats_delta_for_level(p->disp.visual.char_class, p->disp.stats.level + 1);
    p->disp.stats.char_stats.atp += level_incrs.atp;
    p->disp.stats.char_stats.mst += level_incrs.mst;
    p->disp.stats.char_stats.evp += level_incrs.evp;
    p->disp.stats.char_stats.hp += level_incrs.hp;
    p->disp.stats.char_stats.dfp += level_incrs.dfp;
    p->disp.stats.char_stats.ata += level_incrs.ata;
    p->disp.stats.char_stats.lck += level_incrs.lck;
    p->disp.stats.level++;
  } else {
    const auto& cmd = msg.check_size_t<G_ChangePlayerLevel_6x30>();
    p->disp.stats.char_stats.atp = cmd.atp;
    p->disp.stats.char_stats.mst = cmd.mst;
    p->disp.stats.char_stats.evp = cmd.evp;
    p->disp.stats.char_stats.hp = cmd.hp;
    p->disp.stats.char_stats.dfp = cmd.dfp;
    p->disp.stats.char_stats.ata = cmd.ata;
    p->disp.stats.level = cmd.level;
  }

  send_max_level_notification_if_needed(c);
  forward_subcommand(c, msg);
}

asio::awaitable<void> on_quest_create_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_QuestCreateItem_BB_6xCA>();
  auto s = c->require_server_state();
  auto l = c->require_lobby();
  const auto& limits = *s->item_stack_limits(c->version());

  ItemData item;
  item = cmd.item_data;
  // enforce_stack_size_limits must come after this assert since quests may attempt to create stackable items with a
  // count of zero
  assert_quest_item_create_allowed(l, item);
  item.enforce_stack_size_limits(limits);
  item.id = l->generate_item_id(c->lobby_client_id);

  // The logic for the item_create and item_create2 quest opcodes (B3 and B4) includes a precondition check to see if
  // the player can actually add the item to their inventory or not, and the entire command is skipped if not. However,
  // on BB, the implementation performs this check and sends a 6xCA command instead - the item is not immediately added
  // to the inventory, and is instead added when the server sends back a 6xBE command. So if a quest creates multiple
  // items in quick succession, there may be another 6xCA/6xBE sequence in flight, and the client's check if an item
  // can be created may pass when a 6xBE command that would make it fail is already on the way from the server. To
  // handle this, we simply ignore any 6xCA command if the item can't be created.
  try {
    c->character_file()->add_item(item, limits);
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);
    if (l->log.should_log(phosg::LogLevel::L_INFO)) {
      auto name = s->describe_item(c->version(), item);
      l->log.info_f("Player {} created inventory item {:08X} ({}) via quest command",
          c->lobby_client_id, item.id, name);
      c->print_inventory();
    }

  } catch (const out_of_range&) {
    if (l->log.should_log(phosg::LogLevel::L_INFO)) {
      auto name = s->describe_item(c->version(), item);
      l->log.info_f("Player {} attempted to create inventory item {:08X} ({}) via quest command, but it cannot be placed in their inventory",
          c->lobby_client_id, item.id, name);
    }
  }
  co_return;
}

asio::awaitable<void> on_transfer_item_via_mail_message_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_TransferItemViaMailMessage_BB_6xCB>();

  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xCB command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xCB command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xCB command sent during free play");
  }
  if (cmd.header.client_id != c->lobby_client_id) {
    throw runtime_error("6xCB command sent by incorrect client");
  }

  auto s = c->require_server_state();
  auto p = c->character_file();
  const auto& limits = *s->item_stack_limits(c->version());
  auto item = p->remove_item(cmd.item_id, cmd.amount, limits);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} sent inventory item {}:{:08X} ({}) x{} to player {:08X}",
        c->lobby_client_id, cmd.header.client_id, cmd.item_id, name, cmd.amount, cmd.target_guild_card_number);
    c->print_inventory();
  }

  // To receive an item, the player must be online, using BB, have a character loaded (that is, be in a lobby or game),
  // not be at the bank counter at the moment, and there must be room in their bank to receive the item.
  bool item_sent = false;
  auto target_c = s->find_client(nullptr, cmd.target_guild_card_number);
  if (target_c &&
      (target_c->version() == Version::BB_V4) &&
      (target_c->character_file(false) != nullptr) &&
      !target_c->check_flag(Client::Flag::AT_BANK_COUNTER)) {
    try {
      target_c->bank_file()->add_item(item, limits);
      item_sent = true;
    } catch (const runtime_error&) {
    }
  }

  if (item_sent) {
    // See the comment in the 6xCC handler about why we do this. Similar to that case, the 6xCB handler on the client
    // side does exactly the same thing as 6x29, but 6x29 is backward-compatible with other versions and 6xCB is not.
    G_DeleteInventoryItem_6x29 cmd29 = {{0x29, 0x03, cmd.header.client_id}, cmd.item_id, cmd.amount};
    SubcommandMessage delete_item_msg{msg.command, msg.flag, &cmd29, sizeof(cmd29)};
    forward_subcommand(c, delete_item_msg);
    send_command(c, 0x16EA, 0x00000001);
  } else {
    send_command(c, 0x16EA, 0x00000000);
    // If the item failed to send, add it back to the sender's inventory
    item.id = l->generate_item_id(0xFF);
    p->add_item(item, limits);
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);
  }
  co_return;
}

asio::awaitable<void> on_exchange_item_for_team_points_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_ExchangeItemForTeamPoints_BB_6xCC>();

  auto team = c->team();
  if (!team) {
    throw runtime_error("player is not in a team");
  }
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xCC command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xCC command sent in non-game lobby");
  }
  if (cmd.header.client_id != c->lobby_client_id) {
    throw runtime_error("6xCC command sent by incorrect client");
  }

  auto s = c->require_server_state();
  auto p = c->character_file();
  const auto& limits = *s->item_stack_limits(c->version());
  auto item = p->remove_item(cmd.item_id, cmd.amount, limits);
  size_t amount = item.stack_size(limits);

  size_t points = s->item_parameter_table(Version::BB_V4)->get_item_team_points(item);
  s->team_index->add_member_points(c->login->account->account_id, points * amount);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} exchanged inventory item {}:{:08X} ({}) x{} for {} * {} = {} team points",
        c->lobby_client_id, cmd.header.client_id, cmd.item_id, name, amount, points, amount, points * amount);
    c->print_inventory();
  }

  // The original implementation forwarded the 6xCC command to all other clients. However, the handler does exactly the
  // same thing as 6x29 if the affected client isn't the local client. Since the sender has already processed the 6xCC
  // that they sent by the time we receive this, we pretend that they sent 6x29 instead and send that to the others in
  // the game.
  G_DeleteInventoryItem_6x29 cmd29 = {{0x29, 0x03, cmd.header.client_id}, cmd.item_id, cmd.amount};
  SubcommandMessage delete_item_msg{msg.command, msg.flag, &cmd29, sizeof(cmd29)};
  forward_subcommand(c, delete_item_msg);
  co_return;
}

asio::awaitable<void> on_destroy_inventory_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_DeleteInventoryItem_6x29>();

  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }
  if (cmd.header.client_id != c->lobby_client_id) {
    co_return;
  }

  auto s = c->require_server_state();
  auto p = c->character_file();
  auto item = p->remove_item(cmd.item_id, cmd.amount, *s->item_stack_limits(c->version()));

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} destroyed inventory item {}:{:08X} ({})",
        c->lobby_client_id, cmd.header.client_id, cmd.item_id, name);
    c->print_inventory();
  }
  forward_subcommand(c, msg);
}

asio::awaitable<void> on_destroy_floor_item(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_DestroyFloorItem_6x5C_6x63>();

  bool is_6x5C;
  switch (c->version()) {
    case Version::DC_NTE:
      is_6x5C = (cmd.header.subcommand == 0x4E);
      break;
    case Version::DC_11_2000:
      is_6x5C = (cmd.header.subcommand == 0x55);
      break;
    default:
      is_6x5C = (cmd.header.subcommand == 0x5C);
  }

  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  auto s = c->require_server_state();
  shared_ptr<Lobby::FloorItem> fi;
  try {
    fi = l->remove_item(cmd.floor, cmd.item_id, 0xFF);
  } catch (const out_of_range&) {
  }

  if (!fi) {
    // There are generally two data races that could occur here. Either the player attempted to evict the item at the
    // same time the server did (that is, the client's and server's 6x63 commands crossed paths on the network), or the
    // player attempted to evict an item that was already picked up. The former case is easy to handle; we can just
    // ignore the command. The latter case is more difficult - we have to know which player picked up the item and send
    // a 6x2B command to the sender, to sync their item state with the server's again. We can't just look through the
    // players' inventories to find the item ID, since item IDs can be destroyed when stackable items or Meseta are
    // picked up.
    // TODO: We don't actually handle the evict/pickup conflict case. This case is probably quite rare, but we should
    // eventually handle it.
    l->log.info_f("Player {} attempted to destroy floor item {:08X}, but it is missing",
        c->lobby_client_id, cmd.item_id);

  } else {
    auto name = s->describe_item(c->version(), fi->data);
    l->log.info_f("Player {} destroyed floor item {:08X} ({})", c->lobby_client_id, cmd.item_id, name);

    // Only forward to players for whom the item was visible
    for (size_t z = 0; z < l->clients.size(); z++) {
      auto lc = l->clients[z];
      if (lc && fi->visible_to_client(z)) {
        if (lc->version() != c->version()) {
          G_DestroyFloorItem_6x5C_6x63 out_cmd = cmd;
          switch (lc->version()) {
            case Version::DC_NTE:
              out_cmd.header.subcommand = is_6x5C ? 0x4E : 0x55;
              break;
            case Version::DC_11_2000:
              out_cmd.header.subcommand = is_6x5C ? 0x55 : 0x5C;
              break;
            default:
              out_cmd.header.subcommand = is_6x5C ? 0x5C : 0x63;
          }
          send_command_t(lc, msg.command, msg.flag, out_cmd);
        } else {
          send_command_t(lc, msg.command, msg.flag, cmd);
        }
      }
    }
  }
}

asio::awaitable<void> on_identify_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("6xDA command sent in non-game lobby");
  }

  if (c->version() == Version::BB_V4) {
    const auto& cmd = msg.check_size_t<G_IdentifyItemRequest_6xB8>();
    if (!l->is_game() || l->episode == Episode::EP3) {
      co_return;
    }

    auto p = c->character_file();
    size_t x = p->inventory.find_item(cmd.item_id);
    if (p->inventory.items[x].data.data1[0] != 0) {
      throw runtime_error("non-weapon items cannot be unidentified");
    }

    // It seems the client expects an item ID to be consumed here, even though the returned item has the same ID as the
    // original item. Perhaps this was not the case on Sega's original server, and the returned item had a new ID
    // instead.
    l->generate_item_id(c->lobby_client_id);
    p->disp.stats.meseta -= 100;
    c->bb_identify_result = p->inventory.items[x].data;
    c->bb_identify_result.data1[4] &= 0x7F;
    uint8_t effective_section_id = l->effective_section_id();
    if (effective_section_id >= 10) {
      throw std::runtime_error("effective section ID is not valid");
    }
    l->item_creator->apply_tekker_deltas(c->bb_identify_result, effective_section_id);
    send_item_identify_result(c);

  } else {
    forward_subcommand(c, msg);
  }
}

asio::awaitable<void> on_accept_identify_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("6xB5 command sent in non-game lobby");
  }

  if (is_ep3(c->version())) {
    forward_subcommand(c, msg);

  } else if (c->version() == Version::BB_V4) {
    const auto& cmd = msg.check_size_t<G_AcceptItemIdentification_BB_6xBA>();

    if (!c->bb_identify_result.id || (c->bb_identify_result.id == 0xFFFFFFFF)) {
      throw runtime_error("no identify result present");
    }
    if (c->bb_identify_result.id != cmd.item_id) {
      throw runtime_error("accepted item ID does not match previous identify request");
    }
    auto s = c->require_server_state();
    c->character_file()->add_item(c->bb_identify_result, *s->item_stack_limits(c->version()));
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, c->bb_identify_result);
    c->bb_identify_result.clear();
  }
  co_return;
}

asio::awaitable<void> on_sell_item_at_shop_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xC0 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xC0 command sent in non-game lobby");
  }

  const auto& cmd = msg.check_size_t<G_SellItemAtShop_BB_6xC0>();

  auto s = c->require_server_state();
  auto p = c->character_file();
  auto item = p->remove_item(cmd.item_id, cmd.amount, *s->item_stack_limits(c->version()));
  size_t price = (s->item_parameter_table(c->version())->price_for_item(item) >> 3) * cmd.amount;
  p->add_meseta(price);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} sold inventory item {:08X} ({}) for {} Meseta",
        c->lobby_client_id, cmd.item_id, name, price);
    c->print_inventory();
  }

  forward_subcommand(c, msg);
  co_return;
}

asio::awaitable<void> on_buy_shop_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xB7 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xB7 command sent in non-game lobby");
  }

  const auto& cmd = msg.check_size_t<G_BuyShopItem_BB_6xB7>();
  auto s = c->require_server_state();
  const auto& limits = *s->item_stack_limits(c->version());

  ItemData item;
  item = c->bb_shop_contents.at(cmd.shop_type).at(cmd.item_index);
  if (item.is_stackable(limits)) {
    item.data1[5] = cmd.amount;
  } else if (cmd.amount != 1) {
    throw runtime_error("item is not stackable");
  }

  size_t price = item.data2d * cmd.amount;
  item.data2d = 0;
  auto p = c->character_file();
  p->remove_meseta(price, false);

  item.id = cmd.shop_item_id;
  l->on_item_id_generated_externally(item.id);
  p->add_item(item, limits);
  send_create_inventory_item_to_lobby(c, c->lobby_client_id, item, true);

  if (l->log.should_log(phosg::LogLevel::L_INFO)) {
    auto s = c->require_server_state();
    auto name = s->describe_item(c->version(), item);
    l->log.info_f("Player {} purchased item {:08X} ({}) for {} meseta", c->lobby_client_id, item.id, name, price);
    c->print_inventory();
  }
  co_return;
}
