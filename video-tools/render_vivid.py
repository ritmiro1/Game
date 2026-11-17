#!/usr/bin/env python3
"""Clean the verified original and export a fixed grade selected by vivid_search."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
from types import SimpleNamespace

import cv2
import imageio_ffmpeg
import numpy as np

import enhance_variants as base
from optimize_playback import audio_hash, sha256
import restore_strong as cleanup
import vivid_search as vivid

EXPORTS = {
    '720p': (720, 1280, 'armorypass-ejoy-vivid-watch-720p.mp4'),
    '1080p': (1080, 1920, 'armorypass-ejoy-vivid-1080p.mp4'),
    '4k': (2160, 3840, 'armorypass-ejoy-vivid-4k.mp4'),
}


def read_search(folder, allow_test=False):
    folder = Path(folder)
    search = json.loads((folder/'vivid-search.json').read_text())
    if search['source_sha256'] != base.SOURCE_SHA256 or search['release_tag'] != vivid.RELEASE_TAG:
        raise ValueError('Unrelated color selection')
    if search['test_only'] and not allow_test:
        raise ValueError('A test selection cannot be used for full delivery')
    if not search['test_only'] and (search['actual_evaluated_candidates'] != vivid.CANDIDATES or search['unique_parameter_combinations'] != vivid.CANDIDATES or search['sample_frame_indexes'] != list(vivid.SAMPLES)):
        raise ValueError('A full delivery requires all 120,000 actual evaluations and twelve windows')
    selected = search['selected']
    vivid.parameter_row(selected['parameters'])
    if not selected['native_guardrails_passed'] or len(selected['native_sample_metrics']) != len(search['sample_frame_indexes']):
        raise ValueError('Missing native-size guardrails')
    values = np.asarray([[item[name] for name in vivid.METRICS] for item in selected['native_sample_metrics']], np.float32)
    if not vivid.passes(values[None])[0]:
        raise ValueError('The selected grade fails its numerical guardrails')
    if search['archive']['file'] != vivid.ARCHIVE or sha256(folder/vivid.ARCHIVE) != search['archive']['sha256']:
        raise ValueError('The actual candidate evidence changed')
    lut = folder/'vivid-grade.cube'
    if not lut.exists():
        vivid.write_lut(lut, selected['parameters'])
    if sha256(lut) != search['lut']['sha256']:
        raise ValueError('The selected LUT changed')
    return search


def render_cleanup(args, ffmpeg):
    search = read_search(args.output_dir, bool(args.test_frames or args.test_candidates))
    folder = args.output_dir/'cleanup'
    folder.mkdir(parents=True, exist_ok=True)
    options = SimpleNamespace(source=args.source, output_dir=folder, profile='strong', test_frames=args.test_frames, preview_only=False)
    cleanup.prepare(options, ffmpeg)
    cleanup.render(options, ffmpeg)
    analysis = json.loads((folder/'restoration-analysis.json').read_text())
    qa = json.loads((folder/'restoration-sample-qa.json').read_text())
    record = {'source_sha256': base.SOURCE_SHA256, 'frames': analysis['frames'], 'test_only': bool(args.test_frames or search['test_only']), 'render_commit': os.environ.get('GITHUB_SHA'), 'cleanup': analysis, 'cleanup_sample_guardrails': qa, 'color_selection_sha256': sha256(args.output_dir/'vivid-search.json'), 'selected_parameters': search['selected']['parameters'], 'lut_sha256': search['lut']['sha256'], 'color_application': 'One fixed 65x65x65 LUT with tetrahedral interpolation at export, before scaling/padding, directly from the lossless cleanup master', 'limitations': vivid.LIMITATIONS}
    (args.output_dir/'vivid-analysis.json').write_text(json.dumps(record, indent=2)+'\n')


def export_command(args, ffmpeg, key):
    width, height, name = EXPORTS[key]
    profile = base.profile(width, height, 16 if key == '4k' else (21 if key == '720p' else 18))
    if key == '720p':
        profile.update(maxrate='600k', bufsize='1200k')
    command = [ffmpeg, '-hide_banner', '-loglevel', 'warning', '-nostdin', '-y', '-copyts', '-threads', '1', '-filter_threads', '1', '-i', str(args.output_dir/'cleanup/restored-lossless.mkv')]
    if args.test_frames:
        command += ['-map', '0:v:0', '-an']
    else:
        command += ['-threads', '1', '-i', str(args.source), '-map', '0:v:0', '-map', '1:a:0', '-c:a', 'copy']
    # These paths are locally generated, not strings from media metadata.
    lut = str((args.output_dir/'vivid-grade.cube').resolve())
    if any(char in lut for char in "':,\\\n\r"):
        raise ValueError('Unsafe FFmpeg filter path')
    color_filter = f"lut3d=file='{lut}':interp=tetrahedral,"
    options = base.encode_args(profile, 'fast')
    options[options.index('-threads:v')+1] = '2'
    return command+['-vf', color_filter+base.canvas_filter(width, height), '-frames:v', str(args.test_frames or base.FRAMES), *options, '-x264-params', 'cpu-independent=1', '-nostats', '-progress', str(args.output_dir/(name+'.progress.txt')), str(args.output_dir/name)]


def export(args, ffmpeg):
    search = read_search(args.output_dir, bool(args.test_frames or args.test_candidates))
    analysis = json.loads((args.output_dir/'vivid-analysis.json').read_text())
    if analysis['color_selection_sha256'] != sha256(args.output_dir/'vivid-search.json') or analysis['selected_parameters'] != search['selected']['parameters']:
        raise ValueError('Selection changed after cleanup')
    for key in EXPORTS:
        if args.preview_only and key != '720p':
            continue
        print('VIVID EXPORT', key, flush=True)
        name = EXPORTS[key][2]
        with (args.output_dir/(name+'.encode.log')).open('wb') as log:
            result = subprocess.run(export_command(args, ffmpeg, key), stderr=log)
        if result.returncode:
            raise RuntimeError(f'Vivid export failed: {key}')


def validate(args, ffmpeg):
    search = read_search(args.output_dir, bool(args.test_frames or args.test_candidates))
    analysis = json.loads((args.output_dir/'vivid-analysis.json').read_text())
    if analysis['color_selection_sha256'] != sha256(args.output_dir/'vivid-search.json') or analysis['selected_parameters'] != search['selected']['parameters'] or analysis['lut_sha256'] != search['lut']['sha256']:
        raise ValueError('Inconsistent color provenance')
    expected = args.test_frames or base.FRAMES
    if analysis['frames'] != expected or analysis['cleanup']['frames'] != expected:
        raise ValueError('Incomplete lossless render')
    aac = None if args.test_frames else audio_hash(ffmpeg, args.source)
    report = {'release_tag': vivid.RELEASE_TAG, 'source_sha256': base.SOURCE_SHA256, 'frames': expected, 'test_only': bool(args.test_frames or search['test_only']), 'render_commit': analysis['render_commit'], 'color_selection_sha256': analysis['color_selection_sha256'], 'actual_evaluated_candidates': search['actual_evaluated_candidates'], 'selected_parameters': search['selected']['parameters'], 'lut_sha256': search['lut']['sha256'], 'aac_packet_hash': aac, 'outputs': {}, 'limitations': vivid.LIMITATIONS}
    for key, (width, height, name) in EXPORTS.items():
        if args.preview_only and key != '720p':
            continue
        item = base.validate_one(ffmpeg, args.output_dir/name, base.profile(width, height), expected, aac)
        item['download_url'] = vivid.RELEASE_ROOT+name
        report['outputs'][key] = item
        print('VIVID VERIFIED', key, item['size'], item['sha256'], flush=True)
    (args.output_dir/'vivid-validation.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def notes(args):
    report = json.loads((args.output_dir/'vivid-validation.json').read_text())
    search = json.loads((args.output_dir/'vivid-search.json').read_text())
    if report['test_only'] or args.preview_only:
        return
    rows = '\n'.join(f"| {key} | {item['width']}×{item['height']} | {item['size']/1_000_000:.2f} МБ | {item['sha256']} |" for key,item in report['outputs'].items())
    params = '\n'.join(f'- `{name}`: {value:.7f}' for name,value in report['selected_parameters'].items())
    text = f'''# Сочный SDR-цвет: 120 000 проверенных настроек

Выполнено **{search['actual_evaluated_candidates']} различных численных тестов настроек**
на 12 сетках 16×21 настоящих пикселей из разных частей исходного ролика.
Это не 120 000 полных рендеров и не человеческих визуальных оценок.
{search['native_finalists_evaluated']} финалиста повторно проверены на 12 очищенных кадрах
в исходном размере 480×636. Выбран один неизменный профиль на весь ролик:
более насыщенные цвета, контраст, сдержанное тепло, защита тёплых оттенков и
границ яркости. Цветность сжимается в доступный диапазон без жёсткого обрезания RGB.

Критерий выбора — заданные пожелания к цветности/контрасту и численные ограничения,
не объективная оценка красоты, не гарантия лучшего из всех возможных результатов.
Просмотр всего видео человеком и скорость на устройстве пользователя не подтверждены.

Параметры:
{params}

Обработка выполнена заново из предоставленного 480×636 оригинала, с прежней
пятикадровой очисткой `strong`, не из сжатого 720p/4K. Три экспорта независимы
и сделаны из FFV1 lossless-мастера через постоянный 65³ LUT, а не друг из друга.
Полный исходный кадр, пропорции, 1518 кадров и исходные AAC-пакеты сохранены.
Чёрные поля 9:16; таймкоды нормализованы к 30 кадрам/с с исходным смещением,
проверены с допуском менее 1 мс. Полное декодирование всех MP4 пройдено.

4K — размер выходного холста, не восстановленная нативная детализация.
Генерации новых деталей, анатомии, действий, промежуточных кадров и HDR нет.
Более сильная очистка может смягчать настоящую текстуру. Старые версии не заменены.
Для менее требовательного просмотра выбирайте 720p.

Архив `{vivid.ARCHIVE}` содержит все реально вычисленные строки параметров,
метрики по 12 образцам, флаги ограничений и баллы эвристики. Используется NumPy NPZ,
без pickle; его SHA-256 записан в `vivid-search.json`. Подробности формул — в коде.

| Версия | Размер кадра | Размер файла | SHA-256 |
| --- | --- | --- | --- |
{rows}
'''
    (args.output_dir/'VIVID-README-RU.md').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--stage', choices=('all','search','cleanup','exports','validate','notes'), default='all')
    parser.add_argument('--test-frames', type=int, default=0)
    parser.add_argument('--test-candidates', type=int, default=0)
    parser.add_argument('--preview-only', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.test_frames <= base.FRAMES or not 0 <= args.test_candidates <= vivid.CANDIDATES:
        parser.error('Invalid test size')
    if sha256(args.source) != base.SOURCE_SHA256:
        parser.error('The input is not the verified original')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    cv2.setNumThreads(1)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    if args.stage in ('all','search'):
        vivid.search(args.source, args.output_dir, test_frames=args.test_frames, test_candidates=args.test_candidates)
    if args.stage in ('all','cleanup'):
        render_cleanup(args, ffmpeg)
    if args.stage in ('all','exports'):
        export(args, ffmpeg)
    if args.stage in ('all','validate'):
        validate(args, ffmpeg)
    if args.stage in ('all','notes'):
        notes(args)
    print('VIVID STAGE COMPLETE:', args.stage, flush=True)


if __name__ == '__main__':
    main()
