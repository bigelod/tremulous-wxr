# BUG_FIXES.md - Bugs Fixed So Far

Short bullet list of issues we hit and resolved.

- **Winsock thread affinity**: `WSAStartup` must be called on same thread that calls `recvfrom`. Fixed by doing `WSAStartup` inside receiver thread, not `XR_Init`.
- **UDP bind fails on Wine**: Binding `127.0.0.1:7872` fails if host also binds. Fixed by binding `INADDR_ANY` + `SO_REUSEADDR`.
- **Secure CRT in VS2022**: `strtok` deprecated, `strcpy` warning C4996. Fixed by switching to `strtok_s`, `strcpy_s`, `_snprintf_s` with template size checks.
- **`ws2_32.lib` missing in VS2022**: Link error LNK2019 `__imp_recvfrom`. Fixed by adding `#pragma comment(lib,"ws2_32.lib")` in `xr_api.c` and explicit AdditionalDependencies in vcxproj.
- **Critical section not visible**: `cg_view.c` needs to lock `g_xr_cs` but was static in xr_api.c. Fixed by exposing `extern CRITICAL_SECTION g_xr_cs` in header.
- **CharacterSet mismatch**: Tremulous uses `char*` for paths, VS2022 defaults to Unicode. Fixed by setting `CharacterSet=MultiByte` in both vcxproj.
- **ConformanceMode**: ioquake3 uses old C style `int x = y * (1 << 2)`. VS2022 conformance errors. Fixed by `ConformanceMode=false`.
- **Wildcard includes double compile**: `code\**\*.c` + explicit `xr_api.c` caused LNK2005 duplicate. Fixed by excluding `winlatorxr` from wildcard and adding explicitly.
- **Red pixel cleared**: Initially drew sync pixel in `CG_Draw2D` which gets cleared. Fixed by moving to `RE_EndFrame` after `R_RenderView` but before `SDL_GL_SwapWindow`.
- **Version file not found in Proton**: `Z:` maps to `/` but `/tmp/xr` not existent. Fixed by creating `mkdir -p /tmp/xr` via `CreateDirectoryA` on `Z:/tmp/xr/` if missing.
- **Float parsing locale**: On some Proton locales `,` is decimal sep, `atof` fails. Fixed by using `strtof` with `setlocale(LC_NUMERIC,"C")` at thread start.
- **Packet header `client0`**: Early parser expected 28 floats at start, but packet starts with "client0". Fixed by detecting non-float first token and skipping it.
- **Button string length**: v0.3 had 10 buttons, v0.5 has 16. Fixed by accepting variable length `T/F` string after floats, not fixed offset.
- **Outbound flood**: Calling `XR_SendOut` every `CG_CalcViewValues` spammed UDP 200Hz. Fixed by calling only once per `RE_EndFrame`.
- **Box64 Dynarec crash**: `ws2_32` async recv in Box64 Performance mode crashed. Fixed by adding `Sleep(1)` in recv loop to yield.
- **SBS vs Mono confusion**: Initially used `MODE_3D=1` which gave double image stretched. Fixed to `MODE_3D=0` mono - WinlatorXR duplicates correctly.
