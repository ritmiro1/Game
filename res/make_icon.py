#!/usr/bin/env python3
"""Generates res/app.ico (original artwork: blue rounded square with a white play triangle)."""
import os
import struct
import zlib


def pixel(u, v):
    """Colour (r, g, b, a) at normalised coordinates u, v in [0, 1]."""
    rad = 0.22
    dx = max(rad - u, 0.0, u - (1.0 - rad))
    dy = max(rad - v, 0.0, v - (1.0 - rad))
    if dx * dx + dy * dy > rad * rad:
        return (0, 0, 0, 0)
    # white play triangle
    ax, ay, bx, by, cx, cy = 0.37, 0.27, 0.37, 0.73, 0.77, 0.50
    d1 = (u - bx) * (ay - by) - (ax - bx) * (v - by)
    d2 = (u - cx) * (by - cy) - (bx - cx) * (v - cy)
    d3 = (u - ax) * (cy - ay) - (cx - ax) * (v - ay)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    if not (neg and pos):
        return (255, 255, 255, 255)
    t = v
    return (int(64 - 28 * t), int(150 - 44 * t), int(245 - 30 * t), 255)


def render(size, ss=4):
    rows = []
    for y in range(size):
        row = []
        for x in range(size):
            r = g = b = a = 0
            for sy in range(ss):
                for sx in range(ss):
                    pr, pg, pb, pa = pixel((x + (sx + 0.5) / ss) / size, (y + (sy + 0.5) / ss) / size)
                    r += pr * pa
                    g += pg * pa
                    b += pb * pa
                    a += pa
            if a:
                row.append((r // a, g // a, b // a, a // (ss * ss)))
            else:
                row.append((0, 0, 0, 0))
        rows.append(row)
    return rows


def as_bmp(rows):
    n = len(rows)
    out = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    for row in reversed(rows):
        out += b"".join(struct.pack("BBBB", b, g, r, a) for r, g, b, a in row)
    out += b"\x00" * (((n + 31) // 32) * 4 * n)
    return out


def as_png(rows):
    n = len(rows)
    raw = b"".join(b"\x00" + b"".join(struct.pack("BBBB", *p) for p in row) for row in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    images = []
    for size in (16, 24, 32, 48, 256):
        rows = render(size, 4 if size < 256 else 2)
        images.append((size, as_png(rows) if size == 256 else as_bmp(rows)))
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for size, data in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "app.ico")
    with open(path, "wb") as f:
        f.write(header + entries + b"".join(d for _, d in images))
    print("written", path)


if __name__ == "__main__":
    main()
