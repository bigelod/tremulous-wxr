What you get:

    Tremulous_VR.sln - open directly in VS2022 17.x
    vs2022/tremulous_client.vcxproj - wildcard code\**\*.c so it automatically includes every Tremulous/ioquake3 C file, no manual file listing. Excludes tools/.
    vs2022/xr_api.vcxproj - static lib, v143 toolset, x64 Debug/Release
    code/winlatorxr/xr_api.h/.c - thread-safe, VS2022-ready (#pragma comment(lib,"ws2_32.lib"), strtok_s, CRITICAL_SECTION g_xr_cs exposed)
    vcpkg.json manifest (SDL2, openal-soft, curl, zlib, libjpeg-turbo, freetype) - VS2022 17.6+ auto-restores on sln open
    scripts/build_full_sln.bat - msbuild Tremulous_VR.sln /p:Configuration=Release /p:Platform=x64
    scripts/build_cmake_vs2022.bat - alternative CMake generator path

How to use:

    Clone darklegion/tremulous so you have code/ at repo root, then merge this zip over it (keep your code/).
    Double-click Tremulous_VR.sln
    If VS asks about vcpkg manifest, click Yes. If SDL.h missing, set Project Properties -> vcpkg -> Use Manifest = Yes
    Build Release x64 -> bin/x64/Release/tremulous_client.exe

The mono VR path is XR_SendOut(0) - WinlatorXR will show the single 1920x1080 image to both eyes. No SBS needed.