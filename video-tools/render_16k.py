#!/usr/bin/env python3
"""Conservative video cleanup and genuine 8640x15360 AV1 encoding.

No anatomy generation, frame interpolation, or new scene content. The output is
an upscale of existing footage, not a reconstruction of native 16K detail.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import urllib.request

os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("OMP_NUM_THREADS", "2")

import cv2
import imageio_ffmpeg
import numpy as np

MODEL_URL = (
    "https://raw.githubusercontent.com/fannymonori/TF-ESPCN/"
    "a899033b12cd0400454fb5777600883a9d7e86c3/export/ESPCN_x4.pb"
)
MODEL_SHA256 = "e403f06309229cf36009cd8fb0da032ba7643fae9f15cf94fe562e8edf8fef47"
SOURCE_SHA256 = "973b4e220d7b5a05ef8cd2c6e14d4cc36a513cd5fe4290f06cc0248b86574429"
FRAMES = 1518
FRAME_BYTES = 480 * 636 * 3
CLEANUP = (
    "deblock=filter=weak:block=8:alpha=0.08:beta=0.04:gamma=0.04:delta=0.04,"
    "hqdn3d=1.1:0.9:2.4:1.8,"
    "deband=1thr=0.007:2thr=0.007:3thr=0.007:range=8"
)
COLOR = "setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def geometry(width: int, height: int) -> tuple[int, int]:
    if width <= 0 or height <= 0 or width % 2 or height % 2:
        raise ValueError("Canvas dimensions must be positive even integers")
    if width * 16 != height * 9:
        raise ValueError("A 9:16 portrait canvas is required")
    active_height = 2 * round(width * 636 / 480 / 2)
    top = 2 * round(height * 488 / 3840 / 2)
    if active_height + top > height:
        raise ValueError("The original picture does not fit inside the canvas")
    return active_height, top


def get_model(path: Path) -> None:
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(MODEL_URL, timeout=60) as response:
            path.write_bytes(response.read(200_000))
    if sha256(path) != MODEL_SHA256:
        raise RuntimeError("The ESPCN model does not match its pinned SHA-256")


def stop(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def enhance(frame: np.ndarray, sr) -> np.ndarray:
    ycc = cv2.cvtColor(frame, cv2.COLOR_BGR2YCrCb)
    observed = ycc[:, :, 0].astype(np.float32) / 255
    estimate = np.maximum(observed, 1e-4).copy()
    for _ in range(5):
        predicted = cv2.GaussianBlur(estimate, (5, 5), 0.55)
        estimate *= cv2.GaussianBlur(observed / (predicted + 1e-4), (5, 5), 0.55)
    restored = observed + 0.2 * np.clip(estimate - observed, -0.03, 0.03)
    ycc[:, :, 0] = np.clip(np.round(restored * 255), 0, 255).astype(np.uint8)
    neural = sr.upsample(cv2.cvtColor(ycc, cv2.COLOR_YCrCb2BGR))
    cubic = cv2.resize(frame, (1920, 2544), interpolation=cv2.INTER_CUBIC)
    mixed = cv2.addWeighted(neural, 0.55, cubic, 0.45, 0)
    return cv2.bilateralFilter(mixed, 5, 4, 1)


def render(args, ffmpeg: str) -> None:
    cv2.setNumThreads(min(2, args.threads))
    sr = cv2.dnn_superres.DnnSuperResImpl_create()
    sr.readModel(str(args.model))
    sr.setModel("espcn", 4)
    sr.setPreferableBackend(cv2.dnn.DNN_BACKEND_OPENCV)
    sr.setPreferableTarget(cv2.dnn.DNN_TARGET_CPU)
    expected = args.test_frames or FRAMES
    active_height, top = geometry(args.width, args.height)
    decoder_cmd = [
        ffmpeg, "-hide_banner", "-loglevel", "warning", "-nostdin",
        "-threads", "1", "-filter_threads", "1", "-i", str(args.source),
        "-map", "0:v:0", "-an", "-sn", "-dn", "-vf", CLEANUP,
        "-frames:v", str(expected), "-pix_fmt", "bgr24",
        "-fps_mode", "passthrough", "-f", "rawvideo", "pipe:1",
    ]
    encoder_cmd = [
        ffmpeg, "-hide_banner", "-loglevel", "warning", "-nostdin", "-y",
        "-threads", "1", "-filter_threads", "1", "-thread_queue_size", "2",
        "-f", "rawvideo", "-pixel_format", "bgr24", "-video_size", "1920x2544",
        "-framerate", "30", "-itsoffset", "0.066656", "-i", "pipe:0",
    ]
    if not args.test_frames:
        encoder_cmd += ["-threads", "1", "-i", str(args.source), "-map", "0:v:0", "-map", "1:a:0", "-c:a", "copy"]
    else:
        encoder_cmd += ["-map", "0:v:0", "-an"]
    vf = (
        f"scale={args.width}:{active_height}:flags=spline:out_color_matrix=bt709:out_range=tv,"
        f"pad={args.width}:{args.height}:0:{top}:color=black,setsar=1,{COLOR}"
    )
    tiles = "4x4" if args.width >= 4000 else "1x1"
    encoder_cmd += [
        "-vf", vf, "-frames:v", str(expected),
        "-c:v", "libaom-av1", "-usage", "realtime", "-cpu-used", str(args.cpu_used),
        "-lag-in-frames", "0", "-threads:v", str(args.threads),
        "-tiles", tiles, "-row-mt", "1", "-enable-reduced-reference-set", "1",
        "-aom-params", "max-reference-frames=3", "-crf", str(args.crf), "-b:v", "0",
        "-g", "240", "-pix_fmt", "yuv420p", "-fps_mode:v", "passthrough",
        "-color_range", "tv", "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
        "-metadata", "comment=Conservative cleanup of original 480x636 footage; full picture preserved. Upscaled canvas, not native 16K detail. No generated anatomy, new actions, or interpolated frames.",
        "-movflags", "+faststart", "-nostats", "-progress", str(args.output.parent / "encoding-progress.txt"),
        str(args.output),
    ]
    decoder = encoder = None
    count = 0
    started = time.monotonic()
    print(f"RENDER START: source=480x636; canvas={args.width}x{args.height}; frames={expected}; AV1 CRF={args.crf}; cpu-used={args.cpu_used}", flush=True)
    with (args.output.parent / "decoder.log").open("wb") as dlog, (args.output.parent / "encoder.log").open("wb") as elog:
        try:
            decoder = subprocess.Popen(decoder_cmd, stdout=subprocess.PIPE, stderr=dlog)
            encoder = subprocess.Popen(encoder_cmd, stdin=subprocess.PIPE, stderr=elog)
            while True:
                raw = decoder.stdout.read(FRAME_BYTES)
                if not raw:
                    break
                if len(raw) != FRAME_BYTES:
                    raise RuntimeError(f"Incomplete decoded frame: {len(raw)} bytes")
                frame = np.frombuffer(raw, np.uint8).reshape(636, 480, 3)
                enhanced = enhance(frame, sr)
                encoder.stdin.write(memoryview(enhanced).cast("B"))
                count += 1
                if count == 1 or count % 30 == 0 or count == expected:
                    elapsed = time.monotonic() - started
                    remaining = (expected - count) * elapsed / count
                    print(f"RENDER {count}/{expected} ({count/expected:.1%}); elapsed={elapsed/60:.1f} min; remaining~{remaining/60:.1f} min", flush=True)
            decoder.stdout.close()
            dcode = decoder.wait(timeout=60)
            encoder.stdin.close()
            ecode = encoder.wait(timeout=600)
            if dcode or ecode or count != expected:
                raise RuntimeError(f"Render failed: decoder={dcode}, encoder={ecode}, frames={count}")
        except Exception:
            stop(decoder)
            stop(encoder)
            for name in ("decoder.log", "encoder.log"):
                print((args.output.parent / name).read_text(errors="replace")[-6000:], file=sys.stderr)
            raise
    print(f"RENDER COMPLETE: {count} frames, {args.output.stat().st_size} bytes", flush=True)


def run(ffmpeg: str, arguments: list[str]) -> subprocess.CompletedProcess:
    result = subprocess.run([ffmpeg, "-nostdin", *arguments], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr[-8000:])
    return result


def audio_hash(ffmpeg: str, file: Path) -> str:
    return run(ffmpeg, ["-v", "error", "-i", str(file), "-map", "0:a:0", "-c:a", "copy", "-f", "hash", "-hash", "sha256", "-"]).stdout.strip()


def faststart(path: Path) -> bool:
    atoms = []
    with path.open("rb") as stream:
        while header := stream.read(8):
            if len(header) != 8:
                raise RuntimeError("Truncated MP4 atom")
            size, kind = struct.unpack(">I4s", header)
            header_size = 8
            if size == 1:
                size = struct.unpack(">Q", stream.read(8))[0]
                header_size = 16
            atoms.append(kind)
            if size == 0:
                break
            if size < header_size:
                raise RuntimeError("Invalid MP4 atom")
            stream.seek(size - header_size, 1)
    return atoms.index(b"moov") < atoms.index(b"mdat")


def validate(args, ffmpeg: str) -> None:
    print(f"VALIDATION START: complete {args.width}x{args.height} decode, frame count, color, fast-start; AAC identity for full renders.", flush=True)
    expected = args.test_frames or FRAMES
    info = subprocess.run([ffmpeg, "-hide_banner", "-i", str(args.output)], capture_output=True, text=True).stderr
    if f"{args.width}x{args.height}" not in info or "Video: av1" not in info or "30 fps" not in info:
        raise RuntimeError(f"Unexpected video metadata: {info}")
    if "yuv420p(tv, bt709, progressive)" not in info or not faststart(args.output):
        raise RuntimeError("Color metadata or fast-start validation failed")
    if args.output.stat().st_size >= 1_900_000_000:
        raise RuntimeError("The output is too large for the planned release asset")
    original_audio = None
    if not args.test_frames:
        original_audio = audio_hash(ffmpeg, args.source)
        if original_audio != audio_hash(ffmpeg, args.output):
            raise RuntimeError("The AAC audio bitstream was changed")
    preview = args.output.parent / "armorypass-ejoy-16k-preview-720p.mp4"
    cmd = ["-hide_banner", "-loglevel", "error", "-y", "-xerror", "-err_detect", "explode", "-threads", "1", "-filter_threads", "1", "-i", str(args.output), "-map", "0:v:0"]
    cmd += ["-an"] if args.test_frames else ["-map", "0:a:0", "-c:a", "copy"]
    cmd += [
        "-vf", f"scale=720:1280:flags=area,setsar=1,{COLOR}",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "21", "-threads:v", "2",
        "-profile:v", "main", "-level:v", "3.1", "-pix_fmt", "yuv420p", "-fps_mode:v", "passthrough",
        "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
        "-movflags", "+faststart", "-nostats", "-progress", "pipe:1", str(preview),
    ]
    result = run(ffmpeg, cmd)
    counts = re.findall(r"^frame=(\d+)", result.stdout, re.M)
    if not counts or int(counts[-1]) != expected:
        raise RuntimeError(f"Decoded frame count is not {expected}: {counts[-3:]}")
    if result.stderr.strip():
        raise RuntimeError(f"Decode emitted errors: {result.stderr}")
    report = {
        "file": args.output.name, "width": args.width, "height": args.height,
        "frames": expected, "fps": 30, "size": args.output.stat().st_size,
        "sha256": sha256(args.output), "source_sha256": sha256(args.source),
        "aac_packet_hash": original_audio, "full_decode_passed": True, "bt709": True,
        "faststart": True, "test_only": bool(args.test_frames),
        "model_sha256": MODEL_SHA256, "codec": "AV1", "crf": args.crf,
        "detail_caveat": "Upscale of a 480x636 source, not native 16K. Cleanup may soften fine texture; no new anatomical details or actions are generated.",
    }
    (args.output.parent / "validation-16k.json").write_text(json.dumps(report, indent=2))
    notes = (
        "# Вертикальная версия 16K\n\n"
        f"Размер: {args.width}×{args.height}; {expected} кадров, 30 кадров/с; AV1 в MP4; {report['size']/1_000_000:.1f} МБ.\n\n"
        "Выполнена сборка из оригинала 480×636: умеренное подавление блоков, временного шума и цветовых ступеней, слабая ограниченная коррекция яркости, ESPCN×4 со смешиванием с интерполяцией, лёгкое сглаживание и увеличение до выходного холста. Полный кадр, поля, длительность и исходная AAC-дорожка сохранены.\n\n"
        "**Это не нативная 16K-детализация.** Пиксельная сетка может быть сглажена, но отсутствовавшие детали достоверно не восстановлены. Очистка может смягчать сохранившуюся текстуру. Новая анатомия, новые действия и промежуточные кадры не создавались. Отдельных генеративных улучшений интимных участков нет.\n\n"
        "Оба прежних 4K-файла оставлены без изменений. Для просмотра на обычном устройстве приложена облегчённая HD-копия. Полный 16K AV1-файл требует совместимого программного декодера и значительных ресурсов; воспроизведение в браузере не гарантируется.\n\n"
        "Проверки: полное декодирование всех кадров без ошибок, размеры, BT.709, fast-start и совпадение исходной AAC-дорожки по хешу. Это техническая проверка целостности, не доказательство восстановления реальных деталей и не непрерывный визуальный просмотр.\n\n"
        f"SHA-256: `{report['sha256']}`\n"
    )
    (args.output.parent / "16k-notes-ru.md").write_text(notes)
    print(json.dumps(report, indent=2), flush=True)
    print("VALIDATION COMPLETE", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--model", type=Path, default=Path.home() / ".cache/video-tools/ESPCN_x4.pb")
    parser.add_argument("--width", type=int, default=8640)
    parser.add_argument("--height", type=int, default=15360)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--cpu-used", type=int, choices=range(5, 9), default=6)
    parser.add_argument("--crf", type=int, choices=range(0, 64), default=18)
    parser.add_argument("--test-frames", type=int, default=0, help="Explicit partial, silent test; never a finished deliverable")
    args = parser.parse_args()
    geometry(args.width, args.height)
    if args.threads < 1 or not 0 <= args.test_frames <= FRAMES:
        parser.error("Invalid thread or test-frame count")
    if sha256(args.source) != SOURCE_SHA256:
        parser.error("Input is not the verified original source")
    get_model(args.model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    print("FFmpeg:", ffmpeg, flush=True)
    render(args, ffmpeg)
    validate(args, ffmpeg)


if __name__ == "__main__":
    main()
