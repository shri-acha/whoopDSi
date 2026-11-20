#!/usr/bin/env python3
"""Generate icon.bmp: the 32x32, 16-colour banner icon embedded by ndstool.

Draws a top-down tiny whoop (four ducts, props, body and FPV camera).
Uses only the Python standard library. Run from the repo root:

    python3 tools/make_icon.py
"""
import math
import struct
import sys

W = H = 32

# 16-colour palette (R, G, B). Index 0 is the transparent/background colour.
PALETTE = [
    (24, 28, 48),     # 0 background (dark navy)
    (230, 230, 235),  # 1 duct white
    (150, 150, 160),  # 2 duct shadow
    (40, 40, 44),     # 3 body dark
    (255, 120, 20),   # 4 orange accent
    (120, 200, 255),  # 5 prop blur
    (20, 20, 20),     # 6 camera lens
    (255, 60, 60),    # 7 red (front LED)
    (60, 220, 90),    # 8 green (rear LED)
] + [(0, 0, 0)] * 7


def draw():
    px = [[0] * W for _ in range(H)]

    def disc(cx, cy, r, c):
        for y in range(H):
            for x in range(W):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                    px[y][x] = c

    def ring(cx, cy, r0, r1, c):
        for y in range(H):
            for x in range(W):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                if r0 <= d <= r1:
                    px[y][x] = c

    ducts = [(8.5, 8.5), (23.5, 8.5), (8.5, 23.5), (23.5, 23.5)]
    for cx, cy in ducts:
        disc(cx, cy, 7.5, 5)             # prop blur
        ring(cx, cy, 6.0, 7.6, 1)        # duct
        ring(cx + 0.6, cy + 0.6, 6.6, 7.6, 2)
        disc(cx, cy, 1.4, 3)             # motor hub

    # arms + body
    for y in range(H):
        for x in range(W):
            dx, dy = x + 0.5 - 16, y + 0.5 - 16
            if abs(abs(dx) - abs(dy)) < 1.3 and abs(dx) < 9:
                px[y][x] = 3
    for y in range(10, 22):
        for x in range(12, 20):
            px[y][x] = 3
    for y in range(11, 21):
        px[y][12] = 4
        px[y][19] = 4
    # FPV camera at the front (top of icon)
    for y in range(9, 13):
        for x in range(14, 18):
            px[y][x] = 4
    disc(16, 11, 1.3, 6)
    px[21][15] = px[21][16] = 8
    return px


def write_bmp(path, px):
    row_bytes = W // 2  # 4 bpp, already 4-byte aligned for W=32
    pixel_data = bytearray()
    for y in range(H - 1, -1, -1):  # BMP rows are bottom-up
        row = px[y]
        for x in range(0, W, 2):
            pixel_data.append((row[x] << 4) | row[x + 1])
    palette = bytearray()
    for r, g, b in PALETTE:
        palette += bytes((b, g, r, 0))
    offset = 14 + 40 + len(palette)
    size = offset + len(pixel_data)
    header = struct.pack("<2sIHHI", b"BM", size, 0, 0, offset)
    info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 4, 0, len(pixel_data),
                       2835, 2835, 16, 16)
    with open(path, "wb") as f:
        f.write(header + info + palette + pixel_data)
    assert row_bytes * H == len(pixel_data)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "icon.bmp"
    write_bmp(out, draw())
    print("wrote", out)
