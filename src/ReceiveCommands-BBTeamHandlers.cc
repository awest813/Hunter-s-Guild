#include "ReceiveCommands.hh"

#include "SendCommands.hh"

using namespace std;

asio::awaitable<void> on_EA_BB(shared_ptr<Client> c, Channel::Message& msg) {
  auto s = c->require_server_state();

  switch (msg.command) {
    case 0x01EA: { // Create team
      const auto& cmd = check_size_t<C_CreateTeam_BB_01EA>(msg.data);
      string team_name = cmd.name.decode(c->language());
      if (s->team_index->get_by_name(team_name)) {
        send_command(c, 0x02EA, 0x00000002);
      } else if (c->login->account->bb_team_id != 0) {
        // TODO: What's the right error code to use here?
        send_command(c, 0x02EA, 0x00000001);
      } else {
        string player_name = c->character_file()->disp.name.decode(c->language());
        auto team = s->team_index->create(team_name, c->login->account->account_id, player_name);
        c->login->account->bb_team_id = team->team_id;
        c->login->account->save();

        send_command(c, 0x02EA, 0x00000000);
        send_team_metadata_change_notifications(s, team, c->login->account->account_id, TeamMetadataChange::TEAM_CREATED);
      }
      break;
    }
    case 0x03EA: { // Add team member
      auto team = c->team();
      if (team && team->members.at(c->login->account->account_id).privilege_level() >= 0x30) {
        const auto& cmd = check_size_t<C_AddOrRemoveTeamMember_BB_03EA_05EA>(msg.data);
        auto s = c->require_server_state();
        shared_ptr<Client> added_c;
        try {
          added_c = s->find_client(nullptr, cmd.guild_card_number);
        } catch (const out_of_range&) {
          send_command(c, 0x04EA, 0x00000006);
        }

        if (added_c && added_c->login) {
          auto added_c_team = added_c->team();
          if (added_c_team) {
            send_command(c, 0x04EA, 0x00000001);
            send_command(added_c, 0x04EA, 0x00000001);

          } else if (!team->can_add_member()) {
            // Send "team is full" error
            send_command(c, 0x04EA, 0x00000005);
            send_command(added_c, 0x04EA, 0x00000005);

          } else {
            added_c->login->account->bb_team_id = team->team_id;
            added_c->login->account->save();
            s->team_index->add_member(
                team->team_id,
                added_c->login->account->account_id,
                added_c->character_file()->disp.name.decode(added_c->language()));
            send_command(c, 0x04EA, 0x00000000);
            send_command(added_c, 0x04EA, 0x00000000);
            send_team_metadata_change_notifications(
                s, team, added_c->login->account->account_id, TeamMetadataChange::TEAM_MEMBER_COUNT);
          }
        }
      }
      break;
    }
    case 0x05EA: { // Remove team member
      auto team = c->team();
      if (team) {
        const auto& cmd = check_size_t<C_AddOrRemoveTeamMember_BB_03EA_05EA>(msg.data);
        bool is_removing_self = (cmd.guild_card_number == c->login->account->account_id);
        if (is_removing_self ||
            (team->members.at(c->login->account->account_id).privilege_level() >= 0x30)) {
          s->team_index->remove_member(cmd.guild_card_number);
          auto removed_account = s->account_index->from_account_id(cmd.guild_card_number);
          removed_account->bb_team_id = 0;
          removed_account->save();
          send_command(c, 0x06EA, 0x00000000);

          shared_ptr<Client> removed_c;
          if (is_removing_self) {
            removed_c = c;
          } else {
            try {
              removed_c = s->find_client(nullptr, cmd.guild_card_number);
            } catch (const out_of_range&) {
            }
          }
          uint32_t removed_account_id = (removed_c && removed_c->login) ? removed_c->login->account->account_id : 0;
          send_team_metadata_change_notifications(s, team, removed_account_id, TeamMetadataChange::TEAM_MEMBER_COUNT);
        } else {
          // TODO: Figure out the right error code to use here.
          send_command(c, 0x06EA, 0x00000001);
        }
      }
      break;
    }
    case 0x07EA: { // Team chat
      auto team = c->team();
      if (team) {
        check_size_v(msg.data.size(), sizeof(SC_TeamChat_BB_07EA) + 4, 0xFFFF);
        static const string required_end("\0\0", 2);
        if (msg.data.ends_with(required_end)) {
          for (const auto& it : team->members) {
            try {
              auto target_c = s->find_client(nullptr, it.second.account_id);
              send_command(target_c, 0x07EA, 0x00000000, msg.data);
            } catch (const out_of_range&) {
            }
          }
        }
      }
      break;
    }
    case 0x08EA:
      send_team_member_list(c);
      break;
    case 0x0DEA: {
      auto team = c->team();
      if (team) {
        S_TeamName_BB_0EEA cmd;
        cmd.team_name.encode(team->name, c->language());
        send_command_t(c, 0x0EEA, 0x00000000, cmd);
      } else {
        throw runtime_error("client is not in a team");
      }
      break;
    }
    case 0x0FEA: { // Set team flag
      auto team = c->team();
      if (team && team->members.at(c->login->account->account_id).check_flag(TeamIndex::Team::Member::Flag::IS_MASTER)) {
        const auto& cmd = check_size_t<C_SetTeamFlag_BB_0FEA>(msg.data);
        s->team_index->set_flag_data(team->team_id, cmd.flag_data);
        send_team_metadata_change_notifications(s, team, 0, TeamMetadataChange::FLAG_DATA);
      }
      break;
    }
    case 0x10EA: { // Disband team
      auto team = c->team();
      if (team && team->members.at(c->login->account->account_id).check_flag(TeamIndex::Team::Member::Flag::IS_MASTER)) {
        s->team_index->disband(team->team_id);
        send_command(c, 0x10EA, 0x00000000);
        send_team_metadata_change_notifications(s, team, 0, TeamMetadataChange::TEAM_DISBANDED);
      }
      break;
    }
    case 0x11EA: { // Change member privilege level
      auto team = c->team();
      if (team) {
        auto& cmd = check_size_t<C_ChangeTeamMemberPrivilegeLevel_BB_11EA>(msg.data);
        if (cmd.guild_card_number == c->login->account->account_id) {
          throw runtime_error("this command cannot be used to modify your own permissions");
        }

        // The client only sends this command with flag = 0x00, 0x30, or 0x40
        switch (msg.flag) {
          case 0x00: // Demote member
            if (s->team_index->demote_leader(c->login->account->account_id, cmd.guild_card_number)) {
              send_command(c, 0x11EA, 0x00000000);
              send_team_metadata_change_notifications(s, team, cmd.guild_card_number, 0);
            } else {
              send_command(c, 0x11EA, 0x00000005);
            }
            break;
          case 0x30: // Promote member
            if (s->team_index->promote_leader(c->login->account->account_id, cmd.guild_card_number)) {
              send_command(c, 0x11EA, 0x00000000);
              send_team_metadata_change_notifications(s, team, cmd.guild_card_number, 0);
            } else {
              send_command(c, 0x11EA, 0x00000005);
            }
            break;
          case 0x40: // Transfer master
            s->team_index->change_master(c->login->account->account_id, cmd.guild_card_number);
            send_command(c, 0x11EA, 0x00000000);
            send_team_metadata_change_notifications(s, team, cmd.guild_card_number, TeamMetadataChange::TEAM_MASTER);
            break;
          default:
            throw runtime_error("invalid privilege level");
        }
      }
      break;
    }
    case 0x13EA:
      send_all_nearby_team_metadatas_to_client(c, true);
      break;
    case 0x14EA:
      send_all_nearby_team_metadatas_to_client(c, false);
      break;
    case 0x18EA: // Ranking information
      send_intra_team_ranking(c);
      break;
    case 0x19EA: // List purchased team rewards
    case 0x1AEA: // List team rewards available for purchase
      send_team_reward_list(c, (msg.command == 0x19EA));
      break;
    case 0x1BEA: { // Buy team reward
      auto team = c->team();
      if (team) {
        check_size_v(msg.data.size(), 0); // No data should be sent
        const auto& reward = s->team_index->reward_definitions().at(msg.flag);

        for (const auto& key : reward.prerequisite_keys) {
          if (!team->has_reward(key)) {
            throw runtime_error("not all prerequisite rewards have been purchased");
          }
        }
        if (reward.is_unique && team->has_reward(reward.key)) {
          throw runtime_error("team reward already purchased");
        }

        s->team_index->buy_reward(team->team_id, reward.key, reward.team_points, reward.reward_flag);

        if (reward.reward_flag != TeamIndex::Team::RewardFlag::NONE) {
          send_team_metadata_change_notifications(s, team, 0, TeamMetadataChange::REWARD_FLAGS);
        }
        if (!reward.reward_item.empty()) {
          c->bank_file()->add_item(reward.reward_item, *s->item_stack_limits(c->version()));
          c->print_bank();
        }
      }
      break;
    }
    case 0x1CEA:
      send_cross_team_ranking(c);
      break;
    case 0x1EEA: {
      const auto& cmd = check_size_t<C_RenameTeam_BB_1EEA>(msg.data);
      auto team = c->team();
      string new_team_name = cmd.new_team_name.decode(c->language());
      if (!team) {
        // TODO: What's the right error code to use here?
        send_command(c, 0x1FEA, 0x00000001);
      } else if (s->team_index->get_by_name(new_team_name)) {
        send_command(c, 0x1FEA, 0x00000002);
      } else {
        s->team_index->rename(team->team_id, new_team_name);
        send_command(c, 0x1FEA, 0x00000000);
        send_team_metadata_change_notifications(s, team, 0, TeamMetadataChange::TEAM_NAME);
      }
      break;
    }
    default:
      throw runtime_error("invalid team command");
  }
  co_return;
}
