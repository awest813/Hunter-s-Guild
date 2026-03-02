#include "ShellCommands.hh"

#include <phosg/Strings.hh>

using namespace std;

ShellCommand c_reload(
    "reload", "reload ITEM [ITEM...]\n\
    Reload various parts of the server configuration. The items are:\n\
      accounts - reindex user accounts\n\
      battle-params - reload the BB enemy stats files\n\
      bb-keys - reload BB private keys\n\
      caches - clear all cached files\n\
      config - reload most fields from config.json\n\
      dol-files - reindex all DOL files\n\
      drop-tables - reload drop tables\n\
      ep3-cards - reload Episode 3 card definitions\n\
      ep3-maps - reload Episode 3 maps (not download quests)\n\
      ep3-tournaments - reload Episode 3 tournament state\n\
      functions - recompile all client-side patches and functions\n\
      item-definitions - reload item definitions files\n\
      item-name-index - regenerate item name list\n\
      level-tables - reload the player stats tables\n\
      patch-files - reindex the PC and BB patch directories\n\
      quests - reindex all quests (including Episode3 download quests)\n\
      set-tables - reload set data tables\n\
      teams - reindex all BB teams\n\
      text-index - reload in-game text\n\
      word-select - regenerate the Word Select translation table\n\
      all - do all of the above\n\
    Reloading will not affect items that are in use; for example, if an Episode\n\
    3 battle is in progress, it will continue to use the previous map and card\n\
    definitions until the battle ends. Similarly, BB clients are not forced to\n\
    disconnect or reload the battle parameters, so if these are changed without\n\
    restarting, clients may see (for example) EXP messages inconsistent with\n\
    the amounts of EXP actually received.",
    +[](ShellCommand::Args& args) -> asio::awaitable<deque<string>> {
      auto types = phosg::split(args.args, ' ');
      for (const auto& type : types) {
        if (type == "all") {
          args.s->load_all(true);
        } else if (type == "bb-keys") {
          args.s->load_bb_private_keys();
        } else if (type == "accounts") {
          args.s->load_accounts();
        } else if (type == "maps") {
          args.s->load_maps();
        } else if (type == "caches") {
          args.s->clear_file_caches();
        } else if (type == "patch-files") {
          args.s->load_patch_indexes();
        } else if (type == "ep3-cards") {
          args.s->load_ep3_cards();
        } else if (type == "ep3-maps") {
          args.s->load_ep3_maps();
        } else if (type == "ep3-tournaments") {
          args.s->load_ep3_tournament_state();
        } else if (type == "functions") {
          args.s->compile_functions();
        } else if (type == "dol-files") {
          args.s->load_dol_files();
        } else if (type == "set-tables") {
          args.s->load_set_data_tables();
        } else if (type == "battle-params") {
          args.s->load_battle_params();
        } else if (type == "level-tables") {
          args.s->load_level_tables();
        } else if (type == "text-index") {
          args.s->load_text_index();
        } else if (type == "word-select") {
          args.s->load_word_select_table();
        } else if (type == "item-definitions") {
          args.s->load_item_definitions();
        } else if (type == "item-name-index") {
          args.s->load_item_name_indexes();
        } else if (type == "drop-tables") {
          args.s->load_drop_tables();
        } else if (type == "config") {
          args.s->load_config_early();
          args.s->load_config_late();
        } else if (type == "teams") {
          args.s->load_teams();
        } else if (type == "quests") {
          args.s->load_quest_index();
        } else {
          throw runtime_error("invalid data type: " + type);
        }
      }

      co_return deque<string>{};
    });
