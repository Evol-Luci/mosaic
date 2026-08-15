# Licensing

Mosaic is **dual-licensed**. This document explains the model, why it was chosen,
and what it requires of contributors. The license files themselves are
[`LICENSE`](../../LICENSE) (AGPLv3) and
[`LICENSE.commercial.md`](../../LICENSE.commercial.md).

## The goal

Keep Mosaic free for almost everyone, keep the creator's name and branding
attached to shipped work, and make it possible to earn from a project that makes
serious money — while staying as open as possible.

There is a hard constraint here: the OSI definition of "open source" forbids both
royalty requirements and restrictions on commercial use. **You cannot have literal
OSI-open-source *and* enforceable royalties.** Dual-licensing is how we get as
close as possible to all of the goals at once.

## Copyleft, plainly

Licenses come in two families:

- **Permissive (MIT / Apache):** "Do whatever you want — just keep my name in the
  credits." A company can build a closed product and give nothing back. No
  leverage for the author.
- **Copyleft (GPL / AGPL):** "You may use, modify, and ship this — **but whatever
  you build on top of it must also be released as open source under this same
  license.**" The openness spreads to derivative works.
  - **GPL** triggers on *distribution* (shipping a binary).
  - **AGPL** additionally triggers on *network use* (running it as a service),
    closing the "we only host it, never distribute" loophole — relevant for
    multiplayer servers and cloud tooling.

The thing a company actually pays to escape is **not the splash screen** — it is
the obligation to open-source their own game's code. A commercial studio will
never publish proprietary game source. That is the pressure point.

## The model

### Free tier — AGPLv3

- Anyone may use Mosaic for free.
- Copyleft: derivative works (including a game that links the engine) must also be
  released under AGPLv3.
- The **"Made with Mosaic"** branding must be retained.
- Perfect for hobbyists, students, open-source games, and game jams. Costs them
  nothing.

### Commercial tier — paid private license

- **Waives copyleft** — the studio keeps its source closed.
- **Removes the branding** requirement.
- Because it is a private contract, it **may carry royalty terms** — e.g. free
  under a revenue threshold, a percentage above it. This is how the author shares
  in a runaway success without breaking the open free tier.

Only studios that want a closed commercial product need the commercial license —
and they are precisely the ones with the revenue to pay for it.

## Requirements this imposes

- **The engine must be full GPL/AGPL, not LGPL.** LGPL specifically permits linking
  without the copyleft spreading, which would gut the commercial-license leverage.
  Full copyleft is what makes the buy-out valuable.
- **Copyright must be consolidated.** Every contributor signs a **Contributor
  License Agreement (CLA)** granting relicensing rights. Without it, a
  contributor's AGPL code cannot legally be included in paid commercial builds.
  The CLA is part of [CONTRIBUTING.md](../../CONTRIBUTING.md).
- **Mosaic Studio is GPLv3.** Studio integrates Blender (even out-of-process), so
  the Studio application is licensed GPLv3. The engine, foundation, runtime, and
  SDK remain dual-licensed as above.

## On tracking and enforcement

You cannot truly *track* who ships games with free software; there is no download
gate. Enforcement is **honor-system plus legal obligation**, and it targets the
big, visible successes — which are exactly the commercial users who owe a license
anyway. This is a deliberate trade: maximum freedom for the many, a claim on the
few who profit and choose to close their source.

## Status

This is the committed default (design spec §2.3). It is reversible before any
public release, but changing it later is disruptive, so it is settled now. Final
commercial terms live in the executed commercial agreement, not in this repo.
