# The repository as I found it

**Verified 16 November 2026** by reading the code, not by reading the status
file. Line numbers are from this revision (`629a1327`, plus the owner's
uncommitted `README.md` edit, which I left alone).

---

## Size and shape

`code/winlatorxr/` is real code, seven modules plus headers, **about 5,000
lines of C and 470 lines of headers**:

| File | Lines | What it owns |
|---|---:|---|
| `xr_api.c` / `.h` | 1255 / 135 | XrApi 0.5 transport: handshake files, `system` parsing, UDP in/out, `vr.log`, cvars, game-state gate |
| `xr_view.c` / `.h` | 823 / 86 | Head pose conversion, recentre, roll/position composition, usercmd yaw/pitch, per-class world scale, one-FOV clamp |
| `xr_input.c` / `.h` | 845 / 60 | Controller booleans -> synthesised key events, sticks -> usercmd, turn, weapon cycling, menu click, verbose button census |
| `xr_weapon.c` / `.h` | 765 / 55 | Captured `RF_FIRST_PERSON` entities at the dominant hand, aim-aligned prop, gun trims, `handsfix.txt`, hand aim on fire |
| `xr_sync.c` / `.h` | 240 / 42 | The 10x10 sync square, drawn in real pixels, last |
| `xr_hud.c` / `.h` | 149 / 36 | HUD shrink about the centre, separate alien scale, crosshair off/on |
| `xr_menu.c` / `.h` | 898 / 55 | Engine-side settings overlay: DEBUG / QUALITY / ACTIONS, real-pixel drawing, controller and keyboard navigation |

Wired into the real build at `cmake/client.cmake:37-42` - no parallel
solution, no wildcard project.

## The hooks, as implemented

| Location | Call | Why there |
|---|---|---|
| `code/client/cl_cgame.c:592` | `CG_R_RENDERSCENE` | Captures first-person entities (573), notes class (610), adjusts the refdef (638), then notes the final FOV (644) before `re.RenderScene` |
| `code/client/cl_input.c:594, 612, 634` | `CL_CreateCmd` path | `XR_StickLook`, `XR_StickMove`, then `XR_AdjustUsercmdAngles` on `cl.viewangles`; usercmd zeroed while the VR menu is open |
| `code/client/cl_scrn.c:562, 567` | `SCR_UpdateScreen` | `XR_MenuDraw()` then `XR_DrawSyncPixel()`, after world + HUD and immediately before both `re.EndFrame` calls |
| `code/client/cl_keys.c:1323` | `CL_KeyEvent` | `XR_MenuKeyEvent` before console/UI/bind dispatch, so ESC and arrows belong to the overlay while it is open |
| `code/sdl/sdl_input.c` | `IN_ProcessEvents` | Drops physical keyboard/mouse downs in a match while the VR menu is closed; synthetic controller keys bypass SDL and are unaffected |
| `code/client/cl_main.c:3209, 4002, 4035` | `CL_Frame`, `CL_Init`, `CL_Shutdown` | Per-frame pump, init, shutdown |
| `code/client/cl_cgame.c:666` | `CG_R_DRAWSTRETCHPIC` | `XR_HudScale2D` - one transform moves every 2D element, glyphs included |

That is the whole integration surface. No cgame, game or ui changes; no
server changes; no renderer DLL changes for the port itself.

## The frame, in order

```
runtime 7872  ->  XR_Receive       drain every datagram, keep newest,
                                   normalise quaternions at parse
              ->  XR_Frame         XR_InputFrame (menu, then buttons -> key
                                   events), XR_SendOut (7278, once per frame)
CL_CreateCmd  ->  XR_StickLook / StickMove / AdjustUsercmdAngles
                                   body yaw on the stick, head yaw+pitch added
                                   for the duration of the move only; all
                                   suppressed while the VR menu is open
cgame         ->  CG_R_RENDERSCENE note class, compose roll + head offset onto
                                   viewaxis, re-express the gun at the hand,
                                   shrink the 2D layer
client        ->  XR_MenuDraw      engine-side settings overlay
              ->  XR_DrawSyncPixel exact HMD_SYNC, RGB(sync,0,0), (0,0)-(10,10)
renderer      ->  re.EndFrame
```

## Settings surface

**49 distinct `vr_*` cvars** (grep, this revision), all archived. The ones
that decide behaviour:

- `vr_enable` (0) - **`CVAR_LATCH`**, so changing it needs a restart.
- `vr_verbose` (0) - raw packet every few seconds, bool string on change,
  composed basis, outbound packet. Off by default.
- `vr_modeVR` (1) / `vr_mode3D` (0) / `vr_autoMode` (1) - the last drops to
  `MODE_VR 0` whenever the player is not actually playing.
- `vr_worldScale` (30) / `vr_classScale` (1) - `vr_worldScale` is the
  **human** reference; the other classes are derived from their config's
  `viewheight - mins[2]`, so a Dretch gets 9.375 u/m and a Tyrant 53.75.
  `vr_classScale 0` pins every class to the single constant for desktop
  tests that must not move when the class changes.
- `vr_fovScale` (1) - comfort only. Scales `refdef.fov_x/y` and is clamped to
  `<= 1`, because a wider FOV than the class allows would be an advantage.
  The outbound FOV is noted after the scale, so the runtime composites what
  was really drawn.
- **One FOV for every class.** Alien classes render at 140 or 120 * 0.75 in
  cgame; `XR_AdjustRefdef` clamps any value wider than the human 90 * 0.75 and
  recomputes the horizontal angle from the frame aspect. The class difference
  is carried by `vr_worldScale`, not by FOV.
- `vr_hudScale` (0.5) / `vr_hudScaleAlien` (0.45) - the alien HUD is drawn
  larger and gets its own shrink; both are sliders in QUALITY.
- `vr_menuButton` (-1) - the bool index that opens the VR settings overlay.
  `-1` means the dominant stick click, which is the one stick click the
  action table leaves free (sprint is the off stick's click).
- `vr_menuInvert` (0) - flips the overlay's vertical stick direction. The
  packet's Y is forward-positive, so 0 is correct for the device capture;
  this exists because the wrap-into-reverse behaviour was mistaken for an
  inverted axis.
- `vr_gunAimAlign` (1) - the visible gun keeps cgame's orientation (down the
  aim) while its position follows the hand. 0 restores the prop rotating
  with the controller. While firing the hand *is* the aim, so hand
  orientation is used then.
- `vr_blockDesktopInput` (1) - drops physical keyboard/mouse downs for A, B,
  X, Y, arrows, Space, Enter and left/right click while in a match and the VR
  menu is closed. Menus, console, chat and ESC always pass; synthetic
  controller keys bypass SDL entirely.
- `vr_posTracking`, `vr_rollEnable`, `vr_pitchEnable` - the stage 6
  isolation switches.
- `vr_dominantHand` (0 = right) - one switch mirrors triggers, grips, sticks
  and face buttons.
- `vr_stickDeadzone` (0.15, rescaled), `vr_turnSpeed` (140 deg/s),
  `vr_moveEnable`, `vr_turnEnable`, `vr_inputAlways`.
- `vr_key_*` - nine action rows plus `vr_key_menuClick`; an empty value
  disables a row.
- `vr_handGun`, `vr_handAimOnFire`, `vr_gunForward/Right/Up`,
  `vr_gunPitch/Yaw/Roll` (`auto`).
- `vr_hudScale` (0.5), `vr_hideCrosshair` (1), `vr_syncPixel` (1),
  `vr_cfgVersion`.

`vr_enable 0` returns before opening `vr.log`, after registering all cvars and
the `vr_recentre`/`vr_recenter` commands. So they exist with VR off, but the
flat game is otherwise untouched.

## XrApi implementation notes

- Speaks **0.5** (`XR_VERSION_STRING`), writes `version` and `vr`, deletes
  both on shutdown. No `input_version`: InputAPI is not used, because input is
  delivered in-process as `Com_QueueEvent(SE_KEY)` rather than synthesised
  Windows input. That matches `docs/general_wxr/PC_INPUT.md` exactly.
- Binds **127.0.0.1:7872** only, non-blocking; on failure it logs which of
  `socket()`/`bind()` failed and retries every 2 s from `XR_Frame`.
- Calls `WSAStartup` itself because `NET_Init` runs after `CL_Init`; the
  reason is in the code comment and is a device finding, not a guess.
- Parser: loose `client` prefix, 29 floats, bools collected across whitespace
  (handles both 19+flags and 21-unbroken spellings), trailing tokens
  ignored, every quaternion normalised on ingest.
- Holds the last good pose on a dropout and logs packets starting and
  stopping; `vr.log` is anchored to `fs_basepath`, with the build stamp on
  line 1 and startup lines buffered until the file exists.
- Outbound FOV comes from `XR_NoteFov(refdef.fov_x, fov_y)` each frame, with
  the cvars only as a fallback before the first frame. That is the fix for
  the drift defect, and it is live.

---

## Verification status, honestly

| Piece | Desktop | Device |
|---|---|---|
| Build, deploy, log, stamp | verified | verified (several builds) |
| Handshake, bind, parse, outbound | verified against spoofer and a real capture | packets have flowed on device, so structurally yes |
| Sync pixel | verified by screenshot: `rgb(12,0,0)` at (0,0) | no explicit checkpoint record |
| 6DOF camera | verified: body yaw holds while head sweeps | device runs exist; the checkpoint 6 questions (swim, wallwalk horizon) are not recorded |
| Controller input | 9/9 actions decode, movement and turn measured | menu-click fix came *from* the device; full census not done |
| Hand gun / hand aim / HUD | verified in AlphaTest10/11 | **desktop only** per the release notes |
| VR settings overlay | **desktop-verified**: draws, opens/closes by command and dominant stick click; real-pixel size; ACTIONS page not executed yet | **not run on device** |
| Desktop input block | implemented at SDL; build clean; synthetic-key path unchanged by construction | **not run on device** |
| Alien FOV / HUD | FOV clamp verified in `vr.log` (Dretch 83.4x67.5 at 4:3 = human); alien HUD scale 0.45 | **not run on device** |
| Gun prop aim | idle `aimAlign=1`, firing `aimAlign=0` in `vr.log`; model position still on the hand | **not visually checked on device** |
| Multiplayer | listen server: created and joined (owner, 16 Nov) | other players / stock server: **not tested** |
| Stage 1 OpenGL characterisation | n/a | **deferred by decision** |
| Per-class world scale | implemented; class table derived from `assets/configs`, build clean | **n/a** - needs one pass per class on device |

Treat "verified" above as "the claim has a log line or screenshot behind it".
It is not the same as a signed-off checkpoint, and only the owner can sign
one off.

---

## Where the documents have drifted from the code

These are not defects in the port. They are places where a future session
could be misled, so they are worth knowing before reading the status file as
fact.

**Owner's decision, 16 November 2026: these documents are archival and are
not to be edited.** What follows is a record of the drift, not a to-do list.
The one exception already actioned is the licensing statement in
`Release/ReadMe.txt`, which is a release file rather than an archived
document.

1. **`CURRENT_STATUS.md`'s one-line status is stale.** It says stages 2 and 4
   are complete and "the next action is stage 6", while its own milestone
   board marks 5 and 6 code-complete and later sections document stages 7, 8
   and 9. The document was appended to without rewriting the summary.
2. **"Stage 5 is the one piece a renderer swap would force us to rewrite"** is
   now too strong. The sync pixel lives in the client and draws through
   `re.DrawStretchPic` before `re.EndFrame`; a replacement renderer that
   implements the existing `refexport_t` gets it unchanged. It is still
   *renderer-adjacent* - a renderer with no `DrawStretchPic` would have to
   provide one - but it is not a rewrite.
3. **The plan says the build stamp is logged "always, even with VR
   disabled".** The code returns before opening `vr.log` when `vr_enable` is
   0. Harmless in practice, because a flat game has no VR question to answer,
   but it is a stated requirement not met.
4. **`BUG_FIXES.md` opens with "No bugs have been fixed ... because there is
   no code yet"**, then Part 4 lists fourteen device fixes. The preamble is
   from the planning phase and was never updated.
5. **`Release/ReadMe.txt` said the mod code was "entirely MIT and/or
   Unlicense"**. This repository is a GPLv2 fork and the root `CLAUDE.md`
   says derived work stays GPL. **Fixed 16 November 2026**: the release
   readme now states GPLv2-or-later for the work as a whole, with media and
   third-party components under their own licences.
6. **`Release/ReadMe.txt` says "no non-VR mode added yet"**; `vr_enable 0`
   does give an unmodified flat game after a restart. Probably means "no
   in-game toggle", but it reads as a missing feature.
7. **The release folder runs ahead of the status file** - `AlphaTest9`,
   `AlphaTest10` and `AlphaTest11` exist while the status file's "currently
   debugging" section still points at AlphaTest6.
8. **`xr_api.h` has a comment-merge artifact**: the `XR_HmdMake`/`XR_HmdModel`
   comment runs straight into the `XR_NoteFov` comment. Cosmetic, but it now
   reads as if FOV hangs off the headset make.
