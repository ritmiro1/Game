#!/usr/bin/env python3
"""Generate the original LoopDesk ribbon icon at multiple Windows sizes."""
import os
import struct
import zlib


def pixel(u, v):
    """Original LoopDesk mark: a luminous, continuous ribbon in a midnight tile."""
    import math
    rad = 0.24
    dx = max(rad-u, 0.0, u-(1.0-rad))
    dy = max(rad-v, 0.0, v-(1.0-rad))
    if dx*dx + dy*dy > rad*rad:
        return (0, 0, 0, 0)
    # Two intersecting, tilted orbits and a small point of light.
    x, y = u-0.5, v-0.5
    c, sn = 0.8660254, 0.5
    rx, ry = c*x + sn*y, -sn*x + c*y
    sx, sy = c*x - sn*y, sn*x + c*y
    ring1 = abs(math.sqrt((rx/0.34)**2 + (ry/0.20)**2)-1.0)
    ring2 = abs(math.sqrt((sx/0.34)**2 + (sy/0.20)**2)-1.0)
    if (u-0.73)**2 + (v-0.31)**2 < 0.033**2:
        return (229, 255, 251, 255)
    if ring1 < 0.075:
        return (161, 239, 229, 255)
    if ring2 < 0.075:
        return (142, 167, 246, 255)
    t = max(0.0,min(1.0,0.22 + v*0.56 + u*0.20))
    return (int(17+13*t), int(26+24*t), int(49+49*t), 255)


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
