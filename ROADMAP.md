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

### Phase 1 — Foundation and Stabilization (In Progress)

This is the active phase for the fork.

| # | Priority Item | Outcome |
|---|---------------|---------|
| 1.1 | Triage and fix high-impact crashes/desyncs | Fewer production incidents and safer cross-version sessions |
| 1.2 | Improve regression confidence with replay/smoke tests | Faster, safer releases with less manual validation |
| 1.3 | Tighten operational docs (`README.md`, setup, recovery notes) | Easier onboarding and lower maintenance overhead |
| 1.4 | Keep TODO/issue tracking aligned to shipped changes | Transparent project status for contributors and operators |

---

## Next Phases

### Phase 2 — Vanilla-Compatible Quality of Life

Deliver opt-in improvements that do not alter core gameplay balance:

- Small usability and accessibility features.
- Documentation and operator ergonomics improvements.
- Additional safe proxy/server compatibility fixes.

### Phase 3 — Long-Term Sustainability

Strengthen maintainability and contributor experience:

- Refactor high-complexity subsystems when justified by bug risk.
- Expand automated test coverage around historically fragile paths.
- Improve internal documentation for protocol and file format behavior.

---

## How Work Gets Prioritized

1. Player-impacting correctness and crash fixes.
2. Changes that reduce maintenance burden for operators and contributors.
3. Optional features that remain vanilla-compatible and low-risk.
