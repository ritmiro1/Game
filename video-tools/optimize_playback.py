#!/usr/bin/env python3
"""Make hardware-friendly playback copies of the published 16K file.

Only resizing and re-encoding: no new image detail, anatomy, or motion is added.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

import imageio_ffmpeg

SOURCE_SHA256 = "a1e91b164efba3b1f7ae278b2c8158b56e560a7834be40afcc50de7c2a7356da"
SOURCE_URL = "https://github.com/ritmiro1/Game/releases/download/video-16k-20261110-cc989019/armorypass-ejoy-16k.mp4"
RELEASE_ROOT = "https://github.com/ritmiro1/Game/releases/download/video-playback-20261110-cc989019/"
FRAMES = 1518
COLOR = "setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709"
PROFILES = {
    "1080p": {"width": 1080, "height": 1920, "crf": 19, "maxrate": "3M", "bufsize": "6M", "profile": "main", "profile_idc": 77, "level": "4.0", "level_idc": 40, "refs": 2, "bframes": 2, "name": "armorypass-ejoy-playback-1080p.mp4"},
    "720p": {"width": 720, "height": 1280, "crf": 22, "maxrate": "600k", "bufsize": "1200k", "profile": "main", "profile_idc": 77, "level": "3.1", "level_idc": 31, "refs": 2, "bframes": 2, "name": "armorypass-ejoy-smooth-720p.mp4"},
    "economy": {"width": 432, "height": 768, "crf": 23, "maxrate": "220k", "bufsize": "440k", "profile": "baseline", "profile_idc": 66, "level": "3.0", "level_idc": 30, "refs": 1, "bframes": 0, "name": "armorypass-ejoy-economy-432p.mp4"},
}


def sha256(file: Path) -> str:
    digest = hashlib.sha256()
    with file.open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(data)
    return digest.hexdigest()


def run(ffmpeg: str, args: list[str]) -> subprocess.CompletedProcess:
    result = subprocess.run([ffmpeg, "-nostdin", *args], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr[-6000:])
    return result


def scale_filter(profile: dict) -> str:
    return f"scale={profile['width']}:{profile['height']}:flags=area:out_color_matrix=bt709:out_range=tv,format=yuv420p,setsar=1,{COLOR}"


def encode_options(profile: dict) -> list[str]:
    return [
        "-c:v", "libx264", "-preset", "slow", "-crf", str(profile["crf"]),
        "-maxrate", profile["maxrate"], "-bufsize", profile["bufsize"],
        "-threads:v", "2", "-profile:v", profile["profile"], "-level:v", profile["level"],
        "-refs", str(profile["refs"]), "-bf", str(profile["bframes"]),
        "-g", "60", "-keyint_min", "30", "-sc_threshold", "0",
        "-x264-params", "cpu-independent=1", "-pix_fmt", "yuv420p", "-fps_mode:v", "passthrough",
        "-color_range", "tv", "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
        "-map_metadata", "-1", "-metadata", f"comment=Playback copy, {profile['width']}x{profile['height']} H.264. Full original picture and timing retained; no new details generated.",
        "-movflags", "+faststart",
    ]


def transcode_pair(ffmpeg: str, source: Path, folder: Path, test_frames: int) -> None:
    graph = f"[0:v]split=2[hi][lo];[hi]{scale_filter(PROFILES['1080p'])}[v1080];[lo]{scale_filter(PROFILES['720p'])}[v720]"
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "warning", "-nostdin", "-y", "-threads", "2", "-filter_complex_threads", "2", "-i", str(source), "-filter_complex", graph]
    for key, label in (("1080p", "v1080"), ("720p", "v720")):
        cmd += ["-map", f"[{label}]"]
        cmd += ["-an"] if test_frames else ["-map", "0:a:0", "-c:a", "copy"]
        if test_frames:
            cmd += ["-frames:v", str(test_frames)]
        cmd += encode_options(PROFILES[key]) + [str(folder / PROFILES[key]["name"])]
    print("TRANSCODE START: decode the source once; create 1080p and 720p H.264.", flush=True)
    with (folder / "transcode.log").open("wb") as log:
        result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=log)
    if result.returncode:
        raise RuntimeError((folder / "transcode.log").read_text(errors="replace")[-8000:])
    print("1080p and 720p encodes completed.", flush=True)


def encode_economy(ffmpeg: str, source720: Path, folder: Path, silent: bool = False) -> Path:
    profile = PROFILES["economy"]
    output = folder / profile["name"]
    cmd = ["-hide_banner", "-loglevel", "error", "-y", "-threads", "1", "-filter_threads", "1", "-i", str(source720), "-map", "0:v:0"]
    cmd += ["-an"] if silent else ["-map", "0:a:0", "-c:a", "copy"]
    cmd += ["-vf", scale_filter(profile), *encode_options(profile), str(output)]
    run(ffmpeg, cmd)
    print("Economy encode completed: 432x768 H.264 Baseline level 3.0.", flush=True)
    return output


def audio_hash(ffmpeg: str, source: Path) -> str:
    return run(ffmpeg, ["-v", "error", "-i", str(source), "-map", "0:a:0", "-c:a", "copy", "-f", "hash", "-hash", "sha256", "-"]).stdout.strip()


def faststart(file: Path) -> bool:
    atoms = []
    with file.open("rb") as stream:
        while header := stream.read(8):
            if len(header) != 8:
                return False
            size, kind = struct.unpack(">I4s", header)
            offset = 8
            if size == 1:
                size = struct.unpack(">Q", stream.read(8))[0]
                offset = 16
            atoms.append(kind)
            if size == 0:
                break
            if size < offset:
                return False
            stream.seek(size - offset, 1)
    return b"moov" in atoms and b"mdat" in atoms and atoms.index(b"moov") < atoms.index(b"mdat")


def avc_profile_level(file: Path) -> tuple[int, int]:
    with file.open("rb") as stream:
        prefix = stream.read(2 * 1024 * 1024)
    index = prefix.find(b"avcC")
    if index < 0 or prefix[index + 4] != 1:
        raise RuntimeError("H.264 AVC configuration not found")
    return prefix[index + 5], prefix[index + 7]


def validate_output(ffmpeg: str, file: Path, profile: dict, expected: int, expected_audio: str | None) -> dict:
    info = subprocess.run([ffmpeg, "-hide_banner", "-i", str(file)], capture_output=True, text=True).stderr
    if f"{profile['width']}x{profile['height']}" not in info or "Video: h264" not in info or "30 fps" not in info:
        raise RuntimeError(f"Unexpected video metadata: {info}")
    if "yuv420p(tv, bt709, progressive)" not in info or not faststart(file):
        raise RuntimeError("Color or fast-start validation failed")
    if avc_profile_level(file) != (profile["profile_idc"], profile["level_idc"]):
        raise RuntimeError("Unexpected AVC profile or level")
    if expected_audio and audio_hash(ffmpeg, file) != expected_audio:
        raise RuntimeError("Original AAC was changed")
    started = time.monotonic()
    result = run(ffmpeg, ["-v", "error", "-xerror", "-err_detect", "explode", "-threads", "1", "-i", str(file), "-map", "0:v:0", "-an", "-fps_mode", "passthrough", "-nostats", "-progress", "pipe:1", "-f", "null", "-"])
    seconds = time.monotonic() - started
    frames = re.findall(r"^frame=(\d+)", result.stdout, re.M)
    if not frames or int(frames[-1]) != expected or result.stderr.strip():
        raise RuntimeError(f"Full decode validation failed: {frames[-3:]} {result.stderr}")
    return {
        "file": file.name, "width": profile["width"], "height": profile["height"],
        "frames": expected, "fps": 30, "codec": "H.264", "profile": profile["profile"], "level": profile["level"],
        "size": file.stat().st_size, "sha256": sha256(file), "full_decode_passed": True,
        "aac_unchanged": expected_audio is not None, "faststart": True, "bt709": True,
        "decode_seconds_on_runner": round(seconds, 3),
        "decode_speed_x_on_runner": round(expected / 30 / seconds, 2),
        "performance_caveat": "Benchmark is for this runner, not a guarantee for the user's device or network.",
        "download_url": RELEASE_ROOT + file.name,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--source", type=Path)
    inputs.add_argument("--economy-from720", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--test-frames", type=int, default=0)
    args = parser.parse_args()
    if not 0 <= args.test_frames <= FRAMES:
        parser.error("Invalid test-frame count")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    if args.economy_from720:
        encode_economy(ffmpeg, args.economy_from720, args.output_dir)
        return
    if not args.test_frames and sha256(args.source) != SOURCE_SHA256:
        parser.error("Input does not match the verified 16K release")
    started = time.monotonic()
    expected = args.test_frames or FRAMES
    audio = None if args.test_frames else audio_hash(ffmpeg, args.source)
    transcode_pair(ffmpeg, args.source, args.output_dir, args.test_frames)
    encode_economy(ffmpeg, args.output_dir / PROFILES["720p"]["name"], args.output_dir, silent=bool(args.test_frames))
    report = {"source_url": SOURCE_URL, "source_sha256": sha256(args.source), "test_only": bool(args.test_frames), "aac_packet_hash": audio, "outputs": {}}
    for key, profile in PROFILES.items():
        output = args.output_dir / profile["name"]
        report["outputs"][key] = validate_output(ffmpeg, output, profile, expected, audio)
        print(f"VERIFIED {key}: {output.stat().st_size} bytes, {expected} frames.", flush=True)
    if not args.test_frames and report["outputs"]["720p"]["size"] >= 6_000_000:
        raise RuntimeError("720p playback copy exceeds the workspace size budget")
    (args.output_dir / "playback-validation.json").write_text(json.dumps(report, indent=2) + "\n")
    table = "\n".join(f"| {key} | {p['width']}×{p['height']} | {p['size']/1_000_000:.1f} МБ | H.264 {p['profile']} {p['level']} |" for key,p in report["outputs"].items())
    checksums = "\n".join(f"- {key}: `{p['sha256']}`" for key,p in report["outputs"].items())
    notes = f"""# Копии для плавного просмотра

| Режим | Размер кадра | Размер файла | Кодек |
| --- | --- | --- | --- |
{table}

Это уменьшенные копии опубликованного 16K-файла, а не новая обработка или дорисовка. Все 1518 кадров, полный исходный кадр и оригинальная AAC-дорожка сохранены. 16K и прежние 4K-файлы не изменены.

H.264 8-bit yuv420p рассчитан на более широкую аппаратную совместимость. Используются fast-start и ключевые кадры не реже двух секунд. Для браузерного плеера по умолчанию выбран 720p; для слабого устройства предусмотрен экономный 432×768 Baseline. Плавность на конкретном устройстве и скорость его интернета не гарантируются.

Проверены полное декодирование всех кадров, H.264 profile/level, размеры, BT.709, fast-start и совпадение AAC-пакетов. Время декодирования в JSON относится к runner, не к пользовательскому устройству.

SHA-256:
{checksums}
"""
    (args.output_dir / "playback-notes-ru.md").write_text(notes)
    print(f"PLAYBACK COPIES VERIFIED: total elapsed {time.monotonic()-started:.1f}s", flush=True)


if __name__ == "__main__":
    main()
