# Invariants - the rules I will not break

Each one has a failure mode attached, because the rule is only remembered
when the symptom is. If a change needs one of these bent, that is a design
discussion with the owner, not a local decision.

---

## 1. Nothing under `code/cgame/`, `code/game/` or `code/ui/`

Pure servers force their own QVMs onto the client at connect time. VR logic
in any of them exists only on the local listen server - not where the game is
played.

**Symptom if broken:** everything works while testing offline, and vanishes
the moment the player joins a real server.

**Implemented by:** the whole port is in `code/client/` plus
`code/winlatorxr/`; the only engine-editable game constants used
(`STAT_TEAM`, `TEAM_HUMANS`, `pm_type`) are spelled numerically in
`xr_weapon.c` so no QVM header is included.

## 2. One source of yaw and pitch: the usercmd

`code/client/cl_input.c:633`, inside `CL_CreateCmd`'s path. The server
derives player state from `cmd->angles`, and cgame builds `viewaxis` from
that. Head yaw/pitch are added to `cl.viewangles` only for the duration of
the move and restored immediately, so the engine keeps accumulating mouse and
stick onto body angles alone.

**Symptom if broken:** the view rotates twice as fast, or the crosshair and
the shot disagree. The classic double-rotation bug.

## 3. Roll and position only in the render hook

`XR_AdjustRefdef` at `code/client/cl_cgame.c:643` composes roll and the head
offset onto cgame's basis. It never touches yaw or pitch - those already
arrived through the player state.

**Symptom if broken:** same as 2, but only while the head is rolled or
leaning, which is harder to attribute.

## 4. Compose the HMD basis onto cgame's - never replace it

`viewaxis` is a basis, not an angle triple. cgame's version already carries
wallwalking, weapon kick, fall-bob and death-cam roll.

**Symptom if broken:** the horizon jumps when a Granger walks onto a wall;
weapon kick and the death cam stop working; the most reliable nausea
generator in the project.

## 5. The player's world yaw includes the server's half

```c
bodyYaw = cl.viewangles[YAW];
if ( cl.snap.valid )
    bodyYaw += SHORT2ANGLE( cl.snap.ps.delta_angles[YAW] );
```

`cl.viewangles` is only the half the client accumulated. Spawns,
teleporters and `setviewpos` turn the player without the mouse moving.

**Symptom if broken:** the hand gun sits beside the wrong hip and a lean goes
sideways; everything is off by the map's spawn angle, which is why a test at
yaw 0 cannot fail.

## 6. Aim and camera are latched, not re-sampled

While the hand aim is active, `XR_AdjustUsercmdAngles` stores the angles that
were actually sent, and the render hook corrects against that latch. Three
things ask `XR_GunAimActive()` and nothing else: the aim, the camera
correction, the movement stick.

**Symptom if broken:** a shimmer on exactly the frames where the player is
shooting, because the correction is computed against an aim that was never
sent.

## 7. The sync pixel is exact, and it is last

10x10 at (0,0) in **real pixels** (the 2D ortho puts y = 0 at the top), drawn
after world and HUD, immediately before `re.EndFrame`. Echo `HMD_SYNC` byte
for byte, green 0, blue 0 outside AER, filtering and blending off. Never put
it through `SCR_AdjustFrom640`.

**Symptom if broken:** the world swims with correct maths, and it is the most
misattributed failure in this stack.

## 8. Send the FOV that was rendered, every frame

`XR_NoteFov(vrRefdef.fov_x, vrRefdef.fov_y)` from `CG_R_RENDERSCENE`; the
cvars are a boot-time fallback only. Games change FOV for zoom and aspect.

**Symptom if broken:** the runtime composites at the wrong scale, so the
world moves at a different rate from the head and differently per axis. It
reads as drift and "rotations do not return", not as a wrong FOV.

## 9. Normalise every quaternion at ingest, unconditionally

Tester packets are routinely 1.4-10.8% long. The standard rotation shortcut
scales by `|q|^2`, so each hand's basis is stretched by a different amount.

**Symptom if broken:** an asymmetry that looks like a deep bug in the pose
maths.

## 10. No handedness flip in this engine

id Tech 3 and OpenXR are both right-handed, so it is a relabel:
`(x,y,z)_xr -> (-z,-x,y)_q3`, determinant +1. The flip in
`XRAPI_V0_4_DESC.md` section 6 is for a left-handed engine.

**Symptom if broken:** a mirrored world. Geometry, lighting and movement all
behave; text and logos are backwards. It can survive a long way into a port.

## 11. Positions are head-centred; there is no floor

A real Quest 3 capture has the head at `y = 0.006` and the hands at
`y = -0.23`. Do not write a sanity bound against "a head must be N metres off
the floor".

**Symptom if broken:** recentre never happens, both hands collapse to one
place, and it presents as "the gun is stuck to my forehead" plus "the
dominant-hand setting does nothing".

## 12. Freeze on the last good pose, never zero

One zeroed frame is a violent camera jolt in a headset.

**Symptom if broken:** nausea on a packet loss that the maths never sees.

## 13. Hands are converted independently of the head

Share only the recentre origin. Each pose comes out of the packet and is
converted on its own.

**Symptom if broken:** hands swing when the head turns - the most common bug
in this family of ports.

## 14. Input is delivered by the game's own code, not by Windows

Digital controls go in as `Com_QueueEvent(SE_KEY)`, so the bind system,
`cl_run`, and cgame's command registrations all keep working. Sticks go
straight into the usercmd. No `SendInput`, no `PostMessage`, no
`keybd_event`.

**Symptom if broken:** it works on the desktop and does nothing on device,
where focus lives with the container rather than the game window.

## 15. `vr_enable 0` is an unchanged flat game

It is `CVAR_LATCH`, so this is a next-start property, not a live switch. All
`vr_*` cvars and commands exist either way.

**Symptom if broken:** a player who turns VR off still gets a modified view,
or an unknown-cvar error.

## 16. The log is the instrument, and it starts at line 1

Build stamp first, startup lines buffered until the file exists, state
transitions rather than per-frame spam, verbose behind one flag. Anything
worth asking later must already be in `vr.log` before the build ships.

**Symptom if broken:** a whole manual copy-to-Quest round trip spent on a
question the log could have answered.

## 17. Change a default only with a migration

Archived cvars outlive the defaults that created them, and the people holding
stale values are the testers. Bump `vr_cfgVersion` and migrate - touching
only values equal to the old default, because discarding deliberate tuning is
worse than the bug.

**Symptom if broken:** a fixed defect appears unfixed on every machine that
ran the previous build.

## 18. The port is GPLv2-or-later, everywhere

This is a GPL fork, not a mod distributed beside a GPL game. New files carry
the GPL header, and release text states GPLv2-or-later for the work as a
whole. Media remains CC BY-SA 2.5, with the third-party exceptions the
original readme lists.

**Symptom if broken:** a licence claim that contradicts the headers on every
file - the one defect in this project that no test can catch.

## 19. `docs/ClaudeCode/` and `docs/MetaAI/` are archival

Owner's decision, 16 November 2026. They are kept as a record of what was
believed at the time, including the parts that are now stale. Do not edit
them, even to correct a claim, unless the owner asks. General findings go to
`docs/general_wxr/`; everything else goes in this folder.

**Symptom if broken:** history is rewritten, and a future session cannot tell
which of two versions was the evidence and which was the hindsight.

## 20. One change per deploy

Nothing is done until the owner has seen it on hardware. A desktop pass is a
precondition, never a substitute.

**Symptom if broken:** two changes, one symptom, no way to attribute it, and
the next session starts by guessing which half to revert.

## 21. World scale is derived per class, anchored on the human

`vr_worldScale` (30) is the human reference: 48 units above the feet at a
1.6 m eye height. Every other class is derived from its config's
`viewheight - mins[2]`, preserving apparent body size - Dretch 9.375 u/m,
Tyrant 53.75 - with `vr_classScale 0` pinning one constant for desktop tests.

Do not replace this with twelve hand-tuned constants. If a class feels wrong,
the fix is the derivation or one strength control that applies to all of
them, because a correct value transfers and twelve tuned numbers are twelve
things to re-tune. The table must stay in step with
`assets/configs/classes/*.cfg`; if the game data changes, the table changes
with it.

**Symptom if broken:** one class feels right and eleven feel like different
games, or the table silently drifts from the configs it was derived from.

## 22. Player-facing settings live in the engine-side overlay

`xr_menu.c` draws and drives the VR menu in the client. Never add it to
`code/ui/` or cgame: a pure server replaces those QVMs at connect time, so a
menu added there does not exist on the servers the game is played on. The
overlay sets cvars only, which is the engine's own settings mechanism and
works with any mod's UI.

**Symptom if broken:** the menu is present while testing flat or hosting
locally and vanishes the moment the player joins a real server.

## 23. Comfort may narrow the view; it may never widen it

`vr_fovScale` is clamped to `<= 1` where it is applied. A narrower FOV is a
comfort choice; a wider one than the class allows is a gameplay advantage,
which `PROJECT_PLAN.md` forbids on a public server.

**Symptom if broken:** the port is a cheat - quietly, and in the one place a
player would never report it.

## 24. Block desktop input only where it would double the VR controls

Physical keyboard/mouse downs are dropped at the SDL layer for A, B, X, Y,
arrows, Space, Enter and left/right click, and only while in a match with the
VR menu closed. **Everything passes in the title screen, menus, console, chat
and any QVM UI, and ESC is never blocked** - WinlatorXR turns the left menu
button into ESC, and a player locked out of a menu is a worse defect than the
doubling this prevents. Synthetic controller keys are queued above SDL and
are never affected; `vr_blockDesktopInput 0` restores physical input for desk
testing.

**Symptom if broken:** a menu that cannot be typed into, an escape key that
stops working, or VR controls that die because the filter was placed in
`CL_KeyEvent` where synthetic and physical events are indistinguishable.
