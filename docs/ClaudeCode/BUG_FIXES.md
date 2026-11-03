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

### 1. The UDP socket never opened on device - WSANOTINITIALISED

**Observed:** Quest 3, build stamp `Nov 1 2026 14:44:46`, first device run.
`vr.log` carried one line:

```
socket: FAILED to create UDP socket (error 10093)
```

10093 is `WSANOTINITIALISED`. No pose ever arrived, so head tracking and the
sync pixel were both dead - though the handshake, the `system` read and the
renderer report all worked, which is why the rest of the log looks healthy.

**The tell that located it:** the very next line is
`out: 0 0 1 0 104.5 104.5`, which is only logged *after* `socket()` succeeds
inside `XR_SendOut`. So a socket created one frame later worked. Winsock was
therefore being initialised by somebody, just not before `XR_Init`.

**Cause.** `code/sys/sys_main.c:882` calls `NET_Init()` - which is where the
engine's `WSAStartup` lives - **after** `Com_Init()`. `Com_Init` is what
calls `CL_Init`, which is what calls `XR_Init`. So our socket is *always*
created before the engine starts Winsock. This was never device-specific:
on the desktop something else in the process (SDL or wininet) had already
called `WSAStartup`, so it worked by luck and hid the defect through every
PC test.

**Fixed by** calling `WSAStartup` ourselves in `XR_Init` - it is reference
counted per process, so this is safe alongside the engine's call provided we
`WSACleanup` once - and by retrying the bind every 2 s from `XR_Frame`
rather than giving up for the session.

**Note on the inherited material.** `docs/MetaAI/BUG_FIXES.md` had a
`WSAStartup` entry, and part 3 above correctly rejects its stated reason
(it claims thread affinity; `WSAStartup` is per-process). But the instinct
behind it was right, and discarding the entry along with its wrong
explanation is how this defect got written. **A bad reason for a real
precaution is still a signal.**

**Confirmed gone:** not yet. Awaiting the next device run - on PC the log
now reads `winsock: WSAStartup ok`, but PC never reproduced the failure, so
PC cannot confirm the fix.

### 2. Borderless re-applied every frame (precaution, not a confirmed defect)

`GLimp_EndFrame` re-asserted the borderless flag whenever
`SDL_GetWindowFlags` did not report `SDL_WINDOW_BORDERLESS`. That flag has
no obligation to round-trip under Wine, and if it does not, the result is
`SDL_SetWindowBordered` on every frame inside two translation layers.

Changed to ask once per window pointer, which still satisfies "re-apply
because engines rebuild windows" - a rebuilt window is a new pointer.

**Status: suspected contributor to the hosting crash, unconfirmed.** Listed
here so that if the crash persists, this has already been ruled out rather
than being discovered as a red herring later.

### 3. NULL pointer write in UI_GETCLIENTSTATE during level load

**Observed:** Quest 3, build stamp `Nov 1 2026 15:39:12` (AlphaTest4),
running in **bytecode** VM mode. Crashed every time a level started
loading; menus, server browser and create-server screens were all fine.
Crashed identically with `vr_enable 0`, so never a VR defect.

**What the WinlatorXR log gave us:**

```
wine: Unhandled page fault on write access to 0000000000000004
      at address 0000000140036257
```

**How it was located, which is worth repeating:**

1. Wine logged `WINEPRELOADRESERVE="140000000-1400b2000"` for
   `tremulous.exe`, and `dumpbin /HEADERS` confirms our image base is
   `140000000`. So the fault address is **RVA 0x36257 in our own binary** -
   not a renderer DLL, not JIT-generated code.
2. `dumpbin /DISASM` at that address shows `mov dword ptr [rbx+4],eax`
   preceded by the inlined body of `VM_ArgPtr` - the `VMA()` macro - with
   both of its NULL exits (`args[n] == 0`, and `currentVM == NULL`).
3. Relinking with `/MAP` resolved it to `CL_UISystemCalls + 0x13C7`. The
   relink produced a different file hash, so the map was only trusted after
   disassembling the **new** binary at the same RVA and confirming the
   instructions were identical.
4. The write pattern - offset 4, then offset 0, then a 1024-byte copy at
   offset 0xC - matches `GetClientState()` field for field
   (`connectPacketCount`, `connState`, `servername`).

**Cause.** `case UI_GETCLIENTSTATE: GetClientState( VMA(1) );` in
`code/client/cl_ui.c` writes through the VM pointer with no NULL check, and
on device `VMA(1)` resolved to NULL. The UI polls this while drawing the
connection screen, which is exactly when a level loads.

**Fixed by** checking the pointer, logging once, and returning. A stale
struct for one frame is invisible; a page fault is not. The log also prints
the raw VM argument, because `VMA()` yields NULL for two quite different
reasons and the argument tells them apart: `0` means the QVM really passed
NULL, non-zero means the engine lost `currentVM` under the syscall - which
would be the more serious finding, since every other `VMA()` in that window
would be NULL too.

**Confirmed gone:** not yet. Awaiting the next device run.

**Hypothesis that was wrong, recorded so it is not retried.** Before this
log arrived, the suspect was the QVM JIT: `vm_*` defaults to 2, `vm_x86.c`
generates x86-64 at runtime, and Box64's dynarec has to translate code that
did not exist at process start. It fitted the symptom - menus use the small
`ui.qvm`, level load brings in the much larger `game.qvm` and `cgame.qvm`.
**It was wrong.** AlphaTest4 ran in bytecode mode, which generates no code
at all, and crashed identically. The lesson is the ordinary one: the
hypothesis was plausible and cost a round trip, while the fault address
settled it in minutes. Ask for the crash address first.

**Logging gap this exposed:** nothing recorded which VM mode actually ran,
so "did autoexec.cfg take effect?" could not be answered from the logs. The
`vm:` line now in `vr.log` closes that.

### 4. Hosting a local game crashed: `VM_Create on UI failed` (sv_pure hides every loose file)

**Status: root cause found, fixed, and verified on the desktop.** This
supersedes the "Open: crash when hosting a local game" entry that stood here,
including its claim that the crash did not reproduce on PC. It reproduces
every time.

**Observed:** AlphaTest5, on the owner's PC, 1 Nov 2026. Main menu ->
Create Server -> level select -> loading -> crash. The owner also reported
the UI itself was "totally broken" on the way through.

**What it actually is - one root cause, two symptoms.** `sv_pure` defaults
to `1` (`code/server/sv_init.c:642`), so the instant you host a game the
client goes pure **against its own server**. A pure client can only read
files that live *inside a pk3*. `FS_FindVM` says so outright:

```c
if(search->dir && !fs_numServerPaks)        // code/qcommon/files.c
```

Every directory search path - including `base/` itself - is skipped whole.
And we shipped our 1.2 content as **loose files**: the QVMs in `base/vm/`,
the menus and models under `base/` from `assets/`. So:

1. **The crash.** `vm/ui.qvm` became unfindable, `FS_FindVM` returned `-1`,
   `VM_Create` returned `NULL` at its `if(retval < 0)` exit, and `CL_InitUI`
   raised `Com_Error( ERR_FATAL, "VM_Create on UI failed" )`. This happens on
   the *second* UI VM creation: hosting clears the hunk and frees the UI VM,
   then `CL_StartHunkUsers` rebuilds it - by which time the client is pure.
   That is why the menus work and only hosting dies.

2. **The broken UI.** The loose `ui/*.menu` files went invisible too, so our
   1.2 `ui.qvm` fell back to the **1.1.0** menus inside `data-1.1.0.pk3` and
   could not parse them. Measured: **0** parse errors when not pure, **401**
   when pure. Same cause as the crash, which is why fixing one fixed both.

**Fixed by** packing `assets/` *and* the built QVMs into a single pk3,
`zzz-wxr.pk3`, staged into `base/` by `scripts/deploy_wxr.ps1`. The name has
to sort last: `FS_AddGameDirectory` adds pk3s in ascending order and prepends
each to the search path, so the alphabetically **last** pk3 is searched
**first**. The engine's own `FS_Startup` listing confirms the ordering
(`map-uncreation` first, `data-1.1.0` last).

The loose copies are still staged as well. They cost nothing, they win while
the client is not pure, and they keep asset iteration a file copy rather than
a repack. Both are generated from the same source on every deploy, so they
cannot drift.

This does **not** affect joining real servers: a pure server forces its own
pk3s and QVMs onto us regardless of what we ship.

**Verified, on the desktop, against the deployed folder** - not reasoned:

| | crash | menu parse errors |
|---|---|---|
| loose only, `sv_pure 1` (as shipped) | `VM_Create on UI failed` | 401 |
| loose only, `sv_pure 0` | none, spawns | 0 |
| `zzz-wxr.pk3`, `sv_pure 1` (the fix) | none, spawns | 0 |

The fixed run loads `vm/ui.qvm` and `vm/cgame.qvm` from `zzz-wxr.pk3` and
reaches "entered the game".

**Two prior hypotheses were wrong, and neither was ever actually tested.**

* **The Box64/JIT theory (AlphaTest4).** Wrong, and worse, untested - see
  the autoexec defect below. `autogen.cfg` still read `vm_* "2"`, the JIT.
  The bytecode run that supposedly cleared the JIT never happened.
* **The `UI_GETCLIENTSTATE` NULL pointer (item 3 above).** Also not the root
  cause. The fatal path on the desktop is a clean `Com_Error`, not a page
  fault, and the NULL guard alone would have changed nothing: the UI VM still
  would not have loaded. **Keep the guard** - it is correct hardening, and a
  pure-client failure plausibly leaves `uivm` in exactly the state that
  produces it under Wine - but stop crediting it with fixing this.

**How it was found, which is the transferable part.** The owner reported it
reproduced *on the PC*. That single fact retired the whole Box64/dynarec
branch and, more importantly, meant a debugger was available: `cdb.exe` ships
with the Windows SDK and is already on this machine. The project's "the log is
the only channel back" rule is a Quest constraint, **not a PC one** - when a
device bug reproduces on the desktop, stop reading logs and attach. Diagnosis
took minutes after that, and `VM_Create`'s single `NULL` return made the
filesystem the only possible culprit without needing to run anything.

### 5. `autoexec.cfg` was never deployed, so no device config ever applied

**Observed:** `couldn't exec autoexec.cfg` in the engine console log.

`scripts/autoexec_alphatest.cfg` existed, and `scripts/deploy_wxr.ps1` never
staged it. `Com_ExecuteCfg` execs `base/autoexec.cfg`; nothing was there.

Everything that file was supposed to guarantee therefore never happened on
the headset:

* `logfile 2` never applied - **which is why no `qconsole.log` ever came
  back**, and why the local-hosting crash had "no diagnosis and no log that
  could supply one" for two builds. The engine was printing
  `VM_Create on UI failed` the whole time and nobody could see it.
* AlphaTest4's `vm_* 1` never applied. `autogen.cfg` still read `vm_* "2"`.
  The bytecode experiment was never run.
* `vr_enable 1` / `vr_verbose 1` happened to be set anyway, because they are
  `seta` and were already saved in the device's `autogen.cfg`. That is what
  hid the problem: VR came up, so the config looked like it had worked.

**Fixed by** staging `scripts/autoexec_alphatest.cfg` to
`$Dest/base/autoexec.cfg` in `deploy_wxr.ps1`, and warning loudly if the
source file is missing. Verified: a run with a fresh, empty `fs_homepath`
starts the XrApi client, which only `autoexec.cfg` could have turned on.

**The general lesson, and it is the expensive one.** A config file that is
written but not shipped is worse than no config file: it makes a test look
conducted when it was not. Two device round trips were spent on a hypothesis
that the test run could not possibly have evaluated. If a build is supposed
to change a setting, the build must *prove* the setting changed - which is
what the `vm:` line in `vr.log` now does, and it should have existed first.

### 6. `vr.log` was written to the working directory, not beside the executable

**Observed** while verifying AlphaTest6: `vr.log` appeared in the shell's
current directory, and `CLAUDE.md`, `XR_Log`'s own comment and the deploy
script's reminder all said "next to the executable".

`XR_Init` called `fopen( "vr.log", "w" )`, which is relative to the **working
directory**. That happens to be the executable's folder when you double-click
it, which is why it was never noticed - but a shortcut with a "start in"
folder set, or any launch from elsewhere, silently wrote the log somewhere
nobody would look. `Release/` holds exactly such shortcuts.

**Fixed by** anchoring the path to `fs_basepath`, which defaults to
`DEFAULT_BASEDIR` -> `Sys_BinaryPath()`. `XR_Init` now also prints
`XR: vr.log -> <path>` to the engine log, so the two logs that are always
requested as a pair identify each other.

**Verified** by launching from an unrelated working directory: no `vr.log`
there, and one in the game folder with the resolved path confirmed in
`qconsole.log`.

**Why this was worth fixing immediately rather than filing.** `vr.log` is the
project's only channel back off the device. A diagnostic channel that lands in
an unpredictable place costs exactly the manual round trip the whole logging
discipline exists to avoid, and it fails *silently* - the engine log still
says the XrApi client started.
