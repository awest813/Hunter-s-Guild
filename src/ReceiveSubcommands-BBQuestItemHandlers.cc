#include "ReceiveSubcommands-Impl.hh"

#include <ctime>

#include "SendCommands.hh"

using namespace std;

void assert_quest_item_create_allowed(shared_ptr<const Lobby> l, const ItemData& item);

asio::awaitable<void> on_quest_exchange_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xD5 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xD5 command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xD5 command sent during free play");
  }

  const auto& cmd = msg.check_size_t<G_QuestExchangeItem_BB_6xD5>();
  auto s = c->require_server_state();

  try {
    auto p = c->character_file();
    const auto& limits = *s->item_stack_limits(c->version());

    ItemData new_item = cmd.replace_item;
    assert_quest_item_create_allowed(l, new_item);
    new_item.enforce_stack_size_limits(limits);

    size_t found_index = p->inventory.find_item_by_primary_identifier(cmd.find_item.primary_identifier());
    auto found_item = p->remove_item(p->inventory.items[found_index].data.id, 1, limits);
    send_destroy_item_to_lobby(c, found_item.id, 1);

    new_item.id = l->generate_item_id(c->lobby_client_id);
    p->add_item(new_item, limits);
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, new_item);

    send_quest_function_call(c, cmd.success_label);

  } catch (const exception& e) {
    c->log.warning_f("Quest item exchange failed: {}", e.what());
    send_quest_function_call(c, cmd.failure_label);
  }
  co_return;
}

asio::awaitable<void> on_wrap_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xD6 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xD6 command sent in non-game lobby");
  }

  const auto& cmd = msg.check_size_t<G_WrapItem_BB_6xD6>();
  auto s = c->require_server_state();

  auto p = c->character_file();
  auto item = p->remove_item(cmd.item.id, 1, *s->item_stack_limits(c->version()));
  send_destroy_item_to_lobby(c, item.id, 1);
  item.wrap(*s->item_stack_limits(c->version()), cmd.present_color);
  p->add_item(item, *s->item_stack_limits(c->version()));
  send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);
  co_return;
}

asio::awaitable<void> on_photon_drop_exchange_for_item_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xD7 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xD7 command sent in non-game lobby");
  }

  const auto& cmd = msg.check_size_t<G_PaganiniPhotonDropExchange_BB_6xD7>();
  auto s = c->require_server_state();

  try {
    auto p = c->character_file();
    const auto& limits = *s->item_stack_limits(c->version());

    ItemData new_item = cmd.new_item;
    assert_quest_item_create_allowed(l, new_item);
    new_item.enforce_stack_size_limits(limits);

    size_t found_index = p->inventory.find_item_by_primary_identifier(0x03100000);
    auto found_item = p->remove_item(p->inventory.items[found_index].data.id, 0, limits);
    send_destroy_item_to_lobby(c, found_item.id, found_item.stack_size(limits));

    new_item.id = l->generate_item_id(c->lobby_client_id);
    p->add_item(new_item, limits);
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, new_item);

    send_quest_function_call(c, cmd.success_label);

  } catch (const exception& e) {
    c->log.warning_f("Quest Photon Drop exchange for item failed: {}", e.what());
    send_quest_function_call(c, cmd.failure_label);
  }
  co_return;
}

asio::awaitable<void> on_photon_drop_exchange_for_s_rank_special_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xD8 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xD8 command sent in non-game lobby");
  }

  const auto& cmd = msg.check_size_t<G_AddSRankWeaponSpecial_BB_6xD8>();
  auto s = c->require_server_state();
  const auto& limits = *s->item_stack_limits(c->version());

  try {
    auto p = c->character_file();

    static const array<uint8_t, 0x10> costs({60, 60, 20, 20, 30, 30, 30, 50, 40, 50, 40, 40, 50, 40, 40, 40});
    uint8_t cost = costs.at(cmd.special_type);

    size_t payment_item_index = p->inventory.find_item_by_primary_identifier(0x03100000);
    {
      const auto& item = p->inventory.items[p->inventory.find_item(cmd.item_id)];
      if (!item.data.is_s_rank_weapon()) {
        throw std::runtime_error("6xD8 cannot be used for non-ES weapons");
      }
    }

    auto payment_item = p->remove_item(p->inventory.items[payment_item_index].data.id, cost, limits);
    send_destroy_item_to_lobby(c, payment_item.id, cost);

    auto item = p->remove_item(cmd.item_id, 1, limits);
    send_destroy_item_to_lobby(c, item.id, cost);
    item.data1[2] = cmd.special_type;
    p->add_item(item, limits);
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);

    send_quest_function_call(c, cmd.success_label);

  } catch (const exception& e) {
    c->log.warning_f("Quest Photon Drop exchange for S-rank special failed: {}", e.what());
    send_quest_function_call(c, cmd.failure_label);
  }
  co_return;
}

asio::awaitable<void> on_secret_lottery_ticket_exchange_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xDE command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xDE command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xDE command sent during free play");
  }
  if (!l->quest) {
    throw runtime_error("6xDE command sent with no quest loaded");
  }
  if (l->quest->meta.create_item_mask_entries.size() < 2) {
    throw runtime_error("quest does not have enough create item mask entries");
  }

  // See notes about 6xDE in CommandFormats.hh about this weirdness
  const auto& cmd = msg.check_size_t<G_ExchangeSecretLotteryTicket_Incomplete_BB_6xDE>(0x0C);
  uint16_t failure_label;
  if (msg.size >= 0x0C) {
    const auto& cmd = msg.check_size_t<G_ExchangeSecretLotteryTicket_BB_6xDE>();
    failure_label = cmd.failure_label;
  } else {
    failure_label = cmd.success_label;
  }

  // The last mask entry is the currency item (e.g. Secret Lottery Ticket)
  const auto& currency_mask = l->quest->meta.create_item_mask_entries.back();
  uint32_t currency_primary_identifier = currency_mask.primary_identifier();
  auto p = c->character_file();
  ssize_t currency_index = -1;
  try {
    currency_index = p->inventory.find_item_by_primary_identifier(currency_primary_identifier);
    c->log.info_f("Currency item {:08X} found at index {}", currency_primary_identifier, currency_index);
  } catch (const out_of_range&) {
    c->log.info_f("Currency item {:08X} not found in inventory", currency_primary_identifier);
  }

  S_ExchangeSecretLotteryTicketResult_BB_24 out_cmd;
  out_cmd.start_reg_num = cmd.start_reg_num;
  out_cmd.label = (currency_index >= 0) ? cmd.success_label.load() : failure_label;
  for (size_t z = 0; z < out_cmd.reg_values.size(); z++) {
    out_cmd.reg_values[z] = (l->rand_crypt->next() % (l->quest->meta.create_item_mask_entries.size() - 1)) + 1;
    c->log.info_f("Mask index {} is {} ({})", z, out_cmd.reg_values[z] - 1, l->quest->meta.create_item_mask_entries[out_cmd.reg_values[z] - 1].str());
  }

  if (currency_index >= 0) {
    size_t mask_index = out_cmd.reg_values[cmd.index - 1] - 1;
    const auto& mask = l->quest->meta.create_item_mask_entries[mask_index];
    c->log.info_f("Chose mask {} ({})", mask_index, mask.str());

    ItemData item;
    for (size_t z = 0; z < 12; z++) {
      const auto& r = mask.data1_ranges[z];
      if (r.min != r.max) {
        throw std::runtime_error("invalid range for bb_exchange_slt");
      }
      item.data1[z] = r.min;
    }
    auto s = c->require_server_state();
    const auto& limits = *s->item_stack_limits(c->version());
    item.enforce_stack_size_limits(limits);

    uint32_t slt_item_id = p->inventory.items[currency_index].data.id;

    // Note: It seems Sega used 6xDB here; we use 6x29 instead.
    p->remove_item(slt_item_id, 1, limits);
    send_destroy_item_to_lobby(c, slt_item_id, 1);

    item.id = l->generate_item_id(c->lobby_client_id);
    p->add_item(item, limits);
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);
  }

  send_command_t(c, 0x24, (currency_index >= 0) ? 0 : 1, out_cmd);
  co_return;
}

asio::awaitable<void> on_photon_crystal_exchange_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xDF command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xDF command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xDF command sent during free play");
  }

  msg.check_size_t<G_ExchangePhotonCrystals_BB_6xDF>();
  auto s = c->require_server_state();
  auto p = c->character_file();
  size_t index = p->inventory.find_item_by_primary_identifier(0x03100200);
  auto item = p->remove_item(p->inventory.items[index].data.id, 1, *s->item_stack_limits(c->version()));
  send_destroy_item_to_lobby(c, item.id, 1);
  l->drop_mode = ServerDropMode::DISABLED;
  l->allowed_drop_modes = (1 << static_cast<uint8_t>(l->drop_mode)); // DISABLED only
  co_return;
}

asio::awaitable<void> on_quest_F95E_result_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xE0 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xE0 command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xE0 command sent during free play");
  }

  const auto& cmd = msg.check_size_t<G_RequestItemDropFromQuest_BB_6xE0>();
  auto s = c->require_server_state();

  size_t count = (cmd.type > 0x03) ? 1 : (static_cast<size_t>(l->difficulty) + 1);
  for (size_t z = 0; z < count; z++) {
    const auto& results = s->quest_F95E_results.at(cmd.type).at(static_cast<size_t>(l->difficulty));
    if (results.empty()) {
      throw runtime_error("invalid result type");
    }
    ItemData item = (results.size() == 1) ? results[0] : results[l->rand_crypt->next() % results.size()];
    if (item.data1[0] == 0x04) { // Meseta
      // TODO: What is the right amount of Meseta to use here? Presumably it should be random within a certain range,
      // but it's not obvious what that range should be.
      item.data2d = 100;
    } else if (item.data1[0] == 0x00) {
      item.data1[4] |= 0x80; // Unidentified
    } else {
      item.enforce_stack_size_limits(*s->item_stack_limits(c->version()));
    }

    item.id = l->generate_item_id(0xFF);
    l->add_item(cmd.floor, item, cmd.pos, nullptr, nullptr, 0x100F);

    send_drop_stacked_item_to_lobby(l, item, cmd.floor, cmd.pos);
  }
  co_return;
}

asio::awaitable<void> on_quest_F95F_result_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xE1 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xE1 command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xE1 command sent during free play");
  }

  const auto& cmd = msg.check_size_t<G_ExchangePhotonTickets_BB_6xE1>();
  auto s = c->require_server_state();
  auto p = c->character_file();

  const auto& result = s->quest_F95F_results.at(cmd.result_index);
  if (result.second.empty()) {
    throw runtime_error("invalid result index");
  }

  const auto& limits = *s->item_stack_limits(c->version());

  bool failed = false;
  ItemData ticket_item;
  try {
    size_t index = p->inventory.find_item_by_primary_identifier(0x03100400); // Photon Ticket
    ticket_item = p->remove_item(p->inventory.items[index].data.id, result.first, limits);
  } catch (const out_of_range&) {
    failed = true;
  }
  if (failed) {
    send_gallon_plan_result(c, cmd.failure_label, cmd.result_code_reg, 1, cmd.result_index_reg, cmd.result_index);
    co_return;
  }

  ItemData new_item = result.second;
  try {
    new_item.enforce_stack_size_limits(limits);
    new_item.id = l->generate_item_id(c->lobby_client_id);
    p->add_item(new_item, limits);
  } catch (const out_of_range&) {
    failed = true;
  }
  if (failed) {
    p->add_item(ticket_item, limits);
    send_gallon_plan_result(c, cmd.failure_label, cmd.result_code_reg, 2, cmd.result_index_reg, cmd.result_index);
    co_return;
  }

  // Note: It seems Sega used 6xDB here; we use 6x29 instead.
  send_destroy_item_to_lobby(c, ticket_item.id, result.first);
  send_create_inventory_item_to_lobby(c, c->lobby_client_id, new_item);
  send_gallon_plan_result(c, cmd.success_label, cmd.result_code_reg, 0, cmd.result_index_reg, cmd.result_index);
  co_return;
}

asio::awaitable<void> on_quest_F960_result_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xE2 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xE2 command sent in non-game lobby");
  }

  const auto& cmd = msg.check_size_t<G_GetMesetaSlotPrize_BB_6xE2>();
  auto s = c->require_server_state();
  auto p = c->character_file();

  time_t t_secs = phosg::now() / 1000000;
  struct tm t_parsed;
#ifndef PHOSG_WINDOWS
  gmtime_r(&t_secs, &t_parsed);
#else
  gmtime_s(&t_parsed, &t_secs);
#endif
  size_t weekday = t_parsed.tm_wday;

  ItemData item;
  for (size_t num_failures = 0; num_failures <= cmd.result_tier; num_failures++) {
    size_t tier = cmd.result_tier - num_failures;
    const auto& results = s->quest_F960_success_results.at(tier);
    uint64_t probability = results.base_probability + num_failures * results.probability_upgrade;
    if (l->rand_crypt->next() <= probability) {
      c->log.info_f("Tier {} yielded a prize", tier);
      const auto& result_items = results.results.at(weekday);
      item = result_items[l->rand_crypt->next() % result_items.size()];
      break;
    } else {
      c->log.info_f("Tier {} did not yield a prize", tier);
    }
  }
  if (item.empty()) {
    c->log.info_f("Choosing result from failure tier");
    const auto& result_items = s->quest_F960_failure_results.results.at(weekday);
    item = result_items[l->rand_crypt->next() % result_items.size()];
  }
  if (item.empty()) {
    throw runtime_error("no item produced, even from failure tier");
  }

  // The client sends a 6xC9 to remove Meseta before sending 6xE2, so we don't have to deal with Meseta here.

  item.id = l->generate_item_id(c->lobby_client_id);
  // If it's a weapon, make it unidentified
  auto item_parameter_table = s->item_parameter_table(c->version());
  if ((item.data1[0] == 0x00) && (item_parameter_table->is_item_rare(item) || (item.data1[4] != 0))) {
    item.data1[4] |= 0x80;
  }

  // The 6xE3 handler on the client fails if the item already exists, so we need to send 6xE3 before we call
  // send_create_inventory_item_to_lobby.
  G_SetMesetaSlotPrizeResult_BB_6xE3 cmd_6xE3 = {
      {0xE3, sizeof(G_SetMesetaSlotPrizeResult_BB_6xE3) >> 2, c->lobby_client_id}, item};
  send_command_t(c, 0x60, 0x00, cmd_6xE3);

  // Add the item to the player's inventory if possible; if not, drop it on the floor where the player is standing
  bool added_to_inventory;
  try {
    p->add_item(item, *s->item_stack_limits(c->version()));
    added_to_inventory = true;
  } catch (const out_of_range&) {
    // If the game's drop mode is private or duplicate, make the item visible only to this player; in other modes, make
    // it visible to everyone
    uint16_t flags = ((l->drop_mode == ServerDropMode::SERVER_PRIVATE) || (l->drop_mode == ServerDropMode::SERVER_DUPLICATE))
        ? (1 << c->lobby_client_id)
        : 0x000F;
    l->add_item(c->floor, item, cmd.pos, nullptr, nullptr, flags);
    added_to_inventory = false;
  }

  if (c->log.should_log(phosg::LogLevel::L_INFO)) {
    string name = s->describe_item(c->version(), item);
    c->log.info_f("Awarded item {} {}", name, added_to_inventory ? "in inventory" : "on ground (inventory is full)");
  }
  if (added_to_inventory) {
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);
  } else {
    send_drop_item_to_channel(s, c->channel, item, 0, cmd.floor, cmd.pos, 0xFFFF);
  }
  co_return;
}

asio::awaitable<void> on_momoka_item_exchange_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xD9 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xD9 command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xD9 command sent during free play");
  }

  // See notes in CommandFormats.hh about why we allow larger commands here
  const auto& cmd = msg.check_size_t<G_MomokaItemExchange_BB_6xD9>(0xFFFF);
  auto s = c->require_server_state();
  auto p = c->character_file();

  const auto& limits = *s->item_stack_limits(c->version());

  ItemData new_item = cmd.replace_item;
  assert_quest_item_create_allowed(l, new_item);
  new_item.enforce_stack_size_limits(limits);

  bool failed = false;
  ItemData found_item;
  try {
    size_t found_index = p->inventory.find_item_by_primary_identifier(cmd.find_item.primary_identifier());
    found_item = p->remove_item(p->inventory.items[found_index].data.id, 1, limits);
  } catch (const std::out_of_range& e) {
    failed = true;
  }
  if (failed) {
    send_command(c, 0x23, 0x01);
    co_return;
  }

  try {
    new_item.id = l->generate_item_id(c->lobby_client_id);
    p->add_item(new_item, limits);
  } catch (const std::out_of_range& e) {
    failed = true;
  }
  if (failed) {
    p->add_item(found_item, limits); // Add found_item back since we're cancelling the exchange
    send_command(c, 0x23, 0x02);
    co_return;
  }

  // Note: It seems Sega used 6xDB here; we use 6x29 instead.
  send_destroy_item_to_lobby(c, found_item.id, 1);
  send_create_inventory_item_to_lobby(c, c->lobby_client_id, new_item);
  send_command(c, 0x23, 0x00);
  co_return;
}

asio::awaitable<void> on_upgrade_weapon_attribute_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xDA command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xDA command sent in non-game lobby");
  }
  if (!l->check_flag(Lobby::Flag::QUEST_IN_PROGRESS) && !l->check_flag(Lobby::Flag::JOINABLE_QUEST_IN_PROGRESS)) {
    throw runtime_error("6xDA command sent during free play");
  }

  const auto& cmd = msg.check_size_t<G_UpgradeWeaponAttribute_BB_6xDA>();
  auto s = c->require_server_state();
  auto p = c->character_file();
  try {
    size_t item_index = p->inventory.find_item(cmd.item_id);
    auto& item = p->inventory.items[item_index].data;
    if (item.is_s_rank_weapon()) {
      throw std::runtime_error("6xDA command sent for ES weapon");
    }

    uint32_t payment_primary_identifier = cmd.payment_type ? 0x03100100 : 0x03100000;
    size_t payment_index = p->inventory.find_item_by_primary_identifier(payment_primary_identifier);
    auto& payment_item = p->inventory.items[payment_index].data;
    if (payment_item.stack_size(*s->item_stack_limits(c->version())) < cmd.payment_count) {
      throw runtime_error("not enough payment items present");
    }
    p->remove_item(payment_item.id, cmd.payment_count, *s->item_stack_limits(c->version()));
    send_destroy_item_to_lobby(c, payment_item.id, cmd.payment_count);

    uint8_t attribute_amount = 0;
    if (cmd.payment_type == 1 && cmd.payment_count == 1) {
      attribute_amount = 30;
    } else if (cmd.payment_type == 0 && cmd.payment_count == 4) {
      attribute_amount = 1;
    } else if (cmd.payment_type == 0 && cmd.payment_count == 20) {
      attribute_amount = 5;
    } else {
      throw runtime_error("unknown PD/PS expenditure");
    }

    size_t attribute_index = 0;
    for (size_t z = 6; z <= (item.has_kill_count() ? 10 : 8); z += 2) {
      if ((item.data1[z] == 0) || (!(item.data1[z] & 0x80) && (item.data1[z] == cmd.attribute))) {
        attribute_index = z;
        break;
      }
    }
    if (attribute_index == 0) {
      throw runtime_error("no available attribute slots");
    }
    item.data1[attribute_index] = cmd.attribute;
    item.data1[attribute_index + 1] += attribute_amount;

    send_destroy_item_to_lobby(c, item.id, 1);
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);
    send_quest_function_call(c, cmd.success_label);

  } catch (const exception& e) {
    c->log.warning_f("Weapon attribute upgrade failed: {}", e.what());
    send_quest_function_call(c, cmd.failure_label);
  }
  co_return;
}

asio::awaitable<void> on_write_quest_counter_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_SetQuestCounter_BB_6xD2>();
  c->character_file()->quest_counters[cmd.index] = cmd.value;
  co_return;
}
