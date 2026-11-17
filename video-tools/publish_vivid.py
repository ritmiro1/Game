#!/usr/bin/env python3
"""Publish a separate vivid-color release only with a complete audited bundle."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

import numpy as np

import enhance_variants as base
from optimize_playback import sha256
from publish_restoration import api, find_release, run_gh, verify_assets
from render_vivid import EXPORTS, read_search
import restore_strong as cleanup
import vivid_search as vivid

EXTRAS = ('vivid-validation.json', 'vivid-search.json', 'vivid-analysis.json', 'VIVID-README-RU.md', vivid.ARCHIVE)
ORIGINAL_AAC_HASH = 'SHA256=aa1dbf7102e43cf154060bff7114103b92245d4054034639ce85690b91396b0f'


def check_archive(folder, search):
    path = folder/vivid.ARCHIVE
    if path.is_symlink() or path.stat().st_size != search['archive']['size'] or sha256(path) != search['archive']['sha256']:
        raise ValueError('The candidate archive changed')
    with np.load(path, allow_pickle=False) as data:
        parameters, metrics = data['parameters'], data['metrics']
        if parameters.shape != (vivid.CANDIDATES, len(vivid.NAMES)) or not np.array_equal(parameters, vivid.candidate_parameters()):
            raise ValueError('The archive does not contain the complete distinct parameter grid')
        if list(data['parameter_names']) != list(vivid.NAMES) or list(data['metric_names']) != list(vivid.METRICS) or list(data['source_frame_indexes']) != list(vivid.SAMPLES):
            raise ValueError('Candidate evidence has different fields or samples')
        if metrics.shape != (vivid.CANDIDATES, len(vivid.SAMPLES), len(vivid.METRICS)) or not np.isfinite(metrics).all():
            raise ValueError('Missing or non-finite actual evaluations')
        flags = vivid.passes(metrics)
        if not np.array_equal(data['eligible'], flags) or int(flags.sum()) != search['coarse_guardrail_pass_count']:
            raise ValueError('Candidate pass flags do not agree with their metrics')
        scores = data['preference_scores']
        if scores.shape != (vivid.CANDIDATES,) or not np.isfinite(scores).all():
            raise ValueError('Invalid preference scores')
        row = search['selected']['candidate_row']
        if not isinstance(row, int) or not 0 <= row < vivid.CANDIDATES or not flags[row] or vivid.parameter_dict(parameters[row]) != search['selected']['parameters']:
            raise ValueError('The selected candidate is not in the actual evaluated grid')


def bundle_files(folder):
    folder = Path(folder)
    report = json.loads((folder/'vivid-validation.json').read_text())
    if report.get('test_only') or report.get('source_sha256') != base.SOURCE_SHA256 or report.get('frames') != base.FRAMES or report.get('actual_evaluated_candidates') != vivid.CANDIDATES:
        raise ValueError('Only a full validated original-source result with 120,000 evaluations can be published')
    if report.get('release_tag') != vivid.RELEASE_TAG or set(report['outputs']) != set(EXPORTS) or report.get('aac_packet_hash') != ORIGINAL_AAC_HASH:
        raise ValueError('Unexpected release, audio, or incomplete output set')
    files = []
    for key, (width, height, name) in EXPORTS.items():
        item = report['outputs'][key]
        if item['file'] != name or (item['width'], item['height']) != (width, height) or item['frames'] != base.FRAMES:
            raise ValueError('Unexpected output geometry or frame count')
        if not all(item.get(k) for k in ('full_decode_passed', 'aac_unchanged', 'bt709', 'faststart')):
            raise ValueError('Incomplete output verification')
        path = folder/name
        if path.is_symlink() or path.stat().st_size != item['size'] or sha256(path) != item['sha256']:
            raise ValueError('The validated output changed')
        files.append(path)
    for name in EXTRAS:
        path = folder/name
        if not path.is_file() or path.is_symlink():
            raise ValueError('Missing or unsafe evidence file')
        files.append(path)
    search = read_search(folder)
    analysis = json.loads((folder/'vivid-analysis.json').read_text())
    if analysis['test_only'] or analysis['frames'] != base.FRAMES or analysis['source_sha256'] != base.SOURCE_SHA256:
        raise ValueError('Test or incomplete render provenance')
    if not report['render_commit'] or analysis['render_commit'] != report['render_commit']:
        raise ValueError('Missing or different render revision')
    if analysis['cleanup']['render_commit'] != report['render_commit'] or search['search_commit'] != report['render_commit']:
        raise ValueError('The search and render code revisions differ')
    if analysis['selected_parameters'] != report['selected_parameters'] or report['selected_parameters'] != search['selected']['parameters']:
        raise ValueError('The selected grade changed')
    if analysis['color_selection_sha256'] != report['color_selection_sha256'] or report['color_selection_sha256'] != sha256(folder/'vivid-search.json') or analysis['lut_sha256'] != search['lut']['sha256'] or report['lut_sha256'] != search['lut']['sha256']:
        raise ValueError('The search or LUT changed after rendering')
    qa, native = analysis['cleanup_sample_guardrails'], analysis['cleanup']
    if qa['test_only'] or qa['source_sha256'] != base.SOURCE_SHA256 or qa['selected_profile'] != 'strong' or qa['selected_parameters'] != cleanup.PROFILES['strong'] or qa['sample_frame_indexes'] != list(cleanup.SAMPLES) or not qa['candidate_summary']['strong']['guardrails_passed']:
        raise ValueError('The cleanup did not pass its complete source-sample checks')
    if native['test_only'] or native['frames'] != base.FRAMES or native['source_sha256'] != base.SOURCE_SHA256 or native['parameters'] != cleanup.PROFILES['strong']:
        raise ValueError('The original-source cleanup provenance is inconsistent')
    check_archive(folder, search)
    return files, report


def publish(folder, repo, target, existing_release_id=None):
    folder = Path(folder)
    files, report = bundle_files(folder)
    expected = {path.name: {'size': path.stat().st_size, 'sha256': sha256(path)} for path in files}
    if existing_release_id is not None:
        if not isinstance(existing_release_id, int) or existing_release_id <= 0:
            raise ValueError('Invalid explicitly resolved release ID')
        release = api(f'repos/{repo}/releases/{existing_release_id}')
    else:
        release = find_release(api(f'repos/{repo}/releases?per_page=100'), vivid.RELEASE_TAG)
    if release is None:
        release = api(f'repos/{repo}/releases', {'tag_name': vivid.RELEASE_TAG, 'target_commitish': target, 'name': 'Сочный цвет — 120 000 проверенных настроек, 4K / Full HD / 720p', 'body': (folder/'VIVID-README-RU.md').read_text(), 'draft': True, 'prerelease': False, 'make_latest': 'false'})
    release_id = release['id']
    if release['tag_name'] != vivid.RELEASE_TAG or release['prerelease']:
        raise ValueError('The resolved ID does not belong to the requested stable release')
    if release['draft']:
        run_gh('release', 'upload', vivid.RELEASE_TAG, '--repo', repo, '--clobber', *(str(path) for path in files))
        metadata = api(f'repos/{repo}/releases/{release_id}')
        if metadata['tag_name'] != vivid.RELEASE_TAG or metadata['id'] != release_id:
            raise ValueError('Wrong draft identity')
        verify_assets(metadata, expected)
        if set(a['name'] for a in metadata['assets']) != set(expected):
            raise ValueError('Unexpected release assets')
        published = api(f'repos/{repo}/releases/{release_id}', {'draft': False, 'prerelease': False, 'make_latest': 'false', 'body': (folder/'VIVID-README-RU.md').read_text()}, method='PATCH')
    else:
        # Never overwrite a previous public result. Recovery can only confirm
        # exactly the same verified assets, not silently replace their bytes.
        published = release
    if published['draft'] or published['prerelease'] or published['id'] != release_id or published['tag_name'] != vivid.RELEASE_TAG:
        raise RuntimeError('The release is not a stable public publication')
    verify_assets(published, expected)
    if set(a['name'] for a in published['assets']) != set(expected):
        raise ValueError('Unexpected public release assets')
    permanent_url = f'https://github.com/{repo}/releases/tag/{vivid.RELEASE_TAG}'
    if published['html_url'] != permanent_url:
        raise ValueError('The release did not return its permanent public URL')
    proof = {'release_id': release_id, 'release_tag': vivid.RELEASE_TAG, 'release_url': permanent_url, 'published': True, 'published_at': published['published_at'], 'render_commit': report['render_commit'], 'publication_commit': target, 'run_url': f"https://github.com/{repo}/actions/runs/{os.environ['GITHUB_RUN_ID']}" if os.environ.get('GITHUB_RUN_ID') else None, 'actual_evaluated_candidates': vivid.CANDIDATES, 'verified_assets': expected, 'all_sizes_and_sha256_match': True}
    (folder/'publication-vivid.json').write_text(json.dumps(proof, indent=2)+'\n')
    print(json.dumps(proof, indent=2), flush=True)
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder', type=Path, required=True)
    parser.add_argument('--repo', default='ritmiro1/Game')
    parser.add_argument('--target', required=True)
    parser.add_argument('--existing-release-id', type=int)
    args = parser.parse_args()
    publish(args.folder, args.repo, args.target, args.existing_release_id)


if __name__ == '__main__':
    main()
