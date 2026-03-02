#include "ReceiveSubcommands-Impl.hh"

#include <unordered_set>

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

asio::awaitable<void> on_ep3_trade_card_counts(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (c->version() == Version::GC_EP3_NTE) {
    msg.check_size_t<G_CardCounts_Ep3NTE_6xBC>(0xFFFF);
  } else {
    msg.check_size_t<G_CardCounts_Ep3_6xBC>(0xFFFF);
  }

  if (!command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game() || !l->is_ep3()) {
    co_return;
  }
  auto target = l->clients.at(msg.flag);
  if (!target || !target->check_flag(Client::Flag::EP3_ALLOW_6xBC)) {
    co_return;
  }

  forward_subcommand(c, msg);
}

asio::awaitable<void> on_send_guild_card(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!command_is_private(msg.command) || (msg.flag >= l->max_clients) || (!l->clients[msg.flag])) {
    co_return;
  }

  switch (c->version()) {
    case Version::DC_NTE: {
      const auto& cmd = msg.check_size_t<G_SendGuildCard_DCNTE_6x06>();
      c->character_file(true, false)->guild_card.description.encode(cmd.guild_card.description.decode(c->language()), c->language());
      break;
    }
    case Version::DC_11_2000:
    case Version::DC_V1:
    case Version::DC_V2: {
      const auto& cmd = msg.check_size_t<G_SendGuildCard_DC_6x06>();
      c->character_file(true, false)->guild_card.description.encode(cmd.guild_card.description.decode(c->language()), c->language());
      break;
    }
    case Version::PC_NTE:
    case Version::PC_V2: {
      const auto& cmd = msg.check_size_t<G_SendGuildCard_PC_6x06>();
      c->character_file(true, false)->guild_card.description = cmd.guild_card.description;
      break;
    }
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3: {
      const auto& cmd = msg.check_size_t<G_SendGuildCard_GC_6x06>();
      c->character_file(true, false)->guild_card.description.encode(cmd.guild_card.description.decode(c->language()), c->language());
      break;
    }
    case Version::XB_V3: {
      const auto& cmd = msg.check_size_t<G_SendGuildCard_XB_6x06>();
      c->character_file(true, false)->guild_card.description.encode(cmd.guild_card.description.decode(c->language()), c->language());
      break;
    }
    case Version::BB_V4:
      // Nothing to do... the command is blank; the server generates the guild card to be sent
      break;
    default:
      throw logic_error("unsupported game version");
  }

  send_guild_card(l->clients[msg.flag], c);
}

asio::awaitable<void> on_symbol_chat(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_SymbolChat_6x07>();
  if (c->can_chat && (cmd.client_id == c->lobby_client_id)) {
    forward_subcommand(c, msg);
  }
  co_return;
}

template <bool SenderBE>
static asio::awaitable<void> on_word_select_t(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_WordSelectT_6x74<SenderBE>>();
  if (c->can_chat && (cmd.client_id == c->lobby_client_id)) {
    if (command_is_private(msg.command)) {
      co_return;
    }

    auto s = c->require_server_state();
    auto l = c->require_lobby();
    if (l->battle_record && l->battle_record->battle_in_progress()) {
      l->battle_record->add_command(Episode3::BattleRecord::Event::Type::GAME_COMMAND, msg.data, msg.size);
    }

    unordered_set<shared_ptr<Client>> target_clients;
    for (const auto& lc : l->clients) {
      if (lc) {
        target_clients.emplace(lc);
      }
    }
    for (const auto& watcher_l : l->watcher_lobbies) {
      for (const auto& lc : watcher_l->clients) {
        if (lc) {
          target_clients.emplace(lc);
        }
      }
    }
    target_clients.erase(c);

    // In non-Ep3 lobbies, Ep3 uses the Ep1&2 word select table.
    bool is_non_ep3_lobby = (l->episode != Episode::EP3);

    Version from_version = c->version();
    if (is_non_ep3_lobby && is_ep3(from_version)) {
      from_version = Version::GC_V3;
    }
    for (const auto& lc : target_clients) {
      try {
        Version lc_version = lc->version();
        if (is_non_ep3_lobby && is_ep3(lc_version)) {
          lc_version = Version::GC_V3;
        }

        uint8_t subcommand;
        if (lc->version() == Version::DC_NTE) {
          subcommand = 0x62;
        } else if (lc->version() == Version::DC_11_2000) {
          subcommand = 0x69;
        } else {
          subcommand = 0x74;
        }

        if (is_big_endian(lc->version())) {
          G_WordSelectBE_6x74 out_cmd = {
              subcommand, cmd.size, cmd.client_id.load(),
              s->word_select_table->translate(cmd.message, from_version, lc_version)};
          send_command_t(lc, 0x60, 0x00, out_cmd);
        } else {
          G_WordSelect_6x74 out_cmd = {
              subcommand, cmd.size, cmd.client_id.load(),
              s->word_select_table->translate(cmd.message, from_version, lc_version)};
          send_command_t(lc, 0x60, 0x00, out_cmd);
        }

      } catch (const exception& e) {
        string name = escape_player_name(c->character_file()->disp.name.decode(c->language()));
        lc->log.warning_f("Untranslatable Word Select message: {}", e.what());
        send_text_message_fmt(lc, "$C4Untranslatable Word\nSelect message from\n{}", name);
      }
    }
  }
}

asio::awaitable<void> on_word_select(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (is_pre_v1(c->version())) {
    // The Word Select command is a different size in final vs. NTE and proto, so handle that here by appending
    // FFFFFFFF0000000000000000
    string effective_data(reinterpret_cast<const char*>(msg.data), msg.size);
    effective_data.resize(0x20, 0x00);
    effective_data[0x01] = 0x08;
    effective_data[0x14] = 0xFF;
    effective_data[0x15] = 0xFF;
    effective_data[0x16] = 0xFF;
    effective_data[0x17] = 0xFF;
    SubcommandMessage translated_msg{msg.command, msg.flag, effective_data.data(), effective_data.size()};
    co_await on_word_select_t<false>(c, translated_msg);
  } else if (is_big_endian(c->version())) {
    co_await on_word_select_t<true>(c, msg);
  } else {
    co_await on_word_select_t<false>(c, msg);
  }
}
