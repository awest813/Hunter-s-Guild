#include "ReceiveCommands.hh"

#include <phosg/Random.hh>

#include "SendCommands.hh"

using namespace std;

shared_ptr<Lobby> create_game_generic(
    shared_ptr<ServerState> s,
    shared_ptr<Client> creator_c,
    const std::string& name,
    const std::string& password,
    Episode episode,
    GameMode mode,
    Difficulty difficulty,
    bool allow_v1,
    shared_ptr<Lobby> watched_lobby,
    shared_ptr<Episode3::BattleRecordPlayer> battle_player) {

  if ((episode != Episode::EP1) &&
      (episode != Episode::EP2) &&
      (episode != Episode::EP3) &&
      (episode != Episode::EP4)) {
    throw invalid_argument("incorrect episode number");
  }

  if (static_cast<size_t>(difficulty) > 3) {
    throw invalid_argument("incorrect difficulty level");
  }

  auto current_lobby = creator_c->require_lobby();

  size_t min_level = s->default_min_level_for_game(creator_c->version(), episode, difficulty);

  auto p = creator_c->character_file();
  if (!creator_c->login->account->check_flag(Account::Flag::FREE_JOIN_GAMES) && (min_level > p->disp.stats.level)) {
    // Note: We don't throw here because this is a situation players might encounter while playing the game normally
    string msg = std::format("You must be level {}\nor above to play\nthis difficulty.", static_cast<size_t>(min_level + 1));
    send_lobby_message_box(creator_c, msg);
    return nullptr;
  }

  shared_ptr<Lobby> game = s->create_lobby(true);
  game->name = name;
  game->episode = episode;
  game->mode = mode;
  game->difficulty = difficulty;
  game->allowed_versions = s->compatibility_groups.at(static_cast<size_t>(creator_c->version()));
  static_assert(NUM_VERSIONS == 14, "Don't forget to update the group compatibility restrictions");
  if (!allow_v1 || (difficulty == Difficulty::ULTIMATE) || (mode == GameMode::CHALLENGE) || (mode == GameMode::SOLO)) {
    game->forbid_version(Version::DC_NTE);
    game->forbid_version(Version::DC_11_2000);
    game->forbid_version(Version::DC_V1);
  } else if (mode == GameMode::BATTLE) {
    game->forbid_version(Version::DC_NTE);
    game->forbid_version(Version::DC_11_2000);
    // v1 supports battle mode but not battle quests
  }
  switch (game->episode) {
    case Episode::NONE:
      throw logic_error("game episode not set at creation time");
    case Episode::EP1:
      for (Version v : ALL_VERSIONS) {
        if (is_ep3(v)) {
          game->forbid_version(v);
        }
      }
      break;
    case Episode::EP2:
      for (Version v : ALL_VERSIONS) {
        if (is_v1_or_v2(v) || is_ep3(v)) {
          game->forbid_version(v);
        }
      }
      break;
    case Episode::EP3:
      for (Version v : ALL_VERSIONS) {
        if (!is_ep3(v)) {
          game->forbid_version(v);
        }
      }
      break;
    case Episode::EP4:
      for (Version v : ALL_VERSIONS) {
        if (!is_v4(v)) {
          game->forbid_version(v);
        }
      }
      break;
  }

  if (creator_c->check_flag(Client::Flag::DEBUG_ENABLED)) {
    game->set_flag(Lobby::Flag::DEBUG);
  }
  if (creator_c->check_flag(Client::Flag::IS_CLIENT_CUSTOMIZATION)) {
    game->set_flag(Lobby::Flag::IS_CLIENT_CUSTOMIZATION);
  }

  while (game->floor_item_managers.size() < 0x12) {
    game->floor_item_managers.emplace_back(game->lobby_id, game->floor_item_managers.size());
  }

  if (s->behavior_enabled(s->cheat_mode_behavior)) {
    game->set_flag(Lobby::Flag::CHEATS_ENABLED);
  }
  if (!s->behavior_can_be_overridden(s->cheat_mode_behavior)) {
    game->set_flag(Lobby::Flag::CANNOT_CHANGE_CHEAT_MODE);
  }
  if (s->use_game_creator_section_id) {
    game->set_flag(Lobby::Flag::USE_CREATOR_SECTION_ID);
  }
  if (watched_lobby || battle_player) {
    game->set_flag(Lobby::Flag::IS_SPECTATOR_TEAM);
  }
  game->password = password;

  game->creator_section_id = p->disp.visual.section_id;
  game->override_section_id = creator_c->override_section_id;
  if (game->mode == GameMode::CHALLENGE) {
    game->challenge_params = make_shared<Lobby::ChallengeParameters>();
  }
  if (creator_c->override_random_seed >= 0) {
    game->random_seed = creator_c->override_random_seed;
    game->log.info_f("Using random seed {:08X} from client override", game->random_seed);
  } else {
    game->random_seed = phosg::random_object<uint32_t>();
    game->log.info_f("Using random seed {:08X} from system generator", game->random_seed);
  }
  if (s->use_psov2_rand_crypt) {
    game->rand_crypt = make_shared<PSOV2Encryption>(game->random_seed);
    game->log.info_f("Using PSOV2Encryption for item generation", game->random_seed);
  } else {
    game->rand_crypt = make_shared<MT19937Generator>(game->random_seed);
    game->log.info_f("Using MT19937Generator for item generation", game->random_seed);
  }
  if (battle_player) {
    game->battle_player = battle_player;
    battle_player->set_lobby(game);
  }
  game->base_exp_multiplier = s->bb_global_exp_multiplier;
  game->exp_share_multiplier = s->exp_share_multiplier;

  const unordered_map<uint16_t, IntegralExpression>* quest_flag_rewrites;
  switch (creator_c->version()) {
    case Version::DC_NTE:
    case Version::DC_11_2000:
    case Version::DC_V1:
    case Version::DC_V2:
    case Version::PC_NTE:
    case Version::PC_V2:
      quest_flag_rewrites = &s->quest_flag_rewrites_v1_v2;
      if (game->mode == GameMode::BATTLE) {
        game->drop_mode = s->default_drop_mode_v1_v2_battle;
        game->allowed_drop_modes = s->allowed_drop_modes_v1_v2_battle;
      } else if (game->mode == GameMode::CHALLENGE) {
        game->drop_mode = s->default_drop_mode_v1_v2_challenge;
        game->allowed_drop_modes = s->allowed_drop_modes_v1_v2_challenge;
      } else {
        game->drop_mode = s->default_drop_mode_v1_v2_normal;
        game->allowed_drop_modes = s->allowed_drop_modes_v1_v2_normal;
      }
      break;
    case Version::GC_NTE:
    case Version::GC_V3:
    case Version::XB_V3:
      quest_flag_rewrites = &s->quest_flag_rewrites_v3;
      if (game->mode == GameMode::BATTLE) {
        game->drop_mode = s->default_drop_mode_v3_battle;
        game->allowed_drop_modes = s->allowed_drop_modes_v3_battle;
      } else if (game->mode == GameMode::CHALLENGE) {
        game->drop_mode = s->default_drop_mode_v3_challenge;
        game->allowed_drop_modes = s->allowed_drop_modes_v3_challenge;
      } else {
        game->drop_mode = s->default_drop_mode_v3_normal;
        game->allowed_drop_modes = s->allowed_drop_modes_v3_normal;
      }
      break;
    case Version::GC_EP3_NTE:
    case Version::GC_EP3:
      quest_flag_rewrites = nullptr;
      game->drop_mode = ServerDropMode::DISABLED;
      game->allowed_drop_modes = (1 << static_cast<size_t>(game->drop_mode));
      break;
    case Version::BB_V4:
      quest_flag_rewrites = &s->quest_flag_rewrites_v4;
      if (game->mode == GameMode::BATTLE) {
        game->drop_mode = s->default_drop_mode_v4_battle;
        game->allowed_drop_modes = s->allowed_drop_modes_v4_battle;
      } else if (game->mode == GameMode::CHALLENGE) {
        game->drop_mode = s->default_drop_mode_v4_challenge;
        game->allowed_drop_modes = s->allowed_drop_modes_v4_challenge;
      } else {
        game->drop_mode = s->default_drop_mode_v4_normal;
        game->allowed_drop_modes = s->allowed_drop_modes_v4_normal;
      }
      // Disallow CLIENT mode on BB
      if (game->drop_mode == ServerDropMode::CLIENT) {
        throw logic_error("CLIENT mode not allowed on BB");
      }
      if (game->allowed_drop_modes & (1 << static_cast<size_t>(ServerDropMode::CLIENT))) {
        throw logic_error("CLIENT mode not allowed on BB");
      }
      break;
    default:
      throw logic_error("invalid quest script version");
  }
  game->create_item_creator(creator_c->version());

  game->event = current_lobby->event;
  game->block = 0xFF;
  game->max_clients = game->check_flag(Lobby::Flag::IS_SPECTATOR_TEAM) ? 12 : 4;
  game->min_level = min_level;
  game->max_level = 0xFFFFFFFF;
  if (watched_lobby) {
    game->watched_lobby = watched_lobby;
    watched_lobby->watcher_lobbies.emplace(game);
  }

  bool is_solo = (game->mode == GameMode::SOLO);

  if (game->mode == GameMode::CHALLENGE) {
    game->rare_enemy_rates = s->rare_enemy_rates_challenge;
  } else {
    game->rare_enemy_rates = s->rare_enemy_rates(game->difficulty);
  }

  if (game->episode != Episode::EP3) {
    // GC NTE ignores the passed-in variations and always uses all zeroes
    if (creator_c->version() == Version::GC_NTE) {
      game->variations = Variations();
      game->log.info_f("Base version is GC_NTE; using blank variations");
    } else if (creator_c->override_variations) {
      game->variations = *creator_c->override_variations;
      creator_c->override_variations.reset();
      auto vars_str = game->variations.str();
      game->log.info_f("Using variations from client override: {}", vars_str);
    } else {
      auto sdt = s->set_data_table(creator_c->version(), game->episode, game->mode, game->difficulty);
      game->variations = sdt->generate_variations(game->episode, is_solo, game->rand_crypt);
      auto vars_str = game->variations.str();
      game->log.info_f("Using random variations: {}", vars_str);
    }
  } else {
    game->variations = Variations();
  }
  game->load_maps(); // Load free-play maps

  // The game's quest flags are inherited from the creator, if known
  if (creator_c->version() == Version::BB_V4) {
    game->quest_flag_values = make_unique<QuestFlags>(p->quest_flags);
    game->quest_flags_known = nullptr;
  } else {
    game->quest_flag_values = make_unique<QuestFlags>();
    game->quest_flags_known = make_unique<QuestFlags>();
  }

  if (quest_flag_rewrites && !quest_flag_rewrites->empty()) {
    IntegralExpression::Env env = {
        .flags = &p->quest_flags.array(difficulty),
        .challenge_records = &p->challenge_records,
        .team = creator_c->team(),
        .num_players = 1,
        .event = game->event,
        .v1_present = is_v1(creator_c->version()),
    };
    for (const auto& it : *quest_flag_rewrites) {
      bool should_set = it.second.evaluate(env);
      game->log.info_f("Overriding quest flag {:04X} = {}", it.first, should_set ? "true" : "false");
      if (should_set) {
        game->quest_flag_values->set(game->difficulty, it.first);
      } else {
        game->quest_flag_values->clear(game->difficulty, it.first);
      }
      if (game->quest_flags_known) {
        game->quest_flags_known->set(game->difficulty, it.first);
      }
    }
    creator_c->set_flag(Client::Flag::SHOULD_SEND_ARTIFICIAL_FLAG_STATE);
  }

  game->switch_flags = make_unique<SwitchFlags>();

  return game;
}
