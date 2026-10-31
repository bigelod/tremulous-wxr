# CURRENT_PROGRESS.md - In-Progress Features

## What Works Today (MVP)

### 1. WinlatorXR Transport Layer - DONE
- File: `code/winlatorxr/xr_api.h` + `xr_api.c`
- Features:
  - Creates `Z:/tmp/xr/vr` and `Z:/tmp/xr/version` (0.5) on init, deletes on shutdown
  - Fallback to `D:/xrtemp/` for PC testing
  - Reads `Z:/tmp/xr/system` for HMD info (make/model/resolution) - logs but not yet used for FOV
  - Winsock2 UDP receiver thread: binds `0.0.0.0:7872`, non-blocking recv, parses ASCII floats
  - Thread-safe via `CRITICAL_SECTION g_xr_cs`
  - Outbound UDP to `127.0.0.1:7278`: `L_HAPTICS,R_HAPTICS,MODE_VR,MODE_3D,FOVX,FOVY`
  - Implements `XR_WaitForNewSync(timeout_ms)` for frame pacing

### 2. Build System - DONE
- MinGW: `build.bat` / `build.sh` adds `code/winlatorxr/xr_api.c` + `-lws2_32` to link
- CMake: Updated `CMakeLists.txt` adds `add_library(xr_api STATIC ...)` + links
- VS2022:
  - `vs2022/xr_api.vcxproj` - static lib, v143, x64 Debug/Release, `#pragma comment(lib,"ws2_32.lib")`
  - `Tremulous_VR.sln` + `tremulous_client.vcxproj` - wildcard `code\**\*.c` includes all ioquake3 files automatically
  - `vcpkg.json` manifest for SDL2, openal-soft, curl, etc. - auto restores in VS2022 17.6+
  - `CMakeSettings.json` for Ninja + vcpkg toolchain
  - Both `build_full_sln.bat` (MSBuild) and `build_cmake_vs2022.bat` provided

### 3. Engine Hooks - PARTIAL
- `code/cgame/cg_view.c` - `CG_CalcViewValues()` override:
  - If `vr_enable 1` and `xr_state.connected`, replaces `cg.refdef.vieworg` with `HMD_X/Y/Z * vr_scale`
  - Converts HMD quaternion to Euler `viewangles`
  - Still TODO: standing offset, smoothing, lean limits

- `code/renderer/tr_main.c` - `RE_EndFrame()`:
  - Draws 10x10 red block top-left with `HMD_SYNC` shade
  - Calls `XR_SendOut(0)` - mono mode
  - Still TODO: only draw when `vr_enable`, avoid UI overlap

- `code/client/cl_main.c` - `CL_Init()` calls `XR_Init()`, `CL_Shutdown()` calls `XR_Shutdown()`
  - Still TODO: verify shutdown deletes files even on crash (add `atexit`)

### 4. Testing Tools - DONE
- `scripts/test_udp_spoofer.py` - sends fake tracking packets to 7872, prints outbound from 7278
- `XrAPITestUDP.zip` reference - Java test client for PC

## Currently Debugging
- Packet parsing for v0.5: altitude + grip quats order - parser tolerates missing fields but need real Quest 3 packet log to confirm order
- Coordinate conversion: OpenXR +Y up, -Z forward vs Q3 +Z up - current `QuatToEuler` may have yaw inverted - need headset test
- Red pixel visibility: Tremulous clears screen after `RE_EndFrame`? Need to ensure pixel survives to swap
- Scale: `vr_scale 40` quake units per meter is guess - humans may be 2x too tall

## Next Immediate Steps (This Week)
1. Get real UDP log from WinlatorXR on Quest 3 (run `tcpdump -i lo -A port 7872`)
2. Fix `QuatToEuler` sign based on log
3. Test mono rendering in container: does image duplicate correctly?
4. Lock FPS to 72/90 via `com_maxfps` to match HMD
