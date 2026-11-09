#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Процедурная генерация текстур PBR-набора «Тихого Залива» (PNG, без внешних библиотек)."""
import math, os, struct, zlib
import numpy as np

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "raw")
os.makedirs(OUT, exist_ok=True)


# ------------------------------------------------------------------ PNG-запись
def write_png(name, rgb, alpha=None):
    """rgb: float-массив HxWx3 [0..1]; alpha: HxW [0..1] или None."""
    H, W = rgb.shape[:2]
    img = np.clip(rgb * 255.0, 0, 255).astype(np.uint8)
    if alpha is not None:
        a = np.clip(alpha * 255.0, 0, 255).astype(np.uint8)[..., None]
        img = np.concatenate([img, a], axis=2)
        ctype = 6
    else:
        ctype = 2
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(H))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, ctype, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    path = os.path.join(OUT, name)
    with open(path, "wb") as f:
        f.write(png)
    print(f"  {name:22s} {W}x{H}  {len(png)/1024:7.1f} КБ")


# --------------------------------------------------------------- шум (tileable)
def periodic_grid(res, seed):
    r = np.random.default_rng(seed)
    return r.random((res, res))

def pnoise(size, res, seed):
    g = periodic_grid(res, seed)
    ys = np.linspace(0, res, size, endpoint=False)
    xs = np.linspace(0, res, size, endpoint=False)
    y0 = np.floor(ys).astype(int) % res
    x0 = np.floor(xs).astype(int) % res
    y1 = (y0 + 1) % res
    x1 = (x0 + 1) % res
    ty = ys - np.floor(ys)
    tx = xs - np.floor(xs)
    ty = (ty * ty * (3 - 2 * ty))[:, None]
    tx = (tx * tx * (3 - 2 * tx))[None, :]
    a = g[np.ix_(y0, x0)]
    b = g[np.ix_(y0, x1)]
    c = g[np.ix_(y1, x0)]
    d = g[np.ix_(y1, x1)]
    top = a + (b - a) * tx
    bot = c + (d - c) * tx
    return top + (bot - top) * ty

def fbm(size, res=4, octaves=5, seed=0, gain=0.5):
    acc = np.zeros((size, size))
    amp = 1.0
    tot = 0.0
    r = res
    for o in range(octaves):
        acc += pnoise(size, max(2, r), seed + o * 101) * amp
        tot += amp
        amp *= gain
        r *= 2
    return acc / tot

def stretch(field, kx, kz):
    return np.roll(field, 0, axis=0) * kx + field * kz  # заглушка (не используется)

def normal_from_height(h, strength=2.0):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * strength
    nz = np.ones_like(h)
    ln = np.sqrt(dx * dx + dy * dy + nz * nz)
    n = np.stack([-dx / ln, -dy / ln, nz / ln], axis=2)
    return n * 0.5 + 0.5

def lerp3(a, b, t):
    t = t[..., None]
    return np.array(a)[None, None, :] * (1 - t) + np.array(b)[None, None, :] * t

def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0 + 1e-9), 0, 1)
    return t * t * (3 - 2 * t)


# ------------------------------------------------------------------- текстуры
def tex_sand(S=512):
    grain = fbm(S, 8, 6, 1)
    fine = fbm(S, 64, 3, 2)
    ripple = 0.5 + 0.5 * np.sin(np.linspace(0, 2 * np.pi * 28, S)[None, :] + grain * 6.0)
    h = grain * 0.6 + fine * 0.3 + ripple * 0.1
    base = lerp3((0.72, 0.66, 0.50), (0.88, 0.83, 0.68), h)
    specks = (fbm(S, 128, 2, 9) > 0.62).astype(float)
    shell = (fbm(S, 96, 2, 12) > 0.79).astype(float)
    base = base * (1 - 0.20 * specks[..., None])
    base = base + np.array([0.10, 0.09, 0.04])[None, None, :] * shell[..., None]
    write_png("tex_sand.png", np.clip(base, 0, 1))
    write_png("tex_sand_n.png", normal_from_height(h, 3.0))


def tex_rock(S=512):
    big = fbm(S, 4, 6, 21)
    fine = fbm(S, 32, 5, 22)
    crack = 1.0 - np.abs(fbm(S, 6, 4, 23) - 0.5) * 2.0
    crack = smoothstep(0.55, 1.0, crack)
    h = big * 0.5 + fine * 0.5 - crack * 0.35
    base = lerp3((0.34, 0.35, 0.34), (0.62, 0.61, 0.57), h)
    lichen = smoothstep(0.62, 0.82, fbm(S, 10, 4, 26)) * 0.6
    base = base * (1 - lichen[..., None]) + np.array([0.38, 0.46, 0.28])[None, None, :] * lichen[..., None]
    base = base * (1 - 0.35 * crack[..., None] * 0.5)
    write_png("tex_rock.png", np.clip(base, 0, 1))
    write_png("tex_rock_n.png", normal_from_height(h, 4.5))


def tex_grass(S=512):
    clump = fbm(S, 6, 5, 31)
    blades = fbm(S, 96, 3, 32)
    h = clump * 0.7 + blades * 0.3
    base = lerp3((0.20, 0.34, 0.14), (0.42, 0.55, 0.22), h)
    dry = smoothstep(0.70, 0.92, fbm(S, 8, 3, 35))
    base = base * (1 - dry[..., None] * 0.6) + np.array([0.55, 0.50, 0.24])[None, None, :] * dry[..., None] * 0.6
    write_png("tex_grass.png", np.clip(base, 0, 1))
    write_png("tex_grass_n.png", normal_from_height(h, 2.2))


def tex_bark(S=256):
    v = fbm(S, 8, 5, 41)
    ridges = np.zeros((S, S))
    for k in range(1, 7):
        ridges += np.sin(np.linspace(0, 2 * np.pi * k, S)[None, :] * 3.0 + v * 12.0) / k
    h = 0.5 * ridges * 0.5 + 0.5 * v
    base = lerp3((0.16, 0.11, 0.07), (0.42, 0.32, 0.21), smoothstep(-0.4, 0.9, h))
    moss = smoothstep(0.75, 0.95, fbm(S, 6, 3, 44)) * 0.5
    base = base * (1 - moss[..., None]) + np.array([0.25, 0.32, 0.16])[None, None, :] * moss[..., None]
    write_png("tex_bark.png", np.clip(base, 0, 1))
    write_png("tex_bark_n.png", normal_from_height(h, 3.4))


def tex_wood(S=512):
    grain = fbm(S, 4, 5, 51)
    lines = np.sin(np.arange(S)[:, None] * 0.35 + grain * 9.0) * 0.5 + 0.5
    plank_h = S // 8
    planks = (np.arange(S)[:, None] // plank_h).astype(float)
    gap = (np.arange(S)[:, None] % plank_h)
    seam = ((gap < 2) | (gap > plank_h - 3)).astype(float)
    tone = 0.85 + 0.15 * np.sin(planks * 2.7)
    h = lines * 0.6 + grain * 0.4
    base = lerp3((0.34, 0.23, 0.14), (0.62, 0.46, 0.29), h) * tone[..., None]
    base = base * (1 - 0.65 * seam[..., None])
    nail = ((gap > plank_h * 0.45) & (gap < plank_h * 0.5) &
            ((np.arange(S)[None, :] % 128) < 3)).astype(float)
    base = base * (1 - 0.7 * nail[..., None])
    write_png("tex_wood.png", np.clip(base, 0, 1))
    write_png("tex_wood_n.png", normal_from_height(h - seam * 0.6, 2.0))


def tex_metal(S=256):
    base_f = fbm(S, 6, 5, 61)
    h = base_f
    base = lerp3((0.52, 0.55, 0.56), (0.72, 0.74, 0.73), h)
    rust = smoothstep(0.62, 0.88, fbm(S, 5, 4, 63))
    base = base * (1 - rust[..., None] * 0.8) + np.array([0.55, 0.28, 0.14])[None, None, :] * rust[..., None] * 0.8
    streak = smoothstep(0.7, 1.0, fbm(S, 3, 3, 65))
    base = base * (1 - streak[..., None] * 0.35)
    write_png("tex_metal.png", np.clip(base, 0, 1))
    write_png("tex_metal_n.png", normal_from_height(h, 1.2))


def tex_water_normal(S=512):
    a = fbm(S, 6, 5, 71)
    b = fbm(S, 12, 4, 72)
    h = a * 0.6 + b * 0.4
    write_png("tex_water_n.png", normal_from_height(h, 8.0))


def tex_caustics(S=512):
    x = np.linspace(0, 2 * np.pi, S, endpoint=False)
    xx, yy = np.meshgrid(x, x)
    warp = fbm(S, 5, 4, 81) * 4.0
    f = (np.sin(xx * 3 + warp) * np.sin(yy * 2.7 - warp) +
         np.sin((xx + yy) * 2.1 + warp * 0.6) * 0.7)
    c = smoothstep(0.35, 1.35, f)
    c = c ** 1.6
    rgb = np.stack([c * 0.95, c * 0.99, c * 1.0], axis=2)
    write_png("tex_caustics.png", np.clip(rgb, 0, 1))


def tex_foam(S=256):
    f = fbm(S, 8, 5, 91)
    a = smoothstep(0.42, 0.78, f)
    bubbles = np.zeros((S, S))
    r = np.random.default_rng(92)
    for _ in range(160):
        cx, cy = r.integers(0, S, 2)
        rad = r.integers(2, 7)
        yy, xx = np.ogrid[-rad:rad + 1, -rad:rad + 1]
        m = (xx * xx + yy * yy) <= rad * rad
        ys = (np.arange(-rad, rad + 1) + cy) % S
        xs = (np.arange(-rad, rad + 1) + cx) % S
        sub = bubbles[np.ix_(ys, xs)]
        bubbles[np.ix_(ys, xs)] = np.maximum(sub, m.astype(float) * 0.8)
    a = np.clip(a * 0.85 + bubbles * 0.5, 0, 1)
    rgb = np.ones((S, S, 3)) * 0.97
    write_png("tex_foam.png", rgb, a)


def tex_rain(S=128):
    a = np.zeros((S, S))
    r = np.random.default_rng(101)
    for _ in range(90):
        x = r.integers(0, S)
        y0 = r.integers(0, S)
        ln = r.integers(S // 3, S)
        thick = r.integers(1, 3)
        for k in range(ln):
            yy = (y0 + k) % S
            a[yy, x] = max(a[yy, x], 0.9)
            for t in range(1, thick):
                a[yy, (x + t) % S] = max(a[yy, (x + t) % S], 0.5)
    a *= np.linspace(0.2, 1.0, S)[:, None]
    rgb = np.ones((S, S, 3))
    write_png("tex_rain.png", rgb, np.clip(a, 0, 1))


def tex_glow(S=128):
    y, x = np.mgrid[0:S, 0:S]
    cx = cy = (S - 1) / 2
    d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / (S * 0.5)
    a = np.clip(1.0 - d, 0, 1) ** 2.2
    rgb = np.ones((S, S, 3))
    write_png("tex_glow.png", rgb, a)


def tex_scales(S=256):
    x = np.linspace(0, 1, S, endpoint=False)
    xx, yy = np.meshgrid(x, x)
    rows = 18
    v = np.zeros((S, S))
    for r in range(rows):
        y0 = r / rows
        offset = 0.5 if r % 2 else 0.0
        u = ((xx * rows * 0.85 + offset) % 1.0)
        w = ((yy - y0) * rows) % 1.0
        arc = np.sqrt((u - 0.5) ** 2 + (w * 0.9) ** 2 * 0.6)
        v = np.maximum(v, 1.0 - arc * 1.6)
    grain = fbm(S, 32, 3, 111)
    h = v * 0.7 + grain * 0.3
    base = lerp3((0.62, 0.66, 0.68), (0.92, 0.94, 0.92), h)
    write_png("tex_scales.png", np.clip(base, 0, 1))
    write_png("tex_scales_n.png", normal_from_height(h, 2.6))


def tex_leaf(S=256):
    # Крона: пятнистая листва (используется как цветовая карта)
    f = fbm(S, 10, 5, 121)
    h = smoothstep(0.32, 0.75, f)
    base = lerp3((0.12, 0.26, 0.10), (0.34, 0.50, 0.18), h)
    spots = smoothstep(0.78, 0.95, fbm(S, 40, 3, 123))
    base = base + np.array([0.16, 0.20, 0.05])[None, None, :] * spots[..., None]
    write_png("tex_leaf.png", np.clip(base, 0, 1))


def tex_cloth(S=256):
    f = fbm(S, 8, 4, 131)
    weave = (np.sin(np.arange(S)[:, None] * 1.7) * 0.5 + 0.5) * 0.12
    h = f * 0.8 + weave
    base = lerp3((0.26, 0.32, 0.40), (0.55, 0.60, 0.66), h)
    write_png("tex_cloth.png", np.clip(base, 0, 1))
    write_png("tex_cloth_n.png", normal_from_height(h, 1.5))


def tex_detail(S=512):
    """Нейтральный серый детальный шум для рельефа (умножается на вершинные цвета)."""
    a = fbm(S, 16, 6, 141)
    b = fbm(S, 64, 4, 142)
    h = a * 0.6 + b * 0.4
    g = 0.45 + h * 0.55
    pebble = smoothstep(0.72, 0.90, fbm(S, 48, 3, 143)) * 0.25
    g = np.clip(g - pebble, 0, 1)
    rgb = np.stack([g, g, g * 0.98], axis=2)
    write_png("tex_detail.png", rgb)


if __name__ == "__main__":
    print("Генерация текстур...")
    tex_sand()
    tex_rock()
    tex_grass()
    tex_bark()
    tex_wood()
    tex_metal()
    tex_water_normal()
    tex_caustics()
    tex_foam()
    tex_rain()
    tex_glow()
    tex_scales()
    tex_leaf()
    tex_cloth()
    tex_detail()
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT) if f.startswith("tex_"))
    print(f"Готово: {total/1048576:.1f} МБ текстур")
