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


def build_packet(t, frame_id, quat_scale, pressed=None):
    """One inbound packet: client <28 floats> <frame id> <bools> <flags> <name>."""
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
            "q": [0.0, 0.0, 0.0, 1.0 * quat_scale],
            "stick": [0.0, 0.0],
            "pos": [sign * 0.13, -0.25, -0.35],
        })

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
        0.15 * math.sin(t * 0.70),
        1.60 + 0.05 * math.sin(t * 0.90),
        0.10 * math.sin(t * 0.40),
    ]

    f += hmd_q                                      # 18-21
    f += head_pos                                   # 22-24 head position
    f += [0.063]                                    # 25 IPD, metres
    f += [1.19, 1.19]                               # 26-27 FOV, tan half-angle
    f += [float(frame_id)]                          # 28 HMD_SYNC

    bools = ["F"] * BUTTONS
    if pressed is not None and 0 <= pressed < BUTTONS:
        bools[pressed] = "T"

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
    ap.add_argument("--sync-fixed", type=int, default=None,
                    help="send this HMD_SYNC every frame instead of a ramp. "
                         "Used to prove the rendered red is the value sent, "
                         "byte for byte, with no gamma or rescale applied.")
    args = ap.parse_args()

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
            pkt = build_packet(t, sync, args.quat_scale,
                               pressed=(frame // 72) % BUTTONS)
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
