#include "SendCommands.hh"

#include <algorithm>
#include <cstring>
#include <stdexcept>

using namespace std;

void send_quest_file_chunk(
    shared_ptr<Client> c, const string& filename, size_t chunk_index, const void* data, size_t size, bool is_download_quest) {
  if (size > 0x400) {
    throw logic_error("quest file chunks must be 1KB or smaller");
  }

  S_WriteFile_13_A7 cmd;
  cmd.filename.encode(filename);
  memcpy(cmd.data.data(), data, size);
  if (size < 0x400) {
    memset(&cmd.data[size], 0, 0x400 - size);
  }
  cmd.data_size = size;

  c->log.info_f("Sending quest file chunk {}:{}", filename, chunk_index);
  const auto& s = c->require_server_state();
  c->channel->send(is_download_quest ? 0xA7 : 0x13, chunk_index, &cmd, sizeof(cmd), s->hide_download_commands);
}

template <typename CommandT>
void send_open_quest_file_t(
    shared_ptr<Client> c,
    const string& quest_name,
    const string& filename,
    const string&,
    uint32_t file_size,
    uint32_t, // quest_number (only used on Xbox)
    QuestFileType type) {
  CommandT cmd;
  uint8_t command_num;
  switch (type) {
    case QuestFileType::ONLINE:
      command_num = 0x44;
      cmd.name.encode("PSO/" + quest_name);
      cmd.type = 0;
      break;
    case QuestFileType::GBA_DEMO:
      command_num = 0xA6;
      cmd.name.encode("GBA Demo");
      cmd.type = 2;
      break;
    case QuestFileType::DOWNLOAD_WITHOUT_PVR:
    case QuestFileType::DOWNLOAD_WITH_PVR:
      command_num = 0xA6;
      cmd.name.encode("PSO/" + quest_name);
      cmd.type = (type == QuestFileType::DOWNLOAD_WITH_PVR) ? 1 : 0;
      break;
    case QuestFileType::EPISODE_3:
      command_num = 0xA6;
      cmd.name.encode("PSO/" + quest_name);
      cmd.type = 3;
      break;
    default:
      throw logic_error("invalid quest file type");
  }
  cmd.file_size = file_size;
  cmd.filename.encode(filename);
  send_command_t(c, command_num, 0x00, cmd);
}

template <>
void send_open_quest_file_t<S_OpenFile_XB_44_A6>(
    shared_ptr<Client> c,
    const string& quest_name,
    const string& filename,
    const string& xb_filename,
    uint32_t file_size,
    uint32_t quest_number,
    QuestFileType type) {
  S_OpenFile_XB_44_A6 cmd;
  cmd.name.encode("PSO/" + quest_name);
  cmd.type = (type == QuestFileType::DOWNLOAD_WITH_PVR) ? 1 : 0;
  cmd.file_size = file_size;
  cmd.filename.encode(filename);
  cmd.xb_filename.encode(xb_filename);
  cmd.content_meta = 0x30000000 | quest_number;
  send_command_t(c, (type == QuestFileType::ONLINE) ? 0x44 : 0xA6, 0x00, cmd);
}

void send_open_quest_file(
    shared_ptr<Client> c,
    const string& quest_name,
    const string& filename,
    const string& xb_filename,
    uint32_t quest_number,
    QuestFileType type,
    shared_ptr<const string> contents) {

  switch (c->version()) {
    case Version::DC_11_2000:
    case Version::DC_V1:
    case Version::DC_V2:
    case Version::GC_NTE:
      send_open_quest_file_t<S_OpenFile_DC_44_A6>(c, quest_name, filename, xb_filename, contents->size(), quest_number, type);
      break;
    case Version::PC_NTE:
    case Version::PC_V2:
    case Version::GC_V3:
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
      send_open_quest_file_t<S_OpenFile_PC_GC_44_A6>(c, quest_name, filename, xb_filename, contents->size(), quest_number, type);
      break;
    case Version::XB_V3:
      send_open_quest_file_t<S_OpenFile_XB_44_A6>(c, quest_name, filename, xb_filename, contents->size(), quest_number, type);
      break;
    case Version::BB_V4:
      send_open_quest_file_t<S_OpenFile_BB_44_A6>(c, quest_name, filename, xb_filename, contents->size(), quest_number, type);
      break;
    default:
      throw logic_error("cannot send quest files to this version of client");
  }

  // On most versions, we can trust the TCP stack to do the right thing when we send a lot of data at once, but on GC,
  // the client will crash if too much quest data is sent at once. This is likely a bug in the TCP stack, since the
  // client should apply backpressure to avoid bad situations, but we have to deal with it here instead.
  size_t total_chunks = (contents->size() + 0x3FF) / 0x400;
  size_t chunks_to_send = is_v1_or_v2(c->version()) ? total_chunks : min<size_t>(V3_V4_QUEST_LOAD_MAX_CHUNKS_IN_FLIGHT, total_chunks);

  for (size_t z = 0; z < chunks_to_send; z++) {
    size_t offset = z * 0x400;
    size_t chunk_bytes = contents->size() - offset;
    if (chunk_bytes > 0x400) {
      chunk_bytes = 0x400;
    }
    send_quest_file_chunk(c, filename, offset / 0x400, contents->data() + offset, chunk_bytes, (type != QuestFileType::ONLINE));
  }

  // If there are still chunks to send, track the file so the chunk acknowledgement handler (13 or A7) can know what to
  // send next
  if (chunks_to_send < total_chunks) {
    c->sending_files.emplace(filename, contents);
    c->log.info_f("Opened file {}", filename);
  }
}
