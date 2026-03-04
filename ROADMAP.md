# newserv Modernization Roadmap

## Guiding Principles

This roadmap describes improvements to newserv under two complementary goals:

1. **Vanilla PSO preservation** — Core gameplay mechanics (drop rates, enemy behavior, spawn logic, quest scripts, progression) must remain faithful to the original Sega server experience unless a fix addresses a verified Sega-side bug.
2. **Quality-of-life (QoL) fixes** — Improvements that reduce friction or fix client/server bugs are welcome as long as they do not alter the feel of playing PSO: item drop visibility, enemy HP bars, rare-drop notifications, and similar cosmetic or UX improvements.

The roadmap is organized into phases. Earlier phases address correctness and stability; later phases address new optional features and technical health.

---

## Current State Analysis

### Strengths
- Full multi-version support (DC, PC, GC, XB, BB, Ep3) with cross-version play.
- Rich server-side drop-mode system (`CLIENT`, `SERVER_SHARED`, `SERVER_PRIVATE`, `SERVER_DUPLICATE`).
- Extensive client-function patch infrastructure for GC and BB (BugFixes, EnemyHPBars, RareDropNotifications, CommonBank, ChatFeatures, DrawDistance, etc.).
- Proxy mode supports playing on official or third-party servers with newserv patches applied.
- Quest scripting support for all versions including full opcode documentation.
- Replay and smoke-test infrastructure for regression testing.

### Known Gaps and Technical Debt
- `ItemParameterTable` implementation explicitly marked as needing a rewrite (see its class comment).
- UI strings are not localizable; all menu text is hard-coded in English.
- `$switchit` chat command (activate nearby switch/laser-fence/door) is not yet implemented.
- `MeetUserExtensions` handling in proxy commands 41/C4 is incomplete.
- BB tests coverage is thin; serialization for ItemPMT/ItemPT/ItemRT is unimplemented.
- Several platform-specific gaps: DC HL check server, XB Guild Card exchange bug, XB F94D opcode, remaining GC ports.
- No persistent quest-flag mechanism across connections (needed for Meet User + B2 enable interactions).
- Episode 3 tournament deck restrictions are not enforced at COM population time.

---

## Phase 1 — Bug Fixes and Correctness (Highest Priority)

These items fix incorrect behavior without changing intended gameplay.

| # | Item | Area | Notes |
|---|------|------|-------|
| 1.1 | Fix enemy flag mapping in v2/v3 crossplay | Crossplay | Enemy flags differ between versions; incorrect mapping causes game-state divergence |
| 1.2 | Handle item replacement table in crossplay | Crossplay | Items that don't exist in target version must be substituted |
| 1.3 | Fix v2 challenge data in `$savechar`/`$loadchar` | PSO DC | Challenge-mode save data is silently corrupted |
| 1.4 | Fix XB Guild Card exchange from non-XB players | PSO XB | Receiving GCs from other versions crashes or fails silently |
| 1.5 | Fix Pouilly Slime EXP on BB | PSOBB | EXP grant for this enemy is currently broken |
| 1.6 | Fix proxy login flow for non-BB versions | Proxy | Proxy does not send 9C command when required by some login paths |
| 1.7 | Make `reload accounts` safe against online writes | Ep3 | Race condition can cause in-memory accounts to be overwritten from disk |
| 1.8 | Add all necessary GC number rewrites in BB proxy | PSOBB Proxy | Some BB commands still carry incorrect Guild Card numbers through the proxy |

---

## Phase 2 — QoL Improvements (Vanilla-Compatible)

These items improve the player experience without changing drop rates, enemy stats, or core gameplay rules. All features in this phase are opt-in via server configuration or chat commands.

| # | Item | Area | Notes |
|---|------|------|-------|
| 2.1 | Add `$switchit` chat command | Server-side | Activates nearest switch/laser fence/door flag; useful for testing and accessibility |
| 2.2 | Persist quest flags across connections | Server-side | Required for Meet User + B2 enable quest to work reliably |
| 2.3 | Add server-side story flag fixer | Server-side | Port the existing story-flag-fixer quest to run as a server patch at login |
| 2.4 | Make server-specified rare enemies work with proxy maps | PSOBB Proxy | Rare enemy overrides are ignored when maps are loaded through the proxy |
| 2.5 | Make `MeetUserExtensions` fully transparent in proxy | Proxy | Rewrite embedded 19 commands and track state in persistent per-connection config |
| 2.6 | Make UI strings localizable | Server-side | Move all menu/welcome text to an external string table so operators can translate it |
| 2.7 | Ep3 tournament deck restriction enforcement | Ep3 | Enforce rank checks and No Assist option when populating COMs at tournament start |
| 2.8 | Ep3 Meseta-based rank system | Ep3 | Implement visible player ranks derived from total lifetime Meseta earned |
| 2.9 | Research and document XB F94D quest opcode | PSO XB | Opcode behavior is unknown; document or implement once understood |
| 2.10 | Port remaining GC memory patches to XB | PSO XB | Several GC client-function patches have no XB equivalent yet |
| 2.11 | Ep3 NTE: AR code to remove SAMPLE overlays | Ep3 NTE | Visual polish for the trial edition; safe client-side patch |

---

## Phase 3 — Technical Modernization

These items improve code health, maintainability, and long-term extensibility. They have no visible effect on gameplay.

| # | Item | Area | Notes |
|---|------|------|-------|
| 3.1 | Rewrite `ItemParameterTable` | Core | Replace raw offset-table branching with virtual-function class hierarchy (see existing TODO comment in the class header) |
| 3.2 | Implement serialization for ItemPMT, ItemPT, ItemRT | PSOBB | Required for round-tripping modified tables; also blocks BB proxy rare-enemy feature |
| 3.3 | Expand BB smoke and regression tests | PSOBB | Record more BB game scenarios as replay tests; pair with the serialization work above |
| 3.4 | Investigate DC HL check server (Rust TLS) | PSO DC | Evaluate `blaze-ssl-async` crate as a basis for implementing the HL check server |
| 3.5 | Extend crossplay test coverage | Crossplay | Add replay tests covering v2/v3 crossplay drop scenarios once Phase 1 fixes land |

---

## Implementation Notes

### Adding a New Chat Command (`$switchit` example)
1. Implement the handler function in `src/ChatCommands.cc` following the pattern of existing commands (e.g., `cc_warp`).
2. Register a `ChatCommandDefinition` instance with the desired name(s).
3. Add a corresponding test entry in `tests/` following the shell-based test conventions.
4. Document the command in `README.md` under the "Chat commands" section.

### Adding or Modifying Client-Function Patches
- Patches live under `system/client-functions/<PatchName>/`.
- Each file is named `<PatchName>.<VersionTag>.patch.s` (e.g., `3___` = GC, `4___` = BB, `59NL` = PSOBB NA).
- Memory region assignments for GC patches are tracked in `system/client-functions/notes.txt`; update this file when adding new patches.

### Tracking Progress
- Individual bug-fix items from this roadmap should be moved to `TODO.md` once they are actively being worked on, following the existing format in that file.
- Completed items should be removed from both this file and `TODO.md` and noted in the commit message.
