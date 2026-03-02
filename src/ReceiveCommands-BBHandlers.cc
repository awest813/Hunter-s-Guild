#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_login_complete(shared_ptr<Client> c);

asio::awaitable<void> on_93_BB(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& base_cmd = check_size_t<C_LoginBase_BB_93>(msg.data, 0xFFFF);
  c->sub_version = base_cmd.sub_version;
  // c->channel->language set after version check
  c->bb_character_index = base_cmd.character_slot;
  c->bb_bank_character_index = base_cmd.character_slot;
  c->bb_connection_phase = base_cmd.connection_phase;
  c->bb_client_code = base_cmd.client_code;
  c->bb_security_token = base_cmd.security_token;
  c->username = base_cmd.username.decode();
  c->password = base_cmd.password.decode();

  if (msg.data.size() == sizeof(C_LoginWithoutHardwareInfo_BB_93)) {
    c->bb_client_config = check_size_t<C_LoginWithoutHardwareInfo_BB_93>(msg.data).client_config;
  } else {
    const auto& full_cmd = check_size_t<C_LoginWithHardwareInfo_BB_93>(msg.data);
    c->hardware_id = full_cmd.hardware_id;
    c->bb_client_config = full_cmd.client_config;
  }
  c->set_flags_for_version(c->version(), base_cmd.sub_version);

  auto s = c->require_server_state();
  try {
    c->set_login(s->account_index->from_bb_credentials(c->username, &c->password, s->allow_unregistered_users));
  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_client_init_bb(c, 0x08);
  } catch (const AccountIndex::incorrect_password& e) {
    c->log.info_f("Login failed (incorrect password)");
    send_client_init_bb(c, 0x03);
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_client_init_bb(c, 0x08);
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_client_init_bb(c, 0x06);
  }
  if (!c->login) {
    c->channel->disconnect();
    co_return;
  }

  string version_string = c->bb_client_config.as_string();
  phosg::strip_trailing_zeroes(version_string);
  // If the version string starts with "Ver.", assume it's Sega and apply the normal version encoding logic. Otherwise,
  // assume it's a community mod, almost all of which are based on TethVer12513, so assume that version.
  if (version_string.starts_with("Ver.")) {
    // Basic algorithm: take all numeric characters from the version string and ignore everything else. Treat that as a
    // decimal integer, then base36-encode it into the low 3 bytes of specific_version.
    uint64_t version = 0;
    for (char ch : version_string) {
      if (isdigit(ch)) {
        version = (version * 10) + (ch - '0');
      }
    }
    uint8_t shift = 0;
    uint32_t specific_version = 0;
    while (version) {
      if (shift > 16) {
        throw runtime_error("invalid version string");
      }
      uint8_t ch = (version % 36) + '0';
      version /= 36;
      if (ch > '9') {
        ch += 7;
      }
      specific_version |= (ch << shift);
      shift += 8;
    }
    if (!(specific_version & 0x00FF0000)) {
      specific_version |= 0x00300000;
    }
    if (!(specific_version & 0x0000FF00)) {
      specific_version |= 0x00003000;
    }
    if (!(specific_version & 0x000000FF)) {
      specific_version |= 0x00000030;
    }
    c->specific_version = 0x35000000 | specific_version;

  } else {
    c->specific_version = 0x35394E4C; // 59NL

    // Note: Tethealla PSOBB is actually Japanese PSOBB, but with most of the files replaced with English
    // text/graphics/etc. For this reason, it still reports its language as Japanese, so we have to account for that
    // manually here.
    if (version_string.starts_with("TethVer")) {
      c->log.info_f("Client is TethVer subtype; forcing English language");
      c->set_flag(Client::Flag::FORCE_ENGLISH_LANGUAGE_BB);
    }
  }
  c->channel->language = c->check_flag(Client::Flag::FORCE_ENGLISH_LANGUAGE_BB) ? Language::ENGLISH : base_cmd.language;

  if (base_cmd.menu_id == MenuID::LOBBY) {
    c->preferred_lobby_id = base_cmd.preferred_lobby_id;
  }

  if (base_cmd.guild_card_number == 0) {
    // There is a (bug? feature?) in the BB client such that it has to receive a reconnect command during the data
    // server phase, or else it won't know where to connect to during character selection. It's not clear why they
    // didn't just make it use the initial connection address by default...
    send_client_init_bb(c, 0);
    send_reconnect(c, s->connect_address_for_client(c), s->name_to_port_config.at("bb-data1")->port);
    co_return;

  } else if (s->proxy_destination_bb.has_value()) {
    // Start a proxy session immediately if there's a destination set. We don't send 00E6 (send_client_init_bb) in this
    // case. This is because the login command is resent to the remote server, and we forward its response back to the
    // client directly.
    const auto& [host, port] = *s->proxy_destination_bb;
    co_await start_proxy_session(c, host, port, c->bb_connection_phase != 0);
    c->proxy_session->remote_client_config_data = c->bb_client_config;
    co_return;

  } else {
    send_client_init_bb(c, 0);
  }

  if (c->bb_connection_phase >= 0x04) {
    // This means the client is done with the data server phase and is in the game server phase; we should send the
    // ship select menu or a lobby join command.
    co_await on_login_complete(c);

  } else if (s->hide_download_commands) {
    // The BB data server protocol is fairly well-understood and has some large commands, so we omit data logging for
    // clients on the data server.
    c->log.info_f("Client is in the BB data server phase; disabling command data logging for the rest of this client\'s session");
    c->channel->terminal_recv_color = phosg::TerminalFormat::END;
    c->channel->terminal_send_color = phosg::TerminalFormat::END;
  }
}

asio::awaitable<void> on_96(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_t<C_CharSaveInfo_DCv2_PC_V3_BB_96>(msg.data);
  c->set_flag(Client::Flag::SHOULD_SEND_ENABLE_SAVE);
  co_return;
}

asio::awaitable<void> on_B1(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);
  send_server_time(c);
  co_return;
}
