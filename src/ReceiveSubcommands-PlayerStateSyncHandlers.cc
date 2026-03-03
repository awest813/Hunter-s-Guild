#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

shared_ptr<Client> get_sync_target(shared_ptr<Client> sender_c, uint8_t command, uint8_t flag, bool allow_if_not_loading);

asio::awaitable<void> on_sync_joining_player_disp_and_inventory(
    shared_ptr<Client> c, SubcommandMessage& msg) {
  auto s = c->require_server_state();

  // In V1/V2 games, this command sometimes is sent after the new client has finished loading, so we don't check
  // l->any_client_loading() here.
  auto target = get_sync_target(c, msg.command, msg.flag, true);
  if (!target) {
    co_return;
  }

  // If the sender is the leader and is pre-V1, and the target is V1 or later, we need to synthesize a 6x71 command to
  // tell the target all state has been sent. (If both are pre-V1, the target won't expect this command; if both are V1
  // or later, the leader will send this command itself.)
  Version target_v = target->version();
  Version c_v = c->version();
  if (is_pre_v1(c_v) && !is_pre_v1(target_v)) {
    static const be_uint32_t data = 0x71010000;
    send_command(target, 0x62, target->lobby_client_id, &data, sizeof(data));
  }

  bool is_client_customisation = c->check_flag(Client::Flag::IS_CLIENT_CUSTOMIZATION);
  switch (c_v) {
    case Version::DC_NTE:
      c->last_reported_6x70.reset(new Parsed6x70Data(
          msg.check_size_t<G_SyncPlayerDispAndInventory_DCNTE_6x70>(),
          c->login->account->account_id, c_v, is_client_customisation));
      c->last_reported_6x70->clear_dc_protos_unused_item_fields();
      break;
    case Version::DC_11_2000:
      c->last_reported_6x70.reset(new Parsed6x70Data(
          msg.check_size_t<G_SyncPlayerDispAndInventory_DC112000_6x70>(),
          c->login->account->account_id, c->language(), c_v, is_client_customisation));
      c->last_reported_6x70->clear_dc_protos_unused_item_fields();
      break;
    case Version::DC_V1:
    case Version::DC_V2:
    case Version::PC_NTE:
    case Version::PC_V2:
      c->last_reported_6x70.reset(new Parsed6x70Data(
          msg.check_size_t<G_SyncPlayerDispAndInventory_DC_PC_6x70>(),
          c->login->account->account_id, c_v, is_client_customisation));
      if (c_v == Version::DC_V1) {
        c->last_reported_6x70->clear_v1_unused_item_fields();
      }
      break;
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
      c->last_reported_6x70.reset(new Parsed6x70Data(
          msg.check_size_t<G_SyncPlayerDispAndInventory_GC_6x70>(),
          c->login->account->account_id, c_v, is_client_customisation));
      break;
    case Version::XB_V3:
      c->last_reported_6x70.reset(new Parsed6x70Data(
          msg.check_size_t<G_SyncPlayerDispAndInventory_XB_6x70>(),
          c->login->account->account_id, c_v, is_client_customisation));
      break;
    case Version::BB_V4:
      c->last_reported_6x70.reset(new Parsed6x70Data(
          msg.check_size_t<G_SyncPlayerDispAndInventory_BB_6x70>(),
          c->login->account->account_id, c_v, is_client_customisation));
      break;
    default:
      throw logic_error("6x70 command from unknown game version");
  }

  c->pos = c->last_reported_6x70->base.pos;
  send_game_player_state(target, c, false);
}
