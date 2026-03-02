#include "ReceiveCommands.hh"

#include <phosg/Filesystem.hh>

#include "SendCommands.hh"

using namespace std;

void on_quest_loaded(shared_ptr<Lobby> l);
void on_joinable_quest_loaded(shared_ptr<Client> c);

asio::awaitable<void> on_B3(shared_ptr<Client> c, Channel::Message& msg) {
  auto& cmd = check_size_t<C_ExecuteCodeResult_B3>(msg.data);
  if (!c->function_call_response_queue.empty()) {
    auto& prom = c->function_call_response_queue.front();
    if (prom) {
      prom->set_value(std::move(cmd));
    }
    c->function_call_response_queue.pop_front();
  } else {
    c->log.warning_f("Received function call response but response queue is empty");
  }
  co_return;
}

asio::awaitable<void> on_AC_V3_BB(shared_ptr<Client> c, Channel::Message& msg) {
  check_size_v(msg.data.size(), 0);

  if (c->check_flag(Client::Flag::LOADING_RUNNING_JOINABLE_QUEST)) {
    on_joinable_quest_loaded(c);

  } else if (c->check_flag(Client::Flag::LOADING_QUEST)) {
    c->clear_flag(Client::Flag::LOADING_QUEST);
    c->log.info_f("LOADING_QUEST flag cleared");
    auto l = c->require_lobby();
    if (l->quest && send_quest_barrier_if_all_clients_ready(l)) {
      on_quest_loaded(l);
    }
  }
  co_return;
}

asio::awaitable<void> on_AA(shared_ptr<Client> c, Channel::Message& msg) {
  if (is_v1_or_v2(c->version())) {
    throw runtime_error("pre-V3 client sent update quest stats command");
  }

  const auto& cmd = check_size_t<C_SendQuestStatistic_V3_BB_AA>(msg.data);
  auto l = c->require_lobby();
  if (l->is_game() && l->quest.get()) {
    // TODO: Send the right value here. (When should we send label2?)
    send_quest_function_call(c, cmd.label1);
  }
  co_return;
}

asio::awaitable<void> on_D7_GC(shared_ptr<Client> c, Channel::Message& msg) {
  string filename(msg.data);
  phosg::strip_trailing_zeroes(filename);
  if (filename.find('/') != string::npos) {
    send_command(c, 0xD7, 0x00);
  } else {
    try {
      auto s = c->require_server_state();
      auto f = s->gba_files_cache->get_or_load("system/gba/" + filename).file;
      send_open_quest_file(c, "", filename, "", 0, QuestFileType::GBA_DEMO, f->data);
    } catch (const out_of_range&) {
      send_command(c, 0xD7, 0x00);
    } catch (const phosg::cannot_open_file&) {
      send_command(c, 0xD7, 0x00);
    }
  }
  co_return;
}

asio::awaitable<void> on_13_A7_V3_V4(shared_ptr<Client> c, Channel::Message& msg) {
  const auto& cmd = check_size_t<C_WriteFileConfirmation_V3_BB_13_A7>(msg.data);
  bool is_download_quest = (msg.command == 0xA7);
  string filename = cmd.filename.decode();
  size_t chunk_to_send = msg.flag + V3_V4_QUEST_LOAD_MAX_CHUNKS_IN_FLIGHT;

  shared_ptr<const string> file_data;
  try {
    file_data = c->sending_files.at(filename);
  } catch (const out_of_range&) {
  }

  if (!file_data) {
    co_return;
  }

  size_t chunk_offset = chunk_to_send * 0x400;
  if (chunk_offset >= file_data->size()) {
    c->log.info_f("Done sending file {}", filename);
    c->sending_files.erase(filename);
  } else {
    const void* chunk_data = file_data->data() + (chunk_to_send * 0x400);
    size_t chunk_size = min<size_t>(file_data->size() - chunk_offset, 0x400);
    send_quest_file_chunk(c, filename, chunk_to_send, chunk_data, chunk_size, is_download_quest);
  }
}
