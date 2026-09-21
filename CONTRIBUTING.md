# Contributing to Mosaic

Thanks for your interest in Mosaic. This document covers how the repository is
organized, how changes are proposed, and the one legal requirement every
contribution carries.

## Before you start

Read the [architecture](docs/architecture/ARCHITECTURE.md) and the
[repository layout](docs/project/repo-layout.md). Two things matter most:

1. **Respect the dependency rules.** `Foundation → Engine → Editor → Studio`.
   Forbidden edges (`Engine → Blender`, `Engine → AI`, `Engine → Editor`,
   `Foundation → *` upward) are not yet mechanically enforced (CI currently runs
   the unit suite and a headless smoke test, not a dependency-rule lint — see
   `docs/project/deferred-work-backlog.md`), so review carefully and don't
   introduce a violation; a lint pass is tracked as a follow-up.
2. **Respect the "Things Mosaic Will Never Do"**
   [contract](docs/architecture/ARCHITECTURE.md#things-mosaic-will-never-do). If a
   change conflicts with it, the change is wrong, not the list — amendments require
   an RFC.

Run `git config core.hooksPath .githooks` once per clone to enable the
pre-commit hook that catches stale auto-generated docs (the component
reference, scripting-globals reference, and `docs/index.html`'s nav
sections) before they land in a commit. This **replaces**, not
augments, any hooks already configured under `.git/hooks/` for this
clone — check there first if you have personal hooks set up.

## The RFC workflow

Non-trivial changes — new subsystems, data-model changes, public API surface,
anything touching an architectural boundary — start as an **RFC**:

1. Copy [`docs/rfcs/0000-template.md`](docs/rfcs/0000-template.md) to
   `docs/rfcs/NNNN-short-title.md` (next free number).
2. Fill it in, open a pull request, and discuss.
3. Implement only once the RFC is accepted.

Small, self-contained fixes (typos, docs, obvious bugs) can skip the RFC and go
straight to a PR.

## Contributor License Agreement (CLA)

**All contributions require signing the CLA.** This is not bureaucratic
box-ticking — it is what makes the project's licensing model possible.

Mosaic core is [dual-licensed](docs/project/licensing.md): AGPLv3 for the free
tier, plus a commercial license for closed products. To offer the commercial
license, the project must hold the right to relicense all of the code. The CLA
grants that right. Without it, a contribution made under AGPL could not legally
appear in a commercial build, and the dual-license model would break.

The CLA does **not** take away your rights to your own work — you retain
copyright; you grant Mosaic the right to relicense your contribution. Sign it once;
it covers your future contributions.

Two agreements are provided:

- **[Individual CLA (ICLA)](docs/legal/mosaic-icla.md)** — for contributors acting
  on their own behalf. Most contributors sign this.
- **[Corporate CLA (CCLA)](docs/legal/mosaic-ccla.md)** — for contributions made on
  behalf of an employer; lists authorized employees in Schedule A.

If your employer has rights to work you create, either have them sign the CCLA or
confirm they have waived their rights before you sign the ICLA.

> ⚠️ Both agreements are drafts adapted from the Apache CLAs and **must be reviewed
> by legal counsel before the project accepts signatures**. The signing mechanism
> (CLA-assistant bot vs. signed commit) will be finalized during project setup.

## Pull requests

- Branch from the current development branch; keep PRs focused.
- Write clear commit messages describing *why*, not just *what*.
- Include tests for behavioral changes where the subsystem supports them.
- Ensure CI passes (unit suite + headless smoke test).

## Where work is tracked

The near-term target is the **v0.1 vertical slice** in the
[roadmap](docs/project/roadmap.md). If you want to help, that slice — and the
Phase 0 foundation work under it — is the place to start. The first buildable-
against artifact is [RFC 0001: Core Data Model](docs/rfcs/0001-core-data-model.md).

## Code of Conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).
