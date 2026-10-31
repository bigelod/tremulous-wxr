# Tremulous VR FULL VS2022 Solution

Double-click Tremulous_VR.sln in VS2022 17.x (v143).

- vs2022/tremulous_client.vcxproj uses wildcard code/**/*.c so all ioquake3 files are included automatically.
- xr_api.vcxproj is separate static lib, linked via ProjectReference.
- ws2_32.lib auto-linked via pragma in xr_api.c
- Defines: USE_SDL, USE_OPENAL, USE_CURL, VR_ENABLE, _CRT_SECURE_NO_WARNINGS
- OutDir: bin/x64/Release/tremulous_client.exe

Prereqs: vcpkg with sdl2, openal-soft, curl, zlib, libjpeg-turbo, freetype (manifest auto-restore in VS2022 17.6+)

If missing SDL.h, enable vcpkg manifest: Project -> vcpkg -> Use Manifest = Yes.

Mono VR: MODE_3D=0 in XR_SendOut, WinlatorXR duplicates image to both eyes.
Container: 1920x1080, proton-9.0, Turnip, DXVK OFF, XR API ON.
