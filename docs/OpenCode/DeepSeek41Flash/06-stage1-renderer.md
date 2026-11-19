# Stage 1 in detail - the on-device OpenGL measurement

**Status: deferred since 31 October 2026, not cancelled.** This file is the
long version of item 2 in `04-open-items.md`: what stage 1 is, why it decides
something no amount of desktop testing can, and what a single device session
would settle.

---

## 1. What stage 1 actually asks

From `docs/ClaudeCode/PROJECT_PLAN.md`, stage 1 is:

> Unmodified Tremulous runs in a WinlatorXR container, flat, and the OpenGL
> path is characterised on device.

"Unmodified" is the important word. No VR code, no `vr_enable`, no mod - just
the stock game on the flat screen inside the container. The question is not
"does the port work?" but **"is the graphics path this port is built on good
enough to build on?"**

The plan asks for a record, for each driver and resolution:

- does it launch at all, and does it reach a map
- frame rate from the **container's own counters**, not from code in the game
- visual defects: missing shaders, black geometry, broken lightmaps, stalls
- stability over **ten minutes** of play, not ten seconds
- whether `renderergl1` differs from `renderergl2` - one latched cvar, and
  the simpler renderer may be more robust on an emulated GL stack
- whether post-processing stays off **after a relaunch**

The output is a decision, not a score: **keep OpenGL, or schedule the Vulkan
renderer port (`renderervk`) as stage 1b before any more VR polish.**

## 2. Why this is a real gate and not box-ticking

Tremulous is OpenGL-only. There is no Direct3D backend anywhere in ioquake3's
history, and writing one would be a project larger than the port that reaches
Vulkan one layer later. So the choice is not "GL or D3D"; it is **"GL through
a translation layer, or Quake3e's native Vulkan renderer"**.

In a WinlatorXR container the GL path is:

```
Tremulous (x86-64, Wine)
  -> SDL2 / opengl32        (Wine's Windows-side GL)
     -> Zink  (or VirGL)    GL translated onto Vulkan (or another layer)
        -> Turnip           the Adreno Vulkan driver
           -> Adreno GPU
```

The native Vulkan path is shorter:

```
Tremulous with renderervk (x86-64, Wine)
  -> winevulkan             Wine's Vulkan ICD shim
     -> Turnip
        -> Adreno GPU
```

Two translation layers disappear (the GL emulation and its shader
translation). That is why "Turnip struggles with OpenGL" is an argument *for*
Vulkan rather than against the hardware: **Turnip is a Vulkan driver; there
is no OpenGL in it.** GL in the container is GL -> Zink -> Turnip -> Adreno,
so a slow GL result indicts **Zink**, while a slow Vulkan result indicts
something deeper.

The question stage 1 cannot answer on the desktop is which of those two
worlds this container lives in. A desktop GL driver tells you nothing about
Zink, Box64 and a mobile GPU behind a compositor.

## 3. The cheap discriminator: a native Vulkan probe

This is the part worth doing even if the rest of stage 1 slips. Run a
**known-good native Vulkan Windows application in the same container**.

What it proves: that `winevulkan` is exposed to Windows applications and that
Turnip can render at a usable speed. Both are currently inferences. (The
inference is strong - DXVK is itself a Windows-side Vulkan client, so any
DXVK game that runs already proves `winevulkan` is reachable - but an
inference is not a measurement.)

What it does **not** prove: that `renderervk` will be fast in Tremulous. The
probe is an upper bound on the Vulkan path, not a prediction.

Good probes, in order of usefulness:

1. **vkQuake** (Windows x64 build). A real Quake-family renderer on Vulkan:
   it exercises shaders, textures, a game loop and presentation. If this runs
   well, the Vulkan path is healthy.
2. **A Vulkan SDK sample** (Sascha Willems' samples, or the classic triangle).
   Smaller and easier to obtain, proves device enumeration and presentation,
   says less about sustained load.
3. **`vulkaninfo.exe`** from the LunarG SDK. Proves enumeration and names the
   device; does not render. Useful as a five-second sanity check before the
   bigger probe.

If a DXVK-based game already runs in the owner's container, that is a free
(if weaker) positive signal and worth recording alongside.

### The decision matrix

| GL result | Native Vulkan probe | Reading | Action |
|---|---|---|---|
| good | not needed | Zink is holding up | **Keep OpenGL.** Close D1. Spend the time on VR |
| bad / marginal | good | Zink is the cost | **Port `renderervk` as stage 1b**, before more visual tuning |
| bad / marginal | also bad | The problem is Turnip, the container, Box64 or thermals - not the renderer | Do **not** port a renderer to fix it. Investigate driver/container settings, resolution, `renderergl1`, Wine version |
| good | bad | Rare, and GL is the better path anyway | Keep OpenGL |

The third row is the one that saves the most time. Without the probe, "GL is
slow" looks like a mandate to port a Vulkan renderer, and the port would not
fix it.

## 4. What stage 1 gates, and what it does not

**Does not depend on it** - which is why deferring was a reasonable bet:

- the XrApi transport, handshake, parsing and logging
- the sync pixel, which is drawn in the client through `re.DrawStretchPic`
  before `re.EndFrame` (it needs the renderer to implement that export, not
  to be OpenGL specifically)
- the camera maths, hand poses, input, hand gun and HUD scaling
- all of the game logic

**Does depend on it:**

- **Final container resolution.** The runtime captures the window and filters
  it down, so rendering above panel resolution is free supersampling. The
  resolution/cost curve is a product decision, and it is also the HUD's
  reference width.
- **Whether stage 1b exists.** If GL fails, the renderer port is real work
  with a real schedule cost, and doing it early is cheaper than doing it
  after the polish that depends on it.
- **Stereo ambition.** SBS doubles scene cost. If mono is already marginal,
  SBS is not a measured upgrade, it is a fantasy.
- **Where visual tuning is safe.** Values tuned on Zink may need
  re-verification after a renderer swap, though not a rewrite.

## 5. A measurement protocol that produces an answer

One session, two resolutions, two renderers, ten minutes each. Keep it
boring and write the numbers down as you go.

**Setup**

- One container, one Wine/Proton version, recorded.
- Two candidate container sizes, one variable at a time. Start with
  **1400x1400** and **2800x1400** (the plan's candidates); substitute
  1920x1080 only if the owner prefers.
- `r_swapInterval 0` so vsync is not the cap; `com_maxfps 0` to expose
  headroom. Record a second figure at the HMD rate (72/90) because an
  uncapped run distorts thermals.
- Post-processing off, then **relaunch and verify it stayed off** - games of
  this era silently restore graphics settings.

**Each cell of the matrix**

1. Launch, reach a map, play for ten minutes.
2. Record frame rate from the container's counters at one minute and at ten
   minutes. The difference is thermal throttling, and it is the number that
   predicts a real play session.
3. Note visual defects by name and take a screenshot.
4. Record stability: any stall, crash, or return to desktop.

**Matrix**

| | `renderergl2` | `renderergl1` |
|---|---|---|
| 1400x1400 | FPS 1 min / 10 min, defects | same |
| 2800x1400 | FPS 1 min / 10 min, defects | same |

`cl_renderer` is latched: change it and restart (or launch with
`+set cl_renderer opengl1`). Do not compare a warm run from one renderer with
a cold run from the other.

## 6. Record sheet

Fill this in `05-session-log.md` after the session; it is the evidence the
decision is made from.

```
Date:
Container / Wine / Box64 / driver:
Container resolution:
Game: stock Tremulous, flat, no VR code

renderergl2 @ 1400x1400   fps 1min:   fps 10min:   defects:
renderergl1 @ 1400x1400   fps 1min:   fps 10min:   defects:
renderergl2 @ 2800x1400   fps 1min:   fps 10min:   defects:
renderergl1 @ 2800x1400   fps 1min:   fps 10min:   defects:

post-processing off before relaunch:   still off after:
native Vulkan probe (app / result):

Verdict: keep OpenGL / schedule renderervk / investigate container
```

## 7. What "schedule renderervk" would mean in practice

Not a rewrite of the port. `cl_renderer` is a latched cvar and
`code/client/cl_main.c` loads `renderer_<name>` at runtime, so the work is:

- add Quake3e's `renderervk` as a `renderer_vulkan` target in CMake
- implement the same `refexport_t` the client already calls, including
  `DrawStretchPic` (sync pixel and HUD), `RenderScene` (with our modified
  `refdef_t`), and the 2D ortho path
- re-verify the sync pixel and HUD scale on the new backend
- re-check the outbound FOV, which comes from the refdef and should carry
  over unchanged

The VR logic itself does not move: it is in the client, not in the renderer.
That is the architectural payoff of the stage 5 decision, and it is why the
old "a renderer swap forces us to rewrite the sync pixel" line in
`CURRENT_STATUS.md` reads as stronger than the code warrants.

## 8. Why this is worth doing on the next trip

The probe and the characterisation are mostly *playing the game*, which the
owner does anyway. Everything above them in the port is now desktop-verified
and device-tested in part, so the marginal cost of this measurement is low
and the information is the highest-value thing still missing. Defer it again
and the project continues to make visual decisions on an unmeasured
foundation - which is exactly what `docs/general_wxr/TIPS.md` warns against.
