#!/usr/bin/env python3
"""Actually evaluate 120,000 distinct global SDR grades on source-frame proxies.

This is a bounded preference heuristic, not 120,000 video renders, subjective
reviews, a learned quality model, or recovery of missing detail. The shortlist
is re-evaluated on the same cleaned frames at their native size. One fixed
grade is then used throughout the video, avoiding per-frame color pumping.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import itertools
import json
import os
from pathlib import Path
import time

os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ.setdefault('OMP_NUM_THREADS', '2')

import cv2
import imageio_ffmpeg
import numpy as np

import enhance_variants as base
from optimize_playback import sha256
import restore_strong as cleanup

RELEASE_TAG = 'video-vivid-120k-20261110-cc989019'
RELEASE_ROOT = f'https://github.com/ritmiro1/Game/releases/download/{RELEASE_TAG}/'
SAMPLES = (30, 165, 300, 435, 570, 705, 840, 975, 1110, 1245, 1380, 1485)
PROXY_SIZE = (16, 21)  # A stratified point grid, not averaged colors or a full render.
NAMES = ('contrast', 'saturation', 'vibrance', 'exposure_stops', 'warmth', 'highlight_protection')
GRID = {
    'contrast': np.linspace(1.04, 1.26, 10).tolist(),
    'saturation': np.linspace(1.10, 1.55, 10).tolist(),
    'vibrance': np.linspace(0.00, 0.35, 10).tolist(),
    'exposure_stops': np.linspace(-0.04, 0.16, 8).tolist(),
    'warmth': np.linspace(-0.004, 0.012, 5).tolist(),
    'highlight_protection': np.linspace(0.15, 0.55, 3).tolist(),
}
CANDIDATES = 120_000
ARCHIVE = 'vivid-120000-candidates.npz'
METRICS = ('chroma_rms_ratio', 'luma_std_ratio', 'mean_luma_change_8bit', 'mean_abs_luma_change_8bit', 'shadow_loss_fraction', 'highlight_loss_fraction', 'gamut_compression_fraction', 'mean_gamut_compression')
GUARDS = {'minimum_frame_chroma_ratio': 1.08, 'maximum_frame_chroma_ratio': 1.55, 'minimum_frame_contrast_ratio': 1.015, 'maximum_frame_contrast_ratio': 1.25, 'minimum_frame_mean_luma_change_8bit': -2.5, 'maximum_frame_mean_luma_change_8bit': 8.0, 'maximum_frame_mean_abs_luma_change_8bit': 12.0, 'maximum_new_shadow_or_highlight_fraction': 0.002, 'maximum_frame_gamut_compression_fraction': 0.30, 'maximum_frame_mean_gamut_compression': 0.035}
LIMITATIONS = [
    '120,000 attempts means distinct parameter configurations evaluated on twelve fixed 16x21 stratified source-pixel grids, not 120,000 complete video encodes or human visual reviews.',
    'The shortlist is numerically rechecked on twelve cleaned native-size frames. Sampling can miss artifacts elsewhere.',
    'Vividness is subjective. The ranking targets chosen relative chroma and contrast; it is not an objective perceptual quality score or proof of the best possible look.',
    'Only the supplied 480x636 original is used. Cleanup can soften texture; 4K is an output canvas, not authentic recovered 4K detail.',
    'A single constant SDR color transform adds saturation, contrast and bounded warmth. It does not create HDR or new scene detail, anatomy, actions, or intermediate frames.',
    'The original full picture, 1518 frames and AAC packets are retained. Tiny original timestamp jitter is normalized to 30 fps within one millisecond.',
    'Use 720p for less demanding playback. Performance on the user device is unmeasured.',
]


@dataclass
class Context:
    rgb: np.ndarray
    y: np.ndarray
    chroma: np.ndarray
    mid: np.ndarray
    warm: np.ndarray
    vibrance_weight: np.ndarray
    chroma_rms: np.ndarray
    luma_std: np.ndarray

    @classmethod
    def make(cls, rgb):
        """F,N,3 RGB samples in full-range gamma-coded BT.709 coordinates."""
        rgb = np.asarray(rgb, np.float32)
        if rgb.ndim != 3 or rgb.shape[-1] != 3 or not np.isfinite(rgb).all() or rgb.min() < 0 or rgb.max() > 1:
            raise ValueError('Expected finite F,N,3 RGB in [0,1]')
        y = rgb @ np.array([0.2126, 0.7152, 0.0722], np.float32)
        chroma = rgb - y[..., None]
        warm = np.clip((rgb[..., 0] - rgb[..., 2]) / 0.12, 0, 1) * np.clip((rgb[..., 0] - rgb[..., 1]) / 0.06, 0, 1)
        weight = np.clip(1 - 2 * np.ptp(rgb, axis=-1), 0, 1)
        return cls(rgb, y, chroma, 4*y*(1-y), warm, weight, np.sqrt(np.mean(np.sum(chroma**2, axis=-1), axis=-1)), np.std(y, axis=-1))


def candidate_parameters():
    values = np.asarray(list(itertools.product(*(GRID[name] for name in NAMES))), np.float32)
    if values.shape != (CANDIDATES, len(NAMES)):
        raise ValueError('The declared search count differs from the actual grid')
    return values


def parameter_dict(row):
    return {name: float(value) for name, value in zip(NAMES, row, strict=True)}


def parameter_row(parameters):
    if set(parameters) != set(NAMES):
        raise ValueError('Unexpected grade parameters')
    row = np.asarray([parameters[name] for name in NAMES], np.float32)
    if not np.isfinite(row).all() or any(not min(GRID[k])-1e-6 <= v <= max(GRID[k])+1e-6 for k, v in zip(NAMES, row, strict=True)):
        raise ValueError('Grade parameters outside the searched safe domain')
    return row


def transform(context, parameters):
    """Return B,F,N tone, hue-preserving compressed chroma and compression factor."""
    p = np.asarray(parameters, np.float32)
    if p.ndim != 2 or p.shape[1] != len(NAMES) or not np.isfinite(p).all():
        raise ValueError('Expected a finite parameter batch')
    contrast, saturation, vibrance, exposure, warmth, protection = [p[:, i, None, None] for i in range(len(NAMES))]
    y, mid = context.y[None], context.mid[None]
    shoulder = 1 - protection * np.clip((y - 0.5) * 2, 0, 1)
    tone = y + (contrast-1)*(y-0.5)*mid*shoulder + (np.exp2(exposure)-1)*y*(1-y)
    tone = np.clip(tone, 0, 1)
    gain = 1 + (saturation-1 + 0.6*vibrance*context.vibrance_weight[None]) * (1-0.45*context.warm[None])
    chroma = context.chroma[None]*gain[..., None]
    # A luma-neutral, deliberately small warm shift; black and white stay fixed.
    warm_vector = np.array([1, -(0.2126-0.0722)/0.7152, -1], np.float32)
    chroma += (warmth*mid)[..., None] * warm_vector
    positive = np.max(chroma, axis=-1)
    negative = -np.min(chroma, axis=-1)
    limit = np.minimum((1-tone)/np.maximum(positive, 1e-8), tone/np.maximum(negative, 1e-8))
    scale = np.minimum(1, np.maximum(0, limit)*0.9995)
    chroma *= scale[..., None]
    return tone, chroma, scale


def apply_grade(rgb, parameters):
    array = np.asarray(rgb, np.float32)
    shape = array.shape
    if not shape or shape[-1] != 3:
        raise ValueError('Expected RGB')
    context = Context.make(array.reshape(1, -1, 3))
    tone, chroma, _ = transform(context, parameter_row(parameters)[None])
    return np.clip((tone[..., None]+chroma)[0].reshape(shape), 0, 1)


def evaluate(context, parameters):
    tone, chroma, scale = transform(context, parameters)
    y = context.y[None]
    values = np.stack((
        np.sqrt(np.mean(np.sum(chroma**2, axis=-1), axis=-1)) / np.maximum(context.chroma_rms[None], 0.005),
        np.std(tone, axis=-1) / np.maximum(context.luma_std[None], 0.01),
        np.mean(tone-y, axis=-1)*255,
        np.mean(np.abs(tone-y), axis=-1)*255,
        np.mean((y > 4/255) & (tone <= 4/255), axis=-1),
        np.mean((y < 251/255) & (tone >= 251/255), axis=-1),
        np.mean((scale < 0.998) & (np.max(np.abs(context.chroma), axis=-1)[None] > 0.001), axis=-1),
        np.mean((1-scale)*(np.max(np.abs(context.chroma), axis=-1)[None] > 0.001), axis=-1),
    ), axis=-1).astype(np.float32)  # B,F,M
    eligible = passes(values)
    average = values.mean(axis=1)
    score = -((average[:, 0]-1.27)/0.10)**2 - ((average[:, 1]-1.10)/0.06)**2
    score -= 0.30*((average[:, 2]-1.5)/4.0)**2 + 4*average[:, 7]
    score -= 0.15*((parameters[:, NAMES.index('warmth')]-0.004)/0.008)**2
    if not np.isfinite(values).all() or not np.isfinite(score).all():
        raise RuntimeError('Non-finite candidate evaluation')
    return values, score, eligible


def passes(values):
    g = GUARDS
    per_frame = ((values[..., 0] >= g['minimum_frame_chroma_ratio']) & (values[..., 0] <= g['maximum_frame_chroma_ratio'])
        & (values[..., 1] >= g['minimum_frame_contrast_ratio']) & (values[..., 1] <= g['maximum_frame_contrast_ratio'])
        & (values[..., 2] >= g['minimum_frame_mean_luma_change_8bit']) & (values[..., 2] <= g['maximum_frame_mean_luma_change_8bit'])
        & (values[..., 3] <= g['maximum_frame_mean_abs_luma_change_8bit'])
        & (values[..., 4] <= g['maximum_new_shadow_or_highlight_fraction']) & (values[..., 5] <= g['maximum_new_shadow_or_highlight_fraction'])
        & (values[..., 6] <= g['maximum_frame_gamut_compression_fraction']) & (values[..., 7] <= g['maximum_frame_mean_gamut_compression']))
    return np.isfinite(values).all(axis=(1,2)) & per_frame.all(axis=1)


def collect_frames(source, ffmpeg, test_frames=0):
    expected = test_frames or base.FRAMES
    indexes = SAMPLES if not test_frames else (min(expected-1, 2),)
    wanted = {i for n in indexes for i in range(max(0, n-2), min(expected, n+3))}
    frames = {n: base.to_planes(frame) for n, frame in enumerate(base.decode_frames(ffmpeg, source, expected)) if n in wanted}
    motion, pairs = base.Motion(), {}
    for n in sorted(frames):
        if n+1 in frames:
            a, b = motion.pair(frames[n], frames[n+1])
            pairs[(n,n+1)] = (a.to_neighbor, b.to_neighbor)
    result = []
    for n in indexes:
        cleaned = cleanup.native(frames[n], cleanup.observations(frames, pairs, n), cleanup.PROFILES['strong'])
        result.append(np.ascontiguousarray(base.from_planes(cleaned)[..., ::-1]))
    return indexes, result


def write_lut(path, parameters, size=65):
    if size not in (33, 65):
        raise ValueError('Unsupported LUT size')
    axis = np.linspace(0, 1, size, dtype=np.float32)
    blue, green, red = np.meshgrid(axis, axis, axis, indexing='ij')
    rgb = np.stack((red, green, blue), axis=-1)
    output = apply_grade(rgb, parameters).reshape(-1, 3)
    with Path(path).open('w') as stream:
        stream.write(f'TITLE "Fixed vivid grade; no generative detail"\nLUT_3D_SIZE {size}\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n')
        np.savetxt(stream, output, fmt='%.7f')


def check_selected_on_full_frames(frames, parameters):
    metrics = []
    for frame in frames:
        context = Context.make(frame.reshape(1, -1, 3).astype(np.float32)/255)
        values, _, _ = evaluate(context, parameter_row(parameters)[None])
        metrics.append(values[0,0])
    return np.asarray(metrics, np.float32)


def stratified_proxy(frame):
    height, width = frame.shape[:2]
    xs = np.minimum(width-1, ((np.arange(PROXY_SIZE[0])+.5)*width/PROXY_SIZE[0]).astype(int))
    ys = np.minimum(height-1, ((np.arange(PROXY_SIZE[1])+.5)*height/PROXY_SIZE[1]).astype(int))
    return frame[ys[:, None], xs[None, :]].reshape(-1, 3)


def search(source, folder, *, test_frames=0, test_candidates=0, shortlist=48, batch_size=64):
    if sha256(source) != base.SOURCE_SHA256:
        raise ValueError('The input is not the verified original')
    if not 0 <= test_frames <= base.FRAMES or test_candidates < 0 or test_candidates > CANDIDATES or batch_size < 1 or shortlist < 1:
        raise ValueError('Invalid search arguments')
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    cv2.setNumThreads(1)
    indexes, frames = collect_frames(source, imageio_ffmpeg.get_ffmpeg_exe(), test_frames)
    proxies = np.stack([stratified_proxy(frame) for frame in frames]).astype(np.float32)/255
    context = Context.make(proxies)
    parameters = candidate_parameters()
    if test_candidates:
        parameters = parameters[np.linspace(0, len(parameters)-1, test_candidates, dtype=int)]
    count = len(parameters)
    results = np.empty((count, len(indexes), len(METRICS)), np.float32)
    scores = np.empty(count, np.float32)
    eligible = np.empty(count, bool)
    evaluated = 0
    for start in range(0, count, batch_size):
        stop = min(count, start+batch_size)
        values, score, passed = evaluate(context, parameters[start:stop])
        results[start:stop], scores[start:stop], eligible[start:stop] = values, score, passed
        evaluated += stop-start
        if evaluated == count or evaluated % (batch_size*128) == 0:
            print(f'VIVID ACTUAL EVALUATIONS {evaluated}/{count}; eligible={int(eligible[:evaluated].sum())}; elapsed={(time.monotonic()-started)/60:.1f} min', flush=True)
    if evaluated != count:
        raise RuntimeError('Incomplete candidate evaluation')
    rows = np.flatnonzero(eligible)
    if not len(rows):
        raise RuntimeError('No candidate passes the proxy guardrails')
    # Preserve all actual evaluations even if native-size checks later reject
    # every finalist. This archive is evidence, never itself a pass decision.
    archive = folder / ARCHIVE
    np.savez_compressed(archive, schema_version=np.array(1, np.int32), parameters=parameters, parameter_names=np.array(NAMES), metrics=results, metric_names=np.array(METRICS), eligible=eligible, preference_scores=scores, source_frame_indexes=np.array(indexes, np.int32))
    # Avoid spending the native-size budget on almost identical settings that
    # differ only in warmth or shoulder. Take the best per contrast/color cell.
    ranked, cells = [], set()
    for row in rows[np.argsort(-scores[rows], kind='stable')]:
        cell = tuple(parameters[row, :2])
        if cell not in cells:
            cells.add(cell)
            ranked.append(row)
        if len(ranked) == shortlist:
            break
    finalists = []
    winner = None
    for row in ranked:
        params = parameter_dict(parameters[row])
        native_metrics = check_selected_on_full_frames(frames, params)
        passed = bool(passes(native_metrics[None])[0])
        # Same stated objective, now evaluated at native source resolution.
        average = native_metrics.mean(axis=0)
        score = -((float(average[0])-1.27)/.10)**2 - ((float(average[1])-1.10)/.06)**2 - .30*((float(average[2])-1.5)/4)**2 - 4*float(average[7]) - .15*((params['warmth']-.004)/.008)**2
        item = {'candidate_row': int(row), 'parameters': params, 'native_guardrails_passed': passed, 'native_preference_heuristic_not_quality_score': score}
        finalists.append(item)
        if passed and (winner is None or score > winner['native_preference_heuristic_not_quality_score']):
            winner = {**item, 'native_sample_metrics': [{**dict(zip(METRICS, map(float, values), strict=True)), 'source_frame_index': int(n)} for n, values in zip(indexes, native_metrics, strict=True)]}
    if winner is None:
        raise RuntimeError('No finalist passes the native-size sample guardrails')
    write_lut(folder/'vivid-grade.cube', winner['parameters'])
    report = {'schema_version': 1, 'release_tag': RELEASE_TAG, 'source_sha256': base.SOURCE_SHA256, 'test_only': bool(test_frames or test_candidates), 'search_commit': os.environ.get('GITHUB_SHA'), 'cleanup_profile': 'strong', 'sample_frame_indexes': list(indexes), 'sample_size_native': [base.SOURCE_WIDTH, base.SOURCE_HEIGHT], 'sample_fingerprints_rgb_sha256': [hashlib.sha256(frame.tobytes()).hexdigest() for frame in frames], 'coarse_proxy_width_height': list(PROXY_SIZE), 'coarse_sampling_method': 'Fixed stratified pixel centers without averaging', 'parameter_names': list(NAMES), 'parameter_grid': GRID, 'actual_evaluated_candidates': evaluated, 'unique_parameter_combinations': int(len(np.unique(parameters, axis=0))), 'coarse_guardrail_pass_count': int(eligible.sum()), 'native_finalists_evaluated': len(finalists), 'native_guardrail_pass_count': sum(item['native_guardrails_passed'] for item in finalists), 'guardrails': GUARDS, 'ranking_targets': {'chroma_rms_ratio': 1.27, 'luma_std_ratio': 1.10, 'mean_luma_change_8bit': 1.5, 'warmth': .004}, 'selected': winner, 'finalists': finalists, 'archive': {'file': ARCHIVE, 'size': archive.stat().st_size, 'sha256': sha256(archive)}, 'lut': {'file': 'vivid-grade.cube', 'grid_size': 65, 'sha256': sha256(folder/'vivid-grade.cube'), 'interpolation': 'tetrahedral'}, 'elapsed_seconds': time.monotonic()-started, 'limitations': LIMITATIONS}
    (folder/'vivid-search.json').write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    print('VIVID SELECTED:', json.dumps(winner, indent=2), flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--test-frames', type=int, default=0)
    parser.add_argument('--test-candidates', type=int, default=0)
    args = parser.parse_args()
    search(args.source, args.output_dir, test_frames=args.test_frames, test_candidates=args.test_candidates)


if __name__ == '__main__':
    main()
