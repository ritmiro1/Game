#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Процедурная генерация всех звуковых банков «Тихого Залива» (WAV 22 кГц)."""
import math, os, wave, struct
import numpy as np

SR = 22050
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "raw")
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(20251109)

def t_axis(dur):
    return np.arange(int(SR * dur)) / SR

def noise(dur, seed=0):
    r = np.random.default_rng(seed)
    return r.standard_normal(int(SR * dur)).astype(np.float64) * 0.35

def env_adsr(n, a=0.01, d=0.1, s=0.7, r=0.2):
    a_n = max(1, int(a * SR)); d_n = max(1, int(d * SR)); r_n = max(1, int(r * SR))
    s_n = max(0, n - a_n - d_n - r_n)
    e = np.concatenate([
        np.linspace(0, 1, a_n),
        np.linspace(1, s, d_n),
        np.full(s_n, s),
        np.linspace(s, 0, r_n) if r_n else np.array([]),
    ])
    if len(e) < n:
        e = np.pad(e, (0, n - len(e)))
    return e[:n]

def seg_noise(n, seed):
    r = np.random.default_rng(seed)
    return r.standard_normal(int(n)).astype(np.float64) * 0.35

def lp(x, cutoff, resonance=0.0):
    """Однополюсный ФНЧ, стабильный и быстрый."""
    a = math.exp(-2 * math.pi * cutoff / SR)
    y = np.zeros_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc = (1 - a) * x[i] + a * acc
        y[i] = acc
    return y

def lp_fast(x, cutoff):
    """ФНЧ через БИХ-фильтр (быстрее и мягче)."""
    a = math.exp(-2 * math.pi * cutoff / SR)
    b = 1 - a
    y = np.empty_like(x)
    z = 0.0
    for i in range(len(x)):
        z = b * x[i] + a * z
        y[i] = z
    return y

def hp(x, cutoff):
    return x - lp_fast(x, cutoff)

def reverb(x, mix=0.22, delays=(0.031, 0.047, 0.071, 0.101), decay=0.55):
    y = np.copy(x)
    for d in delays:
        n = int(d * SR)
        if n < len(x):
            tail = np.zeros_like(x)
            tail[n:] = x[:-n] * decay
            tail = lp_fast(tail, 4200)
            y += tail * mix
    return y

def norm(x, peak=0.92):
    m = np.max(np.abs(x)) + 1e-9
    return x / m * peak

def write(name, data, stereo=False, peak=0.92):
    data = norm(np.asarray(data, dtype=np.float64), peak)
    if stereo and data.ndim == 1:
        data = np.stack([data, np.roll(data, 197)], axis=1)
    data = np.clip(data, -1.0, 1.0)
    pcm = (data * 32767.0).astype(np.int16)
    path = os.path.join(OUT, name)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(2 if pcm.ndim > 1 else 1)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(pcm.tobytes())
    print(f"  {name:26s} {len(pcm)/SR:6.2f} c  {os.path.getsize(path)/1024:7.1f} КБ")

# ------------------------------------------------------------------ АМБИЕНТ
def make_waves(dur=12.0):
    t = t_axis(dur)
    n = len(t)
    base = lp_fast(noise(dur, 11), 900)
    swell = 0.55 + 0.45 * np.sin(2 * np.pi * t / 6.0 - 1.2)
    swell2 = 0.35 + 0.25 * np.sin(2 * np.pi * t / 3.1 + 0.7)
    surf = hp(noise(dur, 12), 1800) * swell * 0.5
    body = base * swell2 * 1.4
    # отдельные всплески
    out = body + surf
    for _ in range(7):
        pos = rng.integers(0, n - SR)
        ln = int(SR * rng.uniform(0.3, 1.0))
        seg = (seg_noise(ln, int(rng.integers(0, 9999))) * env_adsr(ln, 0.02, 0.15, 0.25, 0.5))[:ln]
        out[pos:pos + ln] += seg * 0.5
    return out * 0.5

def make_wind(dur=10.0):
    t = t_axis(dur)
    w = lp_fast(noise(dur, 21), 420)
    wob = 0.6 + 0.4 * np.sin(2 * np.pi * t / 4.3) * np.sin(2 * np.pi * t / 9.7)
    whistle = lp_fast(hp(noise(dur, 22), 700), 2600) * 0.25 * (0.5 + 0.5 * np.sin(2 * np.pi * t / 7.1))
    return (w * 1.2 + whistle) * wob * 0.7

def make_rain(dur=10.0):
    n = int(SR * dur)
    out = lp_fast(hp(noise(dur, 31), 900), 6500) * 0.55
    drops = np.zeros(n)
    for _ in range(320):
        p = int(rng.integers(0, n - 600))
        ln = int(rng.uniform(120, 900))
        seg = seg_noise(ln, int(rng.integers(0, 99999)))
        drops[p:p + ln] += (seg * env_adsr(ln, 0.001, 0.02, 0.1, 0.1) * rng.uniform(0.2, 0.8))[:len(drops[p:p+ln])]
    return out + drops * 0.5

def make_night(dur=12.0):
    t = t_axis(dur)
    n = len(t)
    out = lp_fast(noise(dur, 41), 500) * 0.5
    # сверчки: короткие трели
    for base in (0.0, 1.7, 3.4, 5.2, 7.0, 9.1):
        for k in range(4):
            start = int((base + k * 0.055) * SR)
            ln = int(0.035 * SR)
            if start + ln >= n: continue
            seg = np.sin(2 * np.pi * 4300 * np.arange(ln) / SR) * env_adsr(ln, 0.002, 0.01, 0.3, 0.01)
            out[start:start + ln] += seg * 0.25
    # далёкая сова
    for start in (2.2, 6.9):
        ln = int(0.55 * SR)
        i0 = int(start * SR)
        if i0 + ln < n:
            tt = np.arange(ln) / SR
            seg = np.sin(2 * np.pi * (300 - 60 * tt) * tt) * env_adsr(ln, 0.06, 0.2, 0.35, 0.3)
            out[i0:i0 + ln] += seg * 0.12
    return out

def make_fire(dur=8.0):
    n = int(SR * dur)
    out = lp_fast(noise(dur, 51), 700) * 0.45
    for _ in range(160):
        p = int(rng.integers(0, n - 2000))
        ln = int(rng.uniform(200, 1600))
        out[p:p + ln] += (seg_noise(ln, int(rng.integers(0, 9999))) * env_adsr(ln, 0.001, 0.03, 0.15, 0.2) * rng.uniform(0.2, 0.9) * 0.35)[:ln]
    return out

# -------------------------------------------------------------------- МУЗЫКА
def pluck(freq, dur, damp=0.494, seed=0):
    """Карплус-Стронг: щипковый тембр (гитара/гусли)."""
    n = int(SR * dur)
    delay = max(2, int(SR / freq))
    r = np.random.default_rng(seed)
    buf = r.uniform(-1, 1, delay)
    out = np.zeros(n)
    idx = 0
    for i in range(n):
        v = buf[idx]
        out[i] = v
        nxt = damp * 0.5 * (v + buf[(idx + 1) % delay])
        buf[idx] = nxt
        idx = (idx + 1) % delay
    return out * env_adsr(n, 0.002, 0.05, 0.6, 0.3)

def pad(freqs, dur, detune=0.004, cutoff=1600):
    t = t_axis(dur)
    out = np.zeros_like(t)
    for f in freqs:
        for k in (-1, 0, 1):
            ff = f * (1 + detune * k)
            out += np.sin(2 * np.pi * ff * t + rng.uniform(0, 6.28)) * 0.22
            out += np.sin(2 * np.pi * ff * 2 * t) * 0.05
    lfo = 0.75 + 0.25 * np.sin(2 * np.pi * t / 7.3)
    out = lp_fast(out, cutoff) * lfo
    return out * env_adsr(len(t), 1.5, 0.5, 0.85, 2.0)

def music_calm(dur=34.0):
    # Ре-минорная пентатоника, спокойный «рыбацкий» наигрыш
    scale = [146.83, 174.61, 196.00, 220.00, 261.63, 293.66, 349.23, 392.00]
    t = t_axis(dur)
    out = pad([146.83, 220.00, 293.66], dur, cutoff=1200) * 0.55
    out += pad([110.00, 174.61, 261.63], dur, cutoff=900) * 0.35
    # арпеджио
    seq = [0, 2, 4, 2, 5, 4, 2, 0, 3, 5, 7, 5, 4, 2, 0, 2]
    step = 0.75
    pos = 0.0
    i = 0
    while pos < dur - 1.2:
        note = scale[seq[i % len(seq)]]
        p = pluck(note * 2, 1.6, 0.4955, seed=i)
        s = int(pos * SR)
        e = min(len(out), s + len(p))
        out[s:e] += p[:e - s] * 0.20
        pos += step
        i += 1
    return reverb(out, 0.30)

def music_night(dur=34.0):
    t = t_axis(dur)
    out = pad([98.0, 146.83, 196.0], dur, cutoff=700) * 0.6
    scale = [196.0, 220.0, 246.94, 293.66, 329.63]
    pos = 1.0
    i = 0
    while pos < dur - 2.0:
        note = scale[(i * 3) % len(scale)]
        p = pluck(note, 2.4, 0.4975, seed=i + 40)
        s = int(pos * SR)
        e = min(len(out), s + len(p))
        out[s:e] += p[:e - s] * 0.16
        pos += 1.6 + (i % 3) * 0.35
        i += 1
    return reverb(out, 0.34)

def music_tension(dur=26.0):
    t = t_axis(dur)
    out = np.zeros_like(t)
    for f in (73.42, 110.0, 146.83):
        out += np.sin(2 * np.pi * f * t) * 0.2
    pulse = (np.sin(2 * np.pi * t / 1.35) > 0.4).astype(float)
    pulse = lp_fast(pulse, 180)
    out += lp_fast(noise(dur, 61), 300) * 0.35 * (0.4 + pulse)
    arp = [293.66, 349.23, 440.0, 349.23]
    pos = 0.0
    i = 0
    while pos < dur - 1.0:
        p = pluck(arp[i % 4], 1.1, 0.493, seed=i + 90)
        s = int(pos * SR)
        e = min(len(out), s + len(p))
        out[s:e] += p[:e - s] * 0.16 * (0.6 + 0.4 * float(pulse[s]))
        pos += 0.42
        i += 1
    return reverb(out, 0.28)

def music_win(dur=4.0):
    notes = [293.66, 349.23, 440.0, 587.33]
    out = np.zeros(int(SR * dur))
    for i, f in enumerate(notes):
        p = pluck(f, 1.6, 0.4965, seed=i + 7)
        s = int(i * 0.16 * SR)
        e = min(len(out), s + len(p))
        out[s:e] += p[:e - s] * 0.4
    out += pad([146.83, 220.0], dur, cutoff=1400) * 0.18
    return reverb(out, 0.3)

# ---------------------------------------------------------------------- SFX
def sfx_splash(strength=1.0, dur=None):
    dur = dur or (0.35 + 0.5 * strength)
    n = int(SR * dur)
    t = np.arange(n) / SR
    out = hp(noise(dur, int(100 + strength * 50)), 700) * np.exp(-t * (7.0 / strength))
    body = lp_fast(noise(dur, 77), 400) * np.exp(-t * 9) * strength
    out = out * 0.9 + body
    return out

def sfx_cast():
    dur = 0.42
    n = int(SR * dur)
    t = np.arange(n) / SR
    nz = hp(noise(dur, 3), 1500)
    sweep = np.sin(2 * np.pi * (600 + 2200 * t / dur) * t) * 0.15
    e = np.exp(-t * 6) * np.minimum(1.0, t * 60)
    return (nz * 0.7 + sweep) * e

def sfx_reel():
    dur = 1.2
    out = np.zeros(int(SR * dur))
    for i in range(26):
        s = int((i * 0.045 + rng.uniform(0, 0.01)) * SR)
        ln = int(0.012 * SR)
        if s + ln >= len(out): break
        seg = (hp(seg_noise(ln, i), 2500) * env_adsr(ln, 0.0005, 0.002, 0.1, 0.05))[:ln]
        out[s:s + ln] += seg * rng.uniform(0.4, 0.9)
    return out

def sfx_plop():
    dur = 0.5
    n = int(SR * dur)
    t = np.arange(n) / SR
    f = 220 * np.exp(-t * 9) + 90
    tone = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 8)
    nz = lp_fast(noise(dur, 5), 1200) * np.exp(-t * 14) * 0.5
    return tone * 0.7 + nz

def sfx_ui(freq=880, dur=0.09, kind="click"):
    n = int(SR * dur)
    t = np.arange(n) / SR
    if kind == "click":
        return np.sin(2 * np.pi * freq * t) * np.exp(-t * 45) * 0.6
    return (np.sin(2 * np.pi * freq * t) + 0.3 * np.sin(2 * np.pi * freq * 2 * t)) * env_adsr(n, 0.005, 0.05, 0.4, 0.1) * 0.35

def sfx_achieve():
    notes = [523.25, 659.25, 783.99, 1046.50]
    out = np.zeros(int(SR * 1.8))
    for i, f in enumerate(notes):
        n = int(SR * 1.2)
        t = np.arange(n) / SR
        tone = (np.sin(2 * np.pi * f * t) + 0.4 * np.sin(2 * np.pi * f * 2 * t)) * np.exp(-t * 3.2)
        s = int(i * 0.11 * SR)
        out[s:s + n] += tone[:len(out) - s] * 0.3
    return reverb(out, 0.35)

def sfx_coin():
    out = np.zeros(int(SR * 0.5))
    for i, f in enumerate((1318.5, 1760.0)):
        n = int(SR * 0.18)
        t = np.arange(n) / SR
        tone = np.sin(2 * np.pi * f * t) * np.exp(-t * 22)
        s = int(i * 0.08 * SR)
        out[s:s + n] += tone * 0.5
    return out

def sfx_quest():
    notes = [392.0, 523.25, 659.25, 783.99, 1046.5]
    out = np.zeros(int(SR * 2.4))
    for i, f in enumerate(notes):
        p = pluck(f, 1.8, 0.4965, seed=i + 200)
        s = int(i * 0.13 * SR)
        out[s:s + len(p)] += p[:len(out) - s] * 0.35
    return reverb(out, 0.4)

def sfx_footstep(kind):
    dur = 0.22
    n = int(SR * dur)
    t = np.arange(n) / SR
    if kind == "sand":
        out = lp_fast(noise(dur, 9), 1800) * np.exp(-t * 26)
    elif kind == "wood":
        out = lp_fast(noise(dur, 10), 900) * np.exp(-t * 30)
        out += np.sin(2 * np.pi * 190 * t) * np.exp(-t * 40) * 0.3
    else:
        out = hp(noise(dur, 11), 1200) * np.exp(-t * 34) * 0.8
    return out * 0.7

def sfx_engine(dur=2.0):
    t = t_axis(dur)
    base = 42 + 3 * np.sin(2 * np.pi * t / 1.1)
    phase = 2 * np.pi * np.cumsum(base) / SR
    out = np.sin(phase) * 0.5 + np.sin(phase * 2) * 0.22 + np.sin(phase * 3) * 0.1
    out += lp_fast(noise(dur, 13), 260) * 0.5
    return out * 0.8

def sfx_thunder(dur=3.2):
    n = int(SR * dur)
    t = np.arange(n) / SR
    out = lp_fast(noise(dur, 15), 380) * np.exp(-t * 1.1)
    out += lp_fast(noise(dur, 16), 120) * np.exp(-t * 0.6) * 0.8
    crack = hp(noise(dur, 17), 2500) * np.exp(-t * 12)
    return out * 0.9 + crack * 0.3

def sfx_gull(dur=1.1):
    n = int(SR * dur)
    t = np.arange(n) / SR
    f = 1500 - 700 * t / dur + 120 * np.sin(2 * np.pi * 18 * t)
    out = np.sin(2 * np.pi * np.cumsum(f) / SR) * env_adsr(n, 0.02, 0.2, 0.5, 0.4)
    out += hp(noise(dur, 18), 3000) * env_adsr(n, 0.02, 0.2, 0.3, 0.4) * 0.3
    return out * 0.5

def sfx_bell(dur=3.0):
    n = int(SR * dur)
    t = np.arange(n) / SR
    out = np.zeros(n)
    for i, (f, a) in enumerate(((196.0, 1.0), (392.0, 0.5), (587.0, 0.3), (784.0, 0.2), (1176.0, 0.1))):
        out += np.sin(2 * np.pi * f * t + i) * np.exp(-t * (1.2 + i * 0.5)) * a
    return out * 0.5

def sfx_line_break():
    dur = 0.4
    n = int(SR * dur)
    t = np.arange(n) / SR
    return hp(noise(dur, 19), 2200) * np.exp(-t * 30) + np.sin(2 * np.pi * 900 * t) * np.exp(-t * 40) * 0.4

def sfx_flop(dur=0.5):
    out = np.zeros(int(SR * dur))
    for i in range(3):
        s = int(i * 0.12 * SR)
        ln = int(0.10 * SR)
        tt = np.arange(ln) / SR
        seg = (np.sin(2 * np.pi * 160 * tt) * np.exp(-tt * 25) +
               lp_fast(seg_noise(ln, i + 30), 1500) * np.exp(-tt * 18) * 0.7) * 0.5
        out[s:s + ln] += seg[:ln]
    return out

def sfx_dog(dur=0.7):
    n = int(SR * dur)
    t = np.arange(n) / SR
    f = 420 - 120 * t / dur
    tone = np.sin(2 * np.pi * np.cumsum(f) / SR) * env_adsr(n, 0.01, 0.1, 0.4, 0.3)
    return tone * 0.4 + hp(noise(dur, 20), 800) * env_adsr(n, 0.01, 0.1, 0.2, 0.3) * 0.3

def sfx_seal(dur=0.9):
    n = int(SR * dur)
    t = np.arange(n) / SR
    f = 220 + 90 * np.sin(2 * np.pi * 3.2 * t)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * env_adsr(n, 0.08, 0.2, 0.5, 0.4) * 0.45

def sfx_loop_rain_on_wood(dur=6.0):
    n = int(SR * dur)
    out = lp_fast(hp(noise(dur, 23), 1200), 6000) * 0.35
    for _ in range(180):
        p = int(rng.integers(0, n - 900))
        ln = int(rng.uniform(150, 800))
        out[p:p + ln] += (seg_noise(ln, int(rng.integers(0, 9999))) * env_adsr(ln, 0.001, 0.02, 0.08, 0.08) * 0.4)[:ln]
    return out

def main():
    print("Генерация звуковых банков...")
    write("amb_waves.wav", make_waves())
    write("amb_wind.wav", make_wind())
    write("amb_rain.wav", make_rain())
    write("amb_night.wav", make_night())
    write("amb_fire.wav", make_fire())
    write("amb_rain_wood.wav", sfx_loop_rain_on_wood())
    write("mus_calm.wav", music_calm(), stereo=True, peak=0.85)
    write("mus_night.wav", music_night(), stereo=True, peak=0.85)
    write("mus_tension.wav", music_tension(), stereo=True, peak=0.85)
    write("mus_win.wav", music_win(), stereo=True, peak=0.9)
    write("sfx_splash_small.wav", sfx_splash(0.5))
    write("sfx_splash_big.wav", sfx_splash(1.5, 1.1))
    write("sfx_cast.wav", sfx_cast())
    write("sfx_reel.wav", sfx_reel())
    write("sfx_bite.wav", sfx_plop())
    write("sfx_ui_click.wav", sfx_ui(880, 0.09, "click"))
    write("sfx_ui_hover.wav", sfx_ui(660, 0.07, "soft"))
    write("sfx_achieve.wav", sfx_achieve())
    write("sfx_coin.wav", sfx_coin())
    write("sfx_quest.wav", sfx_quest())
    write("sfx_foot_sand.wav", sfx_footstep("sand"))
    write("sfx_foot_wood.wav", sfx_footstep("wood"))
    write("sfx_foot_grass.wav", sfx_footstep("grass"))
    write("sfx_engine.wav", sfx_engine())
    write("sfx_thunder.wav", sfx_thunder())
    write("sfx_gull.wav", sfx_gull())
    write("sfx_bell.wav", sfx_bell())
    write("sfx_line_break.wav", sfx_line_break())
    write("sfx_flop.wav", sfx_flop())
    write("sfx_dog.wav", sfx_dog())
    write("sfx_seal.wav", sfx_seal())
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print(f"Готово: {len(os.listdir(OUT))} файлов, {total/1048576:.1f} МБ")

if __name__ == "__main__":
    main()
