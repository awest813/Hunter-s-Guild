#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

void send_main_menu(shared_ptr<Client> c);

asio::awaitable<void> on_10_patch_switches(shared_ptr<Client> c, uint32_t item_id) {
  if (item_id == PatchesMenuItemID::GO_BACK) {
    send_main_menu(c);

  } else {
    if (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL)) {
      throw runtime_error("client does not support send_function_call");
    }

    auto s = c->require_server_state();
    uint64_t key = (static_cast<uint64_t>(item_id) << 32) | c->specific_version;
    auto fn = s->function_code_index->menu_item_id_and_specific_version_to_patch_function.at(key);
    if (!c->login->account->auto_patches_enabled.emplace(fn->short_name).second) {
      c->login->account->auto_patches_enabled.erase(fn->short_name);
    }
    c->login->account->save();
    send_menu(c, s->function_code_index->patch_switches_menu(c->specific_version, s->auto_patches, c->login->account->auto_patches_enabled));
  }
  co_return;
}

asio::awaitable<void> on_10_programs(shared_ptr<Client> c, uint32_t item_id) {
  if (item_id == ProgramsMenuItemID::GO_BACK) {
    send_main_menu(c);

  } else {
    if (!c->check_flag(Client::Flag::HAS_SEND_FUNCTION_CALL)) {
      throw runtime_error("client does not support send_function_call");
    }

    auto s = c->require_server_state();
    auto dol = s->dol_file_index->item_id_to_file.at(item_id);
    co_await send_dol_file(c, dol); // Disconnects the client
  }
}
