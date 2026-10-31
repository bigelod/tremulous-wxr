# CURRENT_STATUS

**As of 31 October 2026.** Updated when something changes on hardware, not
when something is written.

---

## One-line status

**Stage 0 of 10 complete - intake done, D1 and D2 settled.** No VR code
exists in the repository yet. The next action is **stage 1**: build stock
Tremulous x64 for Windows, run it in a WinlatorXR container on the Quest 3,
and characterise the OpenGL path - which is also the measurement that decides
whether we stay on OpenGL or port a Vulkan renderer.

---

## What is actually in the repository

| | State |
|---|---|
| Tremulous engine, flat, desktop | Stock `darklegion/tremulous` fork at `ecb41eac`, builds via CMake |
| VR code | **None.** `code/winlatorxr/` does not exist; `grep -ri "vr_enable\|xr_api" code/` is empty |
| XrApi client | Not started |
| Sync pixel | Not started |
| Head tracking | Not started |
| Window handling | Stock SDL2 |
| Build-and-deploy script | Not started |
| Container test on Quest 3 | **Not done** - this is the next action |
| Planning docs | `docs/general_wxr/` (shared guidance), `docs/MetaAI/` (prior sketch), `docs/ClaudeCode/` (this set) |

`docs/general_wxr/` and `docs/MetaAI/` are both **untracked** in git. Worth
deciding whether they should be committed.

---

## Milestone board

| | Milestone | State |
|---|---|---|
| 0 | Feasibility assessed, intake complete, D1/D2 signed off | **Done** - 31 Oct 2026 |
| 1 | Unmodified Tremulous runs in a container; OpenGL path characterised | Not started - **next action** |
| 2 | Our code loads, logs, build stamp, deploy guard | Not started |
| 3 | Borderless window filling the display | Not started |
| 4 | XrApi 0.5 client, device-verified button table | Not started |
| 5 | Sync pixel | Not started |
| 6 | 6DOF head tracking feels locked | Not started |
| 7 | Presentation mode chosen and measured | Not started |
| 8 | Controller input, owner-confirmed mapping | Not started |
| 9 | HUD legibility and menu mode switching | Not started |
| 10 | Per-class scale, comfort, in-game menu | Not started |

---

## What the prior attempt leaves us

`docs/MetaAI/` contains a prior design pass: a 193-line XrApi client
(`tremulous_vs2022_full_sln/code/winlatorxr/xr_api.c`), four prose patch
notes, a VS2022 solution, a `vcpkg.json`, a UDP spoofer script, and four
planning documents.

**None of it is integrated, and nothing in this repository suggests it was
ever compiled or run.** Its `CURRENT_PROGRESS.md` describes the transport
layer and build system as DONE, but no corresponding file exists here. Its
`BUG_FIXES.md` lists fixes for defects that are still present in the code it
ships alongside, and at least two of the entries describe mechanisms that do
not work the way stated. Read it as a sketch written from the protocol
documentation, not as a report of work done.

**Useful from it:**

- the overall shape - transport in its own module, handshake files, sync
  pixel in the renderer, mono first
- the `vcpkg.json` dependency list (SDL2, openal-soft, curl, zlib,
  libjpeg-turbo, freetype)
- `scripts/test_udp_spoofer.py` as a starting point, pending a check against
  a real device capture

**Not usable:**

- the client code itself. See `BUG_FIXES.md` for the catalogue - the most
  serious items are a fabricated packet layout, no quaternion normalisation,
  an outbound FOV the runtime ignores, and a hand-rolled quaternion-to-Euler
  conversion the engine does not need
- the `cg_view.c` approach. It puts the camera in a QVM module that pure
  servers replace at connect time, and calls Win32 critical sections from
  inside the VM. The equivalent engine-side hook is
  `CG_R_RENDERSCENE` in `code/client/cl_cgame.c:577`

---

## Decisions settled, 31 October 2026

- **D1, graphics backend.** Keep OpenGL if stage 1 says it holds up;
  otherwise port Quake3e's `renderervk` as `renderer_vulkan`. **DirectX is
  ruled out.** Still gated on stage 1 measurements - Vulkan if we must, not
  on principle.
- **D2, bitness.** **x64**, because we compile the whole game from source.
- **Deploy target.** `D:/VR/tremulous-wxr`. The owner copies that folder to
  the Quest 3 and runs it by hand. No adb loop.
- **Diagnostics.** `vr.log` next to the executable is the primary channel
  both on device and on PC, with the build stamp on line 1. See "Logging and
  the feedback loop" in `PROJECT_PLAN.md`.
- **Desktop testing.** XrAPI UDP Tester at
  `D:\Daven Bigelow\Documents\GitHub\XrAPIUDPTester\XrAPITestUDP\XrAPITestUDP\bin\Release\net8.0-windows\XrAPITestUDP.exe`.

## Still open

- Container resolution, after stage 1.
- Whether `docs/general_wxr/` and `docs/MetaAI/` get committed.

---

## Currently debugging

Nothing. There is no code to debug, and the honest statement of status is
that this project has not run yet.
