#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

void set_console_client_flags(shared_ptr<Client> c, uint32_t sub_version) {
  if (c->channel->crypt_in->type() == PSOEncryption::Type::V2) {
    if (sub_version <= 0x24) {
      c->channel->version = Version::DC_V1;
      c->log.info_f("Game version changed to DC_V1");
      if (specific_version_is_indeterminate(c->specific_version) ||
          (c->specific_version == SPECIFIC_VERSION_DC_11_2000_PROTOTYPE)) {
        c->specific_version = SPECIFIC_VERSION_DC_V1_INDETERMINATE;
      }
    } else if (sub_version <= 0x28) {
      c->channel->version = Version::DC_V2;
      c->log.info_f("Game version changed to DC_V2");
      if (specific_version_is_indeterminate(c->specific_version)) {
        c->specific_version = SPECIFIC_VERSION_DC_V2_INDETERMINATE;
      }
    } else if (is_v3(c->version())) {
      c->channel->version = Version::GC_NTE;
      c->log.info_f("Game version changed to GC_NTE");
      c->specific_version = SPECIFIC_VERSION_GC_NTE;
    }
  } else {
    if (sub_version >= 0x40 && !is_ep3(c->version())) {
      c->channel->version = Version::GC_EP3;
      c->log.info_f("Game version changed to GC_EP3");
      if (specific_version_is_indeterminate(c->specific_version)) {
        c->specific_version = SPECIFIC_VERSION_GC_EP3_INDETERMINATE;
      }
    }
  }
  c->set_flags_for_version(c->version(), sub_version);
  c->sub_version = sub_version;
  if (specific_version_is_indeterminate(c->specific_version)) {
    c->specific_version = default_specific_version_for_version(c->version(), sub_version);
  }
}

asio::awaitable<void> on_login_complete(shared_ptr<Client> c) {
  auto s = c->require_server_state();

  c->convert_account_to_temporary_if_nte();

  if (!is_v4(c->version())) {
    send_set_guild_card_number(c);
  }

  if (c->check_flag(Client::Flag::CAN_RECEIVE_ENABLE_B2_QUEST) &&
      (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL) ||
          !c->check_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE))) {
    shared_ptr<const Quest> q;
    try {
      q = s->quest_index->get(s->enable_send_function_call_quest_numbers.at(c->specific_version));
    } catch (const out_of_range&) {
    }
    if (!q) {
      c->log.info_f("There is no quest to enable server function calls for specific version {:08X}", c->specific_version);
    } else if (q) {
      auto vq = q->version(c->version(), Language::ENGLISH);
      if (vq) {
        c->set_flag(Client::Flag::HAS_SEND_FUNCTION_CALL);
        c->set_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE);
        c->set_flag(Client::Flag::SEND_FUNCTION_CALL_NO_CACHE_PATCH);
        c->set_flag(Client::Flag::AWAITING_ENABLE_B2_QUEST);

        // PCv2 will crash if it receives an online quest file while it's not in a game, so we have to put it into a
        // fake game first
        if (c->version() == Version::PC_V2) {
          S_JoinGame_PC_64 cmd;
          auto& lobby_data = cmd.lobby_data[0];
          lobby_data.player_tag = 0x00010000;
          lobby_data.guild_card_number = c->login->account->account_id;
          send_command_t(c, 0x64, 0x01, cmd);
        } else {
          c->log.info_f("Sending {} version of quest \"{}\"", name_for_language(vq->meta.language), vq->meta.name);
          string bin_filename = vq->bin_filename();
          string dat_filename = vq->dat_filename();
          string xb_filename = vq->xb_filename();
          send_open_quest_file(
              c, bin_filename, bin_filename, xb_filename, vq->meta.quest_number, QuestFileType::ONLINE, vq->bin_contents);
          send_open_quest_file(
              c, dat_filename, dat_filename, xb_filename, vq->meta.quest_number, QuestFileType::ONLINE, vq->dat_contents);

          if (!is_v1_or_v2(c->version())) {
            send_command(c, 0xAC, 0x00);
          }
        }
      }
    }
  }

  // TODO: It'd be nice to use response_futures to wait for the eventual 84 command instead of using flags and calling
  // start_login_server_procedure in on_84 too.
  if (!c->check_flag(Client::Flag::AWAITING_ENABLE_B2_QUEST)) {
    co_await start_login_server_procedure(c);
  }
  co_return;
}

asio::awaitable<void> on_DB_V3(shared_ptr<Client> c, Channel::Message& msg) {
  if (c->channel->crypt_in->type() == PSOEncryption::Type::V2) {
    throw runtime_error("GC trial edition client sent V3 verify account command");
  }

  const auto& cmd = check_size_t<C_VerifyAccount_V3_DB>(msg.data);
  c->v1_serial_number = cmd.v1_serial_number.decode();
  c->v1_access_key = cmd.v1_access_key.decode();
  c->serial_number = cmd.serial_number.decode();
  c->access_key = cmd.access_key.decode();
  c->hardware_id = cmd.hardware_id;
  set_console_client_flags(c, cmd.sub_version);
  c->serial_number2 = cmd.serial_number2.decode();
  c->access_key2 = cmd.access_key2.decode();
  c->password = cmd.password.decode();

  uint32_t serial_number = stoul(c->serial_number, nullptr, 16);
  try {
    auto s = c->require_server_state();
    c->set_login(s->account_index->from_gc_credentials(
        serial_number, c->access_key, &c->password, "", s->allow_unregistered_users));
    send_command(c, 0x9A, 0x02);

  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_command(c, 0x9A, 0x03);
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_command(c, 0x9A, 0x03);
  } catch (const AccountIndex::incorrect_password& e) {
    c->log.info_f("Login failed (incorrect password)");
    send_command(c, 0x9A, 0x01);
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_command(c, 0x9A, 0x04);
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_command(c, 0x9A, 0x0F);
  }

  if (!c->login) {
    c->channel->disconnect();
  }
  co_return;
}

asio::awaitable<void> on_88_DCNTE(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_Login_DCNTE_88>(msg.data);
  c->serial_number = cmd.serial_number.decode();
  c->access_key = cmd.access_key.decode();
  c->channel->version = Version::DC_NTE;
  c->specific_version = SPECIFIC_VERSION_DC_NTE;
  c->set_flags_for_version(c->version(), -1);
  c->log.info_f("Game version changed to DC_NTE");

  try {
    auto s = c->require_server_state();
    c->set_login(s->account_index->from_dc_nte_credentials(c->serial_number, c->access_key, s->allow_unregistered_users));
    send_command(c, 0x88, 0x00);

  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_message_box(c, "Incorrect serial number");
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_message_box(c, "Incorrect access key");
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_message_box(c, "Incorrect serial number");
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_message_box(c, "Account is banned");
  }

  if (!c->login) {
    c->channel->disconnect();
  }
  co_return;
}

asio::awaitable<void> on_8B_DCNTE(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_Login_DCNTE_8B>(msg.data, sizeof(C_LoginExtended_DCNTE_8B));
  c->channel->version = Version::DC_NTE;
  c->hardware_id = cmd.hardware_id;
  c->sub_version = cmd.sub_version;
  c->channel->language = cmd.language;
  c->serial_number = cmd.serial_number.decode();
  c->access_key = cmd.access_key.decode();
  c->username = cmd.username.decode();
  c->password = cmd.password.decode();
  c->login_character_name = cmd.login_character_name.decode();
  c->set_flags_for_version(c->version(), c->sub_version);
  c->specific_version = SPECIFIC_VERSION_DC_NTE;
  c->log.info_f("Game version changed to DC_NTE");

  try {
    auto s = c->require_server_state();
    c->set_login(s->account_index->from_dc_nte_credentials(c->serial_number, c->access_key, s->allow_unregistered_users));
  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_message_box(c, "Incorrect serial number");
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_message_box(c, "Incorrect access key");
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_message_box(c, "Incorrect serial number");
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_message_box(c, "Account is banned");
  }

  if (!c->login) {
    c->channel->disconnect();
  } else {
    if (cmd.is_extended) {
      const auto& ext_cmd = check_size_t<C_LoginExtended_DCNTE_8B>(msg.data);
      if (ext_cmd.extension.lobby_refs[0].menu_id == MenuID::LOBBY) {
        c->preferred_lobby_id = ext_cmd.extension.lobby_refs[0].item_id;
      }
    }
    co_await on_login_complete(c);
  }
}

asio::awaitable<void> on_90_DC(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_LoginV1_DC_PC_V3_90>(msg.data, 0xFFFF);
  c->serial_number = cmd.serial_number.decode();
  c->access_key = cmd.access_key.decode();
  c->channel->version = Version::DC_V1;
  c->set_flags_for_version(c->version(), -1);
  if (specific_version_is_indeterminate(c->specific_version) ||
      (c->specific_version == SPECIFIC_VERSION_DC_11_2000_PROTOTYPE)) {
    c->specific_version = SPECIFIC_VERSION_DC_V1_INDETERMINATE;
  }
  c->log.info_f("Game version changed to DC_V1");

  uint32_t serial_number = 0;
  try {
    auto s = c->require_server_state();
    if (c->serial_number.size() > 8 || c->access_key.size() > 8) {
      c->set_login(s->account_index->from_dc_nte_credentials(c->serial_number, c->access_key, s->allow_unregistered_users));
    } else {
      serial_number = stoull(c->serial_number, nullptr, 16);
      c->set_login(s->account_index->from_dc_credentials(serial_number, c->access_key, "", s->allow_unregistered_users));
    }
    if (c->log.should_log(phosg::LogLevel::L_INFO)) {
      string login_str = c->login->str();
      c->log.info_f("Received login: {}", login_str);
    }
    send_command(c, 0x90, 0x01);

  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_command(c, 0x90, 0x03);
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_command(c, 0x90, 0x03);
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (no account)");
    send_command(c, 0x90, 0x03);
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_command(c, 0x90, 0x0F);
  }
  if (!c->login) {
    c->channel->disconnect();
  }
  co_return;
}

asio::awaitable<void> on_92_DC(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_RegisterV1_DC_92>(msg.data);
  c->hardware_id = cmd.hardware_id;
  c->channel->language = cmd.language;
  c->serial_number2 = cmd.serial_number2.decode();
  c->access_key2 = cmd.access_key2.decode();
  c->email_address = cmd.email_address.decode();
  // It appears that in response to 90 01, 11/2000 sends 93 rather than 92, so we use the presence of a 92 command to
  // determine that the client is actually DCv1 and not the prototype.
  c->set_flag(Client::Flag::CHECKED_FOR_DC_V1_PROTOTYPE);
  c->channel->version = Version::DC_V1;
  if (specific_version_is_indeterminate(c->specific_version) ||
      (c->specific_version == SPECIFIC_VERSION_DC_11_2000_PROTOTYPE)) {
    c->specific_version = SPECIFIC_VERSION_DC_V1_INDETERMINATE;
  }
  set_console_client_flags(c, cmd.sub_version);
  c->log.info_f("Game version changed to DC_V1");
  send_command(c, 0x92, 0x01);
  co_return;
}

asio::awaitable<void> on_93_DC(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_LoginV1_DC_93>(msg.data, sizeof(C_LoginExtendedV1_DC_93));
  auto s = c->require_server_state();
  c->hardware_id = cmd.hardware_id;
  c->channel->language = cmd.language;
  c->serial_number = cmd.serial_number.decode();
  c->access_key = cmd.access_key.decode();
  c->serial_number2 = cmd.serial_number2.decode();
  c->access_key2 = cmd.access_key2.decode();
  c->login_character_name = cmd.login_character_name.decode();
  if (!c->check_flag(Client::Flag::CHECKED_FOR_DC_V1_PROTOTYPE)) {
    set_console_client_flags(c, cmd.sub_version);
  }

  uint32_t serial_number = 0;
  try {
    if (c->serial_number.size() > 8 || c->access_key.size() > 8) {
      c->set_login(s->account_index->from_dc_nte_credentials(c->serial_number, c->access_key, s->allow_unregistered_users));
    } else {
      serial_number = stoull(c->serial_number, nullptr, 16);
      c->set_login(s->account_index->from_dc_credentials(
          serial_number, c->access_key, c->login_character_name, s->allow_unregistered_users));
    }
    if (c->log.should_log(phosg::LogLevel::L_INFO)) {
      string login_str = c->login->str();
      c->log.info_f("Login: {}", login_str);
    }

  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_message_box(c, "Incorrect serial number");
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_message_box(c, "Incorrect access key");
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (no account)");
    send_message_box(c, "Incorrect serial number");
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_message_box(c, "Account is banned");
  }
  if (!c->login) {
    c->channel->disconnect();
    co_return;
  }

  if (cmd.is_extended) {
    const auto& ext_cmd = check_size_t<C_LoginExtendedV1_DC_93>(msg.data);
    if (ext_cmd.extension.lobby_refs[0].menu_id == MenuID::LOBBY) {
      c->preferred_lobby_id = ext_cmd.extension.lobby_refs[0].item_id;
    }
  }

  // The first time we receive a 93 from a DC client, we set this flag and send a 92. The IS_DC_V1_PROTOTYPE flag will
  // be removed if the client sends a 92 command (which it seems the prototype never does). This is why we always
  // respond with 90 01 here - that's the only case where actual DCv1 sends a 92 command. The IS_DC_V1_PROTOTYPE flag
  // will be removed if the client does indeed send a 92.
  if (!c->check_flag(Client::Flag::CHECKED_FOR_DC_V1_PROTOTYPE)) {
    send_command(c, 0x90, 0x01);
    c->set_flag(Client::Flag::CHECKED_FOR_DC_V1_PROTOTYPE);
    c->channel->version = Version::DC_11_2000;
    if (specific_version_is_indeterminate(c->specific_version)) {
      c->specific_version = SPECIFIC_VERSION_DC_11_2000_PROTOTYPE;
    }
    c->log.info_f("Game version changed to DC_11_2000 (will be changed to V1 if 92 is received)");
  } else {
    co_await on_login_complete(c);
  }
}

asio::awaitable<void> on_9A(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_Login_DC_PC_V3_9A>(msg.data);
  c->v1_serial_number = cmd.v1_serial_number.decode();
  c->v1_access_key = cmd.v1_access_key.decode();
  c->serial_number = cmd.serial_number.decode();
  c->access_key = cmd.access_key.decode();
  c->serial_number2 = cmd.serial_number2.decode();
  c->access_key2 = cmd.access_key2.decode();
  c->email_address = cmd.email_address.decode();
  set_console_client_flags(c, cmd.sub_version);

  try {
    auto s = c->require_server_state();
    switch (c->version()) {
      case Version::DC_V2: {
        uint32_t serial_number = stoul(c->serial_number, nullptr, 16);
        c->set_login(s->account_index->from_dc_credentials(serial_number, c->access_key, "", s->allow_unregistered_users));
        if (c->log.should_log(phosg::LogLevel::L_INFO)) {
          string login_str = c->login->str();
          c->log.info_f("Login: {}", login_str);
        }
        break;
      }
      case Version::PC_NTE:
      case Version::PC_V2: {
        if ((c->sub_version == 0x29) &&
            c->v1_serial_number.empty() &&
            c->v1_access_key.empty() &&
            c->serial_number.empty() &&
            c->access_key.empty() &&
            c->serial_number2.empty() &&
            c->access_key2.empty() &&
            c->email_address.empty()) {
          c->channel->version = Version::PC_NTE;
          c->log.info_f("Changed client version to PC_NTE");
          c->set_login(s->account_index->from_pc_nte_credentials(
              cmd.guild_card_number, s->allow_unregistered_users && s->allow_pc_nte));
        } else {
          uint32_t serial_number = stoul(cmd.serial_number.decode(), nullptr, 16);
          c->set_login(s->account_index->from_pc_credentials(serial_number, c->access_key, "", s->allow_unregistered_users));
        }
        break;
      }
      case Version::GC_NTE:
      case Version::GC_V3:
      case Version::GC_EP3_NTE:
      case Version::GC_EP3: {
        uint32_t serial_number = stoul(c->serial_number, nullptr, 16);
        // On V3, the client should have sent a DB command containing the password already, which should have created
        // an account if needed. So if no account exists at this point, disconnect the client even if unregistered
        // users are allowed.
        c->set_login(s->account_index->from_gc_credentials(serial_number, c->access_key, nullptr, "", false));
        break;
      }
      default:
        throw runtime_error("unsupported versioned command");
    }
    send_command(c, 0x9A, 0x02);

  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_command(c, 0x9A, 0x03);
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_command(c, 0x9A, 0x03);
  } catch (const AccountIndex::incorrect_password& e) {
    c->log.info_f("Login failed (incorrect password)");
    send_command(c, 0x9A, 0x01);
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_command(c, 0x9A, 0x03);
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_command(c, 0x9A, 0x0F);
  }

  if (!c->login) {
    c->channel->disconnect();
  }
  co_return;
}

asio::awaitable<void> on_9C(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_Register_DC_PC_V3_9C>(msg.data);
  auto s = c->require_server_state();

  c->channel->language = cmd.language;
  c->hardware_id = cmd.hardware_id;
  set_console_client_flags(c, cmd.sub_version);

  uint32_t serial_number = stoul(cmd.serial_number.decode(), nullptr, 16);
  try {
    switch (c->version()) {
      case Version::DC_V2:
        c->set_login(s->account_index->from_dc_credentials(serial_number, cmd.access_key.decode(), "", false));
        break;
      case Version::PC_V2:
        c->set_login(s->account_index->from_pc_credentials(serial_number, cmd.access_key.decode(), "", false));
        break;
      case Version::GC_NTE:
      case Version::GC_V3:
      case Version::GC_EP3_NTE:
      case Version::GC_EP3: {
        string password = cmd.password.decode();
        c->set_login(s->account_index->from_gc_credentials(serial_number, cmd.access_key.decode(), &password, "", false));
        break;
      }
      default:
        // TODO: PC_NTE can probably send 9C, but due to the way we've implemented PC_NTE's login sequence, it never
        // should send 9C.
        throw logic_error("unsupported versioned command");
    }
    send_command(c, 0x9C, 0x01);

  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_message_box(c, "Incorrect serial number");
  } catch (const AccountIndex::incorrect_password& e) {
    c->log.info_f("Login failed (incorrect password)");
    send_command(c, 0x9C, 0x00);
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_command(c, 0x9C, 0x00);
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_message_box(c, "Account is banned");
  }
  if (!c->login) {
    c->channel->disconnect();
  }
  co_return;
}

asio::awaitable<void> on_9D_9E(shared_ptr<Client> c, Channel::Message& msg) {
  const C_Login_DC_PC_GC_9D* base_cmd;
  auto s = c->require_server_state();

  if (msg.command == 0x9D) {
    base_cmd = &check_size_t<C_Login_DC_PC_GC_9D>(msg.data, sizeof(C_LoginExtended_PC_9D));
    if (base_cmd->is_extended) {
      if ((c->version() == Version::PC_NTE) || (c->version() == Version::PC_V2)) {
        const auto& cmd = check_size_t<C_LoginExtended_PC_9D>(msg.data);
        if (cmd.extension.lobby_refs[0].menu_id == MenuID::LOBBY) {
          c->preferred_lobby_id = cmd.extension.lobby_refs[0].item_id;
        }
      } else {
        const auto& cmd = check_size_t<C_LoginExtended_DC_GC_9D>(msg.data);
        if (cmd.extension.lobby_refs[0].menu_id == MenuID::LOBBY) {
          c->preferred_lobby_id = cmd.extension.lobby_refs[0].item_id;
        }
      }
    }

  } else if (msg.command == 0x9E) {
    auto handle_cmd = [&]<typename BaseCmdT, typename ExtendedCmdT>() {
      const auto& cmd = check_size_t<BaseCmdT>(msg.data, sizeof(ExtendedCmdT));
      base_cmd = &cmd;
      if (cmd.is_extended) {
        const auto& cmd = check_size_t<ExtendedCmdT>(msg.data);
        if (cmd.extension.lobby_refs[0].menu_id == MenuID::LOBBY) {
          c->preferred_lobby_id = cmd.extension.lobby_refs[0].item_id;
        }
      }
    };
    if (is_v3(c->version())) {
      handle_cmd.template operator()<C_Login_PC_GC_9E, C_LoginExtended_GC_9E>();
      c->set_flag(Client::Flag::AT_WELCOME_MESSAGE);
    } else {
      handle_cmd.template operator()<C_Login_PC_GC_9E, C_LoginExtended_PC_9E>();
    }

  } else {
    throw logic_error("9D/9E handler called for incorrect command");
  }

  c->hardware_id = base_cmd->hardware_id;
  c->channel->language = base_cmd->language;
  c->v1_serial_number = base_cmd->v1_serial_number.decode();
  c->v1_access_key = base_cmd->v1_access_key.decode();
  c->serial_number = base_cmd->serial_number.decode();
  c->access_key = base_cmd->access_key.decode();
  c->serial_number2 = base_cmd->serial_number2.decode();
  c->access_key2 = base_cmd->access_key2.decode();
  c->login_character_name = base_cmd->login_character_name.decode(c->language());
  set_console_client_flags(c, base_cmd->sub_version);

  try {
    switch (c->version()) {
      case Version::DC_V2: {
        uint32_t serial_number = stoul(c->serial_number, nullptr, 16);
        c->set_login(s->account_index->from_dc_credentials(
            serial_number, c->access_key, c->login_character_name, s->allow_unregistered_users));
        break;
      }
      case Version::PC_NTE:
      case Version::PC_V2:
        if ((c->sub_version == 0x29) &&
            c->v1_serial_number.empty() &&
            c->v1_access_key.empty() &&
            c->serial_number.empty() &&
            c->access_key.empty() &&
            c->serial_number2.empty() &&
            c->access_key2.empty()) {
          c->channel->version = Version::PC_NTE;
          c->log.info_f("Changed client version to PC_NTE");
          c->set_login(s->account_index->from_pc_nte_credentials(
              base_cmd->guild_card_number, s->allow_unregistered_users && s->allow_pc_nte));
        } else {
          uint32_t serial_number = stoul(base_cmd->serial_number.decode(), nullptr, 16);
          c->set_login(s->account_index->from_pc_credentials(
              serial_number, c->access_key, c->login_character_name, s->allow_unregistered_users));
        }
        break;
      case Version::GC_NTE:
      case Version::GC_V3:
      case Version::GC_EP3_NTE:
      case Version::GC_EP3: {
        uint32_t serial_number = stoul(base_cmd->serial_number.decode(), nullptr, 16);
        // GC clients should have sent a DB command first which would have
        // created the account if needed
        c->set_login(s->account_index->from_gc_credentials(
            serial_number, c->access_key, nullptr, c->login_character_name, false));
        break;
      }
      default:
        throw logic_error("unsupported versioned command");
    }

  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_command(c, 0x04, 0x03);
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_command(c, 0x04, 0x03);
  } catch (const AccountIndex::incorrect_password& e) {
    c->log.info_f("Login failed (incorrect password)");
    send_command(c, 0x04, 0x06);
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_command(c, 0x04, 0x04);
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_command(c, 0x04, 0x04);
  }

  if (!c->login) {
    c->channel->disconnect();
  } else {
    // On PCv2, we send a B2 command to get the client's specific_version and check whether it has patch support or
    // not; we'll call on_login_complete once we receive the B3 response
    if (c->version() == Version::PC_V2) {
      try {
        auto code = s->function_code_index->name_to_function.at("ReturnTokenX86");
        unordered_map<string, uint32_t> label_writes{{"token", c->login->account->account_id}};
        auto resp = co_await send_function_call(c, code, label_writes, nullptr, 0, 0x00400000, 0x0000E000, 0, true);

        if (!c->login) {
          throw logic_error("received PC_V2 version detect response with no login");
        }
        if (resp.return_value == c->login->account->account_id) {
          // Client already has the patch that enables patches
          c->set_flag(Client::Flag::HAS_SEND_FUNCTION_CALL);
          c->set_flag(Client::Flag::SEND_FUNCTION_CALL_ACTUALLY_RUNS_CODE);
          c->set_flag(Client::Flag::SEND_FUNCTION_CALL_NO_CACHE_PATCH);
        }
        if (resp.checksum == 0x3677024C) {
          c->specific_version = SPECIFIC_VERSION_PC_V2_DEFAULT;
          c->log.info_f("Version detected as {:08X} from PE header checksum {:08X}", c->specific_version, resp.checksum);
        } else if (resp.checksum == 0x058BF2FF) {
          c->specific_version = SPECIFIC_VERSION_PC_V2_FINAL;
          c->log.info_f("Version detected as {:08X} from PE header checksum {:08X}", c->specific_version, resp.checksum);
        } else {
          c->specific_version = SPECIFIC_VERSION_PC_V2_INDETERMINATE;
          c->log.info_f("Version cannot be determined from PE header checksum {:08X}", resp.checksum);
        }
      } catch (const out_of_range&) {
      }
    }
    co_await on_login_complete(c);
  }
}

asio::awaitable<void> on_9E_XB(shared_ptr<Client> c, Channel::Message& msg) {
  auto s = c->require_server_state();

  const auto& cmd = check_size_t<C_Login_XB_9E>(msg.data, sizeof(C_LoginExtended_XB_9E));
  if (cmd.is_extended) {
    const auto& cmd = check_size_t<C_LoginExtended_XB_9E>(msg.data);
    if (cmd.extension.lobby_refs[0].menu_id == MenuID::LOBBY) {
      c->preferred_lobby_id = cmd.extension.lobby_refs[0].item_id;
    }
  }
  c->hardware_id = cmd.hardware_id;
  c->channel->language = cmd.language;
  c->v1_serial_number = cmd.v1_serial_number.decode();
  c->v1_access_key = cmd.v1_access_key.decode();
  c->serial_number = cmd.serial_number.decode();
  c->access_key = cmd.access_key.decode();
  c->serial_number2 = cmd.serial_number2.decode();
  c->access_key2 = cmd.access_key2.decode();
  c->login_character_name = cmd.login_character_name.decode(c->language());
  c->xb_netloc = cmd.xb_netloc;
  c->xb_unknown_a1a = cmd.xb_unknown_a1a;
  c->xb_user_id = (static_cast<uint64_t>(cmd.xb_user_id_high) << 32) | cmd.xb_user_id_low;
  c->xb_unknown_a1b = cmd.xb_unknown_a1b;
  set_console_client_flags(c, cmd.sub_version);

  const string& xb_gamertag = c->serial_number;
  uint64_t xb_user_id = stoull(c->access_key, nullptr, 16);
  uint64_t xb_account_id = cmd.xb_netloc.account_id;
  try {
    c->set_login(s->account_index->from_xb_credentials(xb_gamertag, xb_user_id, xb_account_id, s->allow_unregistered_users));
  } catch (const AccountIndex::no_username& e) {
    c->log.info_f("Login failed (no username)");
    send_command(c, 0x04, 0x03);
  } catch (const AccountIndex::incorrect_access_key& e) {
    c->log.info_f("Login failed (incorrect access key)");
    send_command(c, 0x04, 0x03);
  } catch (const AccountIndex::missing_account& e) {
    c->log.info_f("Login failed (missing account)");
    send_command(c, 0x04, 0x03);
  } catch (const AccountIndex::account_banned& e) {
    c->log.info_f("Login failed (account banned)");
    send_command(c, 0x04, 0x04);
  }

  if (!c->login) {
    c->channel->disconnect();
  } else {
    co_await on_login_complete(c);
  }
  co_return;
}
