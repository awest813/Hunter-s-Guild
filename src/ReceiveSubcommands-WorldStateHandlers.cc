#include "ReceiveSubcommands-Impl.hh"

#include <phosg/Encoding.hh>

#include "SendCommands.hh"
#include "Text.hh"

using namespace std;

asio::awaitable<void> on_trigger_set_event(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  const auto& cmd = msg.check_size_t<G_TriggerSetEvent_6x67>();
  auto event_sts = l->map_state->event_states_for_id(c->version(), cmd.floor, cmd.event_id);
  l->log.info_f("Client triggered set events with floor {:02X} and ID {:X} ({} events)",
      cmd.floor, cmd.event_id, event_sts.size());
  for (auto ev_st : event_sts) {
    ev_st->flags |= 0x04;
    if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
      send_text_message_fmt(c, "$C5W-{:03X} START", ev_st->w_id);
    }
  }

  forward_subcommand(c, msg);
}

static inline uint32_t bswap32_high16(uint32_t v) {
  return ((v >> 8) & 0x00FF0000) | ((v << 8) & 0xFF000000) | (v & 0x0000FFFF);
}

asio::awaitable<void> on_update_telepipe_state(shared_ptr<Client> c, SubcommandMessage& msg) {
  if (c->lobby_client_id > 3) {
    throw logic_error("client ID is above 3");
  }
  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }

  auto& cmd = msg.check_size_t<G_SetTelepipeState_6x68>();
  c->telepipe_state = cmd.state;
  c->telepipe_lobby_id = l->lobby_id;

  // See the comments in G_SetTelepipeState_6x68 in CommandsFormats.hh for why we have to do this
  if (is_big_endian(c->version())) {
    c->telepipe_state.room_id = bswap32_high16(phosg::bswap32(c->telepipe_state.room_id));
  }

  for (auto lc : l->clients) {
    if (lc && (lc != c)) {
      if (is_big_endian(lc->version())) {
        cmd.state.room_id = phosg::bswap32(bswap32_high16(c->telepipe_state.room_id));
      } else {
        cmd.state.room_id = c->telepipe_state.room_id;
      }
      send_command_t(lc, 0x60, 0x00, cmd);
    }
  }
}

asio::awaitable<void> on_update_enemy_state(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto& cmd = msg.check_size_t<G_UpdateEnemyState_DC_PC_XB_BB_6x0A>();

  if (command_is_private(msg.command)) {
    co_return;
  }
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }
  if (c->lobby_client_id > 3) {
    throw logic_error("client ID is above 3");
  }

  if ((cmd.enemy_index & 0xF000) || (cmd.header.entity_id != (cmd.enemy_index | 0x1000))) {
    throw runtime_error("mismatched enemy id/index");
  }
  auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.enemy_index);
  uint32_t src_flags = is_big_endian(c->version()) ? bswap32(cmd.game_flags) : cmd.game_flags.load();
  if (l->difficulty == Difficulty::ULTIMATE) {
    src_flags = (src_flags & 0xFFFFFFC0) | (ene_st->game_flags & 0x0000003F);
  }
  ene_st->game_flags = src_flags;
  ene_st->total_damage = cmd.total_damage;
  l->log.info_f("E-{:03X} updated to damage={} game_flags={:08X}", ene_st->e_id, ene_st->total_damage, ene_st->game_flags);
  if (ene_st->alias_target_ene_st) {
    ene_st->alias_target_ene_st->game_flags = src_flags;
    ene_st->alias_target_ene_st->total_damage = cmd.total_damage;
    l->log.info_f("Alias target E-{:03X} updated to damage={} game_flags={:08X}",
        ene_st->alias_target_ene_st->e_id, ene_st->alias_target_ene_st->total_damage, ene_st->alias_target_ene_st->game_flags);
  }

  // TODO: It'd be nice if this worked on bosses too, but it seems we have to use each boss' specific state-syncing
  // command, or the cutscenes misbehave. Just setting flag 0x800 does work on Vol Opt (and the various parts), but
  // doesn't work on other Episode 1 bosses. Other episodes' bosses are not yet tested.
  bool is_fast_kill = c->check_flag(Client::Flag::FAST_KILLS_ENABLED) &&
      !type_definition_for_enemy(ene_st->super_ene->type).is_boss() &&
      !(ene_st->game_flags & 0x00000800);
  if (is_fast_kill) {
    ene_st->game_flags |= 0x00000800;
  }

  for (auto lc : l->clients) {
    if (lc && (is_fast_kill || (lc != c))) {
      cmd.enemy_index = l->map_state->index_for_enemy_state(lc->version(), ene_st);
      if (cmd.enemy_index != 0xFFFF) {
        cmd.header.entity_id = 0x1000 | cmd.enemy_index;
        cmd.game_flags = is_big_endian(lc->version()) ? phosg::bswap32(ene_st->game_flags) : ene_st->game_flags;
        send_command_t(lc, 0x60, 0x00, cmd);
      }
    }
  }
}

asio::awaitable<void> on_activate_timed_switch(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_SetSwitchFlagFromTimer_6x93>();
  auto l = c->require_lobby();
  if (!l->is_game()) {
    co_return;
  }
  if (l->switch_flags) {
    if (cmd.should_set == 1) {
      l->switch_flags->set(cmd.switch_flag_floor, cmd.switch_flag_num);
    } else {
      l->switch_flags->clear(cmd.switch_flag_floor, cmd.switch_flag_num);
    }
  }
  forward_subcommand(c, msg);
}
