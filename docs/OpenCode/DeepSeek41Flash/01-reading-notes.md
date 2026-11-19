# What the three document sets say

Read in the order the project asks for - MetaAI, then ClaudeCode, then
`general_wxr` - because that order makes the corrections visible. Read the
other way round, the ClaudeCode set looks like pedantry; read this way, it is
a list of traps that were already paid for.

---

## 1. `docs/MetaAI/` - the prior sketch

**What it is.** A complete-looking package: four planning documents, a
VS2022 solution, a 193-line XrApi client, four prose patch notes and a UDP
spoofer. It describes a monocular 6DOF port driven entirely from
`code/cgame/cg_view.c`, with the sync pixel in `tr_main.c` and the transport
initialised from `cl_main.c`.

**What it is actually worth.** The shape is right and it has been adopted:

- transport in its own module, not scattered through the client
- handshake files under `Z:\tmp\xr`, with a desktop fallback
- sync pixel drawn in the renderer path
- mono first (`MODE_3D 0`), because WinlatorXR duplicates one image
- outbound packet once per presented frame, not per view calculation

Everything below that shape is unsafe, and the ClaudeCode set documents why
in `docs/ClaudeCode/BUG_FIXES.md` parts 1-3. The three defects that mattered
most:

1. **The camera is in `cgame`.** A pure server replaces cgame at connect
   time, so the port would not exist on the servers where the game is
   actually played. It also calls Win32 critical sections from inside a
   sandboxed QVM, which cannot compile.
2. **The packet layout past float 28 is invented.** It describes an altitude
   and two grip quaternions "in 0.5+". `XRAPI_V0_4_DESC.md` section 8 is
   explicit that nothing before 0.6 changes the packet, and float 28 is the
   last float.
3. **The sync square is in the wrong corner.** The projection puts y = 0 at
   the top, but the quad is drawn at `vidHeight - 10`: bottom-left. The
   runtime samples (0,0). This is the defect that presents as "head tracking
   swims no matter how good the maths is".

Also worth carrying, because it is easy to copy by accident: the inherited
`BUG_FIXES.md` records fixes that are not in the code beside it (no
`setlocale`, no `Sleep` in the receive loop, `WSAStartup` claimed to be
thread-affine when it is per-process). The general lesson is in
`docs/ClaudeCode/BUG_FIXES.md` part 3: **an entry means an observed failure
and an observed recovery**, otherwise it is a design note.

Two things are still worth taking wholesale:

- `scripts/test_udp_spoofer.py` as the starting point for `xr_spoof.py`
- the `vcpkg.json` dependency list (SDL2, openal-soft, curl, zlib,
  libjpeg-turbo, freetype)

---

## 2. `docs/ClaudeCode/` - the current project's record

**What it is.** The plan this repository actually followed. Four files:
overview, plan, status, bug list. The plan stages work in ten milestones
with **human checkpoints**; the status file records what has been verified
and, unusually, what has been *withdrawn* and why.

**The parts that changed the design, not just the implementation:**

- **The render hook is `CG_R_RENDERSCENE`** (`code/client/cl_cgame.c`), not
  cgame. The engine receives cgame's `refdef_t` and can modify it before
  `re.RenderScene`. Pure-safe, works with any mod's cgame.
- **Compose, never replace, `viewaxis`.** The basis cgame built already
  contains wallwalking, weapon kick, fall-bob and death-cam roll. Replacing
  it breaks all four and puts the horizon anywhere. This is a correctness
  requirement for wallwalking, not polish.
- **Yaw and pitch have exactly one home: the usercmd path**
  (`code/client/cl_input.c` -> `cl.viewangles`). The server must agree about
  aim. The render hook applies only roll and the positional offset. Applying
  yaw in both places is the double-rotation bug.
- **World scale is per class.** Twelve classes, twelve viewheights and
  bounding boxes. One constant is right for one of them.
- **Hands are converted independently of the head**, sharing only the
  recentre origin.
- **Send the FOV you actually rendered**, every frame. A configured FOV is
  not the same number; the runtime scales the frame by what it is told, and a
  wrong scale reads as drift, not as a wrong FOV.
- **The player's world yaw is not `cl.viewangles[YAW]`.** Add
  `SHORT2ANGLE(cl.snap.ps.delta_angles[YAW])`. Everything that maps a room
  pose into the world depends on this.
- **A rotation does not cancel against itself across a change of basis.**
  Two frames sharing a body yaw must both *contain* it; omitting it from both
  conjugates it instead. This is what failed AlphaTest9 and what its fixed
  replacement, AlphaTest11, does correctly.

**What the status file is honest about.** Stage 1 (on-device OpenGL
characterisation) is deferred, not done - and the renderer decision rides on
it. The button table is from documents, not from this device. Stages 9 and 10
are desktop-only. Multiplayer is untested. Everything in the "still open"
lists is still open.

**Where it has drifted.** See `02-code-state.md` for the specific claims that
are now stale. The general one: `CURRENT_STATUS.md` is a record of a
particular day, and the release folder has moved past it.

---

## 3. `docs/general_wxr/` - the shared framework

**What it is.** Game-agnostic guidance for this family of ports, copied per
project. It is the authority on the protocol and on working method, and it
says when it is uncertain. Claims are tagged `[spec]`, `[runtime]`,
`[device]`; where it says "unverified", it means it.

**The protocol in six lines** (`XRAPI_V0_4_DESC.md`):

- Handshake through files in `Z:\tmp\xr`: we write `version` and `vr`, the
  runtime writes `system` (seven lines) and optionally `mask`.
- `version` is read **once**, and an unrecognised value means **total
  silence** - no listener, no packets, no error.
- Inbound on **7872** (and 7873 from 0.4 - duplicates, not a fallback):
  `client <29 floats> <bool string> <HMD name>`, where float 28 is
  `HMD_SYNC`.
- Outbound on **7278**: `<L haptic> <R haptic> <MODE_VR> <MODE_3D> <FOVx>
  <FOVy>`, once per frame, always - including while `MODE_VR 0`, or the
  stream cannot be turned back on.
- The **red sync pixel** at (0,0), 10x10, drawn last, filtering and blending
  off, echoing `HMD_SYNC` byte for byte. The runtime calibrates itself to the
  red values it observes; do not pre-correct.
- Quaternions arrive OpenXR-style (right-handed, +Y up, -Z forward, metres)
  and are **often non-unit** from testers. Normalise unconditionally.

**The three facts that make this project different from the rest of the
family:**

1. **id Tech 3 is right-handed, like OpenXR**, so the conversion is a pure
   relabel `(x,y,z)_xr -> (-z,-x,y)_q3`, determinant +1, **no sign flip**.
   The flip in the framework's section 6 is for a left-handed engine (Unity);
   applying it here mirrors the world, which looks plausible until someone
   reads text in it.
2. **The runtime's FOV fields are degrees and are applied only if > 1.** A
   client that sends tangents sends values the runtime ignores.
3. **Haptics are a duration in frames, not an amplitude**, and only values
   `> 0` are stored; sending zero does not clear a rumble.

**The method that is not optional here:**

- Instrument before theorising; the log is the instrument.
- Build stamp on line 1, and a deploy script that refuses stale builds.
- One change per deploy.
- Measure offsets in the frame you will use them in. **A correct value
  transfers.** If it only works where it was found, the frame is wrong.
- Test any compensation against a **moving** input. Static poses make the bug
  and the fix produce the same log line.
- Get a real capture before writing a sanity bound. Synthetic poses encode
  what you believe the protocol says.
- Version archived config before you need to, because the people holding
  stale defaults are the testers.
- If the log is the only channel back, it must work from the first line of
  initialisation.

**The one place this project deliberately does not follow the framework:**
the licence-leak discipline. This is a public GPL fork; real code is
committed to real files. The reason is recorded in `PROJECT_OVERVIEW.md` and
repeated in the root `CLAUDE.md` so no future session reinstates it.

---

## What I would tell a new agent in five lines

1. The port lives in the engine's client, never in a QVM module.
2. Yaw and pitch are usercmd; roll and position are the render hook.
3. Compose the HMD basis onto cgame's, never replace it.
4. The sync pixel and the outbound FOV are how the runtime knows what you
   drew - both must describe reality, every frame.
5. Trust the log, then the code; trust no document that has not been checked
   against either.
