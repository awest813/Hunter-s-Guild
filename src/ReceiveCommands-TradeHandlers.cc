#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_D0_V3_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<SC_TradeItems_D0_D3>(msg.data);

  if (c->pending_item_trade) {
    throw runtime_error("player started a trade when one is already pending");
  }
  if (cmd.item_count > 0x20) {
    throw runtime_error("invalid item count in trade items command");
  }

  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("trade command received in non-game lobby");
  }
  auto target_c = l->clients.at(cmd.target_client_id);
  if (!target_c) {
    throw runtime_error("trade command sent to missing player");
  }

  c->pending_item_trade = make_unique<Client::PendingItemTrade>();
  c->pending_item_trade->other_client_id = cmd.target_client_id;
  for (size_t x = 0; x < cmd.item_count; x++) {
    auto& item = c->pending_item_trade->items.emplace_back(cmd.item_datas[x]);
    item.decode_for_version(c->version());
  }

  // If the other player has a pending trade as well, assume this is the second half of the trade sequence, and send a
  // D1 to both clients (which should cause them to delete the appropriate inventory items and send D2s). If the other
  // player does not have a pending trade, assume this is the first half of the trade sequence, and send a D1 only to
  // the target player (to request its D0 command). See the description of the D0 command in CommandFormats.hh for more
  // information on how this sequence is supposed to work.
  send_command(target_c, 0xD1, 0x00);
  if (target_c->pending_item_trade) {
    send_command(c, 0xD1, 0x00);
  }
  co_return;
}

asio::awaitable<void> on_D2_V3_BB(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  if (!c->pending_item_trade) {
    throw runtime_error("player executed a trade with none pending");
  }

  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("trade command received in non-game lobby");
  }
  auto target_c = l->clients.at(c->pending_item_trade->other_client_id);
  if (!target_c) {
    throw runtime_error("target player is missing");
  }
  if (!target_c->pending_item_trade) {
    throw runtime_error("player executed a trade with no other side pending");
  }

  auto s = c->require_server_state();
  auto complete_trade_for_side = [s, l](shared_ptr<Client> c, shared_ptr<Client> other_c) -> void {
    if (c->version() == Version::BB_V4) {
      // On BB, the server generates the delete/create item commands
      auto p = c->character_file();
      auto other_p = other_c->character_file();

      // Delete items that are being given away
      for (const auto& item : c->pending_item_trade->items) {
        size_t amount = item.stack_size(*s->item_stack_limits(c->version()));
        p->remove_item(item.id, amount, *s->item_stack_limits(c->version()));

        // This is a special case: when the trade is executed, the client deletes the traded items from its own
        // inventory automatically, so we should NOT send the 6x29 to that client; we should only send it to the other
        // clients in the game.
        G_DeleteInventoryItem_6x29 cmd = {{0x29, 0x03, c->lobby_client_id}, item.id, amount};
        for (auto lc : l->clients) {
          if (lc && (lc != c)) {
            send_command_t(l, 0x60, 0x00, cmd);
          }
        }
      }

      for (const auto& trade_item : other_c->pending_item_trade->items) {
        ItemData added_item = trade_item;
        added_item.id = l->generate_item_id(c->lobby_client_id);
        p->add_item(added_item, *s->item_stack_limits(c->version()));
        send_create_inventory_item_to_lobby(c, c->lobby_client_id, added_item);
      }
      send_command(c, 0xD3, 0x00);

    } else {
      // On V3, the client will handle it; we just have to forward the other client's trade list
      send_execute_item_trade(c, other_c->pending_item_trade->items);
    }

    send_command(c, 0xD4, 0x01);
  };

  c->pending_item_trade->confirmed = true;
  if (target_c->pending_item_trade->confirmed) {
    complete_trade_for_side(c, target_c);
    complete_trade_for_side(target_c, c);
    c->pending_item_trade.reset();
    target_c->pending_item_trade.reset();
  }
  co_return;
}

asio::awaitable<void> on_D4_V3_BB(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  // Annoyingly, if the other client disconnects at a certain point during the trade sequence, the client can get into
  // a state where it sends this command many times in a row. To deal with this, we just do nothing if the client has
  // no trade pending.
  if (!c->pending_item_trade) {
    co_return;
  }
  uint8_t other_client_id = c->pending_item_trade->other_client_id;
  c->pending_item_trade.reset();
  send_command(c, 0xD4, 0x00);

  // Cancel the other side of the trade too, if it's open
  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("trade command received in non-game lobby");
  }
  auto target_c = l->clients.at(other_client_id);
  if (!target_c || !target_c->pending_item_trade) {
    co_return;
  }
  target_c->pending_item_trade.reset();
  send_command(target_c, 0xD4, 0x00);
}

asio::awaitable<void> on_EE_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  if (!is_ep3(c->version())) {
    throw runtime_error("non-Ep3 client sent card trade command");
  }
  auto l = c->require_lobby();
  if (!l->is_game() || !l->is_ep3()) {
    throw runtime_error("client sent card trade command outside of Ep3 game");
  }

  if (msg.flag == 0xD0) {
    auto& cmd = check_size_t<SC_TradeCards_Ep3_EE_FlagD0_FlagD3>(msg.data);

    if (c->pending_card_trade) {
      throw runtime_error("player started a card trade when one is already pending");
    }
    if (cmd.entry_count > 4) {
      throw runtime_error("invalid entry count in card trade command");
    }

    auto target_c = l->clients.at(cmd.target_client_id);
    if (!target_c) {
      throw runtime_error("card trade command sent to missing player");
    }
    if (!is_ep3(target_c->version())) {
      throw runtime_error("card trade target is not Episode 3");
    }

    c->pending_card_trade = make_unique<Client::PendingCardTrade>();
    c->pending_card_trade->other_client_id = cmd.target_client_id;
    for (size_t x = 0; x < cmd.entry_count; x++) {
      c->pending_card_trade->card_to_count.emplace_back(
          make_pair(cmd.entries[x].card_type, cmd.entries[x].count));
    }

    // If the other player has a pending trade as well, assume this is the second half of the trade sequence, and send
    // an EE D1 to both clients. If the other player does not have a pending trade, assume this is the first half of
    // the trade sequence, and send an EE D1 only to the target player (to request its EE D0 command). See the
    // description of the D0 command in CommandFormats.hh for more information on how this sequence is supposed to
    // work. (The EE D0 command is analogous to Episodes 1&2's D0 command.)
    S_AdvanceCardTradeState_Ep3_EE_FlagD1 resp = {0};
    send_command_t(target_c, 0xEE, 0xD1, resp);
    if (target_c->pending_card_trade) {
      send_command_t(c, 0xEE, 0xD1, resp);
    }

  } else if (msg.flag == 0xD2) {
    check_size_v(msg.data.size(), 0);

    if (!c->pending_card_trade) {
      throw runtime_error("player executed a card trade with none pending");
    }

    auto target_c = l->clients.at(c->pending_card_trade->other_client_id);
    if (!target_c) {
      throw runtime_error("card trade target player is missing");
    }
    if (!target_c->pending_card_trade) {
      throw runtime_error("player executed a card trade with no other side pending");
    }

    c->pending_card_trade->confirmed = true;
    if (target_c->pending_card_trade->confirmed) {
      send_execute_card_trade(c, target_c->pending_card_trade->card_to_count);
      send_execute_card_trade(target_c, c->pending_card_trade->card_to_count);
      S_CardTradeComplete_Ep3_EE_FlagD4 resp = {1};
      send_command_t(c, 0xEE, 0xD4, resp);
      send_command_t(target_c, 0xEE, 0xD4, resp);
      c->pending_card_trade.reset();
      target_c->pending_card_trade.reset();
    }

  } else if (msg.flag == 0xD4) {
    check_size_v(msg.data.size(), 0);

    // See the D4 handler for why this check exists (and why it doesn't throw)
    if (!c->pending_card_trade) {
      co_return;
    }
    uint8_t other_client_id = c->pending_card_trade->other_client_id;
    c->pending_card_trade.reset();
    S_CardTradeComplete_Ep3_EE_FlagD4 resp = {0};
    send_command_t(c, 0xEE, 0xD4, resp);

    // Cancel the other side of the trade too, if it's open
    auto target_c = l->clients.at(other_client_id);
    if (!target_c || !target_c->pending_card_trade) {
      co_return;
    }
    target_c->pending_card_trade.reset();
    send_command_t(target_c, 0xEE, 0xD4, resp);

  } else {
    throw runtime_error("invalid card trade operation");
  }
}

asio::awaitable<void> on_EF_Ep3(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  if (!is_ep3(c->version())) {
    throw runtime_error("non-Ep3 client sent card auction request");
  }
  auto l = c->require_lobby();
  if (!l->is_game() || !l->is_ep3()) {
    throw runtime_error("client sent card auction request outside of Ep3 game");
  }

  send_ep3_card_auction(l);
  co_return;
}
