#!/usr/bin/env python3
"""Generates app.ico (multi-resolution, PNG-compressed entries) without PIL.

The mark is a rounded square with a play triangle and a horizon line: the visual
identity of VidDesk, unrelated to any existing wallpaper product artwork.
"""
import struct
import zlib
import os

SIZES = [16, 24, 32, 48, 64, 128, 256]

ACCENT = (86, 196, 255)
DARK = (18, 20, 24)
PANEL = (26, 29, 35)


def blend(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def render(size):
    """Returns RGBA bytes for one square icon."""
    px = bytearray(size * size * 4)
    cx = cy = size / 2.0
    radius = size * 0.30           # outer rounded square half-size
    corner = size * 0.24           # corner radius
    # play triangle geometry
    tri = [
        (cx - size * 0.11, cy - size * 0.20),
        (cx + size * 0.21, cy),
        (cx - size * 0.11, cy + size * 0.20),
    ]

    def inside_round_rect(x, y, half, r):
        dx = abs(x - cx) - (half - r)
        dy = abs(y - cy) - (half - r)
        if dx <= 0 and dy <= 0:
            return True
        dx = max(dx, 0.0)
        dy = max(dy, 0.0)
        return dx * dx + dy * dy <= r * r

    def inside_tri(x, y):
        (x1, y1), (x2, y2), (x3, y3) = tri
        d1 = (x - x2) * (y1 - y2) - (x1 - x2) * (y - y2)
        d2 = (x - x3) * (y2 - y3) - (x2 - x3) * (y - y3)
        d3 = (x - x1) * (y3 - y1) - (x3 - x1) * (y - y1)
        neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
        pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
        return not (neg and pos)

    for y in range(size):
        for x in range(size):
            sx = x + 0.5
            sy = y + 0.5
            # background gradient inside the rounded square
            if inside_round_rect(sx, sy, radius, corner):
                t = (sy / size)
                base = blend(blend(PANEL, DARK, 0.15), DARK, t)
                if inside_tri(sx, sy):
                    color = ACCENT
                else:
                    # subtle diagonal sheen
                    sheen = max(0.0, 1.0 - abs((sx - sy) / size) * 2.2) * 0.10
                    color = blend(base, ACCENT, sheen)
                alpha = 255
                # soft edge
                edge = min(sx, size - sx, sy, size - sy)
                if edge < 1.0:
                    alpha = int(255 * max(0.0, edge))
            else:
                color = (0, 0, 0)
                alpha = 0
            o = (y * size + x) * 4
            px[o] = color[0]
            px[o + 1] = color[1]
            px[o + 2] = color[2]
            px[o + 3] = alpha
    return bytes(px)


def png_bytes(size):
    raw = render(size)
    # filter type 0 for each row
    stride = size * 4
    filtered = bytearray()
    for y in range(size):
        filtered.append(0)
        filtered.extend(raw[y * stride:(y + 1) * stride])

    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)

    ihdr = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', ihdr)
            + chunk(b'IDAT', zlib.compress(bytes(filtered), 9))
            + chunk(b'IEND', b''))


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'app', 'VidDesk', 'app.ico')
    images = [(s, png_bytes(s)) for s in SIZES]
    header = struct.pack('<HHH', 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b''
    body = b''
    for size, data in images:
        dim = 0 if size >= 256 else size
        entries += struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        body += data
    with open(out, 'wb') as f:
        f.write(header + entries + body)
    print('wrote', os.path.normpath(out), os.path.getsize(out), 'bytes')


if __name__ == '__main__':
    main()
