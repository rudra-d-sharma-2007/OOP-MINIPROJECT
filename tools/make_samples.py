#!/usr/bin/env python3
"""Generate the sample images used by `make sample`.

Only the Python standard library is used:
  - gradient.ppm        binary P6 PPM (readable by the tool)
  - gradient.png        PNG of the same picture
  - checker_ascii.ppm   plain-text P3 PPM (tests the ASCII fallback)
"""

import os
import struct
import sys
import zlib

W = H = 512


def make_pixels():
    """Gradient background with a few flat shapes so brightness changes are obvious."""
    pixels = bytearray(W * H * 3)
    for y in range(H):
        for x in range(W):
            r = int(x * 255 / (W - 1))
            g = int(y * 255 / (H - 1))
            b = 64

            # white / dark circles
            if (x - 140) ** 2 + (y - 140) ** 2 < 80 ** 2:
                r = g = b = 235
            if (x - 372) ** 2 + (y - 150) ** 2 < 70 ** 2:
                r = g = b = 25

            # colour bars across the bottom
            if y >= H - 110:
                band = (x * 4) // W
                r, g, b = [(220, 40, 40), (40, 200, 60), (50, 90, 230), (245, 210, 40)][band]

            i = (y * W + x) * 3
            pixels[i:i + 3] = bytes((r, g, b))
    return bytes(pixels)


def write_ppm(path, data):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        f.write(data)


def write_png(path, data):
    def chunk(tag, payload):
        block = tag + payload
        return struct.pack(">I", len(payload)) + block + struct.pack(
            ">I", zlib.crc32(block) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + data[y * W * 3:(y + 1) * W * 3] for y in range(H))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def write_ascii_ppm(path):
    """Small P3 file with comments, to exercise the plain-text PNM loader."""
    size = 8
    lines = ["P3", "# a plain-text PPM with comments", "%d %d" % (size, size), "255"]
    for y in range(size):
        row = []
        for x in range(size):
            v = 255 if (x + y) % 2 == 0 else 30
            row.extend([str(v), str(v // 3), str(255 - v)])
        lines.append(" ".join(row))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "samples"
    os.makedirs(out_dir, exist_ok=True)
    data = make_pixels()
    write_ppm(os.path.join(out_dir, "gradient.ppm"), data)
    write_png(os.path.join(out_dir, "gradient.png"), data)
    write_ascii_ppm(os.path.join(out_dir, "checker_ascii.ppm"))
    print("wrote gradient.ppm, gradient.png, checker_ascii.ppm in", out_dir)


if __name__ == "__main__":
    main()
