# Open items, risks and questions for the owner

Updated 16 November 2026 after the owner's decisions and the first code
changes. The licensing item is closed, per-class scale is implemented, and the
button census logging is in. What remains is one device session, then HUD
depth. "What would settle it" is stated for each, because a plausible claim
without evidence is still open.

---

## Closed 16 November 2026

**Licensing.** `Release/ReadMe.txt` claimed MIT/Unlicense for the mod code
against a GPLv2 fork. Fixed: it now states GPLv2-or-later for the work as a
whole, with media and third-party components under their own licences. See
`03-invariants.md` item 18.

**Documentation drift.** Owner's decision: `docs/ClaudeCode/` and
`docs/MetaAI/` are **archival** and are not to be edited, even where stale.
The drift is recorded in `02-code-state.md` as an observation only. Live docs
are `docs/general_wxr/` (general findings) and this folder.

**Multiplayer, first contact.** The owner created and joined a local server
successfully. That exercises the client's own connection path, the QVM load,
the `sv_pure` packaging fix and spawning. What it does not exercise is a
server that is not ours, other players' cgame versions, or a stock
protocol-72 host. See P1 item 3.

---

## P1 - the next device session

### 1. Stage 1 - characterise OpenGL, and probe native Vulkan

**Why it is still here:** it is the only remaining measurement that can change
the architecture. It was deferred because the renderer is swappable and the
VR work does not depend on it, which was a fair bet - but the bet is still
unsettled.

**The short version.** Run stock Tremulous flat in the container at two
resolutions with `renderergl1` and `renderergl2`, for ten minutes each, and
record frame rate from the container's counters, visual defects and thermal
drop-off. Then run a **known-good native Vulkan Windows application** in the
same container. GL slow + Vulkan good means Zink is the cost and
`renderervk` is worth porting; both slow means the problem is Turnip, the
container or Box64, and a renderer port would not fix it.

**Full protocol, decision matrix and record sheet:** `06-stage1-renderer.md`.

**What would settle it:** the filled record sheet in `05-session-log.md`.

### 2. Census the button table, and finish checkpoint 6

The button table is still derived from `XRAPI_V0_4_DESC.md` section 3 and
Shogo's client. Both are good sources; neither is this headset. The decode
path is already verified on PC - what is missing is which physical control
sets which bit in the runtime's encoder, and that cannot be manufactured on a
desktop.

**Why PC cannot finish it:** the bool string originates in WinlatorXR on the
Quest. The UDP Tester and `xr_spoof.py` only replay our beliefs. A device
capture is the bridge; once captured, it can be replayed on PC forever.

**The device protocol, what is already verified, and an optional raw-index
log line:** `07-button-census.md`.

Ride the same session:

- stand still, sweep the head, lean - does the view stay locked and level?
- **walk up a wall as a Granger** - the horizon test nobody has recorded
- fire while sweeping the hand - `camYaw` must hold against `expected` every
  frame
- spawn one of each class and judge the derived scale (Dretch, Basilisk,
  Marauder, Dragoon, Tyrant, Granger, Battlesuit) - `vr.log` prints the class,
  eye height and resulting u/m, so the number is on the record even when the
  verdict is "feels wrong"
- open the **VR settings overlay** with the dominant stick click and walk both
  pages: every toggle should take effect without a restart, every slider
  should move with the turn stick, and the game must not move or fire while
  it is open. Full checklist in `08-vr-menu.md`
- with a real keyboard/mouse connected, confirm A/B/X/Y/arrows/Space/Enter
  and mouse clicks do nothing **in a match**, that they work in the title
  screen/console/menus, that **ESC from the controller still works**, and that
  `vr_blockDesktopInput 0` restores them

**What would settle it:** a signed-off index table in `05-session-log.md`,
plus the four camera answers.

### 3. Multiplayer beyond our own server

A local listen server is a real milestone: it proves the client hosts, goes
pure against itself, loads the QVMs from `zzz-wxr.pk3` and spawns. It does not
prove the product requirement from `PROJECT_OVERVIEW.md`, which is joining a
**stock, unmodified, `sv_pure` server with other people on it**.

**What would settle it:** one connection to a protocol-72 server hosted
elsewhere, one spawn, one shot, one class change, one death. GPP1 is protocol
70 and cannot join us, so the host has to speak 72.

**Question for the owner:** is there a public 72 server, or should we arrange
a two-client test with a second machine? The latter is more controlled and
also exercises two different cgame builds.

### 4. Owner verdict on the known rough edges

From `Release/AlphaTest11/README.txt`, all still open:

- tracers and muzzle flash come from the view, not the barrel
- no crosshair and no laser sight, so the gun is the only aiming reference
  until the first shot
- no `RF_DEPTHHACK` on the hand gun, so it can clip into nearby geometry
- `vr_hudScale` shrinks the console and chat as well as the HUD

My read: the laser sight has the best effort-to-value ratio; the clipping is
the one most likely to be noticed in a corridor; the tracer origin is
cgame-owned and probably should be accepted rather than filtered. But these
are feel decisions and the owner is the authority.

---

## P2 - the next code

### 5. Per-class world scale (stage 10, D9) - implemented, needs a device pass

Owner's decision, 16 Nov 2026: **preserve the apparent body size.** The
player's real eye height maps to the active class's eye height above its own
feet, so a Tyrant's world is smaller and a Dretch's is larger. The intensity
is the point; comfort options can be layered on later rather than averaging
the classes away now.

Implemented in `xr_view.c`: a 13-row class table derived from
`assets/configs/classes/*.cfg` (`viewheight - mins[2]`), anchored on the
human's 48 units at 1.6 m so `vr_worldScale 30` still means what it always
meant. `vr_classScale 0` pins one constant for desktop tests. The class is
read from `ps.stats[STAT_CLASS]`, and `vr.log` prints:

```
class: tyrant (idx 10) viewheight=64 eye=86 -> worldScale 53.75 u/m
       (human 30.0, classScale 1)
```

**What remains:** one pass per class on device. The formula is derived, but
"does a Dretch feel right at 9.4 u/m" is a comfort judgement only the owner
can make. If a class is uncomfortable, the likely shape of the fix is a
player-facing strength control applied to the derivation, not a per-class
hand-tuned table - a correct value transfers, and twelve hand-tuned numbers
would be twelve things to re-tune.

**Also worth checking:** the scale changes the hand offset as well as the
head, so the hand gun follows the body size. For humans and battlesuits that
is the intended behaviour; aliens have no hand gun.

### 6. HUD legibility and depth (stage 9)

`vr_hudScale` moves the 2D layer towards the centre, which fixes size but not
distance. The stage 9 decision was that the HUD stays in VR and the menus do
not, so this is depth placement, not mode switching.

**Cheap next step:** expose the effective HUD distance as a setting, measure
one value on device, then derive rather than invent.

---

## P3 - later

- In-game settings menu - there are now ~42 `vr_*` cvars and a headset player
  cannot edit an INI
- A comfort vignette, if locomotion proves uncomfortable
- Haptics as decaying frame durations (the protocol model, not an amplitude)
- SBS or AER, only if stage 1 and stage 7 measurements say stereo is
  affordable
- One-file install bundle, if distribution becomes the goal

---

## Risks I am carrying

| Risk | Where it would show | Mitigation already in place |
|---|---|---|
| A device fix regresses on PC or vice versa | `WSAStartup`: worked on PC by luck, failed on device | log every return that can fail; retry bind |
| A stale archived cvar defeats a new default | the "fix did not take" report | `vr_cfgVersion` migration; only old-default values touched |
| Two packet sources interleave | alternating poses, impossible pitch, `drained 2` | run one source; `raw:` line identifies content |
| A desktop-tuned value does not transfer | hand placement, HUD scale | measure in the frame it is used; prefer derived values |
| A test passes because the input was static | the AlphaTest9 lesson | sweep, do not hold - `xr_spoof.py --hand-sweep` |
| A build is tested that is not the build just made | any "no change" result | freshness guard + log line 1; check it first |
| A renderer decision is made on a guess | stage 1 deferred again | run the probe on the same trip as the census |

## Changes made outside this folder

- `Release/ReadMe.txt`: licence section only, GPLv2-or-later.
- `code/winlatorxr/`, `code/client/`, `cmake/client.cmake`: per-class scale,
  FOV comfort scale, button census logging, and the engine-side VR menu.
- `scripts/autoexec_alphatest.cfg`: `bind F1 vr_menu`.
- `scripts/deploy_all.ps1`: new wrapper for the three standard targets;
  `deploy_wxr.ps1` remains the single-target worker and now also stages
  `ReadMe.txt`, `GPL` and `COPYING.txt`.
- `Release/DeployReady`: the clean full build, regenerated by
  `deploy_all.ps1`; intentionally under the gitignored `Release/`.
- No archived documentation touched; the owner's root `README.md` edits are
  untouched.
