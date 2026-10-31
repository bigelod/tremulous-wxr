# FUTURE_GOALS.md - Remaining Features (Not Yet Started)

This file tracks features we have discussed but NOT implemented yet. These are post-MVP.

## Input & Interaction
- **Controller aim / weapon tracking**: Use right controller pose to drive `cg.refdef` + weapon angles. Currently only HMD.
- **Full button mapping**: Parse T/F string into Tremulous actions: jump=`R_GRIP`, crouch=`L_GRIP`, build menu=`L_MENU`, etc. Need cfg.
- **Thumbstick locomotion**: Map `L_THUMBSTICK_X/Y` to `+moveleft/right`, `+forward/back` with deadzone + smooth turn option.
- **Seated vs Standing recalibration**: Long-press `L_MENU + R_MENU` to recenter. Store `HMD altitude offset` from v0.5.
- **Grip quaternions**: Use `L_GRIP_Q / R_GRIP_Q` (v0.5) for hand model rendering in `cg_view` or `cgame` first-person hand.
- **Haptics**: Drive `L_HAPTICS/R_HAPTICS` on damage, building, dretch bite etc. via `CG_DamageFeedback`.

## Rendering
- **Proper mono projection**: Use `IPD, FOVX, FOVY` from packet to set `refdef.fov_x/y` dynamically. Currently static 90.
- **UI / HUD in VR**: `cg_draw.c` HUD is at screen edge - unreadable in HMD. Need 3D projected HUD or fixed HUD distance.
- **2D menus**: Main menu, team select, build menu are 640x480 virtual - need cursor driven by controller ray or gaze.
- **Crosshair depth**: Move crosshair to world space at 5m to avoid double vision.
- **World scale tuning**: Cvar `vr_worldscale` - humans feel too big/small. Need user calibration.
- **Optional SBS / AER path**: Add cvar `vr_mode` to allow `MODE_3D=1 (SBS)` or `2 (AER)` for future stereo rendering if we do true double eye rendering in `tr_main`.

## Performance
- **OpenGL -> Vulkan profiling**: Turnip + DXVK OFF vs ON benchmarking. Currently forced OFF.
- **Dynamic resolution**: If framerate < 60, lower `r_customwidth/height` via `XR_SendOut`.
- **Occlusion / LOD**: Tremulous maps are large - add `r_lod` tweaks for VR.
- **Multithreaded renderer**: `tr_main` sync wait blocks - move to separate thread.

## Platform & Compatibility
- **WinlatorXR v0.6+ support**: New packets may include hand tracking, eye tracking - parser should ignore unknown trailing floats.
- **OpenXR native fallback**: Build flag to compile same mod with OpenXR directly for PCVR (outside WinlatorXR).
- **Linux standalone build**: Native Linux Tremulous with same XrAPI UDP client for testing on PC without Wine.
- **Multiplayer safety**: Ensure `vr_enable` doesn't give wallhack advantage - cvar is client-only, server sees normal.

## UX / Polish
- **In-VR console**: Controller-typed console or voice.
- **Comfort vignette**: Black border on fast movement.
- **Installation zip**: One-click `WinlatorXR_Tremulous_VR.zip` with exe + base + docs + shortcut .lnk
- **Auto-detection**: If `Z:/tmp/xr/system` exists but no marker, show prompt "Enable VR? Y/N"

## Non-Goals (Explicitly Out of Scope)
- No OpenVR / SteamVR dependency
- No full stereo double-render in v1 - mono only
- No modification of WinlatorXR Java side (`XrVersion06.java`)
