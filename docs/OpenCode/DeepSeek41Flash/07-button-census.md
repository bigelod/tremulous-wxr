# The button census - what can be done on PC, and what cannot

Short answer: **the decode can be tested on PC, the hardware truth cannot.**
Everything except the last step is already automated; the last step is about
two minutes on the headset, once.

---

## 1. There are two separate questions

**Q1. Does our decoder turn a bool string into the right actions?**

Given a string of `T`/`F`, does index 12 produce `+moveup`, does the
dominant-hand switch mirror it correctly, does a press produce exactly one
edge, does a release always follow, does the gate drop the right things?

**Q2. Does the runtime assign each physical control to the index we think it
does?**

Is the right grip really bit 12 on *this* headset, or is it bit 13? The
documented table and Shogo's client agree, but neither is the owner's Quest 3
under the owner's WinlatorXR version.

These are different questions, and only one of them is available on the
desktop.

## 2. Q1: yes, on PC, and it is already done

`scripts/xr_spoof.py` can synthesise arbitrary bool strings, so the whole
decode path runs on the desktop:

- press each index and confirm the intended bind fires
- confirm one key-down and one key-up per press, no repeats, no stuck keys
- confirm the dominant-hand mirror (`vr_dominantHand 1`)
- confirm the menu-click handover when the gate closes and opens
- confirm an unknown/unmapped index is silent rather than wrong

This was run during stage 7 and recorded 9/9 actions decoding, zero warnings,
left-handed swap working, movement and turn measured. It should be re-run
after any change to the table, but it cannot discover a wrong table: the
spoofer sends the indices we told it to send.

## 3. Q2: no, not on PC

The bool string is produced by **WinlatorXR's Android-side runtime** from the
headset's controllers. There is no Windows build of WinlatorXR to run on a
desktop, and the PC tools are only *replayers*:

- the XrAPI UDP Tester sends synthetic packets
- `xr_spoof.py` sends synthetic packets
- `SamplePoseData.txt` is a capture of a real packet, but a static one

All three encode what we believe the protocol says. If the belief is wrong,
the rig agrees with the bug - which is exactly the lesson in `BUG_FIXES.md`
item 10, where synthetic poses had been "corrected" to the same wrong
coordinate space as the code.

**The only source of hardware truth is a device capture of a deliberate
press-per-control session.** Once that capture exists, it can be replayed on
PC forever. The capture is the bridge; the headset is the only place it can
be made.

## 4. The minimal device protocol

One session, one build. With `vr_verbose 1` the log now carries both the
changed indices and the decode:

```
input: idx 12 down  idx 2 down
input: bools="TFTFFFFFFFFFFFFFFFF" (21) down=[jump sprint] hand=right
```

`idx N down/up` is the ground truth, including unmapped indices. `bools` is
the raw string, `(21)` its length, `down=[...]` the decoded action names,
`hand=` the mirror state. So the procedure is:

1. Make sure **no other packet source is running** (stop the UDP Tester and
   any spoofer). Two sources interleave and the pose - and the bools -
   alternate, which is indistinguishable from a broken table.
2. `vr_verbose 1`.
3. Spawn as a human with a rifle (`+devmap atcs +exec vrtest.cfg`, or join
   the local server the owner already uses).
4. Press each control **one at a time**, hold for about a second, release:
   - dominant trigger
   - off trigger
   - dominant grip
   - off grip
   - dominant stick click
   - off stick click
   - A, B, X, Y
   - left menu, **last**: it is expected to be unmapped - WinlatorXR turns
     it into ESC, which opens the menu and closes the input gate, so it will
     not produce any further census lines
5. Note the order you pressed them in; the log is a sequence of changes.
6. Repeat with `vr_dominantHand 1`.

Then read `vr.log` and map each change to the control that caused it. The
positions that moved are the truth; the names in `down=[...]` are our
current belief. Where they disagree, **the raw string wins and the table in
`xr_input.c` is what changes** - one table, no architecture.

## 5. The raw-index census line - now implemented

Added 16 November 2026, behind `vr_verbose`, in `xr_input.c`. On every change
it logs each bit that moved, by number, before the decoded line:

```
input: idx 12 down  idx 2 down
input: bools="TFTFFFFFFFFFFFFFFFF" (21) down=[jump sprint] hand=right
```

The first line is ground truth and includes indices the action table does not
map - which is precisely the case a census is looking for. The second line is
our interpretation. On the first packet the edge line is suppressed, because
with no previous string every pressed bit would read as a fresh press.

So the census can now be read without counting characters in `bools`.

## 6. What "done" looks like

A table in `05-session-log.md`:

```
index  physical control     expected action     observed action    verdict
0      left grip            crouch              ...                ...
2      left stick click     sprint              ...                ...
...
18     right trigger        fire                ...                ...
19     (immersive flag)     -                   -                  -
20     (SBS flag)           -                   -                  -
```

Signed off by the owner, with the log line quoted. After that the button
table is measured rather than asserted, and `04-open-items.md` item 4 is
closed except for the camera checks that ride along with the same session.

## 7. The honest cost

- **Desktop:** already paid, re-runnable in seconds.
- **Device:** one session, dominated by putting the headset on and spawning,
  not by the census itself.
- **If skipped:** every wrong index presents as "the controls feel odd", and
  every fix is a guess followed by another deploy. The census converts that
  into one page of numbers.
