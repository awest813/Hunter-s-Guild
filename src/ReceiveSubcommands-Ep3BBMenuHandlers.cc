#include "ReceiveSubcommands-Impl.hh"

#include <phosg/Random.hh>

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

asio::awaitable<void> on_ep3_battle_subs(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& header = msg.check_size_t<G_CardBattleCommandHeader>(0xFFFF);

  auto s = c->require_server_state();
  auto l = c->require_lobby();
  if (!l->is_game() || !l->is_ep3()) {
    co_return;
  }

  if (c->version() != Version::GC_EP3_NTE) {
    set_mask_for_ep3_game_command(msg.data, msg.size, 0);
  } else {
    // Ep3 NTE sends uninitialized data in this field; clear it so we know the command isn't masked
    msg.check_size_t<G_CardBattleCommandHeader>(0xFFFF).mask_key = 0;
  }

  if (header.subsubcommand == 0x1A) {
    co_return;
  } else if (header.subsubcommand == 0x20) {
    const auto& cmd = msg.check_size_t<G_Unknown_Ep3_6xB5x20>();
    if (cmd.client_id >= 12) {
      co_return;
    }
  } else if (header.subsubcommand == 0x31) {
    const auto& cmd = msg.check_size_t<G_ConfirmDeckSelection_Ep3_6xB5x31>();
    if (cmd.menu_type >= 0x15) {
      co_return;
    }
  } else if (header.subsubcommand == 0x32) {
    const auto& cmd = msg.check_size_t<G_MoveSharedMenuCursor_Ep3_6xB5x32>();
    if (cmd.menu_type >= 0x15) {
      co_return;
    }
  } else if (header.subsubcommand == 0x36) {
    const auto& cmd = msg.check_size_t<G_RecreatePlayer_Ep3_6xB5x36>();
    if (l->is_game() && (cmd.client_id >= 4)) {
      co_return;
    }
  } else if (header.subsubcommand == 0x38) {
    c->set_flag(Client::Flag::EP3_ALLOW_6xBC);
  } else if (header.subsubcommand == 0x3C) {
    c->clear_flag(Client::Flag::EP3_ALLOW_6xBC);
  }

  for (const auto& lc : l->clients) {
    if (!lc || (lc == c)) {
      continue;
    }
    if (!(s->ep3_behavior_flags & Episode3::BehaviorFlag::DISABLE_MASKING) && (lc->version() != Version::GC_EP3_NTE)) {
      set_mask_for_ep3_game_command(msg.data, msg.size, (phosg::random_object<uint32_t>() % 0xFF) + 1);
    }
    send_command(lc, 0xC9, 0x00, msg.data, msg.size);
  }
}

asio::awaitable<void> on_open_shop_bb_or_ep3_battle_subs(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("6xB5 command sent in non-game lobby");
  }

  if (is_ep3(c->version())) {
    co_await on_ep3_battle_subs(c, msg);
  } else if (l->episode == Episode::EP3) { // There's no item_creator in an Ep3 game
    throw runtime_error("received BB shop subcommand in Ep3 game");
  } else if (c->version() != Version::BB_V4) {
    throw runtime_error("received BB shop subcommand from non-BB client");
  } else {
    const auto& cmd = msg.check_size_t<G_ShopContentsRequest_BB_6xB5>();
    auto s = c->require_server_state();
    size_t level = c->character_file()->disp.stats.level + 1;
    switch (cmd.shop_type) {
      case 0:
        c->bb_shop_contents[0] = l->item_creator->generate_tool_shop_contents(level);
        break;
      case 1:
        c->bb_shop_contents[1] = l->item_creator->generate_weapon_shop_contents(level);
        break;
      case 2: {
        Episode episode = episode_for_area(l->area_for_floor(c->version(), 0));
        c->bb_shop_contents[2] = l->item_creator->generate_armor_shop_contents(episode, level);
        break;
      }
      default:
        throw runtime_error("invalid shop type");
    }
    for (auto& item : c->bb_shop_contents[cmd.shop_type]) {
      item.id = 0xFFFFFFFF;
      item.data2d = s->item_parameter_table(c->version())->price_for_item(item);
    }

    send_shop(c, cmd.shop_type);
  }
}

bool validate_6xBB(G_SyncCardTradeServerState_Ep3_6xBB& cmd) {
  if ((cmd.header.client_id >= 4) || (cmd.slot > 1)) {
    return false;
  }

  // TTradeCardServer uses 4 to indicate the slot is empty, so we allow 4 in the client ID checks below
  switch (cmd.what) {
    case 1:
      if (cmd.args[0] >= 5) {
        return false;
      }
      cmd.args[1] = 0;
      cmd.args[2] = 0;
      cmd.args[3] = 0;
      break;
    case 0:
    case 2:
    case 4:
      cmd.args.clear(0);
      break;
    case 3:
      if (cmd.args[0] >= 5 || cmd.args[1] >= 5) {
        return false;
      }
      cmd.args[2] = 0;
      cmd.args[3] = 0;
      break;
    default:
      return false;
  }
  return true;
}

asio::awaitable<void> on_open_bank_bb_or_card_trade_counter_ep3(
    shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("6xBB command sent in non-game lobby");
  }

  if (c->version() == Version::BB_V4) {
    c->set_flag(Client::Flag::AT_BANK_COUNTER);
    send_bank(c);
  } else if (l->is_ep3() && validate_6xBB(msg.check_size_t<G_SyncCardTradeServerState_Ep3_6xBB>())) {
    forward_subcommand(c, msg);
  }
  co_return;
}

asio::awaitable<void> on_ep3_private_word_select_bb_bank_action(
    shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("6xBD command sent in non-game lobby");
  }

  auto s = c->require_server_state();
  if (is_ep3(c->version())) {
    const auto& cmd = msg.check_size_t<G_PrivateWordSelect_Ep3_6xBD>();
    s->word_select_table->validate(cmd.message, c->version());

    string from_name = c->character_file()->disp.name.decode(c->language());
    static const string whisper_text = "(whisper)";
    auto send_to_client = [&](shared_ptr<Client> lc) -> void {
      if (cmd.private_flags & (1 << lc->lobby_client_id)) {
        try {
          send_chat_message(lc, c->login->account->account_id, from_name, whisper_text, cmd.private_flags);
        } catch (const runtime_error& e) {
          lc->log.warning_f("Failed to encode chat message: {}", e.what());
        }
      } else {
        send_command_t(lc, msg.command, msg.flag, cmd);
      }
    };

    if (command_is_private(msg.command)) {
      if (msg.flag >= l->max_clients) {
        co_return;
      }
      auto target = l->clients[msg.flag];
      if (target) {
        send_to_client(target);
      }
    } else {
      for (auto& lc : l->clients) {
        if (lc && (lc != c) && is_ep3(lc->version())) {
          send_to_client(lc);
        }
      }
    }

    for (const auto& watcher_lobby : l->watcher_lobbies) {
      for (auto& target : watcher_lobby->clients) {
        if (target && is_ep3(target->version())) {
          send_command(target, msg.command, msg.flag, msg.data, msg.size);
        }
      }
    }

    if (l->battle_record && l->battle_record->battle_in_progress()) {
      auto type = ((msg.command & 0xF0) == 0xC0)
          ? Episode3::BattleRecord::Event::Type::EP3_GAME_COMMAND
          : Episode3::BattleRecord::Event::Type::GAME_COMMAND;
      l->battle_record->add_command(type, msg.data, msg.size);
    }

  } else if (c->version() == Version::BB_V4) {
    const auto& cmd = msg.check_size_t<G_BankAction_BB_6xBD>();

    if (!l->is_game()) {
      co_return;
    }

    auto p = c->character_file();
    auto bank = c->bank_file();
    if (cmd.action == 0) { // Deposit
      if (cmd.item_id == 0xFFFFFFFF) { // Deposit Meseta
        if (cmd.meseta_amount > p->disp.stats.meseta) {
          l->log.info_f("Player {} attempted to deposit {} Meseta in the bank, but has only {} Meseta on hand",
              c->lobby_client_id, cmd.meseta_amount, p->disp.stats.meseta);
        } else if ((bank->meseta + cmd.meseta_amount) > bank->max_meseta) {
          l->log.info_f("Player {} attempted to deposit {} Meseta in the bank, but already has {} Meseta in the bank",
              c->lobby_client_id, cmd.meseta_amount, p->disp.stats.meseta);
        } else {
          bank->meseta += cmd.meseta_amount;
          p->disp.stats.meseta -= cmd.meseta_amount;
          l->log.info_f("Player {} deposited {} Meseta in the bank (bank now has {}; inventory now has {})",
              c->lobby_client_id, cmd.meseta_amount, bank->meseta, p->disp.stats.meseta);
        }

      } else { // Deposit item
        const auto& limits = *s->item_stack_limits(c->version());
        auto item = p->remove_item(cmd.item_id, cmd.item_amount, limits);
        // If a stack was split, the bank item retains the same item ID as the inventory item. This is annoying but
        // doesn't cause any problems because we always generate a new item ID when withdrawing from the bank, so
        // there's no chance of conflict later.
        if (item.id == 0xFFFFFFFF) {
          item.id = cmd.item_id;
        }
        bank->add_item(item, limits);
        send_destroy_item_to_lobby(c, cmd.item_id, cmd.item_amount, true);

        if (l->log.should_log(phosg::LogLevel::L_INFO)) {
          string name = s->describe_item(Version::BB_V4, item);
          l->log.info_f("Player {} deposited item {:08X} (x{}) ({}) in the bank",
              c->lobby_client_id, cmd.item_id, cmd.item_amount, name);
          c->print_inventory();
        }
      }

    } else if (cmd.action == 1) { // Take
      if (cmd.item_index == 0xFFFF) { // Take Meseta
        if (cmd.meseta_amount > bank->meseta) {
          l->log.info_f("Player {} attempted to withdraw {} Meseta from the bank, but has only {} Meseta in the bank",
              c->lobby_client_id, cmd.meseta_amount, bank->meseta);
        } else if ((p->disp.stats.meseta + cmd.meseta_amount) > 999999) {
          l->log.info_f("Player {} attempted to withdraw {} Meseta from the bank, but already has {} Meseta on hand",
              c->lobby_client_id, cmd.meseta_amount, p->disp.stats.meseta);
        } else {
          bank->meseta -= cmd.meseta_amount;
          p->disp.stats.meseta += cmd.meseta_amount;
          l->log.info_f("Player {} withdrew {} Meseta from the bank (bank now has {}; inventory now has {})",
              c->lobby_client_id, cmd.meseta_amount, bank->meseta, p->disp.stats.meseta);
        }

      } else { // Take item
        const auto& limits = *s->item_stack_limits(c->version());
        auto item = bank->remove_item(cmd.item_id, cmd.item_amount, limits);
        item.id = l->generate_item_id(c->lobby_client_id);
        p->add_item(item, limits);
        send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);

        if (l->log.should_log(phosg::LogLevel::L_INFO)) {
          string name = s->describe_item(Version::BB_V4, item);
          l->log.info_f("Player {} withdrew item {:08X} (x{}) ({}) from the bank",
              c->lobby_client_id, item.id, cmd.item_amount, name);
          c->print_inventory();
        }
      }

    } else if (cmd.action == 3) { // Leave bank counter
      c->clear_flag(Client::Flag::AT_BANK_COUNTER);
    }
  }
}

static void on_sort_inventory_bb_inner(shared_ptr<Client> c, const SubcommandMessage& msg) {
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xC4 command sent by non-BB client");
  }

  const auto& cmd = msg.check_size_t<G_SortInventory_BB_6xC4>();
  auto p = c->character_file();

  // Make sure the set of item IDs passed in by the client exactly matches the set of item IDs present in the inventory
  unordered_set<uint32_t> sorted_item_ids;
  size_t expected_count = 0;
  for (size_t x = 0; x < 30; x++) {
    if (cmd.item_ids[x] != 0xFFFFFFFF) {
      sorted_item_ids.emplace(cmd.item_ids[x]);
      expected_count++;
    }
  }
  if (sorted_item_ids.size() != expected_count) {
    throw runtime_error("sorted array contains duplicate item IDs");
  }
  if (sorted_item_ids.size() != p->inventory.num_items) {
    throw runtime_error("sorted array contains a different number of items than the inventory contains");
  }
  for (size_t x = 0; x < p->inventory.num_items; x++) {
    if (!sorted_item_ids.erase(cmd.item_ids[x])) {
      throw runtime_error("inventory contains item ID not present in sorted array");
    }
  }
  if (!sorted_item_ids.empty()) {
    throw runtime_error("sorted array contains item ID not present in inventory");
  }

  parray<PlayerInventoryItem, 30> sorted;
  for (size_t x = 0; x < 30; x++) {
    if (cmd.item_ids[x] == 0xFFFFFFFF) {
      sorted[x].data.id = 0xFFFFFFFF;
    } else {
      size_t index = p->inventory.find_item(cmd.item_ids[x]);
      sorted[x] = p->inventory.items[index];
    }
  }
  // It's annoying that extension data is stored in the inventory items array, because we have to be careful to avoid
  // sorting it here too.
  for (size_t x = 0; x < 30; x++) {
    sorted[x].extension_data1 = p->inventory.items[x].extension_data1;
    sorted[x].extension_data2 = p->inventory.items[x].extension_data2;
  }
  p->inventory.items = sorted;
}

asio::awaitable<void> on_sort_inventory_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  // There is a GCC bug that causes this function to not compile properly unless the sorting implementation is in a
  // separate function. I think it's something to do with how it allocates the coroutine's locals, but it's enough to
  // avoid for now.
  on_sort_inventory_bb_inner(c, msg);
  co_return;
}
