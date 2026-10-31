# PROJECT_PLAN.md - Tremulous WinlatorXR Monocular 6DOF VR Mod

## Objective
Create a simple **monocular 6DOF VR mod for Tremulous** (https://github.com/darklegion/tremulous, ioquake3 / id Tech 3 base) that runs on standalone headsets (Quest 3, etc.) inside **WinlatorXR containers** using Proton/Wine. Explicitly **NOT** using standard OpenVR/OpenXR - this is WinlatorXR XrApi v0.3-v0.5.

Target: One exe `tremulous_client.exe` that can be dropped into `D:\Tremulous\` inside WinlatorXR with `base/` and launched with `+set vr_enable 1`.

## Why WinlatorXR XrAPI instead of OpenXR
WinlatorXR provides a translation layer: Android OpenXR runtime -> Wine -> Windows app via files and UDP. Windows app never talks to OpenXR directly. This is the only VR path available inside WinlatorXR containers without modifying WinlatorXR itself.

Activation protocol:
- App creates `Z:/tmp/xr/vr` containing string "VR" to enable VR mode
- App creates `Z:/tmp/xr/version` containing "0.5" (or "0.3") to negotiate protocol version
- Fallback for PC testing: `D:/xrtemp/vr` and `D:/xrtemp/version`
- Read `Z:/tmp/xr/system` for HMD info: make, model, container resolution
- On shutdown: delete marker file to signal VR end

Tracking IN:
- UDP `127.0.0.1:7872` bound `INADDR_ANY`
- ASCII float string, space separated:
  `L_QX QY QZ QW THUMBSTICK_X Y POS_X Y Z  R_QX QY QZ QW THUMBSTICK_X Y POS_X Y Z  HMD_QX QY QZ QW HMD_X Y Z  IPD FOVX FOVY SYNC [v0.5+: ALTITUDE L_GRIP_Qx4 R_GRIP_Qx4]`
- Then button string: `T/F` string for `L_GRIP, L_MENU, L_THUMBSTICK_PRESS, L_TRIGGER, L_THUMBSTICK_X_CLICK, L_THUMBSTICK_Y_CLICK, R_GRIP, R_MENU, R_THUMBSTICK_PRESS, R_TRIGGER, ...` (16 chars)
- Example: `client0 0.213 ... 99.00 103.40 224 TFFFFFFFFFTTTFFFFFT`
- Convention: OpenXR +Y up, -Z forward, meters. Quake is different - needs remap.

Haptics/FOV OUT:
- UDP `127.0.0.1:7278`
- Format: `L_HAPTICS, R_HAPTICS, MODE_VR, MODE_3D, HMD_FOVX, HMD_FOVY`
- `MODE_VR=1` = VR enabled, `MODE_3D`: `2=AER, 1=SBS, 0=flat/mono, -1=user control`
- For this mod: `MODE_VR=1, MODE_3D=0` (monocular, WinlatorXR duplicates to both eyes)

Frame pacing:
- Render `HMD_SYNC` value as 10x10 red block top-left corner every frame
- WinlatorXR container scrapes pixel, waits up to 20ms for new sync to avoid tearing
- If sync not updated, container shows previous frame.

## Engine Hooks (Minimal Touch)
Tremulous = ioquake3. 3 places to patch:

1. `code/cgame/cg_view.c` -> `CG_CalcViewValues()`
   - Override `cg.refdef.vieworg` + `viewangles` with HMD tracking
   - Cvars: `vr_enable`, `vr_scale` (meters->quake units, default 40), `vr_standing`, `vr_height_adjust`

2. `code/renderer/tr_main.c` -> `RE_EndFrame()`
   - Draw red sync pixel (no depth, no viewport transform)
   - Call `XR_SendOut(0)` (mono)
   - Optional: `XR_WaitForNewSync(20)` if we want to throttle

3. `code/client/cl_main.c` -> `CL_Init()` / `CL_Shutdown()`
   - `XR_Init()` creates files + Winsock thread
   - `XR_Shutdown()` deletes marker + closes sockets

No changes to game logic, botlib, ui.

## Build Targets
- Primary: MinGW cross (Linux -> `tremulous_client.exe` via `make`)
- Secondary: VS2022 v143 x64 native for Windows debugging
  - Full sln: `Tremulous_VR.sln` with wildcards `code\**\*.c` (auto includes all ioquake3 files)
  - `xr_api.vcxproj` static lib with `ws2_32.lib` auto-linked
  - `vcpkg.json` for SDL2, openal-soft, curl, zlib, libjpeg-turbo, freetype

## Container Setup (Quest 3)
- Resolution: 1920x1080 mono (NOT 2.2:1 SBS)
- Graphics driver: Turnip, DXVK OFF (OpenGL -> Vulkan bridge)
- Wine: proton-9.0-x86_64, Box64 Performance
- WinlatorXR settings: XR API toggle ON, Environment: Performance
- Copy `tremulous_client.exe` + `base/` to `D:\Tremulous\` inside container
- Launch: `tremulous_client.exe +set vr_enable 1 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 +set r_fullscreen 1 +set cg_fov 90`

## Testing Without Headset
- `D:\xrtemp\version` + `D:\xrtemp\vr` marker for PC
- `XrAPITestUDP.zip` or `scripts/test_udp_spoofer.py` sends fake UDP packets on 7872
- Verify outbound on 7278 with `nc -ul 7278`

## Success Criteria
- Launch Tremulous inside WinlatorXR, see HMD 6DOF head movement (look around, lean)
- No crash, no SBS double image, single image duplicated by runtime
- 60+ fps on Quest 3 with `r_picmip 2`, `r_dynamiclight 0`
- Red sync pixel visible in top-left (can be covered later)
