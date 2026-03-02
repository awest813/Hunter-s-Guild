#include "ShellCommands.hh"

#include <phosg/Strings.hh>

#include "ChatCommands.hh"
#include "ReceiveCommands.hh"
#include "SendCommands.hh"

using namespace std;

ShellCommand c_cc(
    "cc", "cc COMMAND\n\
    Execute a chat command as if a client had sent it in-game. The command\n\
    should be specified exactly as it would be typed in-game; for example:\n\
      cc $itemnotifs rare\n\
    This command cannot send chat messages to other players or to the server\n\
    (in proxy sessions); it can only execute chat commands. Chat commands run\n\
    via this command are exempt from permission checks, so commands that\n\
    require cheat mode or debug mode are always available via cc even if the\n\
    player cannot normamlly use them.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto c = args.get_client();
      co_await on_chat_command(c, args.args, false);
      co_return deque<string>{};
    });

asio::awaitable<deque<string>> f_sc_ss(ShellCommand::Args& args) {
  string data = phosg::parse_data_string(args.args, nullptr, phosg::ParseDataFlags::ALLOW_FILES);
  if (data.size() == 0) {
    throw invalid_argument("no data given");
  }
  data.resize((data.size() + 3) & (~3));

  auto c = args.get_client();
  if (args.command[1] == 's') {
    if (c->proxy_session) {
      send_command_with_header(c->proxy_session->server_channel, data.data(), data.size());
    } else {
      co_await on_command_with_header(c, data);
    }
  } else {
    send_command_with_header(c->channel, data.data(), data.size());
  }

  co_return deque<string>{};
}

ShellCommand c_sc("sc", "sc DATA\n\
    Send a network command to the client.",
    f_sc_ss);
ShellCommand c_ss("ss", "ss DATA\n\
    Send a network command to the server.",
    f_sc_ss);
