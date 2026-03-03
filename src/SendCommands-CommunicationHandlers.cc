#include "SendCommands.hh"

#include <format>
#include <stdexcept>

#include "GameServer.hh"

#include <phosg/Time.hh>

using namespace std;

enum class ColorMode {
  NONE,
  ADD,
  STRIP,
};

static void send_text(
    std::shared_ptr<Channel> ch,
    phosg::StringWriter& w,
    uint16_t command,
    uint32_t flag,
    const string& text,
    ColorMode color_mode) {
  bool is_w = uses_utf16(ch->version);
  if (ch->version == Version::DC_NTE) {
    color_mode = ColorMode::STRIP;
  }

  try {
    switch (color_mode) {
      case ColorMode::NONE:
        w.write(tt_encode_marked_optional(text, ch->language, is_w));
        break;
      case ColorMode::ADD:
        w.write(tt_encode_marked_optional(add_color(text), ch->language, is_w));
        break;
      case ColorMode::STRIP:
        w.write(tt_encode_marked_optional(strip_color(text), ch->language, is_w));
        break;
    }
  } catch (const runtime_error& e) {
    phosg::log_warning_f("Failed to encode message for {:02X} command: {}", command, e.what());
    return;
  }

  if (is_w) {
    w.put_u16(0);
  } else {
    w.put_u8(0);
  }

  while (w.str().size() & 3) {
    w.put_u8(0);
  }
  ch->send(command, flag, w.str());
}

static void send_text(
    std::shared_ptr<Channel> ch, uint16_t command, uint32_t flag, const string& text, ColorMode color_mode) {
  phosg::StringWriter w;
  send_text(ch, w, command, flag, text, color_mode);
}

static void send_header_text(
    std::shared_ptr<Channel> ch, uint16_t command, uint32_t guild_card_number, const string& text, ColorMode color_mode) {
  phosg::StringWriter w;
  w.put(SC_TextHeader_01_06_11_B0_EE({0, guild_card_number}));
  send_text(ch, w, command, 0x00, text, color_mode);
}

void send_message_box(shared_ptr<Client> c, const string& text) {
  if (is_v4(c->version())) {
    phosg::StringWriter w;
    try {
      w.write(tt_encode_marked_optional(add_color(text), c->language(), true));
    } catch (const runtime_error& e) {
      phosg::log_warning_f("Failed to encode text for message box command: {}", e.what());
      return;
    }
    w.put_u16(0);
    while (w.str().size() & 3) {
      w.put_u8(0);
    }
    send_command(c, (w.size() <= 0x400) ? 0x1A : 0xD5, 0x00, w.str());

  } else {
    uint16_t command;
    switch (c->version()) {
      case Version::PC_PATCH:
      case Version::BB_PATCH:
        command = 0x13;
        break;
      case Version::DC_NTE:
      case Version::DC_11_2000:
      case Version::DC_V1:
      case Version::DC_V2:
      case Version::PC_NTE:
      case Version::PC_V2:
        command = 0x1A;
        break;
      case Version::GC_NTE:
      case Version::GC_V3:
      case Version::GC_EP3_NTE:
      case Version::GC_EP3:
      case Version::XB_V3:
        command = 0xD5;
        break;
      case Version::BB_V4:
        throw std::logic_error("BB not handled before version switch");
      default:
        throw logic_error("invalid game version");
    }
    send_text(c->channel, command, 0x00, text, ColorMode::ADD);
  }
}

void send_ep3_timed_message_box(std::shared_ptr<Channel> ch, uint32_t frames, const string& message) {
  string encoded;
  try {
    encoded = tt_encode_marked(add_color(message), ch->language, false);
  } catch (const runtime_error& e) {
    phosg::log_warning_f("Failed to encode message for EA command: {}", e.what());
    return;
  }
  phosg::StringWriter w;
  w.put<S_TimedMessageBoxHeader_Ep3_EA>({frames});
  w.write(encoded);
  w.put_u8(0);
  while (w.size() & 3) {
    w.put_u8(0);
  }
  ch->send(0xEA, 0x00, w.str());
}

void send_lobby_name(shared_ptr<Client> c, const string& text) {
  send_text(c->channel, 0x8A, 0x00, text, ColorMode::NONE);
}

void send_quest_info(shared_ptr<Client> c, const string& text, uint8_t description_flag, bool is_download_quest) {
  send_text(c->channel, is_download_quest ? 0xA5 : 0xA3, description_flag, text, ColorMode::ADD);
}

void send_lobby_message_box(shared_ptr<Client> c, const string& text, bool left_side_on_bb) {
  uint16_t command = (left_side_on_bb && (c->version() == Version::BB_V4)) ? 0x0101 : 0x0001;
  send_header_text(c->channel, command, 0, text, ColorMode::ADD);
}

void send_ship_info(shared_ptr<Client> c, const string& text) {
  send_header_text(c->channel, 0x11, 0, text, ColorMode::ADD);
}

void send_ship_info(std::shared_ptr<Channel> ch, const string& text) {
  send_header_text(ch, 0x11, 0, text, ColorMode::ADD);
}

void send_text_message(std::shared_ptr<Channel> ch, const string& text) {
  if ((ch->version != Version::DC_NTE) && (ch->version != Version::DC_11_2000)) {
    send_header_text(ch, 0xB0, 0, text, ColorMode::ADD);
  }
}

void send_text_message(shared_ptr<Client> c, const string& text) {
  if ((c->version() != Version::DC_NTE) && (c->version() != Version::DC_11_2000)) {
    send_header_text(c->channel, 0xB0, 0, text, ColorMode::ADD);
  }
}

void send_text_message(shared_ptr<Lobby> l, const string& text) {
  for (size_t x = 0; x < l->max_clients; x++) {
    if (l->clients[x]) {
      send_text_message(l->clients[x], text);
    }
  }
}

void send_text_message(shared_ptr<ServerState> s, const string& text) {
  for (auto& c : s->game_server->all_clients()) {
    if (c->login && !is_patch(c->version())) {
      send_text_message(c, text);
    }
  }
}

void send_scrolling_message_bb(shared_ptr<Client> c, const string& text) {
  if (c->version() != Version::BB_V4) {
    throw logic_error("cannot send scrolling message to non-BB player");
  }
  send_header_text(c->channel, 0x00EE, 0, text, ColorMode::ADD);
}

void send_text_or_scrolling_message(shared_ptr<Client> c, const string& text, const string& scrolling) {
  if (is_v4(c->version())) {
    send_scrolling_message_bb(c, scrolling);
  } else {
    send_text_message(c, text);
  }
}

void send_text_or_scrolling_message(
    std::shared_ptr<Lobby> l, std::shared_ptr<Client> exclude_c, const std::string& text, const std::string& scrolling) {
  for (const auto& lc : l->clients) {
    if (!lc || (lc == exclude_c)) {
      continue;
    }
    send_text_or_scrolling_message(lc, text, scrolling);
  }
}

void send_text_or_scrolling_message(shared_ptr<ServerState> s, const std::string& text, const std::string& scrolling) {
  for (const auto& c : s->game_server->all_clients()) {
    if (c->login && !is_patch(c->version())) {
      send_text_or_scrolling_message(c, text, scrolling);
    }
  }
}

string prepare_chat_data(
    Version version,
    Language language,
    uint8_t from_client_id,
    const string& from_name,
    const string& text,
    char private_flags) {
  string data;

  if (version == Version::BB_V4) {
    data.append((language == Language::JAPANESE) ? "\tJ" : "\tE");
  }
  data.append(from_name);
  if (version == Version::DC_NTE) {
    data.append(std::format(">{:X}", from_client_id));
  } else {
    data.append(1, '\t');
  }
  if (private_flags) {
    data.append(1, static_cast<uint16_t>(private_flags));
  }

  if (uses_utf16(version)) {
    data.append((language == Language::JAPANESE) ? "\tJ" : "\tE");
    data.append(text);
    return tt_utf8_to_utf16(data);
  } else if (version == Version::DC_NTE) {
    data.append(tt_utf8_to_sega_sjis(text));
    return data;
  } else {
    data.append(tt_encode_marked(text, language, false));
    return data;
  }
}

void send_chat_message_from_client(std::shared_ptr<Channel> ch, const string& text, char private_flags) {
  if (private_flags != 0) {
    if (!is_ep3(ch->version)) {
      throw runtime_error("nonzero private_flags in non-GC chat message");
    }
    string effective_text;
    effective_text.push_back(private_flags);
    effective_text += text;
    send_header_text(ch, 0x06, 0, effective_text, ColorMode::NONE);
  } else {
    send_header_text(ch, 0x06, 0, text, ColorMode::NONE);
  }
}

void send_prepared_chat_message(shared_ptr<Client> c, uint32_t from_guild_card_number, const string& prepared_data) {
  phosg::StringWriter w;
  w.put(SC_TextHeader_01_06_11_B0_EE{0, from_guild_card_number});
  w.write(prepared_data);
  w.put_u8(0);
  if (uses_utf16(c->version())) {
    w.put_u8(0);
  }
  while (w.size() & 3) {
    w.put_u8(0);
  }
  send_command(c, 0x06, 0x00, w.str());
}

void send_prepared_chat_message(shared_ptr<Lobby> l, uint32_t from_guild_card_number, const string& prepared_data) {
  for (auto c : l->clients) {
    if (c) {
      send_prepared_chat_message(c, from_guild_card_number, prepared_data);
    }
  }
}

void send_chat_message(
    shared_ptr<Client> c,
    uint32_t from_guild_card_number,
    const string& from_name,
    const string& text,
    char private_flags) {
  string prepared_data = prepare_chat_data(
      c->version(), c->language(), c->lobby_client_id, from_name, text, private_flags);
  send_prepared_chat_message(c, from_guild_card_number, prepared_data);
}

template <typename CmdT>
void send_simple_mail_t(shared_ptr<Client> c, uint32_t from_guild_card_number, const string& from_name, const string& text) {
  CmdT cmd;
  cmd.player_tag = from_guild_card_number ? 0x00010000 : 0;
  cmd.from_guild_card_number = from_guild_card_number;
  cmd.from_name.encode(from_name, c->language());
  cmd.to_guild_card_number = c->login->account->account_id;
  cmd.text.encode(text, c->language());
  send_command_t(c, 0x81, 0x00, cmd);
}

void send_simple_mail_bb(shared_ptr<Client> c, uint32_t from_guild_card_number, const string& from_name, const string& text) {
  SC_SimpleMail_BB_81 cmd;
  cmd.player_tag = from_guild_card_number ? 0x00010000 : 0;
  cmd.from_guild_card_number = from_guild_card_number;
  cmd.from_name.encode(from_name, c->language());
  cmd.to_guild_card_number = c->login->account->account_id;
  cmd.received_date.encode(phosg::format_time(phosg::now()), c->language());
  cmd.text.encode(text, c->language());
  send_command_t(c, 0x81, 0x00, cmd);
}

void send_simple_mail(shared_ptr<Client> c, uint32_t from_guild_card_number, const string& from_name, const string& text) {
  switch (c->version()) {
    case Version::DC_NTE:
    case Version::DC_11_2000:
    case Version::DC_V1:
    case Version::DC_V2:
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
    case Version::XB_V3:
      send_simple_mail_t<SC_SimpleMail_DC_V3_81>(c, from_guild_card_number, from_name, text);
      break;
    case Version::PC_NTE:
    case Version::PC_V2:
      send_simple_mail_t<SC_SimpleMail_PC_81>(c, from_guild_card_number, from_name, text);
      break;
    case Version::BB_V4:
      send_simple_mail_bb(c, from_guild_card_number, from_name, text);
      break;
    default:
      throw logic_error("unimplemented versioned command");
  }
}

void send_simple_mail(shared_ptr<ServerState> s, uint32_t from_guild_card_number, const string& from_name, const string& text) {
  for (const auto& c : s->game_server->all_clients()) {
    if (c->login && !is_patch(c->version())) {
      send_simple_mail(c, from_guild_card_number, from_name, text);
    }
  }
}

template <TextEncoding NameEncoding, TextEncoding MessageEncoding>
void send_info_board_t(shared_ptr<Client> c) {
  vector<S_InfoBoardEntryT_D8<NameEncoding, MessageEncoding>> entries;
  auto l = c->require_lobby();
  for (const auto& other_c : l->clients) {
    if (!other_c.get()) {
      continue;
    }
    auto other_p = other_c->character_file(true, false);
    auto& e = entries.emplace_back();
    e.name.encode(other_p->disp.name.decode(other_p->inventory.language), c->language());
    e.message.encode(add_color(other_p->info_board.decode(other_p->inventory.language)), c->language());
  }
  send_command_vt(c, 0xD8, entries.size(), entries);
}

void send_info_board(shared_ptr<Client> c) {
  if (uses_utf16(c->version())) {
    send_info_board_t<TextEncoding::UTF16, TextEncoding::UTF16>(c);
  } else {
    send_info_board_t<TextEncoding::ASCII, TextEncoding::MARKED>(c);
  }
}

template <typename CmdT>
void send_choice_search_choices_t(shared_ptr<Client> c) {
  vector<CmdT> entries;
  for (const auto& cat : CHOICE_SEARCH_CATEGORIES) {
    auto& cat_e = entries.emplace_back();
    cat_e.parent_choice_id = 0;
    cat_e.choice_id = cat.id;
    cat_e.text.encode(cat.name, c->language());
    for (const auto& choice : cat.choices) {
      auto& e = entries.emplace_back();
      e.parent_choice_id = cat.id;
      e.choice_id = choice.id;
      e.text.encode(choice.name, c->language());
    }
  }
  send_command_vt(c, 0xC0, entries.size(), entries);
}

void send_choice_search_choices(shared_ptr<Client> c) {
  switch (c->version()) {
      // DC V1 and the prototypes do not support this command
    case Version::DC_V2:
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
    case Version::XB_V3:
      send_choice_search_choices_t<S_ChoiceSearchEntry_DC_V3_C0>(c);
      break;
    case Version::PC_NTE:
    case Version::PC_V2:
    case Version::BB_V4:
      send_choice_search_choices_t<S_ChoiceSearchEntry_PC_BB_C0>(c);
      break;
    default:
      throw logic_error("unimplemented versioned command");
  }
}

template <typename CommandHeaderT, TextEncoding Encoding>
void send_card_search_result_t(shared_ptr<Client> c, shared_ptr<Client> result, shared_ptr<Lobby> result_lobby) {
  auto s = c->require_server_state();

  S_GuildCardSearchResultT<CommandHeaderT, Encoding> cmd;
  cmd.player_tag = 0x00010000;
  cmd.searcher_guild_card_number = c->login->account->account_id;
  cmd.result_guild_card_number = result->login->account->account_id;
  cmd.reconnect_command_header.size = sizeof(cmd.reconnect_command_header) + sizeof(cmd.reconnect_command);
  cmd.reconnect_command_header.command = 0x19;
  cmd.reconnect_command_header.flag = 0x00;
  cmd.reconnect_command.address = s->connect_address_for_client(c);
  cmd.reconnect_command.port = s->game_server_port_for_version(c->version());
  cmd.reconnect_command.unused = 0;

  string location_string;
  if (result_lobby->is_game()) {
    location_string = std::format("{},,BLOCK01,{}", result_lobby->name, s->name);
  } else if (result_lobby->is_ep3()) {
    location_string = std::format("BLOCK01-C{:02},,BLOCK01,{}", result_lobby->lobby_id - 15, s->name);
  } else {
    location_string = std::format("BLOCK01-{:02},,BLOCK01,{}", result_lobby->lobby_id, s->name);
  }
  cmd.location_string.encode(location_string, c->language());
  cmd.extension.lobby_refs[0].menu_id = MenuID::LOBBY;
  cmd.extension.lobby_refs[0].item_id = result_lobby->lobby_id;
  auto rp = result->character_file(true, false);
  cmd.extension.player_name.encode(rp->disp.name.decode(rp->inventory.language), c->language());

  send_command_t(c, 0x41, 0x00, cmd);
}

void send_card_search_result(shared_ptr<Client> c, shared_ptr<Client> result, shared_ptr<Lobby> result_lobby) {
  switch (c->version()) {
    case Version::DC_NTE:
    case Version::DC_11_2000:
    case Version::DC_V1:
    case Version::DC_V2:
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
    case Version::XB_V3:
      send_card_search_result_t<PSOCommandHeaderDCV3, TextEncoding::SJIS>(c, result, result_lobby);
      break;
    case Version::PC_NTE:
    case Version::PC_V2:
      send_card_search_result_t<PSOCommandHeaderPC, TextEncoding::UTF16>(c, result, result_lobby);
      break;
    case Version::BB_V4:
      send_card_search_result_t<PSOCommandHeaderBB, TextEncoding::UTF16>(c, result, result_lobby);
      break;
    default:
      throw logic_error("unimplemented versioned command");
  }
}
