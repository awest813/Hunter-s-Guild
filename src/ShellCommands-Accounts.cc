#include "ShellCommands.hh"

#include <phosg/Strings.hh>
#include <phosg/Time.hh>

using namespace std;

ShellCommand c_list_accounts(
    "list-accounts", "list-accounts\n\
    List all accounts registered on the server.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      deque<string> ret;
      auto accounts = args.s->account_index->all();
      if (accounts.empty()) {
        ret.emplace_back("No accounts registered");
      } else {
        for (const auto& a : accounts) {
          ret.emplace_back(a->str());
        }
      }
      co_return ret;
    });

uint32_t parse_account_flags(const string& flags_str) {
  try {
    size_t end_pos = 0;
    uint32_t ret = stoul(flags_str, &end_pos, 16);
    if (end_pos == flags_str.size()) {
      return ret;
    }
  } catch (const exception&) {
  }

  uint32_t ret = 0;
  auto tokens = phosg::split(flags_str, ',');
  for (const auto& token : tokens) {
    string token_upper = phosg::toupper(token);
    if (token_upper == "NONE") {
      // Nothing to do
    } else if (token_upper == "KICK_USER") {
      ret |= static_cast<uint32_t>(Account::Flag::KICK_USER);
    } else if (token_upper == "BAN_USER") {
      ret |= static_cast<uint32_t>(Account::Flag::BAN_USER);
    } else if (token_upper == "SILENCE_USER") {
      ret |= static_cast<uint32_t>(Account::Flag::SILENCE_USER);
    } else if (token_upper == "MODERATOR") {
      ret |= static_cast<uint32_t>(Account::Flag::MODERATOR);
    } else if (token_upper == "CHANGE_EVENT") {
      ret |= static_cast<uint32_t>(Account::Flag::CHANGE_EVENT);
    } else if (token_upper == "ANNOUNCE") {
      ret |= static_cast<uint32_t>(Account::Flag::ANNOUNCE);
    } else if (token_upper == "FREE_JOIN_GAMES") {
      ret |= static_cast<uint32_t>(Account::Flag::FREE_JOIN_GAMES);
    } else if (token_upper == "ADMINISTRATOR") {
      ret |= static_cast<uint32_t>(Account::Flag::ADMINISTRATOR);
    } else if (token_upper == "DEBUG") {
      ret |= static_cast<uint32_t>(Account::Flag::DEBUG);
    } else if (token_upper == "CHEAT_ANYWHERE") {
      ret |= static_cast<uint32_t>(Account::Flag::CHEAT_ANYWHERE);
    } else if (token_upper == "DISABLE_QUEST_REQUIREMENTS") {
      ret |= static_cast<uint32_t>(Account::Flag::DISABLE_QUEST_REQUIREMENTS);
    } else if (token_upper == "ALWAYS_ENABLE_CHAT_COMMANDS") {
      ret |= static_cast<uint32_t>(Account::Flag::ALWAYS_ENABLE_CHAT_COMMANDS);
    } else if (token_upper == "ROOT") {
      ret |= static_cast<uint32_t>(Account::Flag::ROOT);
    } else if (token_upper == "IS_SHARED_ACCOUNT") {
      ret |= static_cast<uint32_t>(Account::Flag::IS_SHARED_ACCOUNT);
    } else {
      throw runtime_error("invalid flag name: " + token_upper);
    }
  }
  return ret;
}

uint32_t parse_account_user_flags(const string& user_flags_str) {
  try {
    size_t end_pos = 0;
    uint32_t ret = stoul(user_flags_str, &end_pos, 16);
    if (end_pos == user_flags_str.size()) {
      return ret;
    }
  } catch (const exception&) {
  }

  uint32_t ret = 0;
  auto tokens = phosg::split(user_flags_str, ',');
  for (const auto& token : tokens) {
    string token_upper = phosg::toupper(token);
    if (token_upper == "NONE") {
      // Nothing to do
    } else if (token_upper == "DISABLE_DROP_NOTIFICATION_BROADCAST") {
      ret |= static_cast<uint32_t>(Account::UserFlag::DISABLE_DROP_NOTIFICATION_BROADCAST);
    } else {
      throw runtime_error("invalid user flag name: " + token_upper);
    }
  }
  return ret;
}

ShellCommand c_add_account(
    "add-account", "add-account [PARAMETERS...]\n\
    Add an account to the server. <parameters> is some subset of:\n\
      id=ACCOUNT-ID: preferred account ID in hex (optional)\n\
      flags=FLAGS: behaviors and permissions for the account (see below)\n\
      user-flags=FLAGS: user-set behaviors for the account\n\
      ep3-current-meseta=MESETA: Episode 3 Meseta value\n\
      ep3-total-meseta=MESETA: Episode 3 total Meseta ever earned\n\
      temporary: marks the account as temporary; it is not saved to disk and\n\
          therefore will be deleted when the server shuts down\n\
    If given, FLAGS is a comma-separated list of zero or more the following:\n\
      NONE: Placeholder if no other flags are specified\n\
      KICK_USER: Can kick other users offline\n\
      BAN_USER: Can ban other users\n\
      SILENCE_USER: Can silence other users\n\
      MODERATOR: Alias for all of the above flags\n\
      CHANGE_EVENT: Can change lobby events\n\
      ANNOUNCE: Can make server-wide announcements\n\
      FREE_JOIN_GAMES: Ignores game restrictions (level/quest requirements)\n\
      ADMINISTRATOR: Alias for all of the above flags (including MODERATOR)\n\
      DEBUG: Can use debugging commands\n\
      CHEAT_ANYWHERE: Can use cheat commands even if cheat mode is disabled\n\
      DISABLE_QUEST_REQUIREMENTS: Can play any quest without progression\n\
          restrictions\n\
      ALWAYS_ENABLE_CHAT_COMMANDS: Can use chat commands even if they are\n\
          disabled in config.json\n\
      ROOT: Alias for all of the above flags (including ADMINISTRATOR)\n\
      IS_SHARED_ACCOUNT: Account is a shared serial (disables Access Key and\n\
          password checks; players will get Guild Cards based on their player\n\
          names)",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto account = make_shared<Account>();
      for (const string& token : phosg::split(args.args, ' ')) {
        if (token.starts_with("id=")) {
          account->account_id = stoul(token.substr(3), nullptr, 16);
        } else if (token.starts_with("ep3-current-meseta=")) {
          account->ep3_current_meseta = stoul(token.substr(19), nullptr, 0);
        } else if (token.starts_with("ep3-total-meseta=")) {
          account->ep3_total_meseta_earned = stoul(token.substr(17), nullptr, 0);
        } else if (token == "temporary") {
          account->is_temporary = true;
        } else if (token.starts_with("flags=")) {
          account->flags = parse_account_flags(token.substr(6));
        } else if (token.starts_with("user-flags=")) {
          account->user_flags = parse_account_user_flags(token.substr(11));
        } else {
          throw invalid_argument("invalid account field: " + token);
        }
      }
      args.s->account_index->add(account);
      account->save();
      co_return deque<string>{format("Account {:08X} added", account->account_id)};
    });
ShellCommand c_update_account(
    "update-account", "update-account ACCOUNT-ID PARAMETERS...\n\
    Update an existing license. ACCOUNT-ID (8 hex digits) specifies which\n\
    account to update. The options are similar to the add-account command:\n\
      flags=FLAGS: Sets behaviors and permissions for the account (same as\n\
          add-account).\n\
      user-flags=FLAGS: Sets behaviors for the account (same as add-account).\n\
      ban-duration=DURATION: bans this account for the specified duration; the\n\
          duration should be of the form 3d, 2w, 1mo, or 1y. If any clients\n\
          are connected with this account when this command is run, they will\n\
          be disconnected.\n\
      unban: Clears any existing ban from this account.\n\
      ep3-current-meseta=MESETA: Sets Episode 3 Meseta value.\n\
      ep3-total-meseta=MESETA: Sets Episode 3 total Meseta ever earned.\n\
      temporary: Marks the account as temporary; it is not saved to disk and\n\
          therefore will be deleted when the server shuts down.\n\
      permanent: If the account was temporary, makes it non-temporary.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto tokens = phosg::split(args.args, ' ');
      if (tokens.size() < 2) {
        throw runtime_error("not enough arguments");
      }
      auto account = args.s->account_index->from_account_id(stoul(tokens[0], nullptr, 16));
      tokens.erase(tokens.begin());

      // Do all the parsing first, then the updates afterward, so we won't partially update the account if parsing a
      // later option fails
      int64_t new_ep3_current_meseta = -1;
      int64_t new_ep3_total_meseta = -1;
      int64_t new_flags = -1;
      int64_t new_user_flags = -1;
      uint8_t new_is_temporary = 0xFF;
      int64_t new_ban_duration = -1;
      for (const string& token : tokens) {
        if (token.starts_with("ep3-current-meseta=")) {
          new_ep3_current_meseta = stoul(token.substr(19), nullptr, 0);
        } else if (token.starts_with("ep3-total-meseta=")) {
          new_ep3_total_meseta = stoul(token.substr(17), nullptr, 0);
        } else if (token == "temporary") {
          new_is_temporary = 1;
        } else if (token == "permanent") {
          new_is_temporary = 0;
        } else if (token.starts_with("flags=")) {
          new_flags = parse_account_flags(token.substr(6));
        } else if (token.starts_with("user-flags=")) {
          new_user_flags = parse_account_user_flags(token.substr(11));
        } else if (token == "unban") {
          new_ban_duration = 0;
        } else if (token.starts_with("ban-duration=")) {
          auto duration_str = token.substr(13);
          if (duration_str.ends_with("s")) {
            new_ban_duration = stoull(duration_str.substr(0, duration_str.size() - 1)) * 1000000LL;
          } else if (duration_str.ends_with("m")) {
            new_ban_duration = stoull(duration_str.substr(0, duration_str.size() - 1)) * 60000000LL;
          } else if (duration_str.ends_with("h")) {
            new_ban_duration = stoull(duration_str.substr(0, duration_str.size() - 1)) * 3600000000LL;
          } else if (duration_str.ends_with("d")) {
            new_ban_duration = stoull(duration_str.substr(0, duration_str.size() - 1)) * 86400000000LL;
          } else if (duration_str.ends_with("w")) {
            new_ban_duration = stoull(duration_str.substr(0, duration_str.size() - 1)) * 604800000000LL;
          } else if (duration_str.ends_with("mo")) {
            new_ban_duration = stoull(duration_str.substr(0, duration_str.size() - 2)) * 2952000000000LL;
          } else if (duration_str.ends_with("y")) {
            new_ban_duration = stoull(duration_str.substr(0, duration_str.size() - 1)) * 31536000000000LL;
          } else {
            throw runtime_error("invalid time unit");
          }
        } else {
          throw invalid_argument("invalid account field: " + token);
        }
      }

      if (new_ban_duration >= 0) {
        account->ban_end_time = phosg::now() + new_ban_duration;
      }
      if (new_ep3_current_meseta >= 0) {
        account->ep3_current_meseta = new_ep3_current_meseta;
      }
      if (new_ep3_total_meseta >= 0) {
        account->ep3_total_meseta_earned = new_ep3_total_meseta;
      }
      if (new_flags >= 0) {
        account->flags = new_flags;
      }
      if (new_user_flags >= 0) {
        account->user_flags = new_user_flags;
      }
      if (new_is_temporary != 0xFF) {
        account->is_temporary = new_is_temporary;
      }

      account->save();
      if (new_ban_duration > 0) {
        args.s->disconnect_all_banned_clients();
      }

      co_return deque<string>{format("Account {:08X} updated", account->account_id)};
    });
ShellCommand c_delete_account(
    "delete-account", "delete-account ACCOUNT-ID\n\
    Delete an account from the server. If a player is online with the deleted\n\
    account, they will not be automatically disconnected.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto account = args.s->account_index->from_account_id(stoul(args.args, nullptr, 16));
      args.s->account_index->remove(account->account_id);
      account->is_temporary = true;
      account->delete_file();
      co_return deque<string>{"Account deleted"};
    });
