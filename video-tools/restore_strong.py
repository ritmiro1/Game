#!/usr/bin/env python3
"""Stronger, bounded cleanup from real neighboring frames, not a '100x' claim.

The source's colors, full picture, frame count and AAC are retained. There is
no learned/generative model, outpainting, anatomy reconstruction or temporal
interpolation. A sample-based guardrail is not a perceptual-quality proof.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import time

os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('OMP_NUM_THREADS', '2')

import cv2
import imageio_ffmpeg
import numpy as np

import enhance_variants as base
from optimize_playback import audio_hash, sha256

SOURCE_SHA256 = base.SOURCE_SHA256
FRAMES = base.FRAMES
RELEASE_TAG = 'video-restored-20261110-cc989019'
RELEASE_ROOT = f'https://github.com/ritmiro1/Game/releases/download/{RELEASE_TAG}/'
SAMPLES = (48, 303, 600, 927, 1200, 1485)
PROFILES = {
    'moderate': dict(temporal=0.8, temporal_cap=4, nlm_h=2.5, spatial_blend=0.65, guided_sigma=1.5, deblock=1.5, chroma=0.5, luma_cap=8, chroma_cap=3),
    'clean': dict(temporal=1.2, temporal_cap=6, nlm_h=3.8, spatial_blend=0.85, guided_sigma=2.2, deblock=2.2, chroma=0.7, luma_cap=12, chroma_cap=4),
    'strong': dict(temporal=1.6, temporal_cap=8, nlm_h=5.0, spatial_blend=1.0, guided_sigma=3.0, deblock=2.7, chroma=0.85, luma_cap=16, chroma_cap=5),
}
EXPORTS = {
    '720p': (720, 1280, 'armorypass-ejoy-restored-watch-720p.mp4'),
    '1080p': (1080, 1920, 'armorypass-ejoy-restored-1080p.mp4'),
    '4k': (2160, 3840, 'armorypass-ejoy-restored-4k.mp4'),
}
LIMITATIONS = [
    'A 100-fold quality improvement is neither defined nor measured. No such claim is made.',
    'Only the supplied 480x636 source is available. Missing authentic detail cannot be guaranteed to be recovered.',
    'Five-frame optical-flow fusion rejects mismatches and occlusions, but motion estimation can still be wrong.',
    'Stronger denoising can soften real texture. Sample metrics are guardrails, not perceptual quality scores or proof of improvement.',
    'Blur correction uses an estimated kernel; source white balance and exposure are not automatically changed.',
    '4K is the coded canvas size, not native 4K detail. No new anatomy, actions, scene extensions or intermediate frames are generated.',
    'Playback performance on the user device has not been measured; use 720p rather than 4K if playback is slow.',
]


def confidence(current, neighbor, forward, reverse):
    returned = forward + base.warp(reverse, forward)
    fb = np.linalg.norm(returned, axis=2)
    warped = base.warp(neighbor[:, :, 0], forward)
    dy = np.abs(cv2.GaussianBlur(warped, (3, 3), 0.65) - cv2.GaussianBlur(current[:, :, 0], (3, 3), 0.65))
    raw_difference = np.abs(warped - current[:, :, 0])
    weight = np.exp(-np.square(fb / 0.9)) * np.exp(-np.square(dy / (9 / 255)))
    weight *= base.valid_warp(forward)
    weight *= (raw_difference < 28 / 255) & (np.linalg.norm(forward, axis=2) < 32)
    if float(np.mean(raw_difference)) > 0.10:
        weight.fill(0)
    return weight.astype(np.float32)


def compose(first, second):
    """Compose current→middle and middle→neighbor displacement fields."""
    return first + base.warp(second, first)


def observations(frames, pairs, n):
    def flow(a, b):
        key = (min(a, b), max(a, b))
        forward, reverse = pairs[key]
        return forward if a < b else reverse

    result = []
    for k in sorted(frames):
        if k == n or abs(k - n) > 2:
            continue
        if abs(k - n) == 1:
            outward, inward = flow(n, k), flow(k, n)
        else:
            middle = (k + n) // 2
            outward = compose(flow(n, middle), flow(middle, k))
            inward = compose(flow(k, middle), flow(middle, n))
        result.append((abs(k - n), base.Observation(frames[k], outward, inward, confidence(frames[n], frames[k], outward, inward))))
    return result


def native(raw, neighbors, p):
    total = raw.copy()
    weight = np.ones(raw.shape[:2], np.float32)
    for distance, obs in neighbors:
        w = obs.confidence * p['temporal'] * (0.60 if distance == 1 else 0.27)
        total += w[:, :, None] * base.warp(obs.planes, obs.to_neighbor)
        weight += w
    temporal_delta = total / weight[:, :, None] - raw
    filtered = raw + np.clip(temporal_delta, -p['temporal_cap'] / 255, p['temporal_cap'] / 255)
    filtered += (base.deblock(filtered) - filtered) * p['deblock']
    y = filtered[:, :, 0].copy()
    y8 = np.rint(np.clip(y, 0, 1) * 255).astype(np.uint8)
    nlm = cv2.fastNlMeansDenoising(y8, None, p['nlm_h'], 5, 11).astype(np.float32) / 255
    gradient = np.hypot(cv2.Sobel(y, cv2.CV_32F, 1, 0), cv2.Sobel(y, cv2.CV_32F, 0, 1))
    blend = p['spatial_blend'] * (0.35 + 0.65 * np.exp(-gradient * 10))
    spatial = y + blend * (nlm - y)
    spatial = cv2.ximgproc.guidedFilter(y, spatial, 2, (p['guided_sigma'] / 255) ** 2)
    # Correct only a small, bounded estimated blur component after denoising.
    estimate = np.maximum(spatial, 1e-4).copy()
    for _ in range(4):
        predicted = cv2.GaussianBlur(estimate, (5, 5), 0.60)
        estimate *= cv2.GaussianBlur(np.maximum(spatial, 1e-4) / np.maximum(predicted, 1e-4), (5, 5), 0.60)
    restored = spatial + np.clip((estimate - spatial) * 0.22, -3 / 255, 3 / 255)
    detail = restored - cv2.GaussianBlur(restored, (5, 5), 0.75)
    active = np.clip((np.abs(detail) - 1 / 255) / (3 / 255), 0, 1)
    sharpened = restored + np.clip(0.18 * detail * active, -1.5 / 255, 1.5 / 255)
    kernel = np.ones((3, 3), np.uint8)
    filtered[:, :, 0] = np.clip(sharpened, cv2.erode(spatial, kernel) - 0.5 / 255, cv2.dilate(spatial, kernel) + 0.5 / 255)
    for channel in (1, 2):
        smooth = cv2.bilateralFilter(filtered[:, :, channel], 5, 0.04, 1.5)
        filtered[:, :, channel] += p['chroma'] * (smooth - filtered[:, :, channel])
    # All changes are bounded against the original current frame, not an
    # accumulating history that could drift or invent content.
    delta = filtered - raw
    delta[:, :, 0] = np.clip(delta[:, :, 0], -p['luma_cap'] / 255, p['luma_cap'] / 255)
    delta[:, :, 1:] = np.clip(delta[:, :, 1:], -p['chroma_cap'] / 255, p['chroma_cap'] / 255)
    return raw + delta


def sample_metrics(raw, processed):
    y, out = raw[:, :, 0], processed[:, :, 0]
    def gradient(a):
        return np.hypot(cv2.Sobel(a, cv2.CV_32F, 1, 0), cv2.Sobel(a, cv2.CV_32F, 0, 1))
    g, go = gradient(y), gradient(out)
    flat = (g <= np.percentile(g, 55)) & (y > 8 / 255) & (y < 245 / 255)
    edges = g >= max(float(np.percentile(g, 90)), 0.02)
    high = y - cv2.GaussianBlur(y, (5, 5), 0.65)
    after = out - cv2.GaussianBlur(out, (5, 5), 0.65)
    before_rms = float(np.sqrt(np.mean(high[flat] ** 2))) if flat.any() else 0
    after_rms = float(np.sqrt(np.mean(after[flat] ** 2))) if flat.any() else 0
    retention = float(np.mean(go[edges]) / max(float(np.mean(g[edges])), 1e-6)) if edges.any() else 1.0
    change = np.abs(out - y) * 255
    before_rgb, after_rgb = base.from_planes(raw), base.from_planes(processed)
    clipping_increase = float(np.mean((after_rgb == 0) | (after_rgb == 255)) - np.mean((before_rgb == 0) | (before_rgb == 255)))
    return {'flat_high_frequency_rms_before_8bit': before_rms * 255, 'flat_high_frequency_rms_after_8bit': after_rms * 255, 'flat_high_frequency_reduction': 1 - after_rms / before_rms if before_rms > 1e-6 else 0.0, 'strong_edge_gradient_retention': retention, 'mean_luma_change_8bit': float(change.mean()), 'max_luma_change_8bit': float(change.max()), 'rgb_clipping_fraction_increase': clipping_increase}


def choose_profile(results, requested='auto'):
    summaries = {}
    for key, samples in results.items():
        median_reduction = float(np.median([s['flat_high_frequency_reduction'] for s in samples]))
        median_edge = float(np.median([s['strong_edge_gradient_retention'] for s in samples]))
        min_edge = min(s['strong_edge_gradient_retention'] for s in samples)
        max_mean_change = max(s['mean_luma_change_8bit'] for s in samples)
        clipping = max(s['rgb_clipping_fraction_increase'] for s in samples)
        passed = median_edge >= 0.90 and min_edge >= 0.84 and max_mean_change <= 3.5 and clipping <= 0.005 and median_reduction >= 0.025
        score = median_reduction - 0.7 * max(0, 1 - median_edge)
        summaries[key] = {'median_flat_high_frequency_reduction': median_reduction, 'median_strong_edge_retention': median_edge, 'minimum_strong_edge_retention': min_edge, 'maximum_mean_luma_change_8bit': max_mean_change, 'maximum_clipping_increase': clipping, 'guardrails_passed': passed, 'selection_heuristic_not_quality_score': score}
    eligible = [k for k in PROFILES if summaries[k]['guardrails_passed']]
    if not eligible:
        raise RuntimeError('No candidate passes the structure/artefact guardrails; refusing the full aggressive render')
    selected = max(eligible, key=lambda k: summaries[k]['selection_heuristic_not_quality_score']) if requested == 'auto' else requested
    if selected not in eligible:
        raise RuntimeError(f'Requested profile {selected} fails the sample guardrails')
    return selected, summaries


def prepare(args, ffmpeg):
    expected = args.test_frames or FRAMES
    indexes = SAMPLES if not args.test_frames else (min(expected - 1, 2),)
    wanted = {i for n in indexes for i in range(max(0, n - 2), min(expected, n + 3))}
    frames = {n: base.to_planes(frame) for n, frame in enumerate(base.decode_frames(ffmpeg, args.source, expected)) if n in wanted}
    motion, pairs = base.Motion(), {}
    for n in sorted(frames):
        if n + 1 in frames:
            a, b = motion.pair(frames[n], frames[n + 1])
            pairs[(n, n + 1)] = (a.to_neighbor, b.to_neighbor)
    results = {key: [] for key in PROFILES}
    for n in indexes:
        neighbors = observations(frames, pairs, n)
        for key, p in PROFILES.items():
            item = sample_metrics(frames[n], native(frames[n], neighbors, p))
            item['source_frame_index'] = n
            results[key].append(item)
    selected, summaries = choose_profile(results, args.profile)
    report = {'source_sha256': SOURCE_SHA256, 'test_only': bool(args.test_frames), 'sample_frame_indexes': list(indexes), 'selected_profile': selected, 'selected_parameters': PROFILES[selected], 'candidate_summary': summaries, 'candidate_samples': results, 'limitations': LIMITATIONS}
    (args.output_dir / 'restoration-sample-qa.json').write_text(json.dumps(report, indent=2) + '\n')
    print('SAMPLE GUARDRAILS:', json.dumps(summaries, indent=2), flush=True)
    print('SELECTED PROFILE:', selected, flush=True)
    return report


class Window:
    def __init__(self, ffmpeg, source, expected):
        self.decoder = base.decode_frames(ffmpeg, source, expected)
        self.expected, self.index = expected, 0
        self.frames, self.pairs, self.motion = {}, {}, base.Motion()

    def get(self, n):
        while self.index < min(self.expected, n + 3):
            frame = base.to_planes(next(self.decoder))
            previous = self.frames.get(self.index - 1)
            if previous is not None:
                a, b = self.motion.pair(previous, frame)
                self.pairs[(self.index - 1, self.index)] = (a.to_neighbor, b.to_neighbor)
            self.frames[self.index] = frame
            self.index += 1
        self.frames = {k: f for k, f in self.frames.items() if k >= n - 2}
        self.pairs = {k: f for k, f in self.pairs.items() if k[0] >= n - 2}
        return self.frames[n], observations(self.frames, self.pairs, n)

    def close(self, complete=False):
        try:
            if complete:
                try:
                    next(self.decoder)
                    raise RuntimeError('Unexpected extra source frame')
                except StopIteration:
                    pass
        finally:
            self.decoder.close()


def render(args, ffmpeg):
    qa = json.loads((args.output_dir / 'restoration-sample-qa.json').read_text())
    if qa['source_sha256'] != SOURCE_SHA256 or qa['test_only'] != bool(args.test_frames):
        raise ValueError('Wrong or test-only QA decision')
    p = qa['selected_parameters']
    if p != PROFILES[qa['selected_profile']]:
        raise ValueError('Changed profile parameters')
    if not qa['candidate_summary'][qa['selected_profile']]['guardrails_passed']:
        raise ValueError('The selected profile did not pass the guardrails')
    if not args.test_frames and qa['sample_frame_indexes'] != list(SAMPLES):
        raise ValueError('The full render requires all six source sample windows')
    expected = args.test_frames or FRAMES
    master = args.output_dir / 'restored-lossless.mkv'
    command = [ffmpeg, '-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', '-threads', '1', '-f', 'rawvideo', '-pixel_format', 'bgr24', '-video_size', '960x1272', '-framerate', '30', '-itsoffset', str(base.VIDEO_OFFSET), '-i', 'pipe:0', '-an', '-frames:v', str(expected), '-c:v', 'ffv1', '-level', '3', '-coder', '0', '-context', '0', '-g', '1', '-slices', '4', '-threads:v', '2', '-pix_fmt', 'bgr0', '-fps_mode', 'passthrough', str(master)]
    window = Window(ffmpeg, args.source, expected)
    previous_raw = previous_processed = None
    started, count = time.monotonic(), 0
    changes, confidence_means = [], []
    with (args.output_dir / 'restoration-render.log').open('wb') as log:
        encoder = subprocess.Popen(command, stdin=subprocess.PIPE, stderr=log)
        try:
            for n in range(expected):
                raw, neighbors = window.get(n)
                processed = native(raw, neighbors, p)
                previous = next((obs for distance, obs in neighbors if obs.planes is previous_raw), None)
                if previous is not None:
                    processed = base.stabilize(raw, processed, previous_raw, previous_processed, previous)
                # A second hard bound prevents causal residual stabilization from
                # extending the agreed per-frame cap.
                delta = processed - raw
                delta[:, :, 0] = np.clip(delta[:, :, 0], -p['luma_cap'] / 255, p['luma_cap'] / 255)
                delta[:, :, 1:] = np.clip(delta[:, :, 1:], -p['chroma_cap'] / 255, p['chroma_cap'] / 255)
                processed = raw + delta
                high = base.multiframe(raw, [obs for _, obs in neighbors])
                restored = base.resize(processed)
                restored[:, :, 0] += high[:, :, 0] - base.resize(raw[:, :, 0])
                restored[:, :, 0] = np.clip(restored[:, :, 0], 0, 1)
                encoder.stdin.write(memoryview(np.ascontiguousarray(base.from_planes(restored))).cast('B'))
                changes.append(float(np.mean(np.abs(processed[:, :, 0] - raw[:, :, 0])) * 255))
                confidence_means.append(float(np.mean([o.confidence.mean() for _, o in neighbors])) if neighbors else 0)
                previous_raw, previous_processed = raw, processed
                count += 1
                if count == 1 or count % 30 == 0 or count == expected:
                    elapsed = time.monotonic() - started
                    print(f'RESTORE {count}/{expected}; elapsed={elapsed/60:.1f} min; remaining~{(expected-count)*elapsed/count/60:.1f} min', flush=True)
            window.close(complete=True)
            encoder.stdin.close()
            code = encoder.wait(timeout=120)
            if code or count != expected:
                raise RuntimeError(f'Lossless render failed: exit={code}, frames={count}')
        finally:
            window.close()
            base.terminate(encoder)
    analysis = {'source_sha256': SOURCE_SHA256, 'frames': count, 'test_only': bool(args.test_frames), 'profile': qa['selected_profile'], 'parameters': p, 'temporal_window': 5, 'lossless_intermediate': 'FFV1 RGB, 960x1272, coder 0', 'mean_native_luma_change_8bit': float(np.mean(changes)), 'maximum_frame_mean_luma_change_8bit': max(changes), 'mean_motion_confidence': float(np.mean(confidence_means)), 'render_seconds': time.monotonic()-started, 'render_commit': os.environ.get('GITHUB_SHA'), 'software': {'opencv': cv2.__version__, 'numpy': np.__version__, 'ffmpeg': subprocess.run([ffmpeg, '-version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]}, 'limitations': LIMITATIONS}
    (args.output_dir / 'restoration-analysis.json').write_text(json.dumps(analysis, indent=2) + '\n')


def export(args, ffmpeg):
    expected = args.test_frames or FRAMES
    for key, (width, height, name) in EXPORTS.items():
        if args.preview_only and key != '720p':
            continue
        p = base.profile(width, height, 16 if key == '4k' else (21 if key == '720p' else 18))
        if key == '720p':
            p.update(maxrate='500k', bufsize='1000k')
        command = [ffmpeg, '-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', '-copyts', '-threads', '1', '-filter_threads', '1', '-i', str(args.output_dir / 'restored-lossless.mkv')]
        if args.test_frames:
            command += ['-map', '0:v:0', '-an']
        else:
            command += ['-threads', '1', '-i', str(args.source), '-map', '0:v:0', '-map', '1:a:0', '-c:a', 'copy']
        options = base.encode_args(p, 'fast')
        options[options.index('-threads:v') + 1] = '2'
        command += ['-vf', base.canvas_filter(width, height), '-frames:v', str(expected), *options, '-x264-params', 'cpu-independent=1', '-nostats', '-progress', str(args.output_dir / (name + '.progress.txt')), str(args.output_dir / name)]
        print('EXPORT', key, width, height, flush=True)
        with (args.output_dir / (name + '.encode.log')).open('wb') as log:
            result = subprocess.run(command, stderr=log)
        if result.returncode:
            raise RuntimeError(f'Export {key} failed')


def validate(args, ffmpeg):
    expected = args.test_frames or FRAMES
    qa = json.loads((args.output_dir / 'restoration-sample-qa.json').read_text())
    analysis = json.loads((args.output_dir / 'restoration-analysis.json').read_text())
    aac = None if args.test_frames else audio_hash(ffmpeg, args.source)
    report = {'release_tag': RELEASE_TAG, 'source_sha256': SOURCE_SHA256, 'test_only': bool(args.test_frames), 'frames': expected, 'profile': qa['selected_profile'], 'render_commit': analysis['render_commit'], 'aac_packet_hash': aac, 'outputs': {}, 'limitations': LIMITATIONS}
    for key, (width, height, name) in EXPORTS.items():
        if args.preview_only and key != '720p':
            continue
        item = base.validate_one(ffmpeg, args.output_dir / name, base.profile(width, height), expected, aac)
        item['download_url'] = RELEASE_ROOT + name
        report['outputs'][key] = item
        print('VERIFIED', key, item['size'], item['sha256'], flush=True)
    (args.output_dir / 'restoration-validation.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def notes(args):
    report = json.loads((args.output_dir / 'restoration-validation.json').read_text())
    if report['test_only'] or args.preview_only:
        return
    rows = '\n'.join(f"| {key} | {v['width']}×{v['height']} | {v['size']/1_000_000:.2f} МБ | {v['sha256']} |" for key, v in report['outputs'].items())
    text = f'''# Более сильная очистка — без обещания «в 100 раз»

Предоставленный исходник 480×636. Выбранный по шести фрагментам профиль: **{report['profile']}**.
Окно до пяти настоящих кадров: текущий и до четырёх соседних. Проверка движения
в обе стороны, отбрасывание перекрытий, нелокальное шумоподавление, мягкая коррекция
блоков и цветных пятен, guided filter,
ограниченная коррекция размытия и резкости и стабилизация только добавленной разницы.
Цветовой баланс и экспозиция автоматически не менялись. Нейросетевая генерация не применялась.

Сильная очистка может смягчать настоящую текстуру. Метрики тестовых фрагментов — только
ограничения безопасности обработки, не измерение общего качества. Улучшение «в 100 раз»
не определено и не подтверждено. 4K — выходной холст, не исходная детализация съёмки.
Недостающая анатомия, новые действия, продолжение сцены и промежуточные кадры не создавались.

Во всех MP4 — 1518 кадров, полный исходный кадр с чёрными полями 9:16, приблизительно
исходные видеотаймкоды на сетке 30 кадров/с (допуск менее 1 мс), побитово исходные AAC-пакеты.
Каждый файл проверен полным строгим декодированием видео и звука, AVC profile/level,
BT.709, fast-start, таймкодами и хешами. Старые версии не заменялись.
Все три экспорта сделаны из FFV1 lossless-мастера, а не из уменьшенного сжатого MP4.
Для просмотра при задержках выбирайте 720p; плавность на вашем устройстве не измерена.

| Версия | Размер кадра | Размер файла | SHA-256 |
|---|---|---|---|
{rows}
'''
    (args.output_dir / 'RESTORATION-README-RU.md').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--profile', choices=('auto', *PROFILES), default='auto')
    parser.add_argument('--stage', choices=('all', 'prepare', 'render', 'exports', 'validate', 'notes'), default='all')
    parser.add_argument('--test-frames', type=int, default=0)
    parser.add_argument('--preview-only', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.test_frames <= FRAMES:
        parser.error('Invalid test-frame count')
    if sha256(args.source) != SOURCE_SHA256:
        parser.error('The input is not the verified original')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    cv2.setNumThreads(1)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    if args.stage in ('all', 'prepare'):
        prepare(args, ffmpeg)
    if args.stage in ('all', 'render'):
        render(args, ffmpeg)
    if args.stage in ('all', 'exports'):
        export(args, ffmpeg)
    if args.stage in ('all', 'validate'):
        validate(args, ffmpeg)
    if args.stage in ('all', 'notes'):
        notes(args)
    print('RESTORATION STAGE COMPLETE:', args.stage, flush=True)


if __name__ == '__main__':
    main()
