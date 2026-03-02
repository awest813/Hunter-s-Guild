#pragma once

#include <memory>
#include <vector>

#include "ReceiveSubcommands.hh"

struct SubcommandMessage {
  uint16_t command;
  uint32_t flag;
  void* data;
  size_t size;

  template <typename T>
  const T& check_size_t(size_t min_size, size_t max_size) const {
    return ::check_size_t<const T>(this->data, this->size, min_size, max_size);
  }
  template <typename T>
  T& check_size_t(size_t min_size, size_t max_size) {
    return ::check_size_t<T>(this->data, this->size, min_size, max_size);
  }

  template <typename T>
  const T& check_size_t(size_t max_size) const {
    return ::check_size_t<const T>(this->data, this->size, sizeof(T), max_size);
  }
  template <typename T>
  T& check_size_t(size_t max_size) {
    return ::check_size_t<T>(this->data, this->size, sizeof(T), max_size);
  }

  template <typename T>
  const T& check_size_t() const {
    return ::check_size_t<const T>(this->data, this->size, sizeof(T), sizeof(T));
  }
  template <typename T>
  T& check_size_t() {
    return ::check_size_t<T>(this->data, this->size, sizeof(T), sizeof(T));
  }
};

using SubcommandHandler = asio::awaitable<void> (*)(std::shared_ptr<Client> c, SubcommandMessage& msg);

struct SubcommandDefinition {
  enum Flag {
    ALWAYS_FORWARD_TO_WATCHERS = 0x01,
    ALLOW_FORWARD_TO_WATCHED_LOBBY = 0x02,
  };
  uint8_t nte_subcommand;
  uint8_t proto_subcommand;
  uint8_t final_subcommand;
  SubcommandHandler handler;
  uint8_t flags = 0;
};
using SDF = SubcommandDefinition::Flag;

extern const std::vector<SubcommandDefinition> subcommand_definitions;

const SubcommandDefinition* def_for_subcommand(Version version, uint8_t subcommand);
bool command_is_private(uint8_t command);
void forward_subcommand(std::shared_ptr<Client> c, SubcommandMessage& msg);
