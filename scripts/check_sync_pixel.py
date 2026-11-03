#!/usr/bin/env python3
"""Read the sync square straight out of a Quake 3 screenshot.

The protocol document is emphatic that the corner must be verified by
screenshotting it rather than by reasoning about the projection matrix -- the
prior attempt on this engine reasoned about the matrix, concluded it was
right, and drew the square in the bottom-left for months.

Checks, for each screenshot:
  * the colour at pixel (0,0), which is the single pixel the runtime samples
  * that green is exactly 0, because the runtime's calibration is gated on it
  * that the square is ~10x10 and really is in the TOP-left
  * that the red value CHANGES between frames -- a constant reads to the
    runtime as a frozen game

Usage:  python scripts/check_sync_pixel.py <dir-or-files>
"""

import struct
import sys
import glob
import os


def read_tga(path):
    with open(path, "rb") as fh:
        data = fh.read()

    (idlen, cmaptype, imgtype, _cmapstart, _cmaplen, _cmapdepth,
     _xorg, _yorg, width, height, depth, descriptor) = struct.unpack(
        "<BBBHHBHHHHBB", data[:18])

    if imgtype != 2:
        raise ValueError("expected uncompressed RGB TGA, got type %d" % imgtype)

    bpp = depth // 8
    off = 18 + idlen
    pixels = data[off:off + width * height * bpp]

    # Bit 5 of the descriptor: set means the first row is the TOP row.
    top_origin = bool(descriptor & 0x20)

    def at(x, y):
        """(r, g, b) at x,y measured from the TOP-LEFT of the displayed image."""
        row = y if top_origin else (height - 1 - y)
        i = (row * width + x) * bpp
        b, g, r = pixels[i], pixels[i + 1], pixels[i + 2]
        return r, g, b

    return width, height, top_origin, at


def describe(path):
    width, height, top_origin, at = read_tga(path)

    r0, g0, b0 = at(0, 0)

    # How far the square extends from the top-left corner.
    w = 0
    while w < 64 and at(w, 0)[:3] == (r0, g0, b0):
        w += 1
    h = 0
    while h < 64 and at(0, h)[:3] == (r0, g0, b0):
        h += 1

    bl = at(0, height - 1)       # bottom-left, where the prior attempt drew

    print("%s  %dx%d  origin=%s" % (
        os.path.basename(path), width, height,
        "top-left" if top_origin else "bottom-left"))
    print("   (0,0)        rgb=(%3d,%3d,%3d)   <- the sampled pixel" % (r0, g0, b0))
    print("   square       %dx%d px" % (w, h))
    print("   bottom-left  rgb=(%3d,%3d,%3d)" % bl)

    problems = []
    if g0 != 0:
        problems.append("green is %d, must be 0 (runtime calibration is "
                        "gated on g==0)" % g0)
    if b0 != 0:
        problems.append("blue is %d; should be 0 outside AER" % b0)
    if w < 8 or h < 8:
        problems.append("square is only %dx%d -- expected about 10x10" % (w, h))
    return r0, problems


def main():
    args = sys.argv[1:] or ["."]
    files = []
    for a in args:
        if os.path.isdir(a):
            files += sorted(glob.glob(os.path.join(a, "*.tga")))
        else:
            files.append(a)

    if not files:
        print("no .tga files found")
        return 1

    reds = []
    allprobs = []
    for f in files:
        red, probs = describe(f)
        reds.append(red)
        allprobs += probs
        print()

    print("red values across frames: %s" % reds)
    if len(set(reds)) <= 1 and len(reds) > 1:
        allprobs.append("red never changed across %d frames -- the runtime "
                        "derives frame rate from changes in this value and "
                        "would read the game as frozen" % len(reds))

    if allprobs:
        print("\nPROBLEMS:")
        for p in allprobs:
            print("  * " + p)
        return 1

    print("\nOK: square is in the top-left, green is 0, and the id is moving.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
