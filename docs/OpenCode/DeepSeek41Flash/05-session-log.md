# Session log

Append-only. Newest last. One entry per session, dated, with what was read,
what was changed, and what the next session should start with.

---

## 16 November 2026 - onboarding, no code changes

**Read, in the order the project asks for:**

- `docs/MetaAI/` - `ReadMe.txt`, `PROJECT_PLAN.md`, `CURRENT_PROGRESS.md`,
  `BUG_FIXES.md`, `FUTURE_GOALS.md`, `tremulous_winlatorxr_vr_mod/README.md`
- `docs/ClaudeCode/` - `PROJECT_OVERVIEW.md`, `PROJECT_PLAN.md`,
  `CURRENT_STATUS.md`, `BUG_FIXES.md`
- `docs/general_wxr/` - all nine files: `README.md`, `CLAUDE.md`,
  `WINLATORXR_DESC.md`, `XRAPI_V0_4_DESC.md`, `XRAPI_UPGRADE.md`, `TIPS.md`,
  `FIRST_STEPS.md`, `TOOLS.md`, `PC_INPUT.md`
- root `CLAUDE.md`, `Release/ReadMe.txt`, `Release/AlphaTest11/README.txt`

**Verified in code, not taken from the docs:**

- the six `code/winlatorxr/` modules and their line counts
- the three hook points and their exact call sites
- the cvar set (42 distinct `vr_*`), including `vr_enable` being
  `CVAR_LATCH` and `vr_worldScale` being a single human-derived constant
- that the sync pixel is drawn in the client before `re.EndFrame`, not in a
  renderer DLL
- that input is `Com_QueueEvent(SE_KEY)` + usercmd, with no InputAPI and no
  synthesised Windows input
- that the outbound FOV comes from the live refdef
- that `version` is 0.5, `vr`/`version` are deleted on shutdown, and
  `WSAStartup` is called in `XR_Init` for the documented reason

**Wrote:** this folder - index, reading notes, code state, invariants, open
items, this log. Nothing outside `docs/OpenCode/DeepSeek41Flash/` was
modified.

**Findings I would raise before doing any work:**

1. `Release/ReadMe.txt` claims MIT/Unlicense for the mod code, against a GPL
   fork with GPL headers on every file. Owner decision.
2. Stage 1 (on-device OpenGL) is still the gate on the renderer, and the
   cheap discriminator is a native Vulkan app in the same container.
3. The button table is still document-derived; one `vr_verbose 1` session
   with a press-per-control census closes it permanently.
4. Multiplayer on a stock protocol-72 server has never been recorded as
   working, and it is the product.
5. `vr_worldScale` is explicitly a human-only placeholder; D9 is unimplemented.
6. Several documents have gone stale in ways that could mislead the next
   session - listed in `02-code-state.md`, deliberately not edited.

**Next session should start with:** the owner's answers to the P0 items in
`04-open-items.md`, then a single device checklist covering AlphaTest11
confirmation, the button census and the wallwalk horizon test. Do not start
per-class scale or HUD depth until the camera and input checkpoints are
signed off on hardware.

**State of the tree when I finished:** branch `main` at `629a1327`, one
uncommitted change in `README.md` (owner's, untouched).

---

## 16 November 2026 - owner decisions and the licence fix

**Owner's answers to the onboarding findings:**

1. **Licensing** - fix it to match GPLv2. Done, in `Release/ReadMe.txt`: the
   MIT/Unlicense claim is replaced with GPLv2-or-later for the work as a
   whole, a note that new files carry the same header, and a sentence keeping
   media and third-party components under their own licences. This is the only
   file changed outside `docs/OpenCode/DeepSeek41Flash/`.
2. **Stage 1** - asked for the detailed explanation.
   Written as `06-stage1-renderer.md`: the container's GL stack, why Zink is
   the suspect rather than Turnip, the native Vulkan probe and its decision
   matrix, the two-resolution/two-renderer protocol, a record sheet, and what
   "schedule `renderervk`" would actually involve.
3. **Button census** - asked whether it can be done on PC.
   Written as `07-button-census.md`. The decode is testable on PC and already
   was (9/9); the hardware index assignment cannot be, because the bool string
   is produced by the Android-side runtime on the headset. The document gives
   a two-minute device protocol, notes that the existing `vr_verbose` line
   already carries the raw string, and proposes an optional verbose
   `idx N down/up` line if the owner wants the census to be unambiguous for
   unmapped indices.
4. **Multiplayer** - locally created and joined a server successfully. That
   is now recorded in `02-code-state.md` as "listen server verified; external
   stock server untested". It proves connection, purity, QVM load and spawn;
   it does not yet prove other players or a third-party protocol-72 host.
5. **Documentation policy** - `docs/ClaudeCode/` and `docs/MetaAI/` are
   archival and must not be edited, even where stale. `docs/general_wxr/` and
   this folder are live. Recorded in the README trust order, in
   `03-invariants.md` item 19, and in `02-code-state.md`.

**Files added/changed this session:** `06-stage1-renderer.md`,
`07-button-census.md`; updates to `README.md`, `02-code-state.md`,
`03-invariants.md`, `04-open-items.md`, this log; `Release/ReadMe.txt`
(licence section only).

**Next session start:** the P1 checklist in `04-open-items.md` - one device
trip covering the stage 1 probe, the button census, the checkpoint 6 camera
questions, and the owner's verdict on the four rough edges. Per-class scale
is the first code item after that, and its formula depends on the
body-size-versus-speed answer.

---

## 16 November 2026 - per-class world scale, and the census logging

**Owner's decisions:** preserve apparent body size (the intensity and the
class distinction are the point; comfort options come later), and add the
raw-index census line.

### Per-class world scale

- `xr_view.h`: `XR_NoteViewheight` became `XR_NoteClass( classIndex,
  viewheight )`. `XR_STAT_CLASS` is spelled numerically (5) for the same
  reason `xr_weapon.c` spells `STAT_TEAM`: no game-module headers in the
  client.
- `xr_view.c`: a 13-row table in `PCL_*` order, one row per class plus
  `PCL_NONE`, each holding `eyeAboveFeet = viewheight - mins[2]` from
  `assets/configs/classes/*.cfg`. The human row (48 units) is the anchor, so
  `vr_worldScale 30` still means exactly what it did before.
- `XR_WorldScale()` derives `vr_worldScale * (classEye / 48)`; both the head
  offset and the hand offset use it. Dretch 9.375 u/m, Granger 12.5,
  Basilisk 11.25, Marauder 18.75, Dragoon 29.375, Tyrant 53.75, Battlesuit
  41.875.
- New `vr_classScale` (1). Set it to 0 to pin every class to
  `vr_worldScale`, which is what a desktop test that must not move when the
  class changes wants.
- The class log is now on class change only, not viewheight change - crouch
  would otherwise write a line every time the player ducks. It prints the
  class name, index, viewheight, derived eye height and the resulting scale.
- `cl_cgame.c` passes `cl.snap.ps.stats[XR_STAT_CLASS]` and the current
  viewheight.

Deliberate choices: spectator/NONE keeps the human scale (there is no body to
preserve); an out-of-range class index falls back to human, so a mod that
renumbers classes changes how big the world feels but cannot break the view.

### Census logging

- `xr_input.c`: `XR_LogBoolEdges()` prints every changed index as
  `input: idx N down/up`, before the existing decoded line, behind
  `vr_verbose`. It includes indices the action table does not map, which is
  what a census is for. Suppressed on the first packet, where every pressed
  bit would read as a fresh press.

### Build and verification

- CMake Release x64 builds clean. The only warnings are the pre-existing
  `tr_image_pvr.c` C4701/C4703 pair, unchanged by this work.
- **Not run on device.** The mapping is derived and the code compiles; what
  is untested is whether each class feels right at its derived scale, which
  is a comfort judgement and belongs to the owner.
- Desktop check available now: spawn each class and read the `class:` line;
  `vr_classScale 0` should reproduce the previous single-constant behaviour
  exactly.

### Files changed

`code/winlatorxr/xr_view.c`, `xr_view.h`, `xr_input.c`,
`code/client/cl_cgame.c`. Notes updated: `README.md`, `02-code-state.md`,
`03-invariants.md`, `04-open-items.md`, `07-button-census.md`, this log.
One general finding folded back into `docs/general_wxr/TIPS.md`: per-body
world scale, and logging raw input indices alongside their decoded names.
No archived documentation touched; the owner's root `README.md` edits are
untouched.

---

## 16 November 2026 - the VR settings menu, and both deploy targets

**Owner's asks:** a VR menu with DEBUG and QUALITY submenus, because console
commands are painful on a headset; and deploy the latest build to
`E:\tmp\Q3Mods\trem-wxr` for the Quest and `D:\VR\tremulous-wxr` for PC
testing.

### The menu

New `code/winlatorxr/xr_menu.c` / `.h`, an engine-side 2D overlay. Not a
Tremulous UI menu: `ui.qvm` is replaced by a pure server, so a menu added
there would not exist on a real server. It draws through the client's own
`SCR_DrawStringExt` path and only sets cvars.

- Root page: DEBUG, QUALITY, Close.
- DEBUG: verbose, sync pixel, position, roll, pitch, per-class scale, hand
  gun, hand aim, left-handed, deadzone, turn speed, recentre.
- QUALITY: `vr_fovScale`, `vr_fovX`, `vr_fovY`, `vr_worldScale`,
  `vr_hudScale`, `vr_hideCrosshair`.
- Opens on the dominant stick click (`vr_menuButton -1` = auto; sprint is the
  off stick's click), or F1 / `vr_menu` on the desktop.
- Navigation: move stick up/down, turn stick left/right for sliders,
  dominant trigger selects, dominant B backs out, ESC closes.
- While open: usercmd zeroed (no movement, no fire), head yaw/pitch not sent
  (frozen aim), head roll/lean still composed. It closes itself if a console
  or QVM menu takes the screen.
- `vr_hideCrosshair` now restores the crosshair when turned off, because the
  menu can toggle it without a restart.

New `vr_fovScale` (0.5-1.0), applied in `XR_AdjustRefdef` and clamped because
a wider FOV than the class allows would be a gameplay advantage. `XR_NoteFov`
moved to **after** the adjustment so the runtime is told the scaled FOV that
was actually drawn.

### Wiring

- `cmake/client.cmake`: `xr_menu.c`.
- `xr_api.c`: `XR_MenuInit` with the other modules.
- `cl_scrn.c`: `XR_MenuDraw()` before `XR_DrawSyncPixel()`.
- `cl_keys.c`: `XR_MenuKeyEvent` at the top of `CL_KeyEvent`.
- `cl_input.c`: zero the usercmd while open.
- `xr_input.c` / `xr_view.c`: skip sticks and head aim while open.
- `client.h`: prototype for `SCR_DrawStringExt`.
- `autoexec_alphatest.cfg`: `bind F1 vr_menu`.

### Deploy

- New `scripts/deploy_both.ps1`: one build, staged to both standard targets.
- Ran it; both `E:\tmp\Q3Mods\trem-wxr` and `D:\VR\tremulous-wxr` are current
  with the menu build. Hashes printed by the script.
- Build: CMake Release x64 clean. Not launched, so the menu is
  build-verified and deployed, not runtime-verified.

### Notes

`02-code-state.md`, `03-invariants.md` (items 22-23), `04-open-items.md`,
`README.md`, this log, and a new `08-vr-menu.md` with usage and the device
checklist.

### Desktop verification, after the deploy

- `vr_menu` and the dominant stick click both open the overlay; `vr.log`
  records `menu: ready`, `menu: toggle requested`, `menu: open`, `menu: closed`.
- Screenshot at 1024x768 shows **VR SETTINGS**, DEBUG / QUALITY / CLOSE, the
  live class/scale/packet status line, and the sync pixel still at (0,0).
  The first screenshot caught a duplicated title ("VR VR") and an oversized
  root panel; both fixed before the final deploy (title now **VR SETTINGS**,
  panel height follows the page).
- The controller test used `vr_inputAlways 1` because a held spoofed button
  cannot produce a fresh press after spawn; it confirms the button index and
  the open path, not the "press after spawning" timing, which is the normal
  case.
- **Trap found:** a killed run leaves `tremulous.pid`, and the next launch
  waits on an **Abnormal Exit** dialog after `Hunk_Clear` - indistinguishable
  from a startup hang. Delete
  `%APPDATA%\Tremulous\base\tremulous.pid` and relaunch. It cost a diagnosis
  cycle here; written into `08-vr-menu.md` so it does not cost another.
- Not verified: the DEBUG/QUALITY page contents on screen, toggling an item
  with the controller, and any of it on the Quest. Those are on the P1
  checklist.

---

## 16 November 2026 - desktop input blocking

**Owner's ask:** while the VR menu is closed, block A, B, X, Y, the four
arrow keys, left/right click, Space and Enter from reaching the game; always
allow ESC because the controller's left menu button becomes it. All keyboard
and mouse input must work whenever the title screen or a menu is open.

**Implemented in `code/sdl/sdl_input.c`**, not `CL_KeyEvent`. The reason is
the whole design: the VR controls queue their synthetic keys through
`Com_QueueEvent`, which is above SDL. Filtering at SDL blocks physical and
runtime-emulated input and leaves the synthetic path untouched. Inside
`CL_KeyEvent` the two are indistinguishable by then.

- Blocked on the **down** edge only: a key held while a menu closes still
  sends its up, so nothing sticks down.
- Conditions: `XR_IsEnabled && XR_InGame && !XR_MenuIsOpen`,
  `Key_GetCatcher() == 0`, and `vr_blockDesktopInput` nonzero.
- `Key_GetCatcher() != 0` covers the console, chat fields, the in-game ESC
  menu and the QVM UI. ESC itself is not in the block list at all.
- New `vr_blockDesktopInput` (1), archived, and a QUALITY toggle so it can be
  turned off from the headset and on the desktop.
- Build clean.

**Not verified end to end:** physical key delivery into the game could not be
automated here (posting/SendKeys does not reach SDL's window in this
session), so the predicate is code-reviewed and build-verified rather than
observed blocking a real key. The device checklist in `08-vr-menu.md` covers
the four cases that matter: blocked in match, allowed in menus, ESC always
works, and the cvar switch restores input.

**Side effect cleaned up:** testing had archived `vr_inputAlways 1` into the
PC's `autogen.cfg`; reset to 0 so the default behaviour is back.

---

## 18 November 2026 - DeployReady

**Owner's ask:** keep the latest full build ready to go in
`D:\Daven Bigelow\Documents\GitHub\tremulous-wxr\Release\DeployReady`.

- Renamed `deploy_both.ps1` to **`deploy_all.ps1`**; it now stages three
  targets from one build: Quest folder, PC test install, and DeployReady.
- DeployReady is **deleted and rebuilt** on every run, so it never collects
  screenshots, saved configs or a stale pk3. It is a clean 105 MB folder:
  executable, renderers, SDL2, `tremded`, `base/` with QVMs and all eight
  data/map pk3s, loose assets, `zzz-wxr.pk3`, `autoexec.cfg`, `ReadMe.txt`,
  `GPL` and `COPYING.txt`.
- `deploy_wxr.ps1` now stages the readme and licence files into every target,
  not just DeployReady: a build folder without them is not distributable.
- Ran it with `-SkipBuild`; all three targets are current and share the same
  executable hash (`8642834240D27FA5`).
- `Release/` is gitignored, so DeployReady is deliberately not in version
  control; it is a build artifact, regenerated from source.


