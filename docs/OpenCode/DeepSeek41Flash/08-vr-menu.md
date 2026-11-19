# The VR settings overlay

**Added 16 November 2026.** An engine-side menu for the settings that
otherwise need the console, because typing on a headset means the on-screen
keyboard.

---

## How to open it

| Where | How |
|---|---|
| Headset | **Click the dominant-hand stick** (the one you turn with). `vr_menuButton -1` means this; sprint is the *off* stick's click, so nothing is stolen |
| Desktop | **F1** (bound in `scripts/autoexec_alphatest.cfg`), or the console command **`vr_menu`** |
| Close | The same button, **B** to back out of a page, **ESC** on the keyboard, or the `Close` item |

It is also closed automatically when a console or a QVM menu takes the screen,
because the runtime has handed the mouse back and the overlay would otherwise
sit on top of a menu the player is trying to click.

## Navigation

| Input | Action |
|---|---|
| move stick up / down | move the selection |
| turn stick left / right | adjust a slider |
| dominant trigger | select, toggle, run, enter a submenu |
| dominant B | back one page, then close |
| keyboard arrows / Enter / Esc | the same, for desk testing |

The trigger is ignored until it has been **released once** after opening, so
a trigger still held from firing cannot activate the first item.

## Pages

### VR -> DEBUG

| Item | Cvar | Type |
|---|---|---|
| Verbose log | `vr_verbose` | toggle |
| Sync pixel | `vr_syncPixel` | toggle |
| Position tracking | `vr_posTracking` | toggle |
| Head roll | `vr_rollEnable` | toggle |
| Head pitch | `vr_pitchEnable` | toggle |
| Per-class scale | `vr_classScale` | toggle |
| Hand gun | `vr_handGun` | toggle |
| Hand aim on fire | `vr_handAimOnFire` | toggle |
| Left-handed | `vr_dominantHand` | toggle |
| Stick deadzone | `vr_stickDeadzone` | slider 0 - 0.5 |
| Turn speed | `vr_turnSpeed` | slider 60 - 300 deg/s |
| Recentre | `vr_recentre` | action |

Status lines at the bottom show the current class, the **derived** scale in
u/m, packet count, live rendered FOV and the current sync value.

### VR -> QUALITY

| Item | Cvar | Type |
|---|---|---|
| Game FOV scale | `vr_fovScale` | slider 0.5 - 1.0 |
| XrAPI FOV H (fallback) | `vr_fovX` | slider 80 - 130 |
| XrAPI FOV V (fallback) | `vr_fovY` | slider 80 - 130 |
| World scale (human) | `vr_worldScale` | slider 20 - 50 |
| HUD scale | `vr_hudScale` | slider 0.3 - 1.0 |
| Hide crosshair | `vr_hideCrosshair` | toggle |
| Block desktop input | `vr_blockDesktopInput` | toggle |

Status lines show the **live** rendered FOV and the frame size, plus the
class and its derived scale.

**One distinction worth knowing.** `vr_fovX` / `vr_fovY` are only the
fallback the outbound packet uses before the first frame is drawn. Once the
game renders, the values sent to the runtime come from the live `refdef`
because a configured FOV is not the FOV being drawn. The menu therefore shows
both: the sliders for the fallback, and the live number read from the frame.

**Why FOV scale is capped at 1.0.** A narrower FOV is a comfort choice; a
wider one than the class allows would be a gameplay advantage. The hard rule
in `PROJECT_PLAN.md` forbids the latter, so the applied value is clamped.

## Desktop keyboard and mouse blocking

While the player is in a match and the VR menu is closed, physical keyboard
and mouse events for **A, B, X, Y, the four arrow keys, Space, Enter and left
/ right click** are dropped at the SDL layer. This stops a real keyboard or
the runtime's own key/mouse emulation from doubling the controls the VR
mapping already sends.

- The controller's synthetic keys go through `Com_QueueEvent`, **above**
  SDL, so the filter cannot touch them. That is why the block lives in
  `sdl_input.c` rather than in `CL_KeyEvent`.
- **ESC is never blocked.** WinlatorXR turns the left controller menu button
  into ESC and it must keep working everywhere.
- **The title screen and every menu allow all input.** So do the console,
  chat and a QVM menu holding the keyboard: the filter only applies when the
  player is actually playing. Being locked out of a menu by an input filter
  would be worse than the doubling it prevents.
- Only key-down edges are dropped; a held key's release still reaches the
  game, so nothing sticks down when a menu closes.
- `vr_blockDesktopInput 0` turns the whole thing off, which is what a desktop
  test that wants WASD and the mouse should use.

The DEBUG page does not expose it (it is full); it is a QUALITY toggle and a
console cvar.

## What it does to the game

- **The usercmd is zeroed while it is open**: no movement, no fire, no
  buttons. Multiplayer cannot pause, so the world keeps running around a
  stationary player. That is deliberate and honest.
- **The aim is frozen**: head yaw/pitch are not sent to the server while the
  menu is up, so the view does not swing while a slider is being read.
- **Head roll and positional lean still apply**; the panel is drawn in screen
  space, so it stays centred while the world moves behind it.
- **The runtime is still told the FOV and sync every frame.** The menu is
  drawn before the sync pixel, which remains the last thing written.

## Why it is not a Tremulous menu

`ui.qvm` is replaced by a pure server at connect time, so anything added
there does not exist on the servers where the game is played. This overlay
lives in `code/winlatorxr/xr_menu.c`, draws through the client's own 2D path,
and only sets cvars. It works with any mod's UI, and `vr_enable 0` still
leaves a flat game untouched.

It is also deliberately **not** a settings INI: every item is a cvar, so the
console and the menu change the same value, and a tester never has to edit a
file on the headset.

## Verified on the desktop, 16 November 2026

- `vr_menu` opens and closes the overlay; `vr.log` records
  `menu: toggle requested`, `menu: open`, `menu: closed`.
- The **dominant stick click** opens it too. With `vr_menuButton -1` and
  right-handed, the log shows the toggle coming from bool index 13.
- Screenshot at 1024x768: the panel draws over the world and HUD, the title
  reads **VR SETTINGS**, the items are legible, and the sync pixel is still
  the top-left pixel.
- The class line is live in the panel (`class human, scale 30.00 u/m,
  packets N`), which is the same derivation the DEBUG page reports.
- **Not yet check-pointed:** the DEBUG and QUALITY pages were not
  photographed (same draw path, more rows), and no item was toggled from the
  panel on hardware. The device checklist below covers both.

**A testing trap worth knowing.** If the game appears to hang after
`Hunk_Clear: reset the hunk ok`, it is not hung: it is showing a modal
**"Abnormal Exit"** dialog because a previous run was killed before it could
remove `tremulous.pid`. Delete
`%APPDATA%\Tremulous\base\tremulous.pid` and start again. This cost a
diagnosis cycle here and will look exactly like a startup crash.

## Test checklist

- [ ] Dominant stick click opens and closes it; sprint still works on the
      off stick
- [ ] Both hands: with `vr_dominantHand 1`, the other stick click opens it
      and the mirrored trigger/B navigate
- [ ] Every toggle changes the corresponding behaviour without a restart
      (`vr_hideCrosshair` included - it now restores the crosshair)
- [ ] A slider moves smoothly with the turn stick and survives a page change
- [ ] The game does not move or fire while the menu is open
- [ ] Opening the console or the game's own menu closes the overlay
- [ ] The sync pixel is still exact at (0,0) with the menu open
- [ ] `vr_fovScale 0.8` narrows the view and the live FOV line follows
- [ ] `vr_menuButton -1` on a build with a saved config: confirm the auto
      value was not defeated by an old `vr_menuButton` in `autogen.cfg`
- [ ] **In a match**, physical A/Space/Enter/click do nothing; the controller
      still fires, jumps and uses items
- [ ] **In the title screen, menus and console**, keyboard and mouse work
      normally
- [ ] **ESC from the controller** still opens the in-game menu (it is never
      blocked)
- [ ] `vr_blockDesktopInput 0` restores physical input while playing

## Known limits

- No mouse cursor and no scrolling. The pages fit on one panel; a page that
  grows past 13 rows will need a scroll before it is usable.
- The panel is 640x480 virtual space, so it is readable but not resolution
  independent in the way the 3D world is.
- Values are two decimal places on screen; the cvar itself is exact.
- It is an overlay, not a pause. On a live server the player is vulnerable
  while it is open, which is the correct trade for a multiplayer game.
