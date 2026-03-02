#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

void set_console_client_flags(shared_ptr<Client> c, uint32_t sub_version);

asio::awaitable<void> on_89(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  c->lobby_arrow_color = msg.flag;
  auto l = c->lobby.lock();
  if (l) {
    send_arrow_update(l);
  }
  co_return;
}

asio::awaitable<void> on_40(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_GuildCardSearch_40>(msg.data);
  try {
    auto s = c->require_server_state();
    auto result = s->find_client(nullptr, cmd.target_guild_card_number);
    if (!result->blocked_senders.count(c->login->account->account_id)) {
      auto result_lobby = result->lobby.lock();
      if (result_lobby) {
        send_card_search_result(c, result, result_lobby);
      }
    }
  } catch (const out_of_range&) {
  }
  co_return;
}

asio::awaitable<void> on_C0(shared_ptr<Client> c, Channel::Message& msg) {
  (void)msg;
  send_choice_search_choices(c);
  co_return;
}

asio::awaitable<void> on_C2(shared_ptr<Client> c, Channel::Message& msg) {
  c->character_file()->choice_search_config = check_size_t<ChoiceSearchConfig>(msg.data);
  co_return;
}

template <typename ResultT>
static void on_choice_search_t(shared_ptr<Client> c, const ChoiceSearchConfig& cmd) {
  auto s = c->require_server_state();

  vector<ResultT> results;
  for (const auto& l : s->all_lobbies()) {
    for (const auto& lc : l->clients) {
      if (!lc || !lc->login || lc->character_file()->choice_search_config.disabled) {
        continue;
      }

      bool is_match = true;
      for (const auto& cat : CHOICE_SEARCH_CATEGORIES) {
        int32_t setting = cmd.get_setting(cat.id);
        if (setting == -1) {
          continue;
        }
        try {
          if (!cat.client_matches(c, lc, setting)) {
            is_match = false;
            break;
          }
        } catch (const exception& e) {
          c->log.info_f("Error in Choice Search matching for category {}: {}", cat.name, e.what());
        }
      }

      if (is_match) {
        auto lp = lc->character_file();
        auto& result = results.emplace_back();
        result.guild_card_number = lc->login->account->account_id;
        result.name.encode(lp->disp.name.decode(lc->language()), c->language());
        string info_string = std::format("{} Lv{}\n{}\n",
            name_for_char_class(lp->disp.visual.char_class),
            static_cast<size_t>(lp->disp.stats.level + 1),
            name_for_section_id(lp->disp.visual.section_id));
        result.info_string.encode(info_string, c->language());
        string location_string;
        if (l->is_game()) {
          location_string = std::format("{},,BLOCK01,{}", l->name, s->name);
        } else if (l->is_ep3()) {
          location_string = std::format("BLOCK01-C{:02},,BLOCK01,{}", l->lobby_id - 15, s->name);
        } else {
          location_string = std::format("BLOCK01-{:02},,BLOCK01,{}", l->lobby_id, s->name);
        }
        result.location_string.encode(location_string, c->language());
        result.reconnect_command_header.command = 0x19;
        result.reconnect_command_header.flag = 0x00;
        result.reconnect_command_header.size = sizeof(result.reconnect_command) + sizeof(result.reconnect_command_header);
        result.reconnect_command.address = s->connect_address_for_client(c);
        result.reconnect_command.port = s->game_server_port_for_version(c->version());
        result.meet_user.lobby_refs[0].menu_id = MenuID::LOBBY;
        result.meet_user.lobby_refs[0].item_id = l->lobby_id;
        result.meet_user.player_name.encode(lp->disp.name.decode(lc->language()), c->language());
        // The client can only handle 32 results
        if (results.size() >= 0x20) {
          break;
        }
      }
    }
  }

  if (results.empty()) {
    // There is a client bug that causes garbage to appear in the info window when the server returns no entries in
    // this command, since the client tries to display the first entry in the list even if the list contains "No
    // player". If the server sends no entries at all, the entry will uninitialized memory which can cause crashes on
    // v2, so we send a blank aentry to prevent this.
    auto& result = results.emplace_back();
    result.reconnect_command_header.command = 0x00;
    result.reconnect_command_header.flag = 0x00;
    result.reconnect_command_header.size = 0x0000;
    send_command_vt(c, 0xC4, 0, results);
  } else {
    send_command_vt(c, 0xC4, results.size(), results);
  }
}

asio::awaitable<void> on_C3(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<ChoiceSearchConfig>(msg.data);
  switch (c->version()) {
      // DC V1 and the prototypes do not support this command
    case Version::DC_V2:
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
    case Version::XB_V3:
      on_choice_search_t<S_ChoiceSearchResultEntry_DC_V3_C4>(c, cmd);
      break;
    case Version::PC_NTE:
    case Version::PC_V2:
      on_choice_search_t<S_ChoiceSearchResultEntry_PC_C4>(c, cmd);
      break;
    case Version::BB_V4:
      on_choice_search_t<S_ChoiceSearchResultEntry_BB_C4>(c, cmd);
      break;
    default:
      throw runtime_error("unimplemented versioned command");
  }
  co_return;
}

asio::awaitable<void> on_81(shared_ptr<Client> c, Channel::Message& msg) {
  string message;
  uint32_t to_guild_card_number;
  switch (c->version()) {
    case Version::DC_NTE:
    case Version::DC_11_2000:
    case Version::DC_V1:
    case Version::DC_V2:
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
    case Version::XB_V3: {
      const auto& cmd = check_size_t<SC_SimpleMail_DC_V3_81>(msg.data);
      to_guild_card_number = cmd.to_guild_card_number;
      message = cmd.text.decode(c->language());
      break;
    }
    case Version::PC_NTE:
    case Version::PC_V2: {
      const auto& cmd = check_size_t<SC_SimpleMail_PC_81>(msg.data);
      to_guild_card_number = cmd.to_guild_card_number;
      message = cmd.text.decode(c->language());
      break;
    }
    case Version::BB_V4: {
      const auto& cmd = check_size_t<SC_SimpleMail_BB_81>(msg.data);
      to_guild_card_number = cmd.to_guild_card_number;
      message = cmd.text.decode(c->language());
      break;
    }
    default:
      throw logic_error("invalid game version");
  }

  auto s = c->require_server_state();
  shared_ptr<Client> target;
  try {
    target = s->find_client(nullptr, to_guild_card_number);
  } catch (const out_of_range&) {
  }

  if (!target || !target->login) {
    // TODO: We should store pending messages for accounts somewhere, and send them when the player signs on again.
    if (!c->blocked_senders.count(to_guild_card_number)) {
      try {
        auto target_account = s->account_index->from_account_id(to_guild_card_number);
        if (!target_account->auto_reply_message.empty()) {
          send_simple_mail(
              c, target_account->account_id, target_account->last_player_name, target_account->auto_reply_message);
        }
      } catch (const AccountIndex::missing_account&) {
      }
    }
    send_text_message(c, "$C6Player is offline");

  } else {
    // If the sender is blocked, don't forward the mail
    if (target->blocked_senders.count(c->login->account->account_id)) {
      co_return;
    }

    // If the target has auto-reply enabled, send the autoreply. Note that we also forward the message in this case.
    if (!c->blocked_senders.count(target->login->account->account_id)) {
      auto target_p = target->character_file();
      if (!target_p->auto_reply.empty()) {
        send_simple_mail(
            c,
            target->login->account->account_id,
            target_p->disp.name.decode(target_p->inventory.language),
            target_p->auto_reply.decode(target_p->inventory.language));
      }
    }

    // Forward the message
    send_simple_mail(
        target, c->login->account->account_id, c->character_file()->disp.name.decode(c->language()), message);
  }
}

asio::awaitable<void> on_8A(shared_ptr<Client> c, Channel::Message& msg) {
  if (c->version() == Version::DC_NTE) {
    const auto& cmd = check_size_t<C_ConnectionInfo_DCNTE_8A>(msg.data);
    c->hardware_id = cmd.hardware_id;
    set_console_client_flags(c, cmd.sub_version);
    send_command(c, 0x8A, 0x01);

  } else {
    check_size_v(msg.data.size(), 0);
    auto l = c->lobby.lock();
    if (!l) {
      if (c->check_flag(Client::Flag::AWAITING_ENABLE_B2_QUEST)) {
        send_lobby_name(c, "");
      } else {
        throw std::runtime_error("received 8A command from client not in any lobby");
      }
    } else {
      send_lobby_name(c, l->name);
    }
  }
  co_return;
}
