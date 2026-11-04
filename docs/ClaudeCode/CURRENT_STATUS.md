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

## Stage 7 - controller input, 3 November 2026

**Confirmed on device first:** AlphaTest6 ran on a Quest 3 under WinlatorXR
standalone without crashing. The `sv_pure` packaging fix holds.

Input is implemented in `code/winlatorxr/xr_input.c`, hooked from
`CL_CreateCmd` (sticks) and `XR_Frame` (buttons).

### Two mechanisms, and why

**Digital controls become synthesised key events** (`Com_QueueEvent`,
`SE_KEY`), not usercmd button bits. The reason is the rule that keeps this
port out of the QVMs: half the controls asked for are not bits at all.
`reload`, `deconstruct` and `+scores` are *commands* cgame registers with
`trap_AddCommand`, and the purchase/evolve menu is a UI menu. None can be
expressed as a bit and none can be reimplemented engine-side, because a pure
server replaces the code that implements them. Feeding keys in through the
bind system gets all of them for free, respects the player's own binds, and
keeps `cl_run`/`BUTTON_WALKING` handling in `CL_KeyMove` where it already is.

**The sticks are analog** and cannot be a key event, so they go straight into
the usercmd (`forwardmove`/`rightmove`) and into `cl.viewangles[YAW]` - the
latter being *body* yaw, the same place `CL_MouseMove` puts mouse turn, with
head yaw still added on top in `XR_AdjustUsercmdAngles`.

### The mapping

| Control | Bool index (right-dominant) | Key cvar | Default | Effect |
|---|---|---|---|---|
| dominant trigger | 18 | `vr_key_fire` | MOUSE1 | `+attack` |
| off trigger | 7 | `vr_key_altFire` | MOUSE2 | `+button5` (attack2) |
| dominant grip | 12 | `vr_key_jump` | SPACE | `+moveup` |
| off grip | 0 | `vr_key_crouch` | c | `+movedown` |
| off stick click | 2 | `vr_key_sprint` | SHIFT | `+speed` |
| A | 10 | `vr_key_buy` | q | `+button7`, purchase/evolve |
| B | 11 | `vr_key_reload` | r | `reload` |
| X | 8 | `vr_key_scores` | TAB | `+scores` |
| Y | 9 | `vr_key_deconstruct` | e | `deconstruct` |
| off stick | floats 4-5 | - | - | move |
| dominant stick X | float 13 | - | - | turn body |
| dominant stick Y | float 14 | `vr_cmd_weapNext/Prev` | weapnext/weapprev | change weapon, latched |

`vr_dominantHand 1` mirrors triggers, grips, sticks and the face buttons
together. **Left menu is deliberately unmapped** - WinlatorXR turns it into
ESC and the owner's instruction was not to block it.

Other cvars: `vr_stickDeadzone` (0.15, rescaled not clipped),
`vr_turnSpeed` (140 deg/s), `vr_moveEnable`, `vr_turnEnable`,
`vr_inputAlways` (0; process input outside play, for desk testing).

### Verified on the desktop, against the spoofer

`scripts/xr_spoof.py` gained `--press` and `--sticks` to make this testable;
with neither it sweeps one bool index per second, which is the discovery mode.

| Check | Result |
|---|---|
| bool -> edge -> key event -> **Tremulous bind executed** | probe bind fired (F12 -> `echo`) |
| all 9 actions decode, all default key names resolve | 9/9, zero warnings |
| left-handed swap | index 7 -> fire, 0 -> jump, `hand=left` |
| stick forward / back / neutral | `fwd=+127` / `-127` / no contribution |
| movement actually moves the player | ~200 units as a spectator |
| turn direction and rate | stick right -> yaw decreases, ~stick x turnSpeed |
| `vr_turnEnable 0` / neutral deadzone | no yaw change / no usercmd contribution |
| index 1 (left menu) | `down=[-]`, correctly unmapped |
| trigger -> MOUSE1 while gated off, released on handover | confirmed |

### The menu click - [device, 3 Nov 2026]

The gate below switched the controller off outside play, which was right for
letter keys and wrong for the one control a menu actually needs. The owner
could not get through Tremulous's **spawn menu** at all without sending CTRL
from WinlatorXR's on-screen keyboard.

**Fixed:** while the gate is closed, either trigger sends `vr_key_menuClick`
(default MOUSE1) and nothing else is sent. MOUSE1 answers both halves of the
problem, which is why it is the default - it is the menu click while the UI
holds the keyboard, and it is Tremulous's `+attack` once the menu closes,
which is what CTRL was doing by hand.

Both triggers are OR'd into one logical key. Two independent down/up pairs on
the same key would let the first release strand the second press down.

Verified that the handover is clean in both directions with the trigger held
continuously across it: click while connecting, `down=[fire]` once playing,
and click again when the spawn menu opens - the second click edge only
happens if the first was properly released.

### Select-item, and a sprint mapping that was wrong - 3 Nov 2026

**Select item.** Cycling with weapnext/weapprev only SELECTS; Tremulous
expects ENTER (`+button2`, BUTTON_USE_HOLDABLE) to activate. That split is
deliberate in the game - it lets a player line up a medkit while still
shooting. Added as a second action row sharing the A button's bool index:
A now sends both `vr_key_buy` and `vr_key_useItem`. Two rows rather than one
row sending two keys, so either can be moved or disabled with one cvar.
An empty `vr_key_*` now means "disabled", silently, which is how to unpair
them. Watch for A spending an upgrade while shopping at an armoury.

**Sprint was mapped to the wrong control.** AlphaTest7 sent `+speed`.
`cg_tutorial.c`'s own binding table is unambiguous:

    { "+speed",   "Run/Walk" }      <- a walk modifier
    { "+button8", "Sprint"   }      <- BUTTON_SPRINT, burns stamina

Tremulous ships Sprint **unbound**, and said so on the HUD in as many words:
`Press "Sprint" (unbound) and any direction to sprint`. So the shipped
autoexec now does `bind HOME +button8` and `vr_key_sprint` points at it.
Verified by turning the tutorial back on and reading the HUD: it now says
**"Press HOME and any direction to sprint"**.

The `seta` in the autoexec is not redundant - `vr_key_sprint` is
CVAR_ARCHIVE and AlphaTest7 already saved "SHIFT" to autogen.cfg, which
would otherwise beat the new compiled default.

**The control tutorial text is off** (`cg_tutorial 0`, in the shipped
autoexec). It costs scarce HUD space and reads "Press MOUSE1" to someone
wearing a headset - technically correct for our synthetic keys and useless.
A cvar, not a code change: cg_tutorial belongs to cgame, and cgame is a QVM
a pure server replaces. Verified by screenshot, before and after.

**Screenshots are a usable verification channel on the desktop.** `screenshot`
plus a 40-line TGA-to-PNG converter settled two visual claims in this session
that no log could have. Worth remembering for anything HUD-shaped.

### The input gate, which is worth knowing about

Input is processed only while `clc.state == CA_ACTIVE` and the UI does not
hold the key catcher - the same condition as `vr_autoMode`. Synthesising
`c`, `r` or TAB into a 2D menu would type into its fields.

A consequence found during testing, and **not a defect**: joining a team
opens Tremulous's spawn menu, which holds `KEYCATCH_UI`, so the gate closes
until the player has spawned. The mouse drives those menus, by design.

`vr.log` now logs the gate on every transition, with `clc.state`, the key
catcher and `vr_inputAlways`, because "the controller does nothing" has three
different causes that are indistinguishable from inside a headset. That line
explained two anomalies in this session on its own.

### Still open, and honest about it

**The button table is from documents, not from this device.**
`XRAPI_V0_4_DESC.md` section 3 and Shogo's `XrApiUDP.h` agree on it, and
neither is a guess - but PROJECT_PLAN's instruction to confirm it against a
device log stands, and has not been done. `vr_verbose 1` logs
`input: bools="..." down=[...]` on every change, so one session of pressing
each button in turn settles it, and a wrong index shows up as the wrong name
rather than as "the controls feel odd".

Untested on device: everything in this stage.

---

## Stage 8 - the gun in the hand, 3 November 2026

Two designs were tried on the same day. The second is what ships.

**AlphaTest9, withdrawn.** The usercmd carried the GUN's direction so that
shots followed the hand, and the camera was rotated back onto the head
afterwards. On hardware, rotating the controller rotated the view. Owner's
verdict: *"the VR gun should be completely separate from the player"*.

**AlphaTest10, shipping.** Owner's design: *"make a fake gun, it's for
visuals only, the in-game gun can be the head, just visually offset
everything for VR."* The aim is the head, as it was in AlphaTest8. The
visible gun is a duplicate drawn at the dominant hand in full 6DOF and is
scenery. Shots leave along the view, so the crosshair is the aim and the
model is a prop.

That removed, rather than fixed, all three compensations AlphaTest9 needed:
the camera correction in `XR_AdjustRefdef`, the matching stick pre-rotation
in `XR_StickMove`, and the `vr_handAim` cvar that switched them. There is
nothing left to correct - the server is told what the head is doing, cgame
builds the view from that, and the view is simply right.

### The two defects AlphaTest9 shipped with

Both are worth keeping written down, because the second one is live code and
the first is the reason a passing test meant nothing.

**1. `cl.viewangles` is not the body's yaw in the world.** It is only the
half of the aim the client accumulates; the server keeps the rest in
`ps.delta_angles` and adds it back, which is how a spawn point, a teleporter
or `setviewpos` turns the player without the mouse moving. On a map whose
spawn faces 130 degrees, every `cl.viewangles`-based yaw is 130 degrees out.
Both render hooks now use

```c
bodyYaw = cl.viewangles[YAW];
if ( cl.snap.valid )
    bodyYaw += SHORT2ANGLE( cl.snap.ps.delta_angles[YAW] );
```

Everything downstream rotates the ROOM into the WORLD - where the hand is,
which way a lean goes - so the client's half alone put the gun beside the
wrong hip and sent a lean off sideways. **This one still matters**: the hand
gun and the positional lean both depend on it.

**2. A rotation does not cancel against itself across a change of basis.**
AlphaTest9's camera correction built the gun and head frames *without* body
yaw, on the reasoning that anything present in both would cancel. For an
additive term it would. For a rotation it does not: the result is body yaw
conjugated by the gun, `G^T.Rz(b).G`, which equals `Rz(b)` only when `b` is
zero. Modelled, with body yaw 130 and a 25-degree gun pitch, the camera came
out at pitch 36.6 where it should have been 5.

### Three more defects, found after the first device run

**Meta controller poses arrive upside down.** Owner on a Quest 3: *"the
rifle faced the right way but was upside down."* The 0.4 spec's HWXR excerpt
records this against the Meta codenames it falls through on, but EUREKA
takes its own branch there, so it is the **make** that decides, not the
model. `XR_GunRoll()` applies 180 degrees of roll about the barrel for any
META headset. Model only - the aim is the head, so a rolled controller could
never have moved a shot.

Detection is a guess, so it is overridable two ways, as the spec asks:
`vr_gunRoll` / `vr_gunYaw` (-1 = detect), and **`handsfix.txt`** beside the
executable or in `base/`, read with `fopen` rather than through the
filesystem - the player creates it by hand on the device, so it is not in a
pk3 and a pure server must not be able to hide it.

**The gun was placed twice.** cgame holds the weapon out in front of the
face; re-anchoring every captured entity at the hand without removing that
offset put the model at hand + cgame's eye offset. Roughly double the
distance, in a direction that no longer meant anything once the frame it was
measured in had been replaced. Subtracting the **first** captured entity's
offset - one shared displacement, not each entity's own - lands the root on
the hand and leaves the rigid assembly of hands, gun, barrel and flash
intact. The log now prints both distances so they can be seen to agree.

**The recentre latched the first packet.** Which is the packet most likely
to arrive before the runtime has a tracked position: a desktop run latched a
head height of 0.006 m, after which the hand came out 39 units *above* the
eye instead of 10 below. `XR_TryAutoRecentre()` now retries until the head
is at least 0.5 m up and logs how many packets it rejected. The bound is
loose on purpose - it rejects an origin that is not a measurement, it does
not police how the player sits. `vr_recentre` is never blocked.

Both of the last two produce symptoms that read as a broken pose transform
while the transform is correct, which is worth remembering: when the gun is
in a stupid place, suspect the **origin** before the maths.

### How the model is moved

`CG_AddViewWeapon` builds the gun from several entities - hands, gun, barrel,
flash - each on a tag of the one before, all inheriting `RF_FIRST_PERSON`.
Rather than move them individually, **every RF_FIRST_PERSON entity is
captured** in `CG_R_ADDREFENTITYTOSCENE` and the whole set is re-expressed
from cgame's camera frame into the gun's frame by one rigid transform in
`CG_R_RENDERSCENE`. Relative geometry survives for free and nothing needs to
know which entity is the barrel.

The source frame must be cgame's **original** refdef, before
`XR_AdjustRefdef` moves it - that is the frame cgame placed them in.
`RF_DEPTHHACK` is dropped: it squashes the gun into a narrow depth range,
which is right for a gun welded to the camera and wrong for one held at
arm's length.

**Aliens are excluded.** They have no gun - they attack with the body and the
face - so cgame's first-person model is left alone. `XR_HandGunActive()` is
the single place that decides.

Game constants (`STAT_TEAM`, `TEAM_HUMANS`, the `pm_type` values) are spelled
out numerically rather than included from `bg_public.h`, which is QVM
territory. A mod that renumbers them reads as "always alien", which fails
safe.

### Verified on the desktop, as a live spawned human at body yaw 130

| Check | Result |
|---|---|
| **camera is deaf to the hand** | `camYaw` 130.0 == expected 130.0 while gun yaw swept -40 to +12 and gun pitch swept +-25 |
| camera still follows the head | `camYaw` 150.4 / 167.0 / 179.5 == expected, head sweeping, hand sweeping independently |
| model sits ON the hand | hand 17 u out, model 17 u - the two distances agree, which is what the double-placement bug broke |
| roll is applied | `roll=180 (auto)` on META / QUEST 3 |
| `handsfix.txt` overrides | `roll false` gave `roll=0 (set)`; unknown keys and non-numeric values were rejected by name |
| recentre is sane | 1.596 / 1.641 / 1.646 m over three cold runs, against 0.006 m before the fix |
| gun orientation follows the hand | gun yaw tracked the sweep one-for-one; pitch -17 to -25 |
| body yaw is the world's | `bodyYaw=130.0` after `setviewpos ... 130`, which read 0.0 before the fix |
| no menu parse errors | 0 |

### Cvars

`vr_handGun` (1) master switch; `vr_gunForward` / `vr_gunRight` /
`vr_gunUp` position trim in engine units; `vr_gunPitch` / `vr_gunYaw` /
`vr_gunRoll` orientation trim in degrees, the last two taking -1 for
"detect from the headset". `handsfix.txt` sets the same three without a
console. `vr_handAim` is gone.

### Spoofer: why `--hand-sweep` had to exist

`--head-yaw` and `--hand-yaw` hold the head and hands at FIXED angles, and a
fixed hand angle is exactly what made AlphaTest9's test worthless. The server
absorbs it into `delta_angles` the moment anything sets the view angles;
after that the aim is constant and the camera cannot be observed tracking it.
The test passed with the bug completely intact.

`--hand-sweep` moves the hand in yaw and pitch on different periods, so the
camera has to hold still against a moving gun rather than against a parked
one. Use it, not `--hand-yaw`, for any claim about the camera.

Hand positions were also corrected earlier to absolute room coordinates
(y ~ 1.30, chest height) - they were at y = -0.25, under the floor, which put
the gun 55 units below the eye and looked exactly like a broken transform.

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

---

## Stage 9 - aiming with the hand on the trigger, 3 November 2026

Owner's call: *"when the player shoots we should aim the in-game aim to that
angle of the dominant hand."* So the AlphaTest9 scheme comes back - but
**only while the fire control is held**, instead of permanently.

### Is the render camera now a different camera from the game's?

The owner asked this directly and the answer changed with this build.

Up to AlphaTest10, **no**. The camera was cgame's own, with head roll and
the positional lean composed on top. The aim was the head, the server built
the player state from it, cgame built the view from that. Same camera,
decorated.

From stage 9, **yes, during a squeeze**. The usercmd carries the hand's
direction, cgame points the refdef down the barrel, and `XR_AdjustRefdef`
rotates the basis back onto the head before the renderer sees it.

### What keeps it from being AlphaTest9 again

Three things must switch together or the view swings when the player
shoots: the aim, the camera correction, and the movement stick. All three
now ask **`XR_GunAimActive()`** and nothing else. The AlphaTest9 lesson is
in the structure rather than in a comment.

Two fixes carried into the restored correction:

1. **Body yaw goes in both frames.** `V' = V . G^T . H` is only right when
   `G` and `H` are the frames `V` is expressed against, which are world
   ones. Omitting a shared `Rz(b)` from both does not cancel - it conjugates
   - so the error was zero at yaw 0, where the old test stood.
2. **The correction reads a latch, not a fresh sample.** The angles that
   were actually sent are stored by `XR_AdjustUsercmdAngles` and reused in
   the render hook. Re-sampling the pose a few milliseconds later corrects
   against an aim that was never sent, which shimmers on exactly the frames
   where the player is shooting.

### The 2D layer - `xr_hud.c`

**Crosshair off** by setting cgame's own `cg_drawCrosshair` to 0, not by
filtering draw calls. cgame then also skips the hit marker and the
per-weapon crosshair sizes, rather than us trying to recognise those in a
stream of untagged quads. It works under `sv_pure` because the cvar is read
out of the client.

**`vr_hudScale` (0.5)** shrinks the whole 2D layer towards the centre, so a
HUD laid out for a monitor lands where a headset can resolve it. Every 2D
element - text included, since glyphs are quads - arrives through
`CG_R_DRAWSTRETCHPIC`, so one transform there moves all of it and the 3D
view is untouched. cgame has already run `CG_AdjustFrom640`, so the
coordinates are real pixels and the centre is the renderer's own size
halved.

Full-screen quads are skipped: loading screens and fades are drawn like any
other bar, and shrinking one leaves the un-faded original round the edge.

### Verified

| Check | Result |
|---|---|
| **camera holds while FIRING** | 29 gun-aim frames, `camYaw` 163.1 == expected every frame, gun sweeping 40 deg |
| camera holds while idle | `camYaw` 130.0 == expected |
| crosshair off | `cg_drawCrosshair 0`, logged with the reason |
| HUD shrinks, world does not | screenshot at 1.0 against 0.5 |
