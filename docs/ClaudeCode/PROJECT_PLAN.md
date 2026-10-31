# PROJECT_PLAN - Tremulous to standalone VR via WinlatorXR

Filled from the template at `docs/general_wxr/PROJECT_PLAN.md`, staged
against `docs/general_wxr/FIRST_STEPS.md`.

Read `PROJECT_OVERVIEW.md` for what this is and why. Read
`CURRENT_STATUS.md` for where it has actually got to. This file is the route.

---

## 1. Intake - questions only the owner can answer

> Section 1 is an interview, not a guess. Items marked **TBD** are open and
> are listed again at the end of this section as a short list to work
> through. Nothing downstream that depends on a TBD should be built until it
> is answered.

### The game

| Question | Answer |
|---|---|
| Game, version, patch level | Tremulous, the `darklegion/tremulous` tree; this repo is a fork at `ecb41eac`, `main` |
| Store or distribution | Open source; assets in `assets/`, built from this tree |
| Install path on the owner's machine | Test install is built to **`D:/VR/tremulous-wxr`**; the owner copies that folder to the Quest 3 by hand |
| Bitness | **x64** (decided - see D2) |
| Renderer | **OpenGL** - `renderergl1` and `renderergl2`, via SDL2. No D3D backend exists |
| Engine | ioquake3 / id Tech 3 |
| Source available? Under what licence? | Yes, this repository. **GPLv2 or later** - redistribution is permitted, so no leakcheck discipline is needed |
| Mod API or SDK? | QVM game modules, but they are server-forced on pure servers - unusable as a port vector. See D3 |
| Does a VR build already exist? | No. No official VR, no SteamVR mode, no PCVR mod. Path E does not apply |
| Existing PCVR mod to retarget? | No |
| Existing modding community or documentation? | Yes - the Tremulous and ioquake3 communities, and Quake3e as an engine reference |
| Native head-tracking support? | No. Path D does not apply |

### The port

| Question | Answer |
|---|---|
| Real VR port or injection port? | **Real VR port** - XrApi, 6DOF, in-process, source rebuild |
| Target headsets | Meta Quest 3 (primary), Pico 4 Ultra (secondary) |
| Presentation mode | **Mono first** (`MODE_VR 1`, `MODE_3D 0`); SBS and AER evaluated at stage 7 against measured cost |
| Ship a non-VR mode as a fallback? | Yes - and `MODE_VR 0` for menus from the start |
| Recommended container resolution | **TBD.** Start at 1400x1400 (what the Interstellar Marines port ships) and at 2800x1400; measure both |
| Framing | First person, all twelve classes |
| Hands in view at all? | Not in v1. Aliens have no hands; human weapon models are cgame-owned |
| Multiplayer in scope? | **Yes, and it is the product.** Must join stock `sv_pure` servers |

### Constraints

| Question | Answer |
|---|---|
| May the result be published? Where? | Yes - public GitHub fork, GPL |
| May the game's source appear in the repository? | Yes. It *is* the repository |
| Release-owned files? | **TBD.** Nothing identified yet |
| Who verifies on hardware, and how fast is that loop? | The owner, on a Quest 3, by hand: build to `D:/VR/tremulous-wxr`, copy the folder to the headset, run it, copy the log back. **Slow and manual - the log file is the only channel back, so instrument generously** |
| Prior work to inherit or discard? | `docs/MetaAI/` - **discard the code, keep the shape.** See `BUG_FIXES.md` and section 3 |

### Settled at intake, 31 Oct 2026

- **D1** - Vulkan is the agreed fallback if OpenGL does not hold up on
  device. Still gated on stage 1's measurements; DirectX is ruled out.
- **D2** - **x64**, because we compile the whole game ourselves.
- **Deploy** - build output goes to `D:/VR/tremulous-wxr`. The owner copies
  that folder to the Quest 3 and runs it by hand. There is no adb loop.
- **Diagnostics** - the **log file is the primary channel** for everything,
  on device and on PC. See "Logging" below; it is a first-class requirement
  here rather than a nicety, because a manual copy loop means a question that
  the log cannot answer costs a whole round trip.
- **Desktop testing** - the XrAPI UDP Tester is available at
  `D:\Daven Bigelow\Documents\GitHub\XrAPIUDPTester\XrAPITestUDP\XrAPITestUDP\bin\Release\net8.0-windows\XrAPITestUDP.exe`,
  so stages 4-6 can be exercised on PC with a debugger before any headset
  trip.

### Still open

1. Container resolution, after stage 1 measurements
2. Whether to raise the outbound FOV pair with the WinlatorXR community
   rather than tuning it solo

---

## 2. Hard rules

Binding on every session, script and automated pass.

- **Nothing under `code/cgame/`, `code/game/`, `code/ui/`.** Pure servers
  replace those modules at connect time, so any VR logic there vanishes on a
  real server. If a change seems to require touching them, that is a signal
  the design is wrong - stop and re-plan. (The one legitimate exception
  would be a VR-only *local* build for bring-up, clearly marked as such and
  never shipped.)
- **No server-side changes, ever.** A VR client is a client.
- **No gameplay advantage.** No wider FOV than the class allows, no
  x-ray, no reach beyond what the game grants. The port must be acceptable
  on a public server.
- **Everything in `docs/general_wxr/CLAUDE.md` under "Non-negotiables in the
  code"** applies here verbatim: sync pixel echoed exactly, borderless window
  re-applied every frame, quaternions normalised on ingest, handedness
  swapped once at ingest, poses converted independently of the head, freeze
  on dropped packets, offsets measured in the frame they are used in.
- **The licence-leak discipline does not apply** (GPL fork - see
  `PROJECT_OVERVIEW.md`). Do not reintroduce `leakcheck`-style process here.
- **One change per deploy**, and nothing is "done" until the owner has seen
  it on hardware.

---

## 3. What exists today

### Repository layout

```
code/
  client/        cl_main.c, cl_cgame.c, cl_input.c, cl_scrn.c   <- the port lives here
  renderercommon/ tr_types.h (refdef_t, stereoFrame_t)
  renderergl1/    fixed-function GL renderer
  renderergl2/    GLSL renderer (cl_renderer default: "opengl2")
  cgame/ game/ ui/  QVM modules - OFF LIMITS, see hard rules
  sdl/            SDL2 platform layer: window, GL context, input
cmake/            modular CMake; client.cmake, renderer_gl*.cmake, platforms/windows.cmake
assets/           game assets, including per-class configs
docs/
  general_wxr/    shared porting guidance - read first
  MetaAI/         a prior attempt. Reference only; see below
  ClaudeCode/     this plan
```

### Current state

**No VR code exists in this repository.** `code/winlatorxr/` does not exist;
`grep -ri "vr_enable\|xr_api"` over `code/` returns nothing. The tree is a
clean Tremulous fork. The only non-stock content is `docs/general_wxr/` and
`docs/MetaAI/`, both untracked.

`docs/MetaAI/` is a prior design pass that produced a 193-line XrApi client,
four prose patch notes, a VS2022 solution and four planning documents. **It
was never integrated and, as far as anything in this repository shows, never
compiled or ran.** Its planning documents describe work as DONE that has no
corresponding code here, and its `BUG_FIXES.md` lists fixes for bugs that are
still present in the code it ships. Treat all of it as a sketch.

What is worth keeping from it: the overall shape (UDP client in its own
module, handshake files, sync pixel in the renderer, mono first) and the
`vcpkg.json` dependency list. What must not be carried forward is catalogued
in `BUG_FIXES.md` - read that before reusing a single line.

### The blocker

**Nothing is blocked.** Stage 1 can start immediately and it is also the
measurement that settles the renderer question, which gates the largest
optional piece of work in the project.

---

## 4. Approach

### Injection vector

**Source rebuild**, in-process, engine-side only.

### The three hook points, and why these

This is the heart of the design and it differs fundamentally from the prior
attempt. All three are in engine code, so all three survive a pure server.

**1. `CG_R_RENDERSCENE` in `code/client/cl_cgame.c:577` - the view.**

cgame computes the view and hands the engine a `refdef_t` through a syscall.
The engine can modify that struct before passing it to `re.RenderScene`. This
gives us the camera without touching cgame at all.

Critically, we **compose** rather than replace:

- cgame has already built `viewaxis` including wallwalk orientation, weapon
  kick, fall-bob and death-cam roll. Rotating the HMD basis *into* that frame
  keeps every one of those working, including walking on ceilings.
- `vieworg` is in world units; the head's positional offset is added along
  the composed basis, scaled by the per-class world scale.
- `fov_x`/`fov_y` can be adjusted here, subject to the no-advantage rule.
- `x, y, width, height` can be set per eye, which is how SBS arrives later
  with no further architecture.

**2. `CL_FinishMove` / the usercmd path in `code/client/cl_input.c` - aim.**

Head yaw and pitch must reach `cl.viewangles` so the server sees where the
player is aiming. Controller sticks feed `forwardmove` / `rightmove` /
snap-turn here too, and buttons map to `cmd->buttons`.

**Yaw and pitch belong here and nowhere else.** If they are also applied at
hook 1, the rotation is applied twice - the classic "yaw double-applied"
pitfall in `docs/general_wxr/FIRST_STEPS.md` stage 6. Hook 1 applies only
what the server does not need to know: roll, and the positional offset.

**3. `RE_EndFrame` / `GLimp_EndFrame` in the renderer - the sync pixel and
the outbound packet.**

Drawn last, after the 2D pass, before the buffer swap. One 10x10 red square
at the top-left of the final frame; in SBS, one in each half. Then
`XR_SendOut()` once per presented frame.

### The window

`code/sdl/sdl_glimp.c` owns the window. Borderless, filling the container
display, re-applied every frame, per the general guidance. SDL2 makes this
cheaper than the hand-rolled Win32 the other ports need, but it still has to
be re-asserted - SDL recreates the window on mode changes.

### Decisions taken deliberately

| ID | Decision | Choice | Why |
|---|---|---|---|
| **D1** | Graphics backend | **Keep OpenGL unless stage 1 says otherwise; fall back to porting Quake3e's `renderervk` as `renderer_vulkan`. Do not write a Direct3D renderer.** | **Agreed with the owner 31 Oct 2026.** D3D does not exist for this engine and would reach Vulkan one layer later than Vulkan does. Still gated on stage 1 - we go to Vulkan if measurement says we must, not on principle |
| **D2** | Bitness | **x64** | **Agreed with the owner 31 Oct 2026.** The general guidance's 32-bit rule exists so nobody converts an *existing* 32-bit game for no reason; we compile this one from source, so it does not apply. Box64 is better maintained than Box86 and the Turnip path is exercised at 64-bit |
| **D3** | Where the port lives | Engine only - client, renderer, SDL layer | Pure servers force their own QVMs. Anything in cgame does not exist on a real server |
| **D4** | XrApi version | **0.5** | 0.4's packet plus the InputAPI listener at no cost; human-readable; the UDP tester speaks it. Per `docs/general_wxr/XRAPI_UPGRADE.md`, this is the destination, not a stepping stone |
| **D5** | Process model | In-process, one thread for the UDP receive | No IPC, no second binary |
| **D6** | Presentation | Mono first; SBS/AER measured at stage 7; `MODE_VR 0` in menus | Mono ships. The engine's existing stereo path makes SBS cheap to *try* later |
| **D7** | Supersampling | Container resolution, no code | The runtime filters on downsample |
| **D8** | Settings surface | Cvars, `CVAR_ARCHIVE`, plus an in-game menu later | Cvars are the engine's native mechanism and reachable from the console; a headset player cannot edit an INI |
| **D9** | World scale | **Derived per class**, with a player-facing strength or height control - never one constant | Twelve classes, twelve bodies. See `PROJECT_OVERVIEW.md` |
| **D10** | Wrappers (dgVoodoo, ReShade) | None | Another layer inside two translation layers |

Cvar namespace: everything is `vr_*`, archived, with safe defaults, and
`vr_enable 0` must produce a bit-identical flat game.

### Logging and the feedback loop

The verification loop here is **build to `D:/VR/tremulous-wxr`, owner copies
the folder to the Quest 3 by hand, owner runs it, owner copies the log
back**. There is no adb, no debugger, no live view. One round trip is
expensive, so the log is not a debugging aid - it is the instrument.

That has a concrete design consequence: **anything we would want to ask about
later must already be in the log before the build ships.** A question the log
cannot answer costs a full manual cycle, which is the thing
`docs/general_wxr/TIPS.md` means by "instrument before theorising", made
sharper by the absence of adb.

- One file, `vr.log`, next to the executable, flushed on write - a crash must
  not take the log with it.
- **Line 1 is the build stamp**, `__DATE__ " " __TIME__`, always, even with
  VR disabled. Every device report gets read against it first.
- Then, unconditionally and once: the handshake directory actually used, the
  seven lines of `system` as read, the protocol version written, the bind
  result on 7872, the renderer in use, and the container resolution.
- Behind `vr_verbose 1`, off by default: the raw inbound packet verbatim
  every few seconds, the bool string on every change, the composed view basis
  and head offset, and the outbound packet. Never file dumps by default.
- Log **state transitions** rather than per-frame state: packets start,
  packets stop, `MODE_VR` changes, recentre, class change, resolution change.
  A log the owner has to scroll is a log that gets skimmed.
- Errors loudly. The characteristic XrApi failure is silence, so every
  unchecked return - `socket`, `bind`, the handshake file writes - becomes a
  logged line rather than a mystery.

The **XrAPI UDP Tester** is the cheap half of this loop and should carry as
much of stages 4-6 as it can:

```
D:\Daven Bigelow\Documents\GitHub\XrAPIUDPTester\XrAPITestUDP\XrAPITestUDP\bin\Release\net8.0-windows\XrAPITestUDP.exe
```

It proves the code path runs. It does not prove the values are right - its
quaternions are often not unit length and its hand separation does not match
a real headset. Treat a desktop pass as a precondition for a headset trip,
never as a substitute for one.

---

## 5. Milestones

- [ ] **0** Feasibility assessed, intake complete, D1/D2 signed off
- [ ] **1** Unmodified Tremulous runs in a WinlatorXR container, flat, and the OpenGL path is characterised on device
- [ ] **2** Build-and-deploy script with a freshness guard; `vr_*` cvars registered; log file with a `__DATE__ __TIME__` stamp on line 1
- [ ] **3** Window is borderless and fills the container display, re-applied every frame
- [ ] **4** XrApi 0.5 client parses real Quest 3 packets; button table built from a device log
- [ ] **5** Sync pixel drawn last, 10x10, top-left, echoing `HMD_SYNC` exactly
- [ ] **6** **6DOF head tracking feels locked** - the big one
- [ ] **7** Presentation mode chosen and measured
- [ ] **8** Controller input: locomotion, turning, actions, owner-confirmed mapping
- [ ] **9** HUD legibility and menu mode switching
- [ ] **10** Per-class world scale, comfort settings, in-game menu

### Stage 0 - decide, and sign off

Everything in section 1 answered or explicitly TBD. D1 and D2 agreed with
the owner. Nothing to build.

> **CHECKPOINT 0.** Owner confirms: bitness, that the engine-only
> constraint is understood and accepted, and that stock-server compatibility
> is a requirement rather than a preference.

### Stage 1 - run it flat, and characterise the renderer

Build stock Tremulous for Windows, put it in a container, play it on the flat
screen. This stage is also **the measurement that decides D1**, so do it
properly rather than as a smoke test.

Record, for each of Turnip+Zink and VirGL, and at each candidate resolution:

- does it launch at all, and does it reach a map
- frame rate from the container's own counters - not from code in the game
- visual defects: missing shaders, black geometry, broken lightmaps, stalls
- stability over ten minutes of play, not ten seconds
- whether `renderergl1` differs from `renderergl2` (it may well be more
  robust on an emulated GL stack, and it is one cvar to find out)

Turn post-processing off and verify it *stays* off after a relaunch.

> **CHECKPOINT 1, on the device.** Does stock Tremulous run in a container,
> and what did it take? Is the OpenGL path good enough to build on?
> **If no: stop and schedule the `renderervk` port as stage 1b before
> anything VR.** That is a substantial but well-bounded piece of work, and
> doing it before the VR work is far cheaper than after.

### Stage 2 - our code in the process, and the build loop

- `code/winlatorxr/xr_api.c` and `.h`, wired into `cmake/client.cmake`
- `vr_enable`, `vr_verbose` cvars; everything off by default
- `vr.log`, build stamp on line 1, per "Logging" above
- the build-and-deploy script, **with the freshness guard, now** - before
  there is anything hard to debug. It builds x64 Release, stages the
  executable, renderer DLLs and assets into **`D:/VR/tremulous-wxr`**,
  refuses to stage a binary older than the newest source file, and prints a
  hash per file so the owner can confirm what they are copying

> **CHECKPOINT 2, on the device.** Does the log appear, and does line 1 show
> the stamp of the build just made?

### Stage 3 - own the window

Borderless, filling the display, idempotent, re-applied every frame, in
`code/sdl/sdl_glimp.c`.

> **CHECKPOINT 3, on the device.** Full view, no chrome?

### Stage 4 - the XrApi client

Per `docs/general_wxr/FIRST_STEPS.md` stage 4, with the specific corrections
in `BUG_FIXES.md` applied from the first line written:

1. Handshake files in `Z:\tmp\xr`, desktop fallback, `version` = `0.5`,
   written *after* the runtime is up; `vr` deleted on shutdown
2. Read all seven lines of `system`; map the product codename
3. Bind **7872 only**
4. Drain every queued datagram per frame, keep the newest
5. Parse 29 floats, the 19-char bool string, the two flag chars, the HMD
   name; tolerate trailing tokens
6. **Normalise every quaternion on ingest**
7. Outbound on 7278 every frame: `0 0 1 0 104.5 104.5`
8. Raw packet logged verbatim every few seconds behind `vr_verbose`

Test entirely on the desktop with the UDP tester. Do not go to the device.

> **CHECKPOINT 4, with the owner.** One raw packet captured on the actual
> Quest 3, compared field by field against the tester. Then every button
> pressed in turn while the bool string is logged, and the table built from
> what moved.

### Stage 5 - the sync pixel

`RGB(sync, 0, 0)`, 10x10, top-left, drawn last, filtering and blending off,
echoed exactly - no gamma, no rescale. Before any camera work.

### Stage 6 - 6DOF head tracking

The first real milestone, and nothing after it is trustworthy until it is
right.

1. Orientation only, at first. Head yaw/pitch to `cl.viewangles` (hook 2);
   roll composed onto `viewaxis` (hook 1)
2. Recentre, bound to a button, from the first build - it gets used
   constantly
3. Then position: head offset along the composed basis, scaled
4. Freeze on the last good pose; never zero

> **CHECKPOINT 6, on the device.** Does the world stay still when the head is
> still? Does it swim when turning quickly - suspect stage 5. Does leaning
> move the view the right way and the right amount? Is the horizon level?
> **And, specific to this game: does it stay level after a Granger walks up a
> wall?** Do not proceed until all of these are right.

### Stage 7 - presentation mode

Measure mono, then try SBS through the engine's existing `STEREO_LEFT` /
`STEREO_RIGHT` path with per-eye `refdef` rects, then AER. Pick from the
numbers. Mono is a legitimate shipping mode.

### Stage 8 - input

Full mapping table, written down and owner-confirmed before implementation.
Tremulous needs more than a typical shooter: attack, secondary attack, use,
build, evolve, buy, deconstruct, the team and class menus, taunt, chat,
scoreboard, and class-specific abilities such as pounce, charge and
wallwalk toggle. Expect this stage to be larger than it looks.

Decide deliberately: held versus edge-triggered, which stick turns and
whether turning is smooth or snap, and what happens while a menu is open.

> **CHECKPOINT 8, with the owner.** Table confirmed before it is written,
> and again on the device after.

### Stage 9 - HUD and menus

- `MODE_VR 0` on menus, pause and pre-game; `1` on entering a map. Keep
  sending the outbound packet throughout, and expect a gap when returning
- HUD legibility: scale and inset the cgame 2D at the engine's drawing
  syscalls, derived from measured width against a reference width, with a
  strength control

### Stage 10 - comfort and polish

Per-class world scale; crouch, height and recentre behaviour; haptics as
levels; container-resolution supersampling documented; an in-game settings
menu; and a one-file install bundle.

---

## 6. Verification

**Build and deploy.** Script prints the build stamp, refuses a binary older
than the newest source file, and verifies each copy by hash.

**Confirm the right binary is running.** Line 1 of the log carries the stamp.
Check it before interpreting any device report.

**Desktop pass.** UDP tester. Proves the code path runs; proves nothing about
whether the values are right.

**On-device pass** - the only real gate:

- [ ] per-stage acceptance criteria above
- [ ] head tracking still feels locked
- [ ] frame rate acceptable at the recommended container size
- [ ] **the horizon survives a wallwalk**
- [ ] **the client connects to a stock `sv_pure` server and plays**
- [ ] `vr_enable 0` produces an unchanged flat game
- [ ] correct in both handedness modes

---

## 7. Risks

| Risk | Likelihood | What we do about it |
|---|---|---|
| OpenGL on Zink/VirGL is too slow or unstable | **Medium-high** | Stage 1 measures it before anything is built on it. Fallback is Quake3e's `renderervk`, which is a known quantity rather than new code |
| Wallwalking makes the horizon unusable | Medium | Compose rather than replace the view basis, from the first line. Test it at checkpoint 6, not at the end |
| Per-class scale feels wrong however it is derived | Medium | Derive from class viewheight, expose a strength control, and measure in the class's own frame rather than tuning per class |
| HUD unreadable and uneditable without touching cgame | Medium | Engine-side scaling at the 2D syscalls; `MODE_VR 0` for menus as the immediate mitigation |
| Head aim desyncs from server aim, or yaw is double-applied | Medium | One source of yaw: the usercmd path. Hook 1 never touches yaw or pitch |
| A VR client is seen as cheating on public servers | Low-medium | No FOV advantage, no extra information, no server changes. Raise it with the Tremulous community before release |
| Saved config on the test device overrides a new default | Medium | Say so explicitly with every handover build |
| The engine or SDL fights the borderless window | Medium | Idempotent, re-applied every frame |
| Tuned values do not transfer from tester to device | High | Treat the tester as "the code path runs" only |

---

## 8. What we learned

> Append as the port proceeds. Not code structure - git shows that. The
> things that are invisible afterwards: which frame an offset is measured in
> and why, what a wrong value looked like on screen, which route was tried
> and abandoned.

Nothing yet. The first entries will come from stage 1's renderer
measurements, and they will be worth writing down carefully - no other port
in this family has characterised the OpenGL path, so whatever stage 1 finds
is new information for the whole scene and should be folded back into
`docs/general_wxr/`.
