#!/usr/bin/env python3
"""Bounded, observation-based enhancement experiments for the supplied video.

Ten full-length comparison copies and an optional 4K export are produced from
one verified original, never from a previously lossy upscale. No generative
model, anatomy synthesis, frame interpolation, or frame cropping is used.
The multiframe experiment is not a claim of recovered ground-truth detail.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import subprocess
import time
import zipfile

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("OMP_NUM_THREADS", "2")

import cv2
import imageio_ffmpeg
import numpy as np

from optimize_playback import audio_hash, avc_profile_level, faststart, sha256
from render_16k import geometry

SOURCE_SHA256 = "973b4e220d7b5a05ef8cd2c6e14d4cc36a513cd5fe4290f06cc0248b86574429"
SOURCE_WIDTH, SOURCE_HEIGHT, FRAMES = 480, 636, 1518
FPS, VIDEO_OFFSET = 30, 0.06665555555555556
RELEASE_TAG = "video-variants-20261110-cc989019"
RELEASE_ROOT = f"https://github.com/ritmiro1/Game/releases/download/{RELEASE_TAG}/"
COLOR = "setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709"
STAGES = (
    ("01-reference", "Контроль: исходник без улучшений"),
    ("02-multiframe", "Многокадровая субпиксельная реконструкция — эксперимент"),
    ("03-deblock", "Мягкое подавление блочных границ"),
    ("04-motion-denoise", "Шумоподавление с компенсацией движения"),
    ("05-chroma", "Подавление цветных пятен"),
    ("06-deblur", "Ограниченная коррекция размытия"),
    ("07-adaptive-sharp", "Адаптивная резкость без усиления ореолов"),
    ("08-color", "Сдержанная коррекция цвета и контраста"),
    ("09-stable", "Стабилизация добавленного обработкой мерцания"),
    ("10-balanced", "Совместная сдержанная обработка"),
)
LIMITATIONS = [
    "The only available source is 480x636. A less compressed original was not found or fabricated.",
    "Comparison stages 02-09 isolate methods, not successive generations of lossy re-encoding.",
    "Subpixel reconstruction uses neighboring observations, with motion and occlusion rejection. It is experimental, not verified recovery of missing detail.",
    "Estimated blur and gray-world color correction can be wrong; those effects are bounded and isolated for comparison.",
    "No external scene content, new anatomy, new actions or intermediate frames were generated.",
    "Resolution, sharpness metrics and successful decoding do not prove improved perceptual quality.",
    "Device playback speed has not been measured. Use a 720p copy if 4K is slow.",
]


def to_planes(bgr: np.ndarray) -> np.ndarray:
    """Full-range BT.709 Y, Cb, Cr; signed chroma, float32."""
    rgb = bgr.astype(np.float32) / 255
    b, g, r = cv2.split(rgb)
    y = 0.0722 * b + 0.7152 * g + 0.2126 * r
    return cv2.merge((y, (b - y) / 1.8556, (r - y) / 1.5748))


def from_planes(planes: np.ndarray) -> np.ndarray:
    y, cb, cr = cv2.split(planes)
    r, b = y + 1.5748 * cr, y + 1.8556 * cb
    g = (y - 0.2126 * r - 0.0722 * b) / 0.7152
    return np.rint(np.clip(cv2.merge((b, g, r)), 0, 1) * 255).astype(np.uint8)


def resize(a: np.ndarray, scale: int = 2) -> np.ndarray:
    return cv2.resize(a, (a.shape[1] * scale, a.shape[0] * scale), interpolation=cv2.INTER_CUBIC)


def grid(height: int, width: int) -> np.ndarray:
    x, y = np.meshgrid(np.arange(width, dtype=np.float32), np.arange(height, dtype=np.float32))
    return cv2.merge((x, y))


def warp(a: np.ndarray, flow: np.ndarray) -> np.ndarray:
    coordinates = grid(*flow.shape[:2]) + flow
    return cv2.remap(a, coordinates, None, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT_101)


def valid_warp(flow: np.ndarray) -> np.ndarray:
    h, w = flow.shape[:2]
    c = grid(h, w) + flow
    return (c[:, :, 0] >= 1) & (c[:, :, 0] <= w - 2) & (c[:, :, 1] >= 1) & (c[:, :, 1] <= h - 2)


@dataclass
class Observation:
    planes: np.ndarray
    to_neighbor: np.ndarray
    from_neighbor: np.ndarray
    confidence: np.ndarray


def confidence_for(current: np.ndarray, neighbor: np.ndarray, outward: np.ndarray, inward: np.ndarray) -> np.ndarray:
    back_error = np.linalg.norm(outward + warp(inward, outward), axis=2)
    difference = np.abs(warp(neighbor[:, :, 0], outward) - current[:, :, 0])
    confidence = np.exp(-np.square(back_error / 0.8)) * np.exp(-np.square(difference / (6 / 255)))
    confidence *= valid_warp(outward)
    confidence *= np.linalg.norm(outward, axis=2) < 32
    return confidence.astype(np.float32)


class Motion:
    def __init__(self):
        self.dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
        self.dis.setFinestScale(1)
        self.dis.setGradientDescentIterations(16)
        self.dis.setVariationalRefinementIterations(3)

    def pair(self, a: np.ndarray, b: np.ndarray) -> tuple[Observation, Observation]:
        h, w = a.shape[:2]
        size = (w // 2, h // 2)
        ay = cv2.resize(np.rint(a[:, :, 0] * 255).astype(np.uint8), size, interpolation=cv2.INTER_AREA)
        by = cv2.resize(np.rint(b[:, :, 0] * 255).astype(np.uint8), size, interpolation=cv2.INTER_AREA)
        # DIS requires sufficiently large images. This also handles unit-test fixtures.
        if min(size) < 32:
            ab = np.zeros((h, w, 2), np.float32)
            ba = ab.copy()
        else:
            ab = cv2.resize(self.dis.calc(ay, by, None), (w, h), interpolation=cv2.INTER_LINEAR) * 2
            ba = cv2.resize(self.dis.calc(by, ay, None), (w, h), interpolation=cv2.INTER_LINEAR) * 2
        ca, cb = confidence_for(a, b, ab, ba), confidence_for(b, a, ba, ab)
        # Reject globally unrelated observations instead of fusing a different scene.
        if float(np.mean(np.abs(a[:, :, 0] - warp(b[:, :, 0], ab)))) > 0.10:
            ca.fill(0)
            cb.fill(0)
        return Observation(b, ab, ba, ca), Observation(a, ba, ab, cb)


def deblock(planes: np.ndarray) -> np.ndarray:
    """Small conservative adjustments at the source's eight-pixel block grid."""
    out = planes.copy()
    y = out[:, :, 0]
    for axis in (1, 0):
        a = y if axis == 1 else y.T
        reference = a.copy()
        for x in range(8, a.shape[1] - 1, 8):
            p1, p0, q0, q1 = (reference[:, k] for k in (x - 2, x - 1, x, x + 1))
            step = q0 - p0
            eligible = (np.abs(step) < 10 / 255) & (np.abs(p1 - p0) < 4 / 255) & (np.abs(q1 - q0) < 4 / 255)
            correction = np.where(eligible, np.clip(step * 0.16, -1.2 / 255, 1.2 / 255), 0)
            a[:, x - 1] += correction
            a[:, x] -= correction
    return out


def denoise(planes: np.ndarray, observations: list[Observation]) -> np.ndarray:
    delta = np.zeros_like(planes)
    weight = np.ones(planes.shape[:2], np.float32)
    for obs in observations:
        confidence = obs.confidence * 0.22
        delta += confidence[:, :, None] * (warp(obs.planes, obs.to_neighbor) - planes)
        weight += confidence
    delta /= weight[:, :, None]
    return planes + np.clip(delta, -2 / 255, 2 / 255)


def chroma(planes: np.ndarray) -> np.ndarray:
    out = planes.copy()
    gradients = np.hypot(cv2.Sobel(planes[:, :, 0], cv2.CV_32F, 1, 0), cv2.Sobel(planes[:, :, 0], cv2.CV_32F, 0, 1))
    blend = (0.40 * np.exp(-gradients * 10)).astype(np.float32)
    for k in (1, 2):
        smooth = cv2.bilateralFilter(planes[:, :, k], 5, 0.025, 1.1)
        out[:, :, k] += np.clip(blend * (smooth - planes[:, :, k]), -2 / 255, 2 / 255)
    return out


def deblur(planes: np.ndarray) -> np.ndarray:
    out = planes.copy()
    observed = np.clip(planes[:, :, 0], 1e-4, 1)
    estimate = observed.copy()
    for _ in range(3):
        prediction = cv2.GaussianBlur(estimate, (5, 5), 0.55)
        estimate *= cv2.GaussianBlur(observed / np.maximum(prediction, 1e-4), (5, 5), 0.55)
    out[:, :, 0] += np.clip((estimate - observed) * 0.12, -1.5 / 255, 1.5 / 255)
    return out


def sharpen(planes: np.ndarray) -> np.ndarray:
    out = planes.copy()
    y = planes[:, :, 0]
    detail = y - cv2.GaussianBlur(y, (5, 5), 0.7)
    # Suppress weak noisy differences and bound every added contour.
    active = np.clip((np.abs(detail) - 0.5 / 255) / (3 / 255), 0, 1)
    candidate = y + np.clip(detail * active * 0.20, -1.5 / 255, 1.5 / 255)
    local_min, local_max = cv2.erode(y, np.ones((3, 3), np.uint8)), cv2.dilate(y, np.ones((3, 3), np.uint8))
    out[:, :, 0] = np.clip(candidate, local_min - 0.5 / 255, local_max + 0.5 / 255)
    return out


def color(planes: np.ndarray, gains: np.ndarray) -> np.ndarray:
    bgr = from_planes(planes).astype(np.float32) / 255
    # Tiny gray-world experiment; never a claim of objectively correct lighting.
    adjusted = np.clip(bgr * gains[None, None, :], 0, 1)
    out = to_planes(np.rint(adjusted * 255).astype(np.uint8))
    y = out[:, :, 0]
    out[:, :, 0] = y + 0.06 * y * (1 - y) * (2 * y - 1)
    return out


def stabilize(raw: np.ndarray, processed: np.ndarray, previous_raw: np.ndarray | None, previous_processed: np.ndarray | None, previous: Observation | None) -> np.ndarray:
    """Stabilize only the processing residual, not the actual moving image."""
    if previous is None or previous_raw is None or previous_processed is None:
        return processed.copy()
    residual = processed - raw
    historical = warp(previous_processed - previous_raw, previous.to_neighbor)
    amount = 0.20 * previous.confidence[:, :, None]
    correction = np.clip((historical - residual) * amount, -1 / 255, 1 / 255)
    return processed + correction


def multiframe(planes: np.ndarray, observations: list[Observation]) -> np.ndarray:
    """Two bounded back-projection iterations on a 2x observation grid.

    Neighbors are projected onto their own sampling grids, residuals are warped
    back, and unreliable/occluded samples have zero weight. This does not
    create new time samples and does not use learned/generative image priors.
    """
    y = planes[:, :, 0]
    baseline = resize(y)
    estimate = baseline.copy()
    for _ in range(2):
        prediction = cv2.resize(estimate, (y.shape[1], y.shape[0]), interpolation=cv2.INTER_AREA)
        correction = 0.5 * resize(y - prediction)
        weight = np.full(y.shape, 0.5, np.float32)
        neighbor_residual = np.zeros_like(y)
        for obs in observations:
            high_flow = resize(obs.from_neighbor) * 2
            projected = warp(estimate, high_flow)
            prediction = cv2.resize(projected, (y.shape[1], y.shape[0]), interpolation=cv2.INTER_AREA)
            residual = np.clip(obs.planes[:, :, 0] - prediction, -4 / 255, 4 / 255)
            confidence = obs.confidence * 0.12
            neighbor_residual += confidence * warp(residual, obs.to_neighbor)
            weight += confidence
        correction += resize(neighbor_residual)
        estimate += 0.35 * correction / resize(weight)
        estimate = np.clip(estimate, baseline - 3 / 255, baseline + 3 / 255)
    output = resize(planes)
    output[:, :, 0] = np.clip(estimate, 0, 1)
    return output


def combined_native(raw: np.ndarray, observations: list[Observation], gains: np.ndarray) -> np.ndarray:
    temporal = denoise(raw, observations)
    filtered = chroma(temporal + (deblock(raw) - raw))
    filtered = color(sharpen(deblur(filtered)), gains)
    # The total luma change remains small even when all methods are enabled.
    difference = np.clip(filtered - raw, -5 / 255, 5 / 255)
    return raw + difference


def filename(key: str) -> str:
    return f"armorypass-ejoy-{key}-720p.mp4"


def profile(width: int, height: int, crf: int = 18) -> dict:
    large = width > 1080
    return {"width": width, "height": height, "crf": crf, "profile": "high" if large else "main", "profile_idc": 100 if large else 77, "level": "5.1" if large else ("4.0" if width > 720 else "3.1"), "level_idc": 51 if large else (40 if width > 720 else 31), "maxrate": "20M" if large else ("5M" if width > 720 else "1800k"), "bufsize": "40M" if large else ("10M" if width > 720 else "3600k")}


def encode_args(p: dict, preset: str = "veryfast") -> list[str]:
    return ["-c:v", "libx264", "-preset", preset, "-crf", str(p["crf"]), "-profile:v", p["profile"], "-level:v", p["level"], "-maxrate", p["maxrate"], "-bufsize", p["bufsize"], "-threads:v", "1", "-refs", "2", "-bf", "2", "-g", "60", "-keyint_min", "30", "-sc_threshold", "0", "-pix_fmt", "yuv420p", "-fps_mode:v", "passthrough", "-color_range", "tv", "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709", "-map_metadata", "-1", "-metadata", "comment=Bounded enhancement experiment from original 480x636. Full picture, no new anatomy/actions/intermediate frames. Upscaled dimensions are not native detail.", "-movflags", "+faststart"]


def canvas_filter(width: int, height: int) -> str:
    active_height, top = geometry(width, height)
    return f"scale={width}:{active_height}:flags=bicubic:out_color_matrix=bt709:out_range=tv,pad={width}:{height}:0:{top}:color=black,setsar=1,{COLOR}"


def terminate(p: subprocess.Popen | None) -> None:
    if p is not None and p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()


class Sink:
    def __init__(self, ffmpeg: str, path: Path, source: Path, expected: int, high: bool, test: bool, lossless: bool = False):
        self.path, self.count = path, 0
        self.log = path.with_suffix(".log").open("wb")
        w, h = (960, 1272) if high else (480, 636)
        cmd = [ffmpeg, "-hide_banner", "-loglevel", "warning", "-nostdin", "-y", "-threads", "1", "-filter_threads", "1", "-thread_queue_size", "2", "-f", "rawvideo", "-pixel_format", "bgr24", "-video_size", f"{w}x{h}", "-framerate", str(FPS), "-itsoffset", str(VIDEO_OFFSET), "-i", "pipe:0"]
        if not test and not lossless:
            cmd += ["-threads", "1", "-i", str(source), "-map", "0:v:0", "-map", "1:a:0", "-c:a", "copy"]
        else:
            cmd += ["-map", "0:v:0", "-an"]
        cmd += ["-frames:v", str(expected)]
        if lossless:
            cmd += ["-c:v", "ffv1", "-level", "3", "-coder", "1", "-context", "1", "-g", "1", "-slices", "4", "-threads:v", "1", "-pix_fmt", "bgr0", "-fps_mode:v", "passthrough"]
        else:
            cmd += ["-vf", canvas_filter(720, 1280), *encode_args(profile(720, 1280))]
        cmd += [str(path)]
        self.process = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=self.log)

    def write(self, bgr: np.ndarray) -> None:
        self.process.stdin.write(memoryview(np.ascontiguousarray(bgr)).cast("B"))
        self.count += 1

    def close(self, expected: int) -> None:
        self.process.stdin.close()
        code = self.process.wait(timeout=180)
        self.log.close()
        if code or self.count != expected:
            raise RuntimeError(f"Encoder failed: {self.path.name}, exit={code}, frames={self.count}; {self.path.with_suffix('.log').read_text()[-3000:]}")


def decode_frames(ffmpeg: str, source: Path, expected: int):
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-xerror", "-err_detect", "explode", "-threads", "1", "-filter_threads", "1", "-i", str(source), "-map", "0:v:0", "-an", "-frames:v", str(expected), "-pix_fmt", "bgr24", "-fps_mode:v", "passthrough", "-f", "rawvideo", "pipe:1"]
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        for n in range(expected):
            data = process.stdout.read(SOURCE_WIDTH * SOURCE_HEIGHT * 3)
            if len(data) != SOURCE_WIDTH * SOURCE_HEIGHT * 3:
                raise RuntimeError(f"Incomplete original frame {n}: {len(data)} bytes")
            yield np.frombuffer(data, np.uint8).reshape(SOURCE_HEIGHT, SOURCE_WIDTH, 3)
        process.stdout.close()
        errors = process.stderr.read().decode(errors="replace")
        if process.wait(timeout=30) or errors.strip():
            raise RuntimeError(f"Source decode failed: {errors[-3000:]}")
    finally:
        terminate(process)


def analyze_source(ffmpeg: str, source: Path, expected: int) -> dict:
    sums = np.zeros(3, np.float64)
    count = 0
    for n, frame in enumerate(decode_frames(ffmpeg, source, expected)):
        if n % 30 == 0:
            sample = cv2.resize(frame, (60, 80), interpolation=cv2.INTER_AREA).astype(np.float32) / 255
            # Avoid forcing clipped highlights or near-black pixels to gray.
            mask = (np.min(sample, axis=2) > 0.04) & (np.max(sample, axis=2) < 0.92)
            if mask.any():
                sums += sample[mask].sum(axis=0, dtype=np.float64)
                count += int(mask.sum())
    mean = sums / max(1, count)
    gains = np.clip(float(mean.mean()) / np.maximum(mean, 1e-5), 0.99, 1.01) if count else np.ones(3)
    ffmpeg_version = subprocess.run([ffmpeg, "-version"], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    return {"source_sha256": sha256(source), "source_dimensions": [SOURCE_WIDTH, SOURCE_HEIGHT], "frames": expected, "fps": FPS, "video_offset_seconds": VIDEO_OFFSET, "test_only": expected != FRAMES, "gray_world_bgr_means": mean.tolist(), "bounded_bgr_gains": gains.tolist(), "software": {"opencv": cv2.__version__, "numpy": np.__version__, "ffmpeg": ffmpeg_version, "commit": os.environ.get("GITHUB_SHA")}, "multiframe": {"window": 3, "scale": 2, "iterations": 2, "max_luma_change_vs_cubic_8bit": 3, "generative_model": False}, "limitations": LIMITATIONS}


def render(args, ffmpeg: str, expected: int, analysis: dict) -> None:
    sinks = {}
    decoder = decode_frames(ffmpeg, args.source, expected)
    previous_raw = previous_combined = previous_sharp = None
    prev_observation = None
    motion = Motion()
    gains = np.array(analysis["bounded_bgr_gains"], np.float32)
    totals = {key: {"mean_absolute_change_8bit": 0.0, "largest_change_8bit": 0.0} for key, _ in STAGES}
    started = time.monotonic()
    try:
        for key, _ in STAGES:
            sinks[key] = Sink(ffmpeg, args.output_dir / filename(key), args.source, expected, True, bool(args.test_frames))
        sinks["master"] = Sink(ffmpeg, args.output_dir / "balanced-lossless.mkv", args.source, expected, True, True, lossless=True)
        current_bgr = next(decoder)
        current = to_planes(current_bgr)
        for n in range(expected):
            next_bgr = next(decoder) if n + 1 < expected else None
            future = None
            following_observation = None
            if next_bgr is not None:
                following = to_planes(next_bgr)
                future, following_observation = motion.pair(current, following)
            observations = [o for o in (prev_observation, future) if o is not None]
            multi = multiframe(current, observations)
            sharp = sharpen(current)
            stable = stabilize(current, sharp, previous_raw, previous_sharp, prev_observation)
            combined = combined_native(current, observations, gains)
            combined = stabilize(current, combined, previous_raw, previous_combined, prev_observation)
            balanced = resize(combined)
            balanced[:, :, 0] += multi[:, :, 0] - resize(current[:, :, 0])
            balanced[:, :, 0] = np.clip(balanced[:, :, 0], 0, 1)
            stages = {
                "01-reference": current,
                "02-multiframe": multi,
                "03-deblock": deblock(current),
                "04-motion-denoise": denoise(current, observations),
                "05-chroma": chroma(current),
                "06-deblur": deblur(current),
                "07-adaptive-sharp": sharp,
                "08-color": color(current, gains),
                "09-stable": stable,
                "10-balanced": balanced,
            }
            baseline = from_planes(resize(current))
            for key, planes in stages.items():
                # Give every experiment the same 2x working grid and final scaler;
                # otherwise simple resampling could masquerade as a filter effect.
                working = planes if planes.shape[:2] == balanced.shape[:2] else resize(planes)
                image = from_planes(working)
                sinks[key].write(image)
                change = np.abs(image.astype(np.float32) - baseline.astype(np.float32))
                totals[key]["mean_absolute_change_8bit"] += float(change.mean()) / expected
                totals[key]["largest_change_8bit"] = max(totals[key]["largest_change_8bit"], float(change.max()))
            sinks["master"].write(from_planes(balanced))
            previous_raw, previous_combined, previous_sharp = current, combined, sharp
            prev_observation = following_observation
            if next_bgr is not None:
                current_bgr, current = next_bgr, following
            if n == 0 or (n + 1) % 30 == 0 or n + 1 == expected:
                elapsed = time.monotonic() - started
                print(f"VARIANTS {n+1}/{expected}; elapsed={elapsed/60:.1f} min; remaining~{(expected-n-1)*elapsed/(n+1)/60:.1f} min", flush=True)
        # Advance once to run the decoder's exit/error checks, without reading new frames.
        try:
            next(decoder)
            raise RuntimeError("Unexpected decoded frame")
        except StopIteration:
            pass
        for sink in sinks.values():
            sink.close(expected)
    finally:
        decoder.close()
        for sink in sinks.values():
            terminate(sink.process)
            if not sink.log.closed:
                sink.log.close()
    for metrics in totals.values():
        metrics["mean_absolute_change_8bit"] = round(metrics["mean_absolute_change_8bit"], 5)
    analysis["render_seconds"] = round(time.monotonic() - started, 3)
    analysis["uncompressed_difference_metrics_not_quality_scores"] = totals
    (args.output_dir / "render-analysis.json").write_text(json.dumps(analysis, indent=2) + "\n")


def exports(args, ffmpeg: str, expected: int) -> None:
    for width, height, name in (
        (720, 1280, "armorypass-ejoy-balanced-watch-720p.mp4"),
        (1080, 1920, "armorypass-ejoy-balanced-1080p.mp4"),
        (2160, 3840, "armorypass-ejoy-balanced-4k.mp4"),
    ):
        if args.no_4k and width == 2160:
            continue
        output = args.output_dir / name
        cmd = [ffmpeg, "-hide_banner", "-loglevel", "warning", "-nostdin", "-y", "-copyts", "-threads", "1", "-filter_threads", "1", "-i", str(args.output_dir / "balanced-lossless.mkv")]
        if args.test_frames:
            cmd += ["-map", "0:v:0", "-an"]
        else:
            cmd += ["-threads", "1", "-i", str(args.source), "-map", "0:v:0", "-map", "1:a:0", "-c:a", "copy"]
        p = profile(width, height, 17 if width > 1080 else (22 if width == 720 else 18))
        if width == 720:
            p.update(maxrate="480k", bufsize="960k")
        cmd += ["-vf", canvas_filter(width, height), "-frames:v", str(expected), *encode_args(p, "medium"), "-nostats", "-progress", str(output.with_suffix(".progress.txt")), str(output)]
        print(f"EXPORT {width}x{height}: lossless intermediate, original AAC, no lossy cascade", flush=True)
        with output.with_suffix(".log").open("wb") as log:
            result = subprocess.run(cmd, stderr=log)
        if result.returncode:
            raise RuntimeError(output.with_suffix(".log").read_text(errors="replace")[-4000:])


def validate_one(ffmpeg: str, path: Path, p: dict, expected: int, aac: str | None) -> dict:
    info = subprocess.run([ffmpeg, "-hide_banner", "-i", str(path)], capture_output=True, text=True).stderr
    if f"{p['width']}x{p['height']}" not in info or "Video: h264" not in info or "30 fps" not in info or "yuv420p(tv, bt709, progressive)" not in info:
        raise RuntimeError(f"Unexpected metadata for {path.name}: {info}")
    if not faststart(path) or avc_profile_level(path) != (p["profile_idc"], p["level_idc"]):
        raise RuntimeError(f"Invalid fast-start/profile/level: {path.name}")
    if aac is not None and audio_hash(ffmpeg, path) != aac:
        raise RuntimeError(f"AAC bitstream changed: {path.name}")
    # Video-only showinfo avoids interleaved audio/video log records.
    result = subprocess.run([ffmpeg, "-hide_banner", "-nostdin", "-copyts", "-xerror", "-err_detect", "explode", "-threads", "1", "-filter_threads", "1", "-i", str(path), "-map", "0:v:0", "-an", "-vf", "showinfo", "-fps_mode", "passthrough", "-nostats", "-f", "null", "-"], capture_output=True, text=True)
    path.with_suffix(".decode.log").write_text(result.stderr)
    frames = re.findall(r"\bn:\s*(\d+)\s+pts:\s*(-?\d+)\s+pts_time:([\d.e+-]+)", result.stderr)
    if result.returncode or len(frames) != expected or [int(f[0]) for f in frames] != list(range(expected)):
        raise RuntimeError(f"Decode/frame-count failure: {path.name}; frames={len(frames)}, code={result.returncode}")
    pts = np.array([float(f[2]) for f in frames])
    if not np.all(np.diff(pts) > 0) or np.max(np.abs(pts - (VIDEO_OFFSET + np.arange(expected) / FPS))) > 0.001:
        raise RuntimeError(f"Unexpected video timing: {path.name}")
    if aac is not None:
        decoded = subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-xerror", "-err_detect", "explode", "-threads", "1", "-i", str(path), "-map", "0:a:0", "-vn", "-f", "null", "-"], capture_output=True, text=True)
        if decoded.returncode or decoded.stderr.strip():
            raise RuntimeError(f"Audio decode failure: {path.name}")
    return {"file": path.name, "width": p["width"], "height": p["height"], "frames": expected, "fps": FPS, "codec": "H.264", "profile": p["profile"], "level": p["level"], "first_video_pts": float(pts[0]), "last_video_pts": float(pts[-1]), "size": path.stat().st_size, "sha256": sha256(path), "full_decode_passed": True, "aac_unchanged": aac is not None, "faststart": True, "bt709": True, "download_url": RELEASE_ROOT + path.name}


def validate(args, ffmpeg: str, expected: int) -> dict:
    aac = None if args.test_frames else audio_hash(ffmpeg, args.source)
    report = {"release_tag": RELEASE_TAG, "source_sha256": sha256(args.source), "test_only": bool(args.test_frames), "aac_packet_hash": aac, "source_dimensions": [SOURCE_WIDTH, SOURCE_HEIGHT], "frames": expected, "limitations": LIMITATIONS, "outputs": {}}
    for key, title in STAGES:
        print(f"VERIFY {key}: full decode, timestamps, color, profile, AAC", flush=True)
        item = validate_one(ffmpeg, args.output_dir / filename(key), profile(720, 1280), expected, aac)
        item["title_ru"] = title
        report["outputs"][key] = item
    for key, width, height, name in (("balanced-watch", 720, 1280, "armorypass-ejoy-balanced-watch-720p.mp4"), ("balanced-1080p", 1080, 1920, "armorypass-ejoy-balanced-1080p.mp4"), ("balanced-4k", 2160, 3840, "armorypass-ejoy-balanced-4k.mp4")):
        if args.no_4k and width == 2160:
            continue
        report["outputs"][key] = validate_one(ffmpeg, args.output_dir / name, profile(width, height), expected, aac)
    if not args.test_frames and report["outputs"]["balanced-watch"]["size"] > 4_200_000:
        raise RuntimeError("The lightweight preview exceeds the remaining tracked-file budget")
    (args.output_dir / "variants-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def package(args) -> None:
    report = json.loads((args.output_dir / "variants-validation.json").read_text())
    if report["test_only"]:
        print("Smoke-test outputs will not be packaged as real deliverables.", flush=True)
        return
    # Avoid recursively embedding the archive's previous hash on repeated runs.
    report.pop("archive", None)
    (args.output_dir / "variants-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    rows = []
    for key, item in report["outputs"].items():
        rows.append(f"| {key} | {item['width']}×{item['height']} | {item['size']/1_000_000:.2f} МБ | {item['sha256']} |")
    notes = """# Все технические варианты обработки

Исходник: 480×636, 1518 кадров, примерно 50,65 секунды. Более качественного исходника нет.

01 — контроль без улучшений; 02–09 — отдельные методы, а не последовательное пережатие.
10 — совместная сдержанная обработка. 1080p и 4K сделаны из одного lossless-промежуточного файла.
Все MP4 сохраняют полную исходную картинку, все кадры, приблизительно исходные видеотаймкоды
(отклонение <1 мс) и побитово исходные AAC-пакеты. Формат 9:16 дополнен чёрными полями,
без обрезания кадра и без выдуманного продолжения сцены. Старые версии не заменялись.

Многокадровая реконструкция использует два реальных соседних кадра с компенсацией движения,
отбрасыванием несовпадений/перекрытий и ограниченной обратной проекцией. Это эксперимент,
не доказательство восстановленной истинной детализации. Цветокоррекция и коррекция размытия
также оценочные; они не гарантированно лучше контроля. Добавляемые изменения ограничены.
Нейросетевая генерация, дорисовка анатомии, новые действия и интерполяция кадров не применяются.

4K означает размер файла 2160×3840, не нативное качество съёмки. Если тормозит — используйте 720p.
В архиве находятся все десять полноразмерных по длительности сравнительных версий 720p,
этот текст и технические отчёты. 1080p и 4K скачиваются отдельно, чтобы не раздувать архив.

| Вариант | Размер кадра | Размер файла | SHA-256 |
|---|---|---|---|
""" + "\n".join(rows) + "\n"
    (args.output_dir / "VARIANTS-README-RU.md").write_text(notes)
    archive = args.output_dir / "armorypass-ejoy-all-variants-720p.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as bundle:
        for key, _ in STAGES:
            bundle.write(args.output_dir / filename(key), filename(key))
        for name in ("VARIANTS-README-RU.md", "variants-validation.json", "render-analysis.json"):
            bundle.write(args.output_dir / name, name)
    report["archive"] = {"file": archive.name, "size": archive.stat().st_size, "sha256": sha256(archive), "download_url": RELEASE_ROOT + archive.name}
    (args.output_dir / "variants-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PACKAGED {archive.name}: {archive.stat().st_size/1_000_000:.2f} MB", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--test-frames", type=int, default=0)
    parser.add_argument("--no-4k", action="store_true")
    parser.add_argument("--stage", choices=("all", "render", "exports", "validate", "package"), default="all")
    args = parser.parse_args()
    if not 0 <= args.test_frames <= FRAMES:
        parser.error("Invalid test-frame count")
    if sha256(args.source) != SOURCE_SHA256:
        parser.error("The input does not match the verified original")
    expected = args.test_frames or FRAMES
    args.output_dir.mkdir(parents=True, exist_ok=True)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    cv2.setNumThreads(1)
    if args.stage in ("all", "render"):
        analysis = analyze_source(ffmpeg, args.source, expected)
        analysis["test_only"] = bool(args.test_frames)
        render(args, ffmpeg, expected, analysis)
    if args.stage in ("all", "exports"):
        exports(args, ffmpeg, expected)
    if args.stage in ("all", "validate"):
        validate(args, ffmpeg, expected)
    if args.stage in ("all", "package"):
        package(args)
    print("VARIANTS JOB COMPLETE", flush=True)


if __name__ == "__main__":
    main()
