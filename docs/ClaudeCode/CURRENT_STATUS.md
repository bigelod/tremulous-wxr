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
| 3 | Borderless window filling the display | **Borderless done, PC-verified.** "Fills the display" is fullscreen's job on device; revisit if the container disagrees |
| 4 | XrApi 0.5 client, device-verified button table | **Code complete, PC-verified.** CHECKPOINT 4 outstanding - needs one real device packet and a measured button table |
| 5 | Sync pixel | **Code complete, PC-verified by screenshot.** CHECKPOINT 5/6 outstanding (device) |
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

## Findings from stage 5, 1 November 2026

- **The sync pixel lives in the CLIENT, before `re.EndFrame`.** The escape
  hatch people reach for - the SDL layer - is not one here: `sdl_glimp.c` is
  in `renderer_common.cmake`, so it is compiled into the renderer DLL along
  with everything else, and with `USE_RENDERER_DLOPEN` on it cannot reach
  `XR_GetState()` in the executable. But `re.EndFrame` is *called* from the
  client, in `SCR_UpdateScreen` (`code/client/cl_scrn.c`), after the world
  and the HUD. Drawing there with `re.DrawStretchPic` covers gl1 and gl2
  with one implementation and keeps the client's symbols in scope.

- **The 2D pass is in REAL PIXELS with y = 0 at the top.** `RB_SetGL2D` sets
  `qglOrtho(0, vidWidth, vidHeight, 0, 0, 1)`, so `(0,0,10,10)` is exactly
  the ten-by-ten the runtime wants. **Do not put these through
  `SCR_AdjustFrom640`** - that converts from the virtual 640x480 space and
  the widescreen correction would move the square off the sampled pixel.
  This is also the precise shape of BUG_FIXES item 5: the prior attempt had
  this same top-left ortho and still drew at `vidHeight-10`, the bottom.

- **Red is passed through byte for byte - measured, not assumed.** Sending a
  fixed `HMD_SYNC` of 12 renders as exactly `rgb(12,0,0)` at pixel (0,0). A
  gamma-2.2 encode would have produced about 68, so there is no colour
  management in the path and no reason to pre-correct. Green and blue are 0.
  Verified by screenshot with `scripts/check_sync_pixel.py`, per the
  instruction to check the corner rather than reason about the matrix.

- **`vr_syncPixel` is an off switch**, so "is the camera wrong or is sync
  wrong?" can be settled on the device by toggling one cvar rather than
  shipping two builds. Swimmy-with and swimmy-without are different
  diagnoses.

## Findings from stage 3, 1 November 2026

- **The engine already had borderless**, as `r_noborder` -> SDL_WINDOW_BORDERLESS
  (`code/sdl/sdl_glimp.c`). What was missing was forcing it on for VR and
  re-asserting it. `vr_borderless` (default 1) ORs into the mode-set call and
  is re-checked in `GLimp_EndFrame`, which only calls
  `SDL_SetWindowBordered` when the flag actually disagrees - so the common
  case costs one flag read and no window-manager traffic.

- **The renderer reaches VR settings through cvars, not symbols.**
  `sdl_glimp.c` is in the renderer DLL and cannot link to `XR_IsEnabled()`,
  but `ri.Cvar_Get` crosses the boundary, so `vr_enable` is simply read on
  that side. Worth remembering for anything else that has to live down there.

- **The deploy script's freshness guard was wrong, and the guard itself
  caught it.** It compared every source against `tremulous.exe` alone, so a
  renderer-only edit (`sdl_glimp.c` feeds only `renderer_opengl*.dll`)
  tripped it even though the build was correct. A guard that fires when
  nothing is wrong gets ignored, and then it is not a guard. It now compares
  the newest source against the **newest build artifact**, which answers the
  question that actually matters: did a build run since the last edit?

## Decisions taken 1 November 2026

- **The HUD stays in VR; the MENUS do not.** Revised on device the same day,
  and the distinction turned out to be the whole point.

  The HUD - health, ammo, credits, the build menu - is drawn by cgame over
  the world and must stay in VR, because dropping to flat just to read it
  would be constant and intolerable. Stage 9 is therefore about HUD
  *legibility and depth placement*, not mode switching.

  **Menus are the opposite.** `MODE_VR 1` from startup made the game
  unusable before you were in it: the runtime takes the mouse and presents
  immersively, so Tremulous's 2D menus could neither be clicked nor fitted
  in view. The alternative is rebuilding every menu for VR, which is not a
  trade worth making for a screen you pass through.

  **Implemented as `vr_autoMode` (default 1):** `MODE_VR 1` only while
  `clc.state == CA_ACTIVE` and the UI does not hold the key catcher;
  `MODE_VR 0` otherwise. That covers the main menu, connecting, loading,
  the in-game ESC menu, and - usefully, discovered rather than designed -
  Tremulous's own team and class menus, which are UI menus and so get the
  mouse back exactly when you need to pick a class.

  Owner's call, 1 Nov 2026, superseding the earlier "main menu at most".

- **No green debug square.** It was dropped. In HWXR that marker means "SBS
  is on"; we are mono-only, so it signalled nothing, and "is sync drawing?"
  is already a log line. Owner's call, 1 Nov 2026.

- **The sync pixel draws on every presented frame, unconditionally** - over
  the HUD, in the pause menu, in the main menu, and while `MODE_VR` is 0,
  holding the last known value. Owner's call, 1 Nov 2026. Verified rather
  than assumed: `re.EndFrame` is reached from exactly one place in the whole
  client (`SCR_UpdateScreen`, two call sites), and `XR_DrawSyncPixel()`
  precedes both, so there is no frame that escapes it. The last-value hold
  falls out of never zeroing on a dropout.

  **Known tension, for the device:** the runtime derives our frame rate by
  counting changes in this value, so a held constant during `MODE_VR 0`
  reads as a frozen game. That is harmless while the runtime is not tracking
  us, but if anything misbehaves on re-entry to `MODE_VR 1`, this is the
  first thing to suspect.

## Still open

- Container resolution, after stage 1.
- **Stage 1 itself** - on-device OpenGL characterisation, deferred not cancelled.
- The **button table**, which must be built from a device log of the raw bool
  string rather than from any document. `vr_verbose 1` already logs it.
- Whether a native-Vulkan app runs well under Turnip in the owner's container.

---

## Fixed 1 November 2026 - the local-hosting crash

**`sv_pure` hides every loose file in `base/`, and that one fact caused both
the hosting crash and the broken UI.** Full write-up in `BUG_FIXES.md`
part 4 items 4 and 5; the short version:

- Hosting a game makes the client pure against its own server. `FS_FindVM`
  and `FS_FOpenFileRead` then skip **all** directory search paths, so our
  loose QVMs and loose 1.2 menus became unreadable. The UI VM failed to load
  on its second creation (hosting clears the hunk) and the engine raised
  `Com_Error( ERR_FATAL, "VM_Create on UI failed" )`.
- **Fixed** by packing `assets/` plus the built QVMs into `zzz-wxr.pk3`,
  staged by `deploy_wxr.ps1`. Named to sort last, because the alphabetically
  last pk3 is searched first. Verified on the desktop against the deployed
  folder: hosts and spawns, and menu parse errors went 401 -> 0.
- **Also fixed:** `autoexec.cfg` was never staged at all, so `logfile 2`
  never applied - which is why no `qconsole.log` ever came back from the
  device, and why this crash went two builds without a diagnosis the engine
  was printing the whole time.

**Both earlier hypotheses were wrong and neither had actually been tested**
(the Box64 JIT theory, and the `UI_GETCLIENTSTATE` NULL pointer), because the
config that was supposed to test them never shipped. The `UI_GETCLIENTSTATE`
guard stays as hardening; it is not the fix.

**Method note worth keeping.** "The log is the only channel back" is a
**Quest** constraint. This bug reproduced on the PC, and `cdb.exe` from the
Windows SDK is already on this machine. When a device bug reproduces on the
desktop, stop reading logs and attach a debugger.

---

## Currently debugging

Nothing. The local-hosting crash above is fixed and verified on the desktop,
and shipped as **`Release/AlphaTest6`** (see that folder's `README.txt`). It
has **not** yet been confirmed on the Quest, which is the next device run.

Packing is now one script, `scripts/make_wxr_pk3.ps1 -Target <folder>`, so a
folder staged by hand can be repaired without re-running the whole deploy.
`Release/AlphaTest4` and `AlphaTest5` were deleted on 1 Nov 2026 (owner's
call) once AlphaTest6 was verified; their write-ups are kept in
`Release/_archive/`, and the reproduction no longer needs a folder - staging
any build without the pk3 reproduces it.

Also fixed while verifying AlphaTest6: `vr.log` was written to the working
directory rather than beside the executable (`BUG_FIXES.md` part 4 item 6).

**One known, unrelated defect left over from that session**, recorded so it
is not mistaken for a regression: 26 `failed to parse
models/weapons/*/animation.cfg` errors. Those files exist in neither
`assets/` nor `data-1.1.0.pk3`, and the count is identical pure and non-pure,
so this is a pre-existing 1.1.0-data-against-1.2-code content gap, not a
purity or VR issue. Non-fatal - console spam only.

Stages 2 and 4 behave correctly against `scripts/xr_spoof.py`; the remaining
open items are device observations that have not been made yet.
