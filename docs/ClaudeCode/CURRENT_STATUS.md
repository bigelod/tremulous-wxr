# CURRENT_STATUS

**As of 31 October 2026.** Updated when something changes on hardware, not
when something is written.

---

## One-line status

**Stages 2 and 4 are code-complete and verified on PC. Nothing has run on
hardware yet, so no checkpoint is signed off.** The XrApi 0.5 client parses,
normalises and replies correctly against a spoofed runtime, and `vr.log`
carries the build stamp on line 1. **Stage 1 - the on-device OpenGL
characterisation - has been deliberately deferred** at the owner's decision:
the renderer is swappable later and stages 2, 4 and 6 do not depend on it.

The next action is **stage 6**, the 6DOF camera via `CG_R_RENDERSCENE`,
which can be developed against the spoofer on PC. Stage 5 (sync pixel) is the
one piece that a renderer swap would force us to rewrite.

---

## What is actually in the repository

| | State |
|---|---|
| Tremulous engine, flat, desktop | Builds and runs x64 Release. `tremded` reaches `atcs` with our own QVMs |
| VR code | `code/winlatorxr/xr_api.{c,h}`, wired into `cmake/client.cmake` |
| XrApi client | **Working on PC.** 0.5 handshake, bind 7872, drain, parse, normalise, outbound on 7278 |
| Sync pixel | Not started - **stage 5**, and the only renderer-coupled piece |
| Head tracking | Not started - **stage 6**, next action |
| Window handling | Stock SDL2 |
| Build-and-deploy script | `scripts/deploy_wxr.ps1`, freshness guard tested and confirmed to refuse |
| Runtime simulator | `scripts/xr_spoof.py` - unattended, and sends deliberately non-unit quaternions |
| Container test on Quest 3 | **Not done.** Deferred by decision, not blocked |
| Planning docs | `docs/general_wxr/` (shared guidance), `docs/MetaAI/` (prior sketch), `docs/ClaudeCode/` (this set) |

`docs/general_wxr/` and `docs/MetaAI/` are both **committed** as of
`456dbb74` - 36 files. That open question is closed.

---

## Milestone board

| | Milestone | State |
|---|---|---|
| 0 | Feasibility assessed, intake complete, D1/D2 signed off | **Done** - 31 Oct 2026 |
| 1 | Unmodified Tremulous runs in a container; OpenGL path characterised | **Deferred by decision, 31 Oct 2026.** Builds and runs on PC; not yet on device |
| 2 | Our code loads, logs, build stamp, deploy guard | **Code complete, PC-verified.** CHECKPOINT 2 outstanding (device) |
| 3 | Borderless window filling the display | Not started |
| 4 | XrApi 0.5 client, device-verified button table | **Code complete, PC-verified.** CHECKPOINT 4 outstanding - needs one real device packet and a measured button table |
| 5 | Sync pixel | Not started - renderer-coupled, **next action** |
| 6 | 6DOF head tracking feels locked | **Code complete, PC-verified** as a spawned rifle human. CHECKPOINT 6 outstanding (device) |
| 7 | Presentation mode chosen and measured | Not started |
| 8 | Controller input, owner-confirmed mapping | Not started |
| 9 | HUD legibility and menu mode switching | Not started |
| 10 | Per-class scale, comfort, in-game menu | Not started |

---

## What the prior attempt leaves us

`docs/MetaAI/` contains a prior design pass: a 193-line XrApi client
(`tremulous_vs2022_full_sln/code/winlatorxr/xr_api.c`), four prose patch
notes, a VS2022 solution, a `vcpkg.json`, a UDP spoofer script, and four
planning documents.

**None of it is integrated, and nothing in this repository suggests it was
ever compiled or run.** Its `CURRENT_PROGRESS.md` describes the transport
layer and build system as DONE, but no corresponding file exists here. Its
`BUG_FIXES.md` lists fixes for defects that are still present in the code it
ships alongside, and at least two of the entries describe mechanisms that do
not work the way stated. Read it as a sketch written from the protocol
documentation, not as a report of work done.

**Useful from it:**

- the overall shape - transport in its own module, handshake files, sync
  pixel in the renderer, mono first
- the `vcpkg.json` dependency list (SDL2, openal-soft, curl, zlib,
  libjpeg-turbo, freetype)
- `scripts/test_udp_spoofer.py` as a starting point, pending a check against
  a real device capture

**Not usable:**

- the client code itself. See `BUG_FIXES.md` for the catalogue - the most
  serious items are a fabricated packet layout, no quaternion normalisation,
  an outbound FOV the runtime ignores, and a hand-rolled quaternion-to-Euler
  conversion the engine does not need
- the `cg_view.c` approach. It puts the camera in a QVM module that pure
  servers replace at connect time, and calls Win32 critical sections from
  inside the VM. The equivalent engine-side hook is
  `CG_R_RENDERSCENE` in `code/client/cl_cgame.c:577`

---

## Decisions settled, 31 October 2026

- **D1, graphics backend.** Keep OpenGL if stage 1 says it holds up;
  otherwise port Quake3e's `renderervk` as `renderer_vulkan`. **DirectX is
  ruled out.** Still gated on stage 1 measurements - Vulkan if we must, not
  on principle.
- **D2, bitness.** **x64**, because we compile the whole game from source.
- **Deploy target.** `D:/VR/tremulous-wxr`. The owner copies that folder to
  the Quest 3 and runs it by hand. No adb loop.
- **Diagnostics.** `vr.log` next to the executable is the primary channel
  both on device and on PC, with the build stamp on line 1. See "Logging and
  the feedback loop" in `PROJECT_PLAN.md`.
- **Desktop testing.** XrAPI UDP Tester at
  `D:\Daven Bigelow\Documents\GitHub\XrAPIUDPTester\XrAPITestUDP\XrAPITestUDP\bin\Release\net8.0-windows\XrAPITestUDP.exe`.

## Findings worth not rediscovering, 31 October 2026

- **The `cmake` on the owner's PATH is 3.8.2 (2017).** `CMakeLists.txt`
  requires 3.25 and 3.8 does not know the VS2022 generator exists, so the
  failure is a confusing generator error rather than a version complaint. The
  usable copy is the one Visual Studio ships, at
  `…/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`.
  `scripts/deploy_wxr.ps1` resolves it via `vswhere` and refuses anything
  below 3.25.

- **GPP1 is protocol 70; this tree is protocol 72.** They are not network
  compatible. `tremulous-gpp.exe` cannot join our servers and vice versa.
  **Decision, 31 Oct 2026: we target protocol 72**, this tree, the maintained
  continuation. Revisit only if the live server population turns out to be
  elsewhere.

- **GPP1 was a mod directory, not a game.** The 2009 engine hardcodes
  `BASEGAME "base"` and defaults `fs_game` to `"gpp"`, so GPP overlays the
  1.1.0 data - which is why `gpp/` is under 1 MB. Our tree collapsed that:
  `fs_game` defaults to `""` and the repo's `assets/` is the maintained
  successor to `data-gpp1.pk3` (same top-level shape). Correct data layout is
  **1.1.0's pk3s plus the repo's loose `assets/`**, not `data-gpp1.pk3`.

- **Loose files beat pk3s, verified not assumed.** `FS_AddGameDirectory`
  prepends each pk3 in sorted order and then prepends the plain directory
  *last*, so the loose directory sits at the head of `fs_searchpaths`
  (`code/qcommon/files.c:3000-3010`). Confirmed in a live search-path dump.
  This is what lets the 1.2 `assets/` shadow 1.1.0's stale `ui` and `configs`.

- **`vms-1.1.0.pk3` is deliberately not staged.** It holds 1.1.0's QVMs; ours
  supersede them by precedence, but keeping it is a free variable in a
  debugging session.

- **"Turnip struggles with OpenGL" is an argument *for* Vulkan.** Turnip is a
  Vulkan driver; there is no OpenGL in it. GL in the container runs
  GL → Zink → Turnip → Adreno, so a slow GL path indicts the *translation
  layer*, and a native Vulkan renderer deletes Zink from the stack. Before
  committing to the `renderervk` port, run a known-good **native Vulkan** app
  in the same container: that separates "Zink overhead" from "Turnip is
  broken here" for the price of one trip instead of a renderer port.

- **The engine already stamps the git hash** into its version string
  (`tremulous 1.2.0_g456dbb74`). No separate build id was needed.

- **The headset codename mapping is HWXR's, reproduced verbatim** from
  `XRAPI_V0_4_DESC.md` section 2 - not invented. `EUREKA`/`PANTHER` are a
  Quest 3; every other Meta codename falls through to `QUEST 2`; a non-Meta
  make passes its codename through unchanged. Verified against six cases
  including an empty line 2 and a Pico.

  Two things to carry forward. The fall-through **swallows any codename newer
  than HWXR knew about**, so a future headset reports as a Quest 2 - the raw
  codename is logged alongside so the log still says what the device was.
  And this string is not cosmetic: HWXR's comments record that hands are
  upside down on `SEACLIFF`, `HOLLYWOOD` and `MONTEREY`, so **stage 8's hand
  orientation hangs off it.** Pico 4 Ultra, the secondary target, will report
  its own codename and therefore needs its own hand-orientation answer.

## Findings from stage 6, 1 November 2026

- **No handedness flip in this engine.** Q3 is right-handed (+X forward,
  +Y left, +Z up) and so is OpenXR, so the conversion is the pure relabel
  `(x,y,z)_xr -> (-z,-x,y)_q3`, determinant +1. The generic recipe in
  `XRAPI_V0_4_DESC.md` flipped z for a left-handed engine; applying that here
  would have mirrored the world. **Folded back into the general docs.**

- **World scale is 30 units/metre, derived not guessed.** `human_base.cfg`
  puts the eye at `viewheight 24` with `mins[2] -24`, so 48 units above the
  feet; at a 1.6 m human eye height that is 30. Still only correct for *one*
  class - see D9.

- **Head yaw/pitch go through the usercmd, never the render hook.** The
  server derives the player state from `cmd->angles` and cgame builds
  `viewaxis` from the player state, so the camera follows with no second
  application. `cl.viewangles` is adjusted only for the duration of
  `CL_FinishMove` and restored immediately, so the engine keeps accumulating
  mouse and stick onto body angles alone. Verified: `bodyYaw` holds steady
  while head yaw sweeps 60 degrees.

- **Head tracking is inert while spectating a player.** In follow mode the
  view angles come from the followed client's player state, so our
  `cmd->angles` are ignored. Expected, not a defect - but it means a device
  test must *spawn* before judging the camera, or it will look broken.

- **Unattended test recipe**, for repeating this without touching anything:
  `+devmap atcs +exec vrtest.cfg`, where the cfg does
  `team humans` then `class rifle`. For humans, **`class` takes a WEAPON
  name** (`BG_Weapon(WP_MACHINEGUN)->name` == `"rifle"`), not a class name -
  `class human_base` only queues you and then opens the weapon-choice menu.

- **Two packet sources will silently interleave.** The XrAPI UDP Tester and
  `scripts/xr_spoof.py` both send to 7872; with both running the drain keeps
  whichever arrived last and the pose alternates between two headsets. It
  presents as out-of-range pitch and impossible roll. `raw:` plus
  `drained 2 queued datagrams` is the tell. Run one source at a time.

## Decisions taken 1 November 2026

- **The HUD stays in VR.** Dropping to a flat 2D mode just to read the HUD
  would be frustrating in normal play, and Tremulous's HUD carries
  information the player needs continuously - health, ammo, credits, the
  build menu. **`MODE_VR 0` is reserved for the main menu only, if it is
  needed there at all**, and rendering even that in-world is worth
  investigating rather than assumed impossible. This revises the D6 default
  of "`MODE_VR 0` in menus" to "`MODE_VR 0` for the main menu at most", and
  makes stage 9 a question about HUD *legibility and depth placement* rather
  than about mode switching. Owner's call, 1 Nov 2026.

## Still open

- Container resolution, after stage 1.
- **Stage 1 itself** - on-device OpenGL characterisation, deferred not cancelled.
- The **button table**, which must be built from a device log of the raw bool
  string rather than from any document. `vr_verbose 1` already logs it.
- Whether a native-Vulkan app runs well under Turnip in the owner's container.

---

## Currently debugging

Nothing. Stages 2 and 4 behave correctly against `scripts/xr_spoof.py`; the
open items are all device observations that have not been made yet.
