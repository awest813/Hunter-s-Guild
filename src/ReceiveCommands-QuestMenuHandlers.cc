#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_10_quest_categories(shared_ptr<Client> c, uint32_t item_id) {
  if (is_ep3(c->version())) {
    auto s = c->require_server_state();
    if (!s->ep3_map_index) {
      send_lobby_message_box(c, "$C7Quests are not available.");
      co_return;
    }
    send_ep3_download_quest_menu(c, item_id);

  } else {
    auto s = c->require_server_state();
    if (!s->quest_index) {
      send_lobby_message_box(c, "$C7Quests are not available.");
      co_return;
    }

    shared_ptr<Lobby> l = c->lobby.lock();
    Episode episode = l ? l->episode : Episode::NONE;
    uint16_t version_flags = (1 << static_cast<size_t>(c->version())) | (l ? l->quest_version_flags() : 0);
    QuestIndex::IncludeCondition include_condition = nullptr;
    if (l && !c->login->account->check_flag(Account::Flag::DISABLE_QUEST_REQUIREMENTS)) {
      include_condition = l->quest_include_condition();
    }

    const auto& quests = s->quest_index->filter(episode, version_flags, item_id, include_condition);
    send_quest_menu(c, quests, !l);
  }
}

asio::awaitable<void> on_10_quest_menu(shared_ptr<Client> c, uint32_t item_id) {
  if (is_ep3(c->version())) {
    throw runtime_error("Episode 1/2/4 quests cannot be downloaded by Ep3 clients");
  }

  auto s = c->require_server_state();
  if (!s->quest_index) {
    send_lobby_message_box(c, "$C7Quests are not\navailable.");
    co_return;
  }
  auto q = s->quest_index->get(item_id);
  if (!q) {
    send_lobby_message_box(c, "$C7Quest does not exist.");
    co_return;
  }

  // If the client is not in a lobby, send it as a download quest; otherwise, they must be in a game to load a quest.
  auto l = c->lobby.lock();
  if (l && !l->is_game()) {
    send_lobby_message_box(c, "$C7Quests cannot be\nloaded in lobbies.");
    co_return;
  }

  if (l) {
    if (q->meta.episode == Episode::EP3) {
      send_lobby_message_box(c, "$C7Episode 3 quests\ncannot be loaded\nvia this interface.");
      co_return;
    }
    if (l->quest) {
      send_lobby_message_box(c, "$C7A quest is already\nin progress.");
      co_return;
    }
    if (l->quest_include_condition()(q) != QuestIndex::IncludeState::AVAILABLE) {
      send_lobby_message_box(c, "$C7This quest has not\nbeen unlocked for\nall players in this\ngame.");
      co_return;
    }
    set_lobby_quest(l, q);

  } else {
    auto vq = q->version(c->version(), c->language());
    if (!vq) {
      send_lobby_message_box(c, "$C7Quest does not exist\nfor this game version.");
      co_return;
    }
    vq = vq->create_download_quest(c->language());
    string xb_filename = vq->xb_filename();
    QuestFileType type = vq->pvr_contents ? QuestFileType::DOWNLOAD_WITH_PVR : QuestFileType::DOWNLOAD_WITHOUT_PVR;
    send_open_quest_file(c, q->meta.name, vq->bin_filename(), xb_filename, vq->meta.quest_number, type, vq->bin_contents);
    send_open_quest_file(c, q->meta.name, vq->dat_filename(), xb_filename, vq->meta.quest_number, type, vq->dat_contents);
    if (vq->pvr_contents) {
      send_open_quest_file(c, q->meta.name, vq->pvr_filename(), xb_filename, vq->meta.quest_number, type, vq->pvr_contents);
    }
  }
}

asio::awaitable<void> on_10_ep3_download_quest_menu(shared_ptr<Client> c, uint32_t item_id) {
  auto s = c->require_server_state();
  if (!is_ep3(c->version())) {
    throw runtime_error("Episode 3 quests can only be downloaded by Ep3 clients");
  }
  if (c->lobby.lock()) {
    throw runtime_error("Episode 3 quests can only be downloaded when client is not in a lobby");
  }

  auto map = s->ep3_map_index->map_for_id(item_id);

  auto vis_flag = (c->version() == Version::GC_EP3_NTE)
      ? Episode3::MapIndex::VisibilityFlag::ONLINE_TRIAL
      : Episode3::MapIndex::VisibilityFlag::ONLINE_FINAL;
  if (!map->check_visibility_flag(vis_flag)) {
    throw runtime_error("map is not visible to this client");
  }

  auto vm = map->version(c->language());
  auto name = vm->map->name.decode(vm->language);
  string filename = std::format("m{:06}p_{:c}.bin", map->map_number, tolower(char_for_language(vm->language)));
  auto data = (c->version() == Version::GC_EP3_NTE) ? vm->trial_download() : vm->compressed(false);
  send_open_quest_file(c, name, filename, "", map->map_number, QuestFileType::EPISODE_3, data);
  co_return;
}
