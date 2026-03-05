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

### Phase 2 — Vanilla-Compatible Quality of Life

**Status:** In Progress

This is the active phase for the fork. Focus on opt-in improvements that preserve vanilla gameplay while improving usability and maintainability.

Acceptance criteria:
- Opt-in quality-of-life improvements are documented and shipped without changing default gameplay balance.
- Operator-facing ergonomics or documentation improvements reduce setup/maintenance overhead.
- Compatibility fixes remain low-risk and are validated by existing tests where feasible.

### Phase 1 — Foundation and Stabilization

**Status:** Completed

This phase established the baseline for stability, testing confidence, and documentation alignment.

---

## Next Phases

### Phase 3 — Long-Term Sustainability

Strengthen maintainability and contributor experience:

- Refactor high-complexity subsystems when justified by bug risk.
- Expand automated test coverage around historically fragile paths.
- Improve internal documentation for protocol and file format behavior.

**Entry criterion:** Phase 2 acceptance criteria are met.

---

## How Work Gets Prioritized

1. Player-impacting correctness and crash fixes.
2. Changes that reduce maintenance burden for operators and contributors.
3. Optional features that remain vanilla-compatible and low-risk.

---

## How to Contribute

1. Check the [issue tracker](https://github.com/awest813/Hunter-s-Guild/issues) for open items aligned with the current phase.
2. Pick an issue, comment that you are working on it, and open a draft PR early.
3. Keep PRs small and focused — one fix or improvement per PR.
4. Ensure your changes don't break the CI build before requesting a review.
5. Update `TODO.md` and/or close the related issue when your change ships.
