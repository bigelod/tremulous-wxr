# Tremulous WinlatorXR VR Mod - Monocular 6DOF (XrAPI v0.5 or lower)

Simple VR mod for darklegion/tremulous running in WinlatorXR containers (Proton/Wine) on standalone headsets.

## What this is
- No OpenVR/OpenXR
- Writes Z:\tmp\xr\version = "0.5" and Z:\tmp\xr\vr = "VR"
- Listens UDP 127.0.0.1:7872 for HMD pose (28-38 floats + T/F buttons)
- Sends UDP 127.0.0.1:7278 "0 0 1 0 0 0" => mono VR
- Draws 10x10 red sync pixel at top-left for frame pacing
- Injects HMD quaternion into cg.refdefViewAngles and HMD pos into vieworg

## Files
- code/winlatorxr/xr_api.h/.c - thread-safe XrAPI implementation, v0.3 compatible, v0.5 aware (altitude + grip quats)
- patches/ - snippets to apply to cg_view.c, tr_main.c, cl_main.c
- scripts/build.bat / build.sh
- scripts/test_udp_spoofer.py - PC test without headset

## Apply
1. Copy code/winlatorxr/ into your tremulous checkout at code/winlatorxr/
2. Apply patches manually (search for locations)
3. Add xr_api to CMakeLists.txt per CMakeLists_addition.txt, link ws2_32
4. Build: scripts/build.bat (MinGW) produces tremulous_client.exe

## WinlatorXR Container Setup (Quest 3 / Pico 4 Ultra)
- Use WinlatorXR cats-27+ (XrAPI v0.5)
- New Container:
  Screen: 1920x1080 (mono, NOT double wide)
  Graphics Driver: Turnip
  Wine: proton-9.0-x86_64
  Box64 Preset: Performance
  DXVK: OFF (Tremulous uses OpenGL)
  Enable XR API: ON
- Copy tremulous.exe + base/ + assets to D:\Tremulous\
- Shortcut command: D:\Tremulous\tremulous.exe +set vr_enable 1 +set r_mode -1 +set r_customwidth 1920 +set r_customheight 1080 +set r_fullscreen 1 +set com_maxfps 72
- If black splash: check Z:\tmp\xr\system exists - if not, XR API not enabled

## Mono VR Mode
MODE_3D = 0 is mono. WinlatorXR will show same image to both eyes, no SBS needed. Much faster.

## Testing on PC
mkdir D:\xrtemp
echo 0.5 > D:\xrtemp\version
python scripts/test_udp_spoofer.py
Then run tremulous_client.exe +set vr_enable 1

## Credits
Spec from bigelod/winlatorxr XrAPI 0.3 and SixDOFinator MinimalProject UDP format, HaloCEWXR PROJECT_FOCUS.md
GPL-2.0 like Tremulous
