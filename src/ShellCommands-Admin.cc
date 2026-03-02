#include "ShellCommands.hh"

#include <phosg/Strings.hh>

#include "SendCommands.hh"

using namespace std;

ShellCommand c_lookup(
    "lookup", "lookup USER\n\
    Find the account for a logged-in user.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto target = args.s->find_client(&args.args);
      if (target->login) {
        co_return deque<string>{format("Found client {} with account ID {:08X}",
            target->channel->name, target->login->account->account_id)};
      } else {
        // This should be impossible
        throw logic_error("find_client found user who is not logged in");
      }
    });
ShellCommand c_kick(
    "kick", "kick USER\n\
    Disconnect a user from the server. USER may be an account ID, player name,\n\
    or client ID (beginning with \"C-\"). This does not ban the user; they are\n\
    free to reconnect after doing this.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto target = args.s->find_client(&args.args);
      send_message_box(target, "$C6You have been kicked off the server.");
      target->channel->disconnect();
      co_return deque<string>{format("Client C-{:X} disconnected from server", target->id)};
    });

ShellCommand c_announce(
    "announce", "announce MESSAGE\n\
    Send an announcement message to all players.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      send_text_or_scrolling_message(args.s, args.args, args.args);
      co_return deque<string>{};
    });
ShellCommand c_announce_mail(
    "announce-mail", "announce-mail MESSAGE\n\
    Send an announcement message via Simple Mail to all players.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      send_simple_mail(args.s, 0, args.s->name, args.args);
      co_return deque<string>{};
    });
