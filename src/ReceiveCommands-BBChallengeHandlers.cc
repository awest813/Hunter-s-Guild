#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_DF_BB(shared_ptr<Client> c, Channel::Message& msg) {
  auto s = c->require_server_state();
  auto l = c->require_lobby();
  if (!l->is_game()) {
    throw runtime_error("challenge mode config command sent outside of game");
  }
  if (l->mode != GameMode::CHALLENGE) {
    throw runtime_error("challenge mode config command sent in non-challenge game");
  }
  auto cp = l->require_challenge_params();

  switch (msg.command) {
    case 0x01DF: {
      const auto& cmd = check_size_t<C_SetChallengeModeStageNumber_BB_01DF>(msg.data);
      cp->stage_number = cmd.stage;
      l->log.info_f("(Challenge mode) Stage number set to {:02X}", cp->stage_number);
      break;
    }

    case 0x02DF: {
      const auto& cmd = check_size_t<C_SetChallengeModeCharacterTemplate_BB_02DF>(msg.data);
      if (!l->quest) {
        throw runtime_error("challenge mode character template config command sent in non-challenge game");
      }
      auto leader_c = l->clients.at(l->leader_id);
      if (!leader_c) {
        throw logic_error("lobby has no leader");
      }
      if (leader_c != c) {
        throw runtime_error("non-leader sent 02DF command");
      }
      auto vq = l->quest->version(Version::BB_V4, c->language());
      if (vq->meta.challenge_template_index != static_cast<ssize_t>(cmd.template_index)) {
        throw runtime_error("challenge template index in quest metadata does not match index sent by client");
      }

      for (auto& m : l->floor_item_managers) {
        m.clear();
      }

      for (auto lc : l->clients) {
        // See comment in on_quest_loaded about when the leader is responsible for creating challenge overlays vs. when
        // the server should do it at quest load time
        if (lc) {
          if (is_v4(lc->version())) {
            lc->change_bank(lc->bb_character_index);
          }
          lc->create_challenge_overlay(lc->version(), l->quest->meta.challenge_template_index, s->level_table(lc->version()));
          lc->log.info_f("Created challenge overlay");
          l->assign_inventory_and_bank_item_ids(lc, true);
        }
      }

      l->map_state->reset();
      break;
    }

    case 0x03DF: {
      const auto& cmd = check_size_t<C_SetChallengeModeDifficulty_BB_03DF>(msg.data);
      if (!l->quest) {
        throw runtime_error("challenge mode difficulty config command sent in non-challenge game");
      }
      Difficulty cmd_difficulty = static_cast<Difficulty>(cmd.difficulty32.load());
      if (l->quest->meta.challenge_difficulty != cmd_difficulty) {
        throw runtime_error("incorrect difficulty level");
      }
      if (l->difficulty != cmd_difficulty) {
        l->difficulty = cmd_difficulty;
        l->create_item_creator();
      }
      l->log.info_f("(Challenge mode) Difficulty set to {}", name_for_difficulty(l->difficulty));
      break;
    }

    case 0x04DF: {
      check_size_t<C_SetChallengeModeEXPMultiplier_BB_04DF>(msg.data);
      if (!l->quest) {
        throw runtime_error("challenge mode difficulty config command sent in non-challenge game");
      }
      l->challenge_exp_multiplier = (l->quest->meta.challenge_exp_multiplier < 0)
          ? 1.0
          : l->quest->meta.challenge_exp_multiplier;
      l->log.info_f("(Challenge mode) EXP multiplier set to {:g}", l->challenge_exp_multiplier);
      break;
    }

    case 0x05DF: {
      const auto& cmd = check_size_t<C_SetChallengeRankText_BB_05DF>(msg.data);
      cp->rank_color = cmd.rank_color;
      cp->rank_text = cmd.rank_text.decode();
      l->log.info_f("(Challenge mode) Rank text set to {} (color {:08X})", cp->rank_text, cp->rank_color);
      break;
    }

    case 0x06DF: {
      const auto& cmd = check_size_t<C_SetChallengeRankThreshold_BB_06DF>(msg.data);
      auto& threshold = cp->rank_thresholds[cmd.rank];
      threshold.bitmask = cmd.rank_bitmask;
      threshold.seconds = cmd.seconds;
      string time_str = phosg::format_duration(static_cast<uint64_t>(threshold.seconds) * 1000000);
      l->log.info_f("(Challenge mode) Rank {} threshold set to {} (bitmask {:08X})",
          char_for_challenge_rank(cmd.rank), time_str, threshold.bitmask);
      break;
    }

    case 0x07DF: {
      const auto& cmd = check_size_t<C_CreateChallengeModeAwardItem_BB_07DF>(msg.data);
      auto p = c->character_file(true, false);
      auto& award_state = (l->episode == Episode::EP2)
          ? p->challenge_records.ep2_online_award_state
          : p->challenge_records.ep1_online_award_state;
      award_state.rank_award_flags |= cmd.rank_bitmask;
      p->add_item(cmd.item, *s->item_stack_limits(c->version()));
      l->on_item_id_generated_externally(cmd.item.id);
      string desc = s->describe_item(Version::BB_V4, cmd.item);
      l->log.info_f("(Challenge mode) Item awarded to player {}: {}", c->lobby_client_id, desc);
      break;
    }
  }
  co_return;
}
