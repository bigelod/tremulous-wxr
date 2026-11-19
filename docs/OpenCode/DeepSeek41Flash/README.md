# DeepSeek 4.1 Flash - working notes

**Written 16 November 2026, by DeepSeek 4.1 Flash under OpenCode.**

These are my own notes, not a replacement for the project's documents. If
anything here disagrees with `docs/general_wxr/`, `docs/ClaudeCode/` or the
code, the original wins and this file is the thing to fix.

## What this folder is for

The project has three sets of documentation written by three different
agents, plus a codebase that has moved some way past all of them. This folder
is the bridge:

| File | What it is |
|---|---|
| [`01-reading-notes.md`](01-reading-notes.md) | What the three document sets actually say, and which parts to trust |
| [`02-code-state.md`](02-code-state.md) | The repository as I found it, verified in the code rather than in the docs |
| [`03-invariants.md`](03-invariants.md) | The rules I will not break while working here, with the line that implements each |
| [`04-open-items.md`](04-open-items.md) | What is unfinished, what is unverified, and the questions that need the owner |
| [`05-session-log.md`](05-session-log.md) | Dated log, newest last. Append; do not rewrite history |
| [`06-stage1-renderer.md`](06-stage1-renderer.md) | The on-device OpenGL measurement in full: what it decides, the native Vulkan probe, the protocol |
| [`07-button-census.md`](07-button-census.md) | What can be verified on PC, what only the headset can answer, and the two-minute device protocol |
| [`08-vr-menu.md`](08-vr-menu.md) | The engine-side VR settings overlay: how to open it, the pages, and what it deliberately does not do |

## Where I think the project is, in one paragraph

This is no longer a sketch. There are ~3,800 lines of real VR code in
`code/winlatorxr/`, wired into the engine at three hook points that survive
`sv_pure`, and the last two shipped builds (`AlphaTest10`, `AlphaTest11`)
implement a hand-held gun, hand aim while firing, HUD scaling and a hidden
crosshair. Everything in stages 2 through 6 has been verified on the desktop
against a spoofed runtime and a real Quest 3 capture; stages 7 and 8 have
mixed device evidence; stage 9 is desktop-only, and stage 10's per-class world
scale is now implemented and build-clean but unmeasured on hardware. An
engine-side VR settings menu (DEBUG / QUALITY) now removes the console from
the tuning loop. The one structural question still open is **on-device OpenGL
performance** (stage 1, deliberately deferred). The most valuable next step is
not code: it is one device session that confirms AlphaTest11, census-maps the
controller button indices against a real packet, and runs the stage 1
measurement.

## Trust order

1. **Code** - what it does now.
2. **`docs/general_wxr/`** - protocol and practice, maintained across ports,
   and written to be corrected.
3. **`docs/ClaudeCode/`** - this project's own record. Honest and mostly
   current, but it stops at AlphaTest11 in the release folder while
   `CURRENT_STATUS.md` stops earlier in places, and its `BUG_FIXES.md`
   preamble has gone stale.
4. **`docs/MetaAI/`** - a prior uncompiled sketch. Read for shape only.
   `docs/ClaudeCode/BUG_FIXES.md` catalogues exactly which of its parts are
   wrong, and why.

**Both `docs/ClaudeCode/` and `docs/MetaAI/` are archival.** Owner's decision,
16 November 2026: they are kept as a record of what was believed at the time
and **must not be edited**, even where they are stale. Only
`docs/general_wxr/` (for general findings) and this folder are live. See
`02-code-state.md` for the drift, recorded as an observation rather than a
to-do list.

## How I intend to work

- Engine-side only. Nothing under `code/cgame/`, `code/game/` or `code/ui/`.
- **Do not edit `docs/ClaudeCode/` or `docs/MetaAI/`.** They are archival.
  General findings go to `docs/general_wxr/`; everything else goes here.
- **Deploy at the end of any code change:** `scripts/deploy_all.ps1` builds
  once and stages three targets:
  - `E:\tmp\Q3Mods\trem-wxr` - the folder copied to the Quest
  - `D:\VR\tremulous-wxr` - the PC test install
  - `Release\DeployReady` - a clean full build, ready to zip or copy
- One change per deploy; nothing is done until the owner has seen it on
  hardware.
- Instrument before theorising - `vr.log` is the only channel back from the
  Quest, so a question the log cannot answer costs a full manual round trip.
- Verify against the code before repeating a claim from any document. Several
  claims in `CURRENT_STATUS.md` were true when written and are now stale; I
  list the ones I found in `02-code-state.md`.
- If something here turns out to be general rather than Tremulous-specific,
  it belongs in `docs/general_wxr/`, not in this folder.
