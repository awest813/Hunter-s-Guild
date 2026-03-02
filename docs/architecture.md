# newserv architecture map

This document is a contributor-facing map of the main runtime modules in newserv, where to implement changes, and which files are currently the highest-risk areas to modify.

## Runtime topology

At a high level:

1. `src/Main.cc` parses CLI arguments and runs one of:
   - a one-shot data/utility action (`compress-prs`, `decode-qst`, etc.)
   - the long-running server stack.
2. `ServerState` (`src/ServerState.hh/.cc`) owns configuration and shared runtime indexes/caches.
3. `GameServer` (`src/GameServer.hh/.cc`) accepts game/patch/proxy-entry connections.
4. `ReceiveCommands` / `ReceiveSubcommands` / `SendCommands` perform protocol handling.
5. Domain modules implement game-specific logic:
   - Episode 1/2/4 flow: lobby/map/item/drop handling
   - Episode 3 flow: `src/Episode3/*`
   - Proxy flow: `src/ProxyCommands.cc`, `src/ProxySession.hh/.cc`

## Where to make changes

### Login, lobby, and session flow
- **Primary files:** `src/ReceiveCommands.cc`, `src/SendCommands.cc`
- **Supporting state:** `src/Client.hh/.cc`, `src/Lobby.hh/.cc`, `src/ServerState.hh/.cc`
- **Use this area for:** login command handling, menu flow, lobby transitions, game creation.

### Subcommand/gameplay state
- **Primary file:** `src/ReceiveSubcommands.cc`
- **Supporting files:** `src/Map.hh/.cc`, `src/Items.cc`, `src/ItemCreator.cc`, `src/PlayerSubordinates.cc`
- **Use this area for:** in-game events, drops, object/enemy state transitions, quest flag/game-state updates.

### Proxy behavior
- **Primary files:** `src/ProxyCommands.cc`, `src/ProxySession.hh/.cc`
- **Supporting files:** `src/ReceiveCommands.cc` (proxy session lifecycle), `src/ChatCommands.cc` (proxy options commands)
- **Use this area for:** command rewriting, remote session synchronization, proxy-only cheats/options, saved-file extraction.

### Episode 3
- **Primary files:** `src/Episode3/Server.cc`, `src/Episode3/RulerServer.cc`, `src/Episode3/CardSpecial.cc`
- **Supporting files:** `src/Episode3/Tournament.cc`, `src/Episode3/DataIndexes.hh/.cc`
- **Use this area for:** CARD battles, tournaments, card behavior, Episode 3 map/card data parsing.

### Config/index loading
- **Primary file:** `src/ServerState.cc`
- **Use this area for:** config keys, reload behavior, data file indexing (quests/maps/cards/tables), lifecycle wiring.

### Shell/chat command surfaces
- **Shell commands:** `src/ShellCommands.cc`
- **Chat commands:** `src/ChatCommands.cc`
- **CLI actions:** `src/Main.cc`
- **Use this area for:** new operator/admin commands, debug controls, player-facing command features.

## Test map

### Replay tests
- **Files:** `tests/*.test.txt`
- **Wired by:** `CMakeLists.txt` (`--replay-log=...`)
- Best for validating protocol compatibility and end-to-end behavior regressions.

### Script tests
- **Files:** `tests/*.test.sh`
- **Wired by:** `CMakeLists.txt`
- Best for one-shot CLI behavior and focused integration checks.

### Shared test config
- **File:** `tests/config.json`
- Used by replay and script tests that require server behavior/config context.

## High-risk files (current)

These files are large and contain behavior from multiple concerns. Changes here should be small and replay-backed:

- `src/Map.cc`
- `src/ReceiveCommands.cc`
- `src/ReceiveSubcommands.cc`
- `src/SendCommands.cc`
- `src/QuestScript.cc`
- `src/Episode3/CardSpecial.cc`
- `src/Main.cc`
- `src/ServerState.cc`

## Change strategy guidance

When changing high-risk paths:

1. Add/adjust a targeted test first (`tests/*.test.sh` and/or replay case).
2. Keep behavior-preserving refactors separate from behavior changes.
3. Prefer additive logging while debugging; remove temporary debugging logs before merge.
4. Re-run the affected replay/script tests locally before pushing.
