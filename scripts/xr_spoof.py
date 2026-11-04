#!/usr/bin/env python3
"""Minimal XrApi 0.4/0.5 runtime simulator, for desktop testing.

Sends inbound pose packets to 127.0.0.1:7872 and prints whatever the game
sends back on 127.0.0.1:7278. The XrAPI UDP Tester is the tool of record and
proves interoperability; this exists for the things it cannot do -- run
unattended, and feed deliberately malformed input.

The quaternions it sends are intentionally NOT unit length (see --quat-scale).
Captures used during Shogo's development were 1.4% to 10.8% long, and a client
that does not normalise on ingest silently scales each hand's basis by a
different amount. That failure looks like a deep bug in the pose maths, so it
is worth provoking on purpose.

Usage:
    python scripts/xr_spoof.py                 # 30 s at 72 Hz
    python scripts/xr_spoof.py --seconds 10
    python scripts/xr_spoof.py --malformed     # also send junk packets
"""

import argparse
import math
import socket
import time

PORT_IN = 7872   # runtime -> game
PORT_OUT = 7278  # game -> runtime

# GUEST_BUTTON_COUNT is 19; the two flag characters (immersive, SBS) follow as
# a separate token from 0.3 onward.
BUTTONS = 19


def yaw_quat(deg):
    """Unit quaternion for a yaw of deg degrees about the XR up axis (+Y)."""
    h = math.radians(deg) / 2.0
    return [0.0, math.sin(h), 0.0, math.cos(h)]


def pitch_yaw_quat(pitch_deg, yaw_deg):
    """Pitch about +X then yaw about +Y, XR convention, unit length."""
    cy, sy = math.cos(math.radians(yaw_deg) / 2), math.sin(math.radians(yaw_deg) / 2)
    cp, sp = math.cos(math.radians(pitch_deg) / 2), math.sin(math.radians(pitch_deg) / 2)
    return [sp * cy, cp * sy, -sp * sy, cp * cy]


def build_packet(t, frame_id, quat_scale, pressed=None, sticks=None,
                 head_yaw=None, hand_yaw=None, hand_sweep=False):
    """One inbound packet: client <28 floats> <frame id> <bools> <flags> <name>."""
    # Fixed angles make the one test that a swept head cannot: whether
    # movement still goes where the HEAD faces once the aim has been handed
    # to the gun. With both swinging, "did it go the right way" and "which
    # way was the right way" are the same unknown.
    if head_yaw is not None:
        yaw = math.radians(head_yaw)
        pitch = 0.0
    else:
        yaw = 0.6 * math.sin(t * 0.5)
        pitch = 0.25 * math.sin(t * 0.31)

    # Head orientation as a yaw/pitch quaternion, then deliberately de-normalised.
    cy, sy = math.cos(yaw / 2), math.sin(yaw / 2)
    cp, sp = math.cos(pitch / 2), math.sin(pitch / 2)
    hmd_q = [sp * cy, cp * sy, -sp * sy, cp * cy]
    hmd_q = [c * quat_scale for c in hmd_q]

    # Hand separation of 0.26 m, which is what a real headset reports -- a
    # desktop tester capture showed 0.75 m, so do not tune against that.
    hands = []
    for sign in (-1.0, 1.0):
        hands.append({
            # A MOVING hand is the only honest test of "does the camera
            # follow the gun?". A hand held at a fixed angle gets absorbed
            # into the server's delta_angles the moment anything sets the
            # view angles, after which the aim is constant and the camera
            # cannot be seen tracking it -- the test passes with the bug
            # fully intact, which is how AlphaTest9 shipped. Yaw and pitch
            # sweep on different periods so neither can stand in for the
            # other.
            "q": [c * quat_scale for c in
                  (pitch_yaw_quat(25.0 * math.sin(t * 0.83),
                                  40.0 * math.sin(t * 0.47))
                   if hand_sweep
                   else yaw_quat(hand_yaw) if hand_yaw is not None
                   else [0.0, 0.0, 0.0, 1.0])],
            "stick": [0.0, 0.0],   # overwritten below from --sticks
            # HEAD-CENTRED coordinates, which is what the runtime actually
            # sends. SamplePoseData.txt -- a real Quest 3 capture from the
            # XrApi UDP Tester -- has the head at y = 0.006 and both hands at
            # y = -0.23. There is no floor in this coordinate system.
            #
            # An earlier version of this file "corrected" these to absolute
            # room coordinates with the head at 1.60, reasoning that -0.25
            # would be under the floor. That invented a coordinate system the
            # runtime does not use, and a sanity check written against the
            # invention then rejected every real pose the headset sent.
            "pos": [sign * 0.18, -0.235, -0.27],
        })

    # Sticks, as (lx, ly, rx, ry). Held constant on purpose: a stick that
    # swept would make "did movement happen?" and "did it happen in the right
    # direction?" the same observation, and they are different questions.
    if sticks:
        hands[0]["stick"] = [sticks[0], sticks[1]]
        hands[1]["stick"] = [sticks[2], sticks[3]]

    f = []
    for h in hands:
        f += h["q"] + h["stick"] + h["pos"]        # 0-8 left, 9-17 right
    # Head position. This MOVES: with a static head the positional path
    # reads as a correct [0 0 0] offset and looks verified when it has not
    # been exercised at all. Each axis gets a different period so a swapped
    # or sign-flipped axis cannot hide behind symmetry.
    #   X: lean right/left, 0.15 m
    #   Y: stand/crouch,    0.05 m about a 1.6 m eye height
    #   Z: lean back/fore,  0.10 m  (-Z is forward in OpenXR)
    head_pos = [
        0.037 + 0.15 * math.sin(t * 0.70),
        0.006 + 0.05 * math.sin(t * 0.90),
        -0.017 + 0.10 * math.sin(t * 0.40),
    ]

    f += hmd_q                                      # 18-21
    f += head_pos                                   # 22-24 head position
    f += [0.063]                                    # 25 IPD, metres
    f += [99.00, 103.40]                            # 26-27 FOV, DEGREES
    f += [float(frame_id)]                          # 28 HMD_SYNC

    bools = ["F"] * BUTTONS
    for idx in (pressed or []):
        if 0 <= idx < BUTTONS:
            bools[idx] = "T"

    floats = " ".join("%.4f" % v for v in f)
    return "client0 %s %s TF META" % (floats, "".join(bools))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--hz", type=float, default=72.0)
    ap.add_argument("--quat-scale", type=float, default=1.054,
                    help="multiply quaternions by this; 1.054 is 5.4%% long")
    ap.add_argument("--malformed", action="store_true",
                    help="inject junk packets to prove they are rejected")
    ap.add_argument("--press", default="",
                    help="hold these bool indices down for the whole run, "
                         "comma separated. 18 = right trigger, 12 = right "
                         "grip, 0 = left grip, 7 = left trigger, 2 = left "
                         "stick click, 8/9/10/11 = X/Y/A/B.")
    ap.add_argument("--sticks", default="",
                    help="lx,ly,rx,ry held for the whole run, each -1..1. "
                         "y is forward-positive.")
    ap.add_argument("--head-yaw", type=float, default=None,
                    help="hold the head at this yaw in degrees instead of "
                         "sweeping it")
    ap.add_argument("--hand-yaw", type=float, default=None,
                    help="hold both hands at this yaw in degrees")
    ap.add_argument("--sample", metavar="FILE", nargs="?",
                    const="scripts/SamplePoseData.txt", default=None,
                    help="replay this captured packet verbatim, every frame, "
                         "with only the sync id advancing. Defaults to "
                         "scripts/SamplePoseData.txt, a real Quest 3 capture "
                         "from the XrApi UDP Tester. Synthetic poses encode "
                         "whatever we BELIEVE the protocol says; this one "
                         "encodes what it does.")
    ap.add_argument("--hand-sweep", action="store_true",
                    help="sweep both hands in yaw and pitch. Use this, not "
                         "--hand-yaw, to test whether the camera follows "
                         "the gun: a fixed hand angle is absorbed into the "
                         "server's delta_angles and proves nothing.")
    ap.add_argument("--sync-fixed", type=int, default=None,
                    help="send this HMD_SYNC every frame instead of a ramp. "
                         "Used to prove the rendered red is the value sent, "
                         "byte for byte, with no gamma or rescale applied.")
    args = ap.parse_args()

    pressed = [int(x) for x in args.press.replace(" ", "").split(",") if x]
    if args.sticks:
        sticks = [float(x) for x in args.sticks.split(",")]
        if len(sticks) != 4:
            print("--sticks needs exactly four values: lx,ly,rx,ry")
            return 1
    else:
        sticks = None

    if pressed:
        print("holding bool indices %s" % pressed)
    if sticks:
        print("sticks L=(%.2f,%.2f) R=(%.2f,%.2f)" % tuple(sticks))

    sample = None
    if args.sample:
        with open(args.sample) as fh:
            for raw in fh:
                if raw.strip():
                    sample = raw.strip().split()
                    break
        if not sample:
            print("no packet found in %s" % args.sample)
            return 1
        print("replaying %s verbatim (%d tokens)" % (args.sample, len(sample)))

    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        rx.bind(("127.0.0.1", PORT_OUT))
    except OSError as exc:
        print("could not bind %d: %s" % (PORT_OUT, exc))
        return 1
    rx.setblocking(False)

    print("sending to 127.0.0.1:%d at %.0f Hz, listening on %d"
          % (PORT_IN, args.hz, PORT_OUT))
    print("quaternion scale %.3f (a client that normalises will be unaffected)"
          % args.quat_scale)

    period = 1.0 / args.hz
    start = time.time()
    frame = 0
    seen = 0
    last_report = start

    # HMD_SYNC steps by 12 in the real runtime, because XrFramesync collects
    # 256/12 + 1 distinct red values and calibrates to them.
    while time.time() - start < args.seconds:
        t = time.time() - start
        if args.sync_fixed is not None:
            sync = args.sync_fixed
        else:
            sync = (frame * 12) % 256

        if args.malformed and frame % 180 == 179:
            tx.sendto(b"client0 not a packet at all\n", ("127.0.0.1", PORT_IN))
        else:
            # With no --press, sweep one index per second. That is the
            # discovery mode: it proves the parser reads every position and
            # shows the game reacting to each in turn. With --press, hold
            # exactly what was asked, so one control can be tested alone.
            held = pressed if pressed else [(frame // 72) % BUTTONS]

            if sample:
                # Only the sync id moves. Everything else is exactly what the
                # headset sent, non-unit quaternions and all. Token 0 is
                # "client0" and tokens 1..29 are the 29 floats, so the sync
                # id -- float 28 -- is token 29.
                out = list(sample)
                out[29] = str(sync)
                pkt = " ".join(out)
            else:
                pkt = build_packet(t, sync, args.quat_scale,
                                   pressed=held, sticks=sticks,
                                   head_yaw=args.head_yaw,
                                   hand_yaw=args.hand_yaw,
                                   hand_sweep=args.hand_sweep)
            tx.sendto(pkt.encode("ascii"), ("127.0.0.1", PORT_IN))

        while True:
            try:
                data, _ = rx.recvfrom(1024)
            except BlockingIOError:
                break
            except OSError:
                break
            seen += 1
            if seen == 1 or time.time() - last_report >= 2.0:
                last_report = time.time()
                print("  <- %s" % data.decode("ascii", "replace").strip())

        frame += 1
        time.sleep(period)

    print("sent %d packets, received %d outbound packets" % (frame, seen))
    if seen == 0:
        print("NOTHING came back on %d -- the game is not sending, which means"
              " vr_enable is 0, the client never started, or it crashed."
              % PORT_OUT)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
