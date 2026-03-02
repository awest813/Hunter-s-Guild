#include "ReceiveSubcommands-Impl.hh"

#include "SendCommands.hh"

using namespace std;

void send_max_level_notification_if_needed(shared_ptr<Client> c);

static void add_player_exp(shared_ptr<Client> c, uint32_t exp, uint16_t from_enemy_id) {
  auto s = c->require_server_state();
  auto p = c->character_file();

  p->disp.stats.experience += exp;
  if (c->version() == Version::BB_V4) {
    send_give_experience(c, exp, from_enemy_id);
  }

  bool leveled_up = false;
  do {
    const auto& level = s->level_table(c->version())->stats_delta_for_level(p->disp.visual.char_class, p->disp.stats.level + 1);
    if (p->disp.stats.experience >= level.experience) {
      leveled_up = true;
      level.apply(p->disp.stats.char_stats);
      p->disp.stats.level++;
    } else {
      break;
    }
  } while (p->disp.stats.level < 199);

  if (leveled_up) {
    send_max_level_notification_if_needed(c);
    if (c->version() == Version::BB_V4) {
      send_level_up(c);
    }
  }
}

static uint32_t base_exp_for_enemy_type(
    shared_ptr<const BattleParamsIndex> bp_index,
    shared_ptr<const Quest> quest, // Null in free play
    EnemyType enemy_type,
    Episode current_episode,
    Difficulty difficulty,
    uint8_t floor,
    bool is_solo) {
  if (quest) {
    try {
      return quest->meta.enemy_exp_overrides.at(QuestMetadata::exp_override_key(difficulty, floor, enemy_type));
    } catch (const out_of_range&) {
    }
  }

  // Always try the current episode first. If the current episode is Ep4, try Ep1 next if in Crater and Ep2 next if in
  // Desert (this mirrors the logic in BB Patch Project's omnispawn patch).
  array<Episode, 3> episode_order;
  episode_order[0] = current_episode;
  if (current_episode == Episode::EP1) {
    episode_order[1] = Episode::EP2;
    episode_order[2] = Episode::EP4;
  } else if (current_episode == Episode::EP2) {
    episode_order[1] = Episode::EP1;
    episode_order[2] = Episode::EP4;
  } else if (current_episode == Episode::EP4) {
    uint8_t area = quest
        ? quest->meta.floor_assignments.at(floor).area
        : SetDataTableBase::default_floor_to_area(Version::BB_V4, Episode::EP4).at(floor);
    if (area <= 0x28) { // Crater
      episode_order[1] = Episode::EP1;
      episode_order[2] = Episode::EP2;
    } else { // Desert
      episode_order[1] = Episode::EP2;
      episode_order[2] = Episode::EP1;
    }
  } else {
    throw runtime_error("invalid episode");
  }

  for (const auto& episode : episode_order) {
    try {
      const auto& bp_table = bp_index->get_table(is_solo, episode);
      const auto& bp_stats_indexes = type_definition_for_enemy(enemy_type).bp_stats_indexes;
      if (!bp_stats_indexes.empty()) {
        return bp_table.stats_for_index(difficulty, bp_stats_indexes.back()).experience;
      }
    } catch (const out_of_range&) {
    }
  }
  throw runtime_error(std::format(
      "no base exp is available (type={}, episode={}, difficulty={}, floor={:02X}, solo={})",
      phosg::name_for_enum(enemy_type),
      name_for_episode(current_episode),
      name_for_difficulty(difficulty),
      floor,
      is_solo ? "true" : "false"));
}

asio::awaitable<void> on_steal_exp_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto l = c->require_lobby();
  if (c->version() != Version::BB_V4) {
    throw runtime_error("6xC6 command sent by non-BB client");
  }
  if (!l->is_game()) {
    throw runtime_error("6xC6 command sent in non-game lobby");
  }

  auto s = c->require_server_state();
  const auto& cmd = msg.check_size_t<G_StealEXP_BB_6xC6>();

  auto p = c->character_file();
  if (c->character_file()->disp.stats.level >= 199) {
    co_return;
  }

  auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.enemy_index);
  if (ene_st->alias_target_ene_st) {
    ene_st = ene_st->alias_target_ene_st;
  }
  if (ene_st->super_ene->floor != c->floor) {
    throw runtime_error("enemy is on a different floor");
  }

  const auto& inventory = p->inventory;
  const auto& weapon = inventory.items[inventory.find_equipped_item(EquipSlot::WEAPON)];

  auto item_parameter_table = s->item_parameter_table(c->version());

  uint8_t special_id = 0;
  if (((weapon.data.data1[1] < 0x0A) && (weapon.data.data1[2] < 0x05)) ||
      ((weapon.data.data1[1] < 0x0D) && (weapon.data.data1[2] < 0x04))) {
    special_id = weapon.data.data1[4] & 0x3F;
  } else {
    special_id = item_parameter_table->get_weapon(weapon.data.data1[1], weapon.data.data1[2]).special;
  }

  const auto& special = item_parameter_table->get_special(special_id);
  if (special.type != 3) { // Master's/Lord's/King's
    co_return;
  }

  uint8_t area = l->area_for_floor(c->version(), ene_st->super_ene->floor);
  Episode episode = episode_for_area(area);
  auto type = ene_st->type(c->version(), area, l->difficulty, l->event);
  uint32_t enemy_exp = base_exp_for_enemy_type(
      s->battle_params, l->quest, type, episode, l->difficulty, ene_st->super_ene->floor, l->mode == GameMode::SOLO);

  // Note: The original code checks if special.type is 9, 10, or 11, and skips applying the android bonus if so. We
  // don't do anything for those special types, so we don't check for that here.
  float percent = special.amount + ((l->difficulty == Difficulty::ULTIMATE) && char_class_is_android(p->disp.visual.char_class) ? 30 : 0);
  float ep2_factor = (episode == Episode::EP2) ? 1.3 : 1.0;
  uint32_t stolen_exp = max<uint32_t>(min<uint32_t>((enemy_exp * percent * ep2_factor) / 100.0f, (static_cast<size_t>(l->difficulty) + 1) * 20), 1);
  if (c->check_flag(Client::Flag::DEBUG_ENABLED)) {
    c->log.info_f("Stolen EXP from E-{:03X} with enemy_exp={} percent={:g} stolen_exp={}",
        ene_st->e_id, enemy_exp, percent, stolen_exp);
    send_text_message_fmt(c, "$C5+{} E-{:03X} {}", stolen_exp, ene_st->e_id, phosg::name_for_enum(type));
  }
  add_player_exp(c, stolen_exp, cmd.enemy_index | 0x1000);
}

asio::awaitable<void> on_enemy_exp_request_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  auto s = c->require_server_state();
  auto l = c->require_lobby();

  const auto& cmd = msg.check_size_t<G_EnemyEXPRequest_BB_6xC8>();

  if (!l->is_game()) {
    throw runtime_error("client should not kill enemies outside of games");
  }
  if (c->lobby_client_id > 3) {
    throw runtime_error("client ID is too large");
  }

  auto ene_st = l->map_state->enemy_state_for_index(c->version(), cmd.enemy_index);
  string ene_str = ene_st->super_ene->str();
  c->log.info_f("EXP requested for E-{:03X}: {}", ene_st->e_id, ene_str);
  if (ene_st->alias_target_ene_st) {
    c->log.info_f("E-{:03X} is an alias for E-{:03X}", ene_st->e_id, ene_st->alias_target_ene_st->e_id);
    ene_st = ene_st->alias_target_ene_st;
  }

  // If the requesting player never hit this enemy, they are probably cheating; ignore the command. Also, each player
  // sends a 6xC8 if they ever hit the enemy; we only react to the first 6xC8 for each enemy (and give all relevant
  // players EXP then, if they deserve it).
  if (!ene_st->ever_hit_by_client_id(c->lobby_client_id) ||
      (ene_st->server_flags & MapState::EnemyState::Flag::EXP_GIVEN)) {
    l->log.info_f("EXP already given for this enemy; ignoring request");
    co_return;
  }
  ene_st->server_flags |= MapState::EnemyState::Flag::EXP_GIVEN;

  uint8_t area = l->area_for_floor(c->version(), ene_st->super_ene->floor);
  Episode episode = episode_for_area(area);
  auto type = ene_st->type(c->version(), area, l->difficulty, l->event);
  double base_exp = base_exp_for_enemy_type(
      s->battle_params, l->quest, type, episode, l->difficulty, ene_st->super_ene->floor, l->mode == GameMode::SOLO);
  l->log.info_f("Base EXP for this enemy ({}) is {:g}", phosg::name_for_enum(type), base_exp);

  for (size_t client_id = 0; client_id < 4; client_id++) {
    auto lc = l->clients[client_id];
    if (!lc) {
      l->log.info_f("No client in slot {}", client_id);
      continue;
    }
    if (lc->version() != Version::BB_V4) {
      // EXP is handled on the client side in all non-BB versions
      l->log.info_f("Client in slot {} is not BB", client_id);
      continue;
    }

    if (base_exp != 0.0) {
      // If this player killed the enemy, they get full EXP; if they tagged the enemy, they get 80% EXP; if auto EXP
      // share is enabled and they are close enough to the monster, they get a smaller share; if none of these
      // situations apply, they get no EXP. In Battle and Challenge modes, if a quest is loaded, EXP share is disabled.
      float exp_share_multiplier = (((l->mode == GameMode::BATTLE) || (l->mode == GameMode::CHALLENGE)) && l->quest)
          ? 0.0f
          : l->exp_share_multiplier;
      double rate_factor;
      if (lc->character_file()->disp.stats.level >= 199) {
        rate_factor = 0.0;
        l->log.info_f("Client in slot {} is level 200 and cannot receive EXP", client_id);
      } else if (ene_st->last_hit_by_client_id(client_id)) {
        rate_factor = max<double>(1.0, exp_share_multiplier);
        l->log.info_f("Client in slot {} killed this enemy; EXP rate is {:g}", client_id, rate_factor);
      } else if (ene_st->ever_hit_by_client_id(client_id)) {
        rate_factor = max<double>(0.8, exp_share_multiplier);
        l->log.info_f("Client in slot {} tagged this enemy; EXP rate is {:g}", client_id, rate_factor);
      } else if (lc->floor == ene_st->super_ene->floor) {
        rate_factor = max<double>(0.0, exp_share_multiplier);
        l->log.info_f("Client in slot {} shared this enemy; EXP rate is {:g}", client_id, rate_factor);
      } else {
        rate_factor = 0.0;
        l->log.info_f("Client in slot {} is not near this enemy; EXP rate is {:g}", client_id, rate_factor);
      }

      if (rate_factor > 0.0) {
        // In PSOBB, Sega decided to add a 30% EXP boost for Episode 2. They could have done something reasonable, like
        // edit the BattleParamEntry files so the monsters would all give more EXP, but they did something far lazier
        // instead: they just stuck an if statement in the client's EXP request function. We, unfortunately, have to do
        // the same thing here.
        float episode_multiplier = ((episode == Episode::EP2) ? 1.3 : 1.0);
        uint32_t player_exp = base_exp *
            rate_factor *
            l->base_exp_multiplier *
            l->challenge_exp_multiplier *
            episode_multiplier;
        l->log.info_f(
            "Client in slot {} receives {} EXP (base={:g}, factor={:g} base_mult={:g}, challenge={:g}, episode={:g})",
            client_id, player_exp, base_exp, rate_factor, l->base_exp_multiplier, l->challenge_exp_multiplier, episode_multiplier);
        if (lc->check_flag(Client::Flag::DEBUG_ENABLED)) {
          send_text_message_fmt(lc, "$C5+{} E-{:03X} {}", player_exp, ene_st->e_id, phosg::name_for_enum(type));
        }
        add_player_exp(lc, player_exp, cmd.enemy_index | 0x1000);
      }
    }

    // Update kill counts on unsealable items, but only for the player who actually killed the enemy
    if (ene_st->last_hit_by_client_id(client_id)) {
      auto& inventory = lc->character_file()->inventory;
      for (size_t z = 0; z < inventory.num_items; z++) {
        auto& item = inventory.items[z];
        if ((item.flags & 0x08) && s->item_parameter_table(lc->version())->is_unsealable_item(item.data)) {
          item.data.set_kill_count(item.data.get_kill_count() + 1);
        }
      }
    }
  }
}

asio::awaitable<void> on_adjust_player_meseta_bb(shared_ptr<Client> c, SubcommandMessage& msg) {
  const auto& cmd = msg.check_size_t<G_AdjustPlayerMeseta_BB_6xC9>();

  auto p = c->character_file();
  if (cmd.amount < 0) {
    if (-cmd.amount > static_cast<int32_t>(p->disp.stats.meseta)) {
      p->disp.stats.meseta = 0;
    } else {
      p->disp.stats.meseta += cmd.amount;
    }
  } else if (cmd.amount > 0) {
    auto s = c->require_server_state();
    auto l = c->require_lobby();

    ItemData item;
    item.data1[0] = 0x04;
    item.data2d = cmd.amount;
    item.id = l->generate_item_id(c->lobby_client_id);
    p->add_item(item, *s->item_stack_limits(c->version()));
    send_create_inventory_item_to_lobby(c, c->lobby_client_id, item);
  }
  co_return;
}
