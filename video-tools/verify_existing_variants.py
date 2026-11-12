#!/usr/bin/env python3
"""Resume publication of an already rendered bundle without encoding it again."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

import imageio_ffmpeg

from enhance_variants import FRAMES, SOURCE_SHA256, STAGES, filename, profile, validate_one
from optimize_playback import audio_hash, sha256


def safe_name(name: str) -> str:
    if not isinstance(name, str) or not name or Path(name).name != name or '/' in name or '\\' in name:
        raise ValueError('Only plain asset filenames are accepted')
    return name


def check_report(report: dict) -> None:
    expected = {key for key, _ in STAGES} | {'balanced-watch', 'balanced-1080p', 'balanced-4k'}
    if report.get('test_only') or report.get('source_sha256') != SOURCE_SHA256 or set(report['outputs']) != expected:
        raise ValueError('A complete original-source report with all 13 outputs is required')
    if report['frames'] != FRAMES:
        raise ValueError('Incomplete source frame count')
    names = {key: filename(key) for key, _ in STAGES}
    names.update({'balanced-watch': 'armorypass-ejoy-balanced-watch-720p.mp4', 'balanced-1080p': 'armorypass-ejoy-balanced-1080p.mp4', 'balanced-4k': 'armorypass-ejoy-balanced-4k.mp4'})
    for key, item in report['outputs'].items():
        safe_name(item['file'])
        dimensions = (2160, 3840) if key == 'balanced-4k' else ((1080, 1920) if key == 'balanced-1080p' else (720, 1280))
        if item['file'] != names[key] or (item['width'], item['height']) != dimensions:
            raise ValueError('Unexpected variant filename or dimensions')
        if item['frames'] != FRAMES or not item['aac_unchanged'] or not item['full_decode_passed']:
            raise ValueError('Incomplete or unverified video in the report')
    if safe_name(report['archive']['file']) != 'armorypass-ejoy-all-variants-720p.zip':
        raise ValueError('Unexpected archive name')


def verify(folder: Path, source: Path, render_commit: str) -> dict:
    if sha256(source) != SOURCE_SHA256:
        raise ValueError('Wrong original source')
    report = json.loads((folder / 'variants-validation.json').read_text())
    check_report(report)
    analysis = json.loads((folder / 'render-analysis.json').read_text())
    if analysis['test_only'] or analysis['software']['commit'] != render_commit:
        raise ValueError('The original render provenance does not match the resume request')
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    aac = audio_hash(ffmpeg, source)
    if aac != report['aac_packet_hash']:
        raise ValueError('The recorded AAC hash does not match the original')
    for key, item in report['outputs'].items():
        path = folder / item['file']
        if path.stat().st_size != item['size'] or sha256(path) != item['sha256']:
            raise ValueError(f'Changed video asset: {path.name}')
        checked = validate_one(ffmpeg, path, profile(item['width'], item['height']), FRAMES, aac)
        if checked['sha256'] != item['sha256']:
            raise ValueError('Video changed during verification')
        print(f"RESUME VERIFIED {key}: {FRAMES} frames, original AAC, SHA-256", flush=True)
    archive = report['archive']
    path = folder / archive['file']
    if path.stat().st_size != archive['size'] or sha256(path) != archive['sha256']:
        raise ValueError('Changed ZIP archive')
    expected_members = {filename(key) for key, _ in STAGES} | {'VARIANTS-README-RU.md', 'variants-validation.json', 'render-analysis.json'}
    with zipfile.ZipFile(path) as bundle:
        if set(bundle.namelist()) != expected_members or len(bundle.namelist()) != len(expected_members):
            raise ValueError('Unexpected or duplicate ZIP members')
        if bundle.testzip() is not None:
            raise ValueError('ZIP CRC check failed')
        for key, _ in STAGES:
            digest = hashlib.sha256(bundle.read(filename(key))).hexdigest()
            if digest != report['outputs'][key]['sha256']:
                raise ValueError(f'ZIP/video mismatch: {key}')
        embedded = json.loads(bundle.read('variants-validation.json'))
        detached = {k: value for k, value in report.items() if k != 'archive'}
        if embedded != detached:
            raise ValueError('Embedded and external validation reports differ')
        for name in ('VARIANTS-README-RU.md', 'render-analysis.json'):
            if bundle.read(name) != (folder / name).read_bytes():
                raise ValueError(f'Embedded and external files differ: {name}')
    result = {'render_commit': render_commit, 'verified_videos': len(report['outputs']), 'frames_per_video': FRAMES, 'aac_packet_hash': aac, 'zip_crc_and_embedded_hashes_passed': True, 'no_reencoding': True}
    (folder / 'resume-verification.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--render-commit', required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.folder, args.source, args.render_commit), indent=2), flush=True)


if __name__ == '__main__':
    main()
