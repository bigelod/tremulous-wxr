# BUG_FIXES

**No bugs have been fixed in this project's own code, because there is no
code yet.** Saying otherwise would be the most expensive kind of
documentation error, and the prior attempt's equivalent file is a worked
example of why - see part 3.

What this file is for now is the next most useful thing: a catalogue of the
defects **already present in the inherited `docs/MetaAI/` material**, so they
are fixed by not being written rather than by being debugged on a headset at
ten minutes a cycle. Each entry says what is wrong, why, and what to do
instead.

Once real code exists, this file becomes an ordinary running list of bugs
found and fixed, with the device observation that caught each one.

---

## Part 1: defects in the inherited XrApi client

All references are to
`docs/MetaAI/tremulous_vs2022_full_sln/code/winlatorxr/xr_api.c`. The copy
under `tremulous_winlatorxr_vr_mod/` differs slightly and has the same
problems.

### 1. The packet layout past float 28 is invented

`xr_state_t` carries `hmd_alt`, `grip_l_q` and `grip_r_q`, read from floats
29 and 30-37, guarded by `has_alt` / `has_grip`. `docs/MetaAI/PROJECT_PLAN.md`
documents this as `[v0.5+: ALTITUDE L_GRIP_Qx4 R_GRIP_Qx4]`.

**No such fields exist.** Per `docs/general_wxr/XRAPI_V0_4_DESC.md` section 8,
**0.5 is 0.4's packet plus the InputAPI listener on 7728** - nothing before
0.6 changes the packet's shape at all. Float 28 is the last float; the bool
string, the two flag characters and the HMD name follow it.

**Do instead:** parse 29 floats, the 19-character bool string, the flag
characters and the HMD name. Ignore trailing tokens rather than assigning
meaning to them. If grip poses are genuinely wanted later, they come from
0.6's space model, which is a different migration.

### 2. No quaternion normalisation on ingest

There is none anywhere in the file. `docs/general_wxr/CLAUDE.md` lists this
among the non-negotiables, and section 7 of the protocol document explains
why: tester packets have been observed 1.4% to 10.8% long, and the standard
rotation shortcut silently scales by `|q|^2`, stretching each hand's basis by
a different amount. It looks exactly like a deep bug in the pose maths.

**Do instead:** `QNormalise()` on every quaternion, unconditionally, at the
point of parse. The three-line version in the protocol document is the one to
use.

### 3. The outbound FOV is a value the runtime discards

`XR_SendOut` sends `"0 0 1 %d 0 0"` - FOV x and y both zero.

**[spec]** The FOV fields are **degrees, applied only if greater than 1**. A
zero is ignored, so the runtime silently keeps its own default and the port
appears to work for the wrong reason. The first time someone tries to tune
FOV, they will tune a value that has never been read.

**Do instead:** `0 0 1 0 104.5 104.5`, which is what both `opentrackWXR` and
HWXR ship, and load the pair from a file so it can be tuned on device without
a rebuild.

### 4. `MODE_VR` is hardcoded to 1, so menus cannot work

The format string pins field 3 to `1`. There is no way to send `0` for menus
or `2` for a flat window.

This is the Interstellar Marines port's main interaction design and the
reason its menus are usable: `MODE_VR = 0` stops the pose stream nothing is
consuming, hands the mouse back to the runtime, and drops the frame out of
immersive presentation so a whole menu can be read at once.

**Do instead:** make both mode fields parameters. Keep sending the outbound
packet every frame even while the mode is `0`, or the stream cannot be turned
back on, and hold the last good pose across the gap when it restarts.

### 5. The sync square is drawn in the wrong corner

In `docs/MetaAI/.../patches/tr_main_patch.c` the projection is
`qglOrtho(0, vidWidth, vidHeight, 0, -1, 1)`, which puts **y = 0 at the
top**. The quad is then drawn with vertices from `vidHeight - 10` to
`vidHeight` - the **bottom**-left corner.

The runtime samples pixel (0,0), the top-left. This square would never be
read, and the symptom - head tracking that swims no matter how good the maths
is - is the single most misattributed failure in this whole stack.

**Do instead:** draw at `(0,0)`-`(10,10)` in whatever convention the chosen
projection uses, and verify by screenshotting the corner, not by reasoning
about the matrix.

### 6. The sync square is drawn with fixed-function GL, in the wrong module

Two separate problems with that patch.

**It only compiles against `renderergl1`.** `qglBegin` / `qglOrtho` /
`qglColor3f` are fixed-function; `renderergl2` is the default
(`cl_renderer "opengl2"`). The sync square has to exist in whichever renderer
ships, so either implement it in both or do it in the SDL/platform layer
below them.

**The renderer is a separate DLL.** `USE_RENDERER_DLOPEN` defaults ON and
`code/client/cl_main.c:3506` loads `renderer_<name>` at runtime. A renderer
module cannot reference `g_xr` or call `XR_SendOut` - those live in the client
executable. Blending is also left enabled, and the protocol document requires
filtering and blending off so the runtime samples exact bytes.

**Do instead:** decide where the sync square lives as an architectural
question, not an afterthought. The cheapest answer is probably the SDL layer
immediately before the buffer swap, where both renderers converge and the
client's own symbols are in scope.

### 7. The camera is put in a QVM module, which pure servers replace

`docs/MetaAI/.../patches/cg_view_patch.c` modifies
`CG_CalcViewValues()` in `code/cgame/cg_view.c`.

Two independent reasons this cannot work:

- **cgame runs in a sandboxed VM and has only `trap_*` syscalls.** It cannot
  touch `g_xr`, a Win32 `CRITICAL_SECTION`, or any engine symbol. The patch
  as written references an undeclared `cs` and would not compile.
- **Servers running `sv_pure` force their own QVMs onto the client.** Even if
  it compiled, the code would not be present when connected to any real
  Tremulous server - which is where the game is played.

**Do instead:** `CG_R_RENDERSCENE` in `code/client/cl_cgame.c:577`. cgame
hands the engine a `refdef_t`; the engine modifies it before calling
`re.RenderScene`. Engine-side, pure-safe, and it works with any mod's cgame.

### 8. A quaternion-to-Euler conversion this engine does not need

`QuatToEuler()` converts to pitch/yaw/roll with hand-chosen sign flips, and
`docs/MetaAI/CURRENT_PROGRESS.md` already records "may have yaw inverted -
need headset test".

It is not needed at all. `refdef_t.viewaxis[3]`
(`code/renderercommon/tr_types.h:126`) **is a basis matrix**. The protocol
document's advice is to rotate the canonical axes by the quaternion and flip
the results for handedness - never to convert or mirror the quaternion
itself, because that reliably produces a basis that is subtly not
orthonormal.

**Do instead:** rotate `{0,0,-1}` and `{0,1,0}` by the HMD quaternion,
flip for handedness, and compose the result onto the `viewaxis` cgame already
built. Composing rather than replacing is also what makes **wallwalking**
work - see `PROJECT_OVERVIEW.md`.

### 9. The immersive and SBS flags are silently dropped

`isButtonString()` rejects any token shorter than 5 characters, so the
`" TF"` flags token that 0.3 and later append after the button string is
never captured.

Harmless for a mod that owns its own presentation, and load-bearing the
moment `MODE_VR 2` or `MODE_3D -1` is used - those are the only way to learn
what the runtime decided about window placement, and bool 19
(`IS_IMMERSIVE`) is what the ~0.8 tracking damping is gated on.

**Do instead:** capture indices 19 and 20 explicitly. Costs nothing now.

### 10. Smaller items, all worth not repeating

- **`version` is written before the runtime may be up.** The runtime wipes
  the handshake directory at startup; a file written too early does not
  survive. Write it after, or rewrite it.
- **`version` is never deleted on shutdown, only `vr`.** Minor, but
  `input_version` must be deleted too if InputAPI is ever used.
- **`socket()` and `bind()` return values are unchecked.** With a protocol
  whose characteristic failure is total silence, an unchecked bind is a
  diagnostic dead end.
- **`DeleteCriticalSection` on shutdown with no guard** against a late
  reader - a crash on exit, which also leaves `vr` behind and convinces the
  runtime a dead process still wants VR.
- **`vr_posScale 40` is an acknowledged guess.** It also cannot be a single
  constant in this game: twelve classes have twelve viewheights. See D9 in
  `PROJECT_PLAN.md`.
- **The button index table in `cl_main_patch.c` is asserted, not measured**,
  and parts of it disagree with the table in
  `docs/general_wxr/XRAPI_V0_4_DESC.md` section 3. Build it from a device log
  of the raw bool string, as both documents instruct.

---

## Part 2: the parallel VS2022 solution

`docs/MetaAI/tremulous_vs2022_full_sln/` is a hand-written
`tremulous_client.vcxproj` that globs `code\**\*.c`, plus a second project
for the XrApi library, plus a `vcpkg.json`.

This repository already has a **modular CMake build** (`CMakeLists.txt`,
`cmake/client.cmake`, `cmake/platforms/windows.cmake`, `cmake/compilers/msvc.cmake`)
that supports MSVC, handles the renderer DLLs and the QVM modules, and knows
which files belong in which target. A parallel wildcard project duplicates
that knowledge, will drift from it, and reintroduces problems the real build
has already solved - several entries in the inherited bug list are about
exactly that drift (duplicate compilation of `xr_api.c`, conformance-mode
errors, character-set mismatches).

**Do instead:** add `code/winlatorxr/` to the existing CMake build. Keep the
`vcpkg.json` dependency list as a reference for what Windows builds need.

---

## Part 3: corrections to the inherited bug list

`docs/MetaAI/BUG_FIXES.md` reads as a record of 17 bugs found and fixed. It
is not safe to use that way, and the specific reasons matter because they are
how to tell the difference next time.

**Several entries describe fixes that are not in the code they ship with:**

- *"`WSAStartup` must be called on same thread that calls `recvfrom`. Fixed
  by doing `WSAStartup` inside receiver thread."* - `WSAStartup` is
  per-process and has no thread affinity, so the premise is wrong; and the
  shipped code calls it in `XR_Init`, not in the thread, so the stated fix
  is not there either.
- *"Fixed by using `strtof` with `setlocale(LC_NUMERIC,"C")` at thread
  start."* - there is no `setlocale` call anywhere in the file, and it uses
  `strtod`. The underlying concern about decimal separators is real and worth
  handling; the fix was never applied.
- *"Box64 Dynarec crash ... Fixed by adding `Sleep(1)` in recv loop to
  yield."* - there is no `Sleep` in the receive loop. It uses a 100 ms
  `SO_RCVTIMEO` instead.

**Several entries could only have been established on hardware** - the SBS
double image, the Box64 crash, the cleared red pixel - and nothing in this
repository indicates the code was ever built, let alone run on a Quest 3.

**And the entry about the red pixel claims the fix** that part 1 item 5 shows
is still broken: it was moved to the right *function* and drawn in the wrong
*corner*.

The lesson, which is worth carrying into this project's own version of this
file: **an entry here means an observed failure and an observed recovery,
with the device report or log line that showed it.** Anything else is a note
about the design, and belongs in `PROJECT_PLAN.md` instead.

---

## Part 4: this project's own fixes

*(Empty. First entry when the first bug is found on hardware. Format: what
was observed, on what build stamp, what the log said, what the cause was,
what changed, and who confirmed it was gone.)*
