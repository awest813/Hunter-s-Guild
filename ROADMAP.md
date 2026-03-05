# Hunter's Guild Roadmap

## Mission

Build and maintain a **stable, open-source, and sustainable** PSO server fork based on newserv, with vanilla-compatible behavior by default.

## Project Principles

1. **Stability over novelty**: prioritize bug fixes, replay safety, and reliable operation.
2. **Open-source sustainability**: prefer maintainable designs, clear docs, and contributor-friendly workflows.
3. **Vanilla-first defaults**: preserve core PSO gameplay unless a change is an explicit fix or opt-in feature.
4. **Incremental delivery**: ship small, testable improvements in phases.

---

## Current Phase

### Phase 1 — Foundation and Stabilization

**Status:** In Progress

This is the active phase for the fork. Each item has a clear acceptance criterion so contributors know exactly when it is done.

#### 1.1 — Triage and fix high-impact crashes/desyncs

**Outcome:** Fewer production incidents and safer cross-version sessions.

Acceptance criteria:
- Known crash and desync bugs are triaged in the GitHub issue tracker with reproduction steps.
- High-impact issues (crashes, data loss, persistent desyncs) are fixed and verified before merging.
- Fixes include a brief description of the root cause in the commit message or PR body.

#### 1.2 — Improve regression confidence with replay/smoke tests

**Outcome:** Faster, safer releases with less manual validation.

Acceptance criteria:
- At least one reproducible smoke test or replay test exists for a previously reported crash or desync path.
- The CI workflow runs the test suite on every PR.
- New bug fixes are accompanied by a test where feasible.

#### 1.3 — Tighten operational documentation

**Outcome:** Easier onboarding and lower maintenance overhead.

Acceptance criteria:
- `README.md` is updated to reflect the fork's goals, setup steps, and correct repository URLs.
- `ROADMAP.md` clearly describes phases, acceptance criteria, and how contributions fit in.
- `TODO.md` is kept aligned with the GitHub issue tracker (no stale or duplicate entries).
- Setup instructions are verified to work on a fresh Linux environment.

#### 1.4 — Keep issue tracking aligned to shipped changes

**Outcome:** Transparent project status for contributors and operators.

Acceptance criteria:
- Every merged fix closes or updates its corresponding GitHub issue or TODO entry.
- Open TODO items that have shipped are removed or marked done.
- The issue tracker reflects only active, unresolved work.

---

## Next Phases

### Phase 2 — Vanilla-Compatible Quality of Life

Deliver opt-in improvements that do not alter core gameplay balance:

- Small usability and accessibility features.
- Documentation and operator ergonomics improvements.
- Additional safe proxy/server compatibility fixes.

**Entry criterion:** All Phase 1 acceptance criteria are met.

### Phase 3 — Long-Term Sustainability

Strengthen maintainability and contributor experience:

- Refactor high-complexity subsystems when justified by bug risk.
- Expand automated test coverage around historically fragile paths.
- Improve internal documentation for protocol and file format behavior.

**Entry criterion:** Phase 2 is substantially complete.

---

## How Work Gets Prioritized

1. Player-impacting correctness and crash fixes.
2. Changes that reduce maintenance burden for operators and contributors.
3. Optional features that remain vanilla-compatible and low-risk.

---

## How to Contribute

1. Check the [issue tracker](https://github.com/awest813/Hunter-s-Guild/issues) for open Phase 1 items.
2. Pick an issue, comment that you are working on it, and open a draft PR early.
3. Keep PRs small and focused — one fix or improvement per PR.
4. Ensure your changes don't break the CI build before requesting a review.
5. Update `TODO.md` and/or close the related issue when your change ships.
