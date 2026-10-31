# Tremulous WXR

A VR port of **Tremulous** (ioquake3 / id Tech 3) to standalone headsets -
Meta Quest 3, Pico 4 Ultra - running as a Windows executable inside a
**WinlatorXR** container and speaking **XrApi** over UDP. This repository is
a GPL fork of `darklegion/tremulous`; the port is a source rebuild, not a
mod DLL or a hook.

## Read these before planning or writing any code

This is a WinlatorXR VR port. Read **`docs/general_wxr/CLAUDE.md`** first -
it carries the standing instructions for every port in this family, and
`docs/general_wxr/README.md` is the map to the rest of that set. Do not
re-derive what those files already settle.

Then read, in this order:

| File | What it is |
|---|---|
| `docs/ClaudeCode/PROJECT_OVERVIEW.md` | the goal, and what makes this game unusual |
| `docs/ClaudeCode/PROJECT_PLAN.md` | intake, hard rules, hook design, ten staged milestones |
| `docs/ClaudeCode/CURRENT_STATUS.md` | where it has actually got to |
| `docs/ClaudeCode/BUG_FIXES.md` | defects found, and the inherited ones not to repeat |

`docs/MetaAI/` is a **prior, uncompiled sketch**. It may be read for shape,
but its planning documents describe work that does not exist and its bug list
contains fixes that are not in its code. `BUG_FIXES.md` says which parts are
safe to reuse. Do not copy from it without checking there first.

## The five things that will bite you here

1. **Nothing in `code/cgame/`, `code/game/` or `code/ui/`.** Tremulous is
   played on `sv_pure` servers, which force their own QVM modules onto the
   client at connect time. VR logic there does not exist on a real server.
   The port lives in the **engine**: `code/client/`, the renderer, and
   `code/sdl/`. The prior attempt got this wrong and it invalidated its whole
   design.
2. **The view hook is `CG_R_RENDERSCENE`** in `code/client/cl_cgame.c:577`.
   cgame hands the engine a `refdef_t`; the engine modifies it before calling
   `re.RenderScene`. `refdef_t.viewaxis` is a basis matrix, so **compose** the
   HMD rotation onto it - never replace it. Replacing it breaks wallwalking,
   weapon kick and the death cam.
3. **Yaw and pitch belong in the usercmd path only** (`code/client/cl_input.c`
   -> `cl.viewangles`), because the server has to agree about where the player
   is aiming. The render hook applies only roll and the positional offset.
   Applying yaw in both places is the classic double-rotation bug.
4. **World scale is per class.** Twelve classes, twelve viewheights and
   bounding boxes, from a Dretch to a Tyrant. A single units-per-metre
   constant is wrong for eleven of them.
5. **The log is the only channel back.** Builds go to `D:/VR/tremulous-wxr`
   and the owner copies the folder to the Quest by hand - no adb, no
   debugger. Anything you would want to ask about later must already be in
   `vr.log` before the build ships. Build stamp on line 1, always.

## Settled decisions - do not relitigate without asking

- **x64.** We compile the whole game, so the general guidance's "stay 32-bit"
  rule does not apply.
- **XrApi 0.5.** Human-readable, tester-compatible, and it is the
  destination, not a stepping stone to 0.6.
- **OpenGL for now; Vulkan (Quake3e's `renderervk`) as the fallback if
  stage 1's on-device measurements demand it. DirectX is ruled out** - no
  D3D renderer exists for this engine, and writing one would reach Vulkan a
  layer later than Vulkan does.
- **Mono first** (`MODE_VR 1`, `MODE_3D 0`); SBS and AER are measured
  upgrades, not goals. `MODE_VR 0` for menus.
- **The GPL licence-leak discipline in `docs/general_wxr/` does not apply
  here.** This is a public GPL fork; we commit real code to real files.

## Build and test

- Build: the repository's own CMake (`CMakeLists.txt`, `cmake/`). Do **not**
  resurrect the hand-written VS2022 solution under `docs/MetaAI/` - it
  duplicates knowledge the real build already has.
- Deploy target: `D:/VR/tremulous-wxr`, copied to the headset by hand.
- Desktop testing: the XrAPI UDP Tester at
  `D:\Daven Bigelow\Documents\GitHub\XrAPIUDPTester\XrAPITestUDP\XrAPITestUDP\bin\Release\net8.0-windows\XrAPITestUDP.exe`.
  It proves the code path runs; it does not prove the values are right.

## How to work

- **Instrument before theorising.** One manual round trip is expensive. Add
  the log line; read the number.
- **One change per deploy.** Nothing is done until the owner has seen it on
  hardware.
- All settings are `vr_*` cvars, archived, safe by default. `vr_enable 0`
  must produce an unchanged flat game.
- Diagnostics behind `vr_verbose`, off by default.
- Keep per-game findings in `docs/ClaudeCode/`. If something turns out to be
  **general** - a protocol fact, a trap any port would hit - fold it back
  into `docs/general_wxr/` so the next port inherits it.
