#include "SendCommands.hh"

#include <algorithm>
#include <stdexcept>

using namespace std;

static S_TeamInfoForPlayer_BB_13EA_15EA_Entry team_metadata_for_client(shared_ptr<Client> c) {
  auto team = c->team();
  S_TeamInfoForPlayer_BB_13EA_15EA_Entry cmd;
  cmd.lobby_client_id = c->lobby_client_id;
  cmd.guild_card_number = c->login->account->account_id;
  cmd.player_name = c->character_file()->disp.name;
  if (team) {
    cmd.membership = team->base_membership_for_member(c->login->account->account_id);
    if (team->flag_data) {
      cmd.flag_data = *team->flag_data;
    }
  }
  return cmd;
}

void send_update_team_membership(shared_ptr<Client> c) {
  auto team = c->team();
  S_UpdateTeamMembership_BB_12EA cmd;
  if (team) {
    cmd.membership = team->base_membership_for_member(c->login->account->account_id);
  }
  send_command_t(c, 0x12EA, 0x00000000, cmd);
}

void send_update_team_metadata_for_client(shared_ptr<Client> c) {
  auto l = c->require_lobby();
  auto metadata = team_metadata_for_client(c);
  for (auto lc : l->clients) {
    if (lc && lc->version() == Version::BB_V4) {
      send_command_t(lc, 0x15EA, 0x00000001, metadata);
    }
  }
}

void send_all_nearby_team_metadatas_to_client(shared_ptr<Client> c, bool is_13EA) {
  auto l = c->require_lobby();

  vector<S_TeamInfoForPlayer_BB_13EA_15EA_Entry> entries;
  entries.reserve(l->count_clients());
  for (auto lc : l->clients) {
    if (lc) {
      entries.emplace_back(team_metadata_for_client(lc));
    }
  }
  send_command_vt(c, is_13EA ? 0x13EA : 0x15EA, entries.size(), entries);
}

void send_update_team_reward_flags(shared_ptr<Client> c) {
  auto team = c->team();
  send_command(c, 0x1DEA, team ? team->reward_flags : 0x00000000);
}

void send_team_member_list(shared_ptr<Client> c) {
  auto team = c->team();
  if (!team) {
    throw runtime_error("client is not in a team");
  }

  vector<const TeamIndex::Team::Member*> members;
  for (const auto& it : team->members) {
    members.emplace_back(&it.second);
  }
  auto rank_fn = +[](const TeamIndex::Team::Member* a, const TeamIndex::Team::Member* b) {
    return a->points > b->points;
  };
  sort(members.begin(), members.end(), rank_fn);

  S_TeamMemberList_BB_09EA header;
  header.entry_count = members.size();

  vector<S_TeamMemberList_BB_09EA::Entry> entries;
  entries.reserve(header.entry_count);
  for (size_t z = 0; z < members.size(); z++) {
    const auto* m = members[z];
    auto& e = entries.emplace_back();
    e.rank = z + 1;
    e.privilege_level = m->privilege_level();
    e.guild_card_number = m->account_id;
    e.name.encode(m->name, c->language());
  }

  send_command_t_vt(c, 0x09EA, 0x00000000, header, entries);
}

void send_intra_team_ranking(shared_ptr<Client> c) {
  auto team = c->team();
  if (!team) {
    throw runtime_error("client is not in a team");
  }

  // TODO: At some point we should maintain a sorted index instead of sorting these on-demand.
  vector<const TeamIndex::Team::Member*> members;
  for (const auto& it : team->members) {
    members.emplace_back(&it.second);
  }
  auto rank_fn = +[](const TeamIndex::Team::Member* a, const TeamIndex::Team::Member* b) {
    return a->points > b->points;
  };
  sort(members.begin(), members.end(), rank_fn);

  S_IntraTeamRanking_BB_18EA cmd;
  cmd.points_remaining = team->points - team->spent_points;
  cmd.num_entries = members.size();

  vector<S_IntraTeamRanking_BB_18EA::Entry> entries;
  for (size_t z = 0; z < members.size(); z++) {
    const auto* m = members[z];
    cmd.ranking_points += m->points;
    auto& e = entries.emplace_back();
    e.rank = z + 1;
    e.privilege_level = m->privilege_level();
    e.guild_card_number = m->account_id;
    e.player_name.encode(m->name);
    e.points = m->points;
  }

  send_command_t_vt(c, 0x18EA, 0x00000000, cmd, entries);
}

void send_cross_team_ranking(shared_ptr<Client> c) {
  auto s = c->require_server_state();

  // TODO: At some point we should maintain a sorted index instead of sorting these on-demand.
  auto teams = s->team_index->all();
  auto rank_fn = +[](const shared_ptr<const TeamIndex::Team>& a, const shared_ptr<const TeamIndex::Team>& b) {
    return a->points > b->points;
  };
  sort(teams.begin(), teams.end(), rank_fn);

  size_t num_to_send = min<size_t>(teams.size(), 0x300);

  S_CrossTeamRanking_BB_1CEA cmd;
  cmd.num_entries = num_to_send;

  vector<S_CrossTeamRanking_BB_1CEA::Entry> entries;
  for (size_t z = 0; z < num_to_send; z++) {
    auto t = teams[z];
    auto& e = entries.emplace_back();
    e.team_name.encode(t->name, c->language());
    e.team_points = t->points;
    e.unknown_a1 = 0x01020304;
  }

  send_command_t_vt(c, 0x1CEA, 0x00000000, cmd, entries);
}

void send_team_reward_list(shared_ptr<Client> c, bool show_purchased) {
  auto team = c->team();
  if (!team) {
    throw runtime_error("user is not in a team");
  }
  auto s = c->require_server_state();

  // Hide item rewards if the player's bank is full
  auto bank = c->bank_file();
  bool show_item_rewards = show_purchased || (bank->items.size() < bank->max_items);

  vector<S_TeamRewardList_BB_19EA_1AEA::Entry> entries;
  for (const auto& reward : s->team_index->reward_definitions()) {
    // In the buy menu, hide rewards that can't be bought again (that is, unique rewards that the team already has). In
    // the bought menu, hide rewards that the team does not have or that can be bought again.
    if (show_purchased != (team->has_reward(reward.key) && reward.is_unique)) {
      continue;
    }
    if (!show_item_rewards && !reward.reward_item.empty()) {
      continue;
    }
    bool has_all_prerequisites = true;
    for (const auto& key : reward.prerequisite_keys) {
      if (!team->has_reward(key)) {
        has_all_prerequisites = false;
        break;
      }
    }
    if (!has_all_prerequisites) {
      continue;
    }
    auto& e = entries.emplace_back();
    e.name.encode(reward.name, c->language());
    e.description.encode(reward.description, c->language());
    e.reward_id = reward.menu_item_id;
    e.team_points = reward.team_points;
  }

  S_TeamRewardList_BB_19EA_1AEA cmd;
  cmd.num_entries = entries.size();

  send_command_t_vt(c, show_purchased ? 0x19EA : 0x1AEA, 0x00000000, cmd, entries);
}

void send_team_metadata_change_notifications(
    shared_ptr<ServerState> s,
    shared_ptr<const TeamIndex::Team> team,
    uint32_t changed_member_account_id,
    uint8_t what) {
  using TMC = TeamMetadataChange;
  for (const auto& it : team->members) {
    try {
      auto member_c = s->find_client(nullptr, it.second.account_id);
      bool is_changed_client = (member_c->login && (member_c->login->account->account_id == changed_member_account_id));
      if (is_changed_client || (what & TMC::TEAM_MASTER)) {
        send_update_lobby_data_bb(member_c);
      }
      if (is_changed_client || (what & (TMC::TEAM_MASTER | TMC::TEAM_NAME | TMC::TEAM_MEMBER_COUNT))) {
        send_update_team_membership(member_c);
      }
      if (is_changed_client || (what & (TMC::TEAM_MASTER | TMC::FLAG_DATA | TMC::TEAM_NAME | TMC::TEAM_MEMBER_COUNT))) {
        send_update_team_metadata_for_client(member_c);
      }
      if (is_changed_client || (what & TMC::REWARD_FLAGS)) {
        send_update_team_reward_flags(member_c);
      }
    } catch (const out_of_range&) {
    }
  }
}
