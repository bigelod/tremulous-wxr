# Tremulous WXR - project overview

**What this is.** A VR port of **Tremulous** (the `darklegion/tremulous`
ioquake3 / id Tech 3 tree this repository forks) to **standalone headsets** -
Meta Quest 3 primarily, Pico 4 Ultra secondarily - running as an ordinary
Windows executable inside a **WinlatorXR** container and speaking **XrApi**
over UDP.

Tremulous has no VR mode, no PCVR mod and no head-tracking support of any
kind, so there is nothing to retarget and nothing to feed. It does have full
GPL source, which is the best possible starting position: the injection
vector is a **source rebuild**, not a hook, a proxy DLL or an injected
process.

Read `docs/general_wxr/` first. It is the shared, game-agnostic guidance for
this family of ports and it settles most of the questions this document does
not. In particular `docs/general_wxr/CLAUDE.md` carries standing instructions
that bind every session here.

---

## The goal, stated as the thing a player gets

Open WinlatorXR on a Quest 3, launch Tremulous, and be *inside* a Tremulous
map: look around with your head, with the world staying locked where you put
it, and play a real game of Tremulous against other people on a stock,
unmodified server.

That last clause is a design constraint, not a nice-to-have - see
"Multiplayer is the product" below.

---

## Why this port is unusual

Most ports in this family are single-player, closed-source, DirectX-era games
reached through a hook. Tremulous is none of those, and the differences all
point the same way: **the work is easier than usual at the engine level and
harder than usual at the game-design level.**

### What makes it easier

**Full source.** No disassembly, no DLL proxy, no MinHook, no "find the
camera". The view transform is a named struct field in a C file we own.

**The renderer is already a plugin.** `cl_renderer` is a latched cvar;
`code/client/cl_main.c:3506` loads `renderer_<name>` at runtime. A second
graphics backend is a new DLL, not a fork of the client. That matters a great
deal for the renderer decision below.

**The engine already renders in stereo.** `SCR_UpdateScreen`
(`code/client/cl_scrn.c:552`) already draws the frame twice as `STEREO_LEFT`
and `STEREO_RIGHT` when stereo is enabled, and `refdef_t`
(`code/renderercommon/tr_types.h:126`) carries `x, y, width, height` per
scene. Side-by-side is therefore a viewport change on an existing code path,
not new machinery.

**The view arrives as a matrix, not as Euler angles.** `refdef_t.viewaxis[3]`
is a basis. We can rotate the canonical axes by the HMD quaternion and write
them straight in, exactly as `docs/general_wxr/XRAPI_V0_4_DESC.md` section 6
says to - with no quaternion-to-Euler conversion anywhere in the port. That
deletes a whole category of bug before it exists.

### What makes it harder

**Multiplayer is the product.** Tremulous is a competitive team game; nobody
plays it alone. Three things follow, and they constrain everything:

- The VR player must be able to join **stock servers**. Servers run `sv_pure`
  and force their own QVMs onto the client, so **the port cannot live in
  `code/cgame/`**. Every change must sit in the engine - client, renderer, or
  the syscall boundary between the engine and the game modules. This is the
  single most important architectural fact in the project, and the first
  attempt got it wrong.
- Head aim is *server-visible* aim. Where the player looks is where they
  shoot, and the server must agree. HMD orientation has to reach
  `cl.viewangles` and therefore the usercmd, not only the local view.
- There is no pausing to think. Comfort settings that cost responsiveness are
  not free here the way they are in a single-player game.

**Twelve player classes with twelve different bodies.** A Dretch is
knee-high; a Tyrant fills a corridor. Each class carries its own
`viewheight`, bounding box, `zOffset` and FOV in `code/game/bg_misc.c` and in
the class configs under `assets/configs/`. A single world-scale constant is
therefore wrong for eleven of the twelve classes. **World scale must be
derived per class** - from the active class's viewheight against the player's
real eye height - not tuned to one number. No other port in this family has
this problem and the general guidance does not cover it.

**Wallwalking.** `SCA_WALLCLIMBER` (`code/game/bg_public.h:239`) lets
Grangers, Basilisks and Marauders walk up walls and across ceilings, which
rotates the player's entire reference frame away from gravity. The headset's
up vector is gravity-aligned and always will be. An implementation that
*replaces* the view basis will put the horizon anywhere; one that *composes*
the HMD rotation onto the basis the game already computed will follow the
player onto the ceiling. This is a correctness requirement, not polish, and
it is also the most likely source of nausea in the whole project.

**Heavy, information-dense 2D.** Build menus, evolve menus, the team HUD, the
scoreboard and the console are all cgame- and ui-drawn 2D at the screen
edges. In a headset the edges are outside comfortable reading. We cannot edit
cgame to fix it, so it has to be handled at the engine's 2D syscall boundary
or by switching presentation mode for menus.

---

## The renderer question

**Tremulous is OpenGL** - `renderergl1` and `renderergl2`, through SDL2, with
no Direct3D backend anywhere in ioquake3's history. Under WinlatorXR a native
Windows OpenGL application reaches the GPU through Wine's `opengl32` and then
through the container's GL implementation: **Zink** (OpenGL emulated on
Vulkan) or **VirGL**. That is an extra translation layer on top of Box64 and
Wine, and it is the least-travelled path in the stack. The owner's concern is
well founded.

The three candidate routes, with the recommendation:

| Route | Path to the GPU | Verdict |
|---|---|---|
| **Keep OpenGL** | app -> Wine `opengl32` -> Zink or VirGL -> Vulkan -> Turnip | Free, and already written. Unproven for this game. **Measure it before deciding anything.** |
| **Write a Direct3D renderer** | app -> Wine d3d -> DXVK -> Vulkan -> Turnip | **Not recommended.** No D3D renderer for ioquake3 exists, so this means writing one from scratch - a project larger than the port - and it arrives at *more* layers than the option below, not fewer. |
| **Port Quake3e's Vulkan renderer** | app -> winevulkan -> Turnip | **The recommended fallback.** `ec-/Quake3e`'s `renderervk` is a mature, complete Vulkan backend for this engine, descended from Quake-III-Arena-Kenny-Edition. It drops in as `renderer_vulkan` behind the existing `cl_renderer` cvar, and it is the *shortest* path to the GPU of the three - no GL emulation, no D3D translation. |

So the instinct to move off OpenGL is right, but the destination is
**Vulkan, not DirectX**. DirectX would mean writing a renderer that does not
exist, in order to reach Vulkan anyway, one layer later.

**Agreed with the owner, 31 October 2026: Vulkan is the fallback if needed;
DirectX is ruled out.**

This remains a **gated decision, not a plan item**. Stage 1 runs stock Tremulous
in a container and records what the OpenGL build actually does on device. If
it is stable and fast enough we ship it and spend the time on the VR instead.
If it is not, we port `renderervk`. Either way the decision comes from a
measurement, which is what `docs/general_wxr/TIPS.md` demands.

*(One assumption to confirm on device: that winevulkan is exposed to Windows
applications inside a WinlatorXR container. It must be, since DXVK is itself
a Windows-side Vulkan client running under Wine - but it is an inference
until someone runs a Vulkan binary in a container.)*

---

## Scope

**In scope for v1:**

- 6DOF head tracking driving both the view and server-visible aim
- Mono presentation (`MODE_3D = 0`), with SBS and AER as measured upgrades
- Controller input: locomotion, turning, and the full Tremulous action set
- `MODE_VR = 0` for menus, as the Interstellar Marines port does
- HUD legibility in a headset
- Per-class world scale, and wallwalk-correct view composition
- Playable on stock, unmodified, `sv_pure` servers

**Out of scope:**

- Any change under `code/cgame/`, `code/game/` or `code/ui/` that a pure
  server would override - the engine side is where the port lives
- OpenXR, OpenVR, OXRWXR or OpenComposite. There is no PCVR build to run, and
  this is a direct mod, per `docs/general_wxr/CLAUDE.md`
- Motion-controlled hands with visible art. Tremulous aliens have no hands
  and the human weapon models are cgame-owned. Revisit after v1
- Server-side changes of any kind. A VR client must not be a different client

---

## Licensing

Tremulous and ioquake3 are **GPLv2 or later**, and this repository is a
public fork of the source tree. The licence-leak discipline in
`docs/general_wxr/CLAUDE.md` - prose-only edit notes, `leakcheck.py`, no
diffs in the repository - exists for ports of games whose source may not be
redistributed, and **does not apply here**. We commit real code to real
files, as a normal fork. Derived work stays GPL and carries the licence; that
is the whole obligation.

This is the one place where the general guidance is deliberately not
followed, and the reason is recorded here so no future session reinstates it.
