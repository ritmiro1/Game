#!/usr/bin/env python3
"""Publish only the complete, verified restoration bundle to a separate release."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess

from optimize_playback import sha256
from restore_strong import EXPORTS, FRAMES, PROFILES, RELEASE_TAG, SAMPLES, SOURCE_SHA256

EXTRAS = ('restoration-validation.json', 'restoration-sample-qa.json', 'restoration-analysis.json', 'RESTORATION-README-RU.md')


def bundle_files(folder: Path, report: dict) -> list[Path]:
    if report.get('test_only') or report.get('source_sha256') != SOURCE_SHA256 or report.get('frames') != FRAMES:
        raise ValueError('Only complete validated original-source footage can be published')
    if report.get('release_tag') != RELEASE_TAG or set(report['outputs']) != set(EXPORTS):
        raise ValueError('Unexpected release or incomplete output set')
    files = []
    for key, (width, height, name) in EXPORTS.items():
        item = report['outputs'][key]
        if item['file'] != name or (item['width'], item['height']) != (width, height):
            raise ValueError('Unexpected output name or dimensions')
        if item['frames'] != FRAMES or not all(item.get(k) for k in ('full_decode_passed', 'aac_unchanged', 'faststart', 'bt709')):
            raise ValueError('Unverified, incomplete or audio-modified output')
        path = folder / name
        if path.is_symlink() or path.stat().st_size != item['size'] or sha256(path) != item['sha256']:
            raise ValueError(f'Changed output: {name}')
        files.append(path)
    for name in EXTRAS:
        path = folder / name
        if not path.is_file() or path.is_symlink():
            raise ValueError(f'Missing or unsafe report: {name}')
        files.append(path)
    qa = json.loads((folder / 'restoration-sample-qa.json').read_text())
    analysis = json.loads((folder / 'restoration-analysis.json').read_text())
    if qa['test_only'] or analysis['test_only'] or qa['source_sha256'] != SOURCE_SHA256 or analysis['source_sha256'] != SOURCE_SHA256:
        raise ValueError('Test-only or unrelated provenance')
    if not qa['candidate_summary'][qa['selected_profile']]['guardrails_passed']:
        raise ValueError('The selected profile failed its sample guardrails')
    if qa['selected_profile'] != report['profile'] or analysis['profile'] != report['profile']:
        raise ValueError('Inconsistent profile provenance')
    if analysis['frames'] != FRAMES or qa['sample_frame_indexes'] != list(SAMPLES):
        raise ValueError('Incomplete render or sample provenance')
    if qa['selected_parameters'] != PROFILES[report['profile']] or analysis['parameters'] != qa['selected_parameters']:
        raise ValueError('The processed profile parameters changed after selection')
    if not report.get('render_commit') or analysis['render_commit'] != report['render_commit']:
        raise ValueError('Missing or inconsistent render revision')
    return files


def run_gh(*arguments):
    result = subprocess.run(['gh', *arguments], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'GitHub operation failed: {result.stderr.strip()}')
    return result.stdout


def api(path):
    return json.loads(run_gh('api', path))


def find_release(releases, tag):
    matches = [r for r in releases if r['tag_name'] == tag]
    if len(matches) > 1:
        raise ValueError('Ambiguous release tag')
    return matches[0] if matches else None


def verify_assets(metadata, expected):
    assets = {a['name']: a for a in metadata['assets']}
    for name, item in expected.items():
        asset = assets.get(name)
        if not asset or asset['state'] != 'uploaded' or asset['size'] != item['size'] or asset.get('digest') != 'sha256:' + item['sha256']:
            raise ValueError(f'GitHub asset does not match the validated file: {name}')


def publish(folder, repo, target):
    report = json.loads((folder / 'restoration-validation.json').read_text())
    files = bundle_files(folder, report)
    expected = {p.name: {'size': p.stat().st_size, 'sha256': sha256(p)} for p in files}
    release = find_release(api(f'repos/{repo}/releases?per_page=100'), RELEASE_TAG)
    if release is None:
        run_gh('release', 'create', RELEASE_TAG, '--repo', repo, '--draft', '--latest=false', '--target', target, '--title', 'Усиленная очистка — 4K, Full HD и лёгкая 720p', '--notes-file', str(folder / 'RESTORATION-README-RU.md'))
        release = find_release(api(f'repos/{repo}/releases?per_page=100'), RELEASE_TAG)
        if release is None:
            raise RuntimeError('The created draft was not found in the release list')
    # Drafts must be resolved by their listed ID, not the published-by-tag API.
    release_id = release['id']
    run_gh('release', 'upload', RELEASE_TAG, '--repo', repo, '--clobber', *(str(p) for p in files))
    metadata = api(f'repos/{repo}/releases/{release_id}')
    verify_assets(metadata, expected)
    run_gh('release', 'edit', RELEASE_TAG, '--repo', repo, '--draft=false', '--latest=false', '--notes-file', str(folder / 'RESTORATION-README-RU.md'))
    published = api(f'repos/{repo}/releases/{release_id}')
    if published['draft'] or published['prerelease'] or published['tag_name'] != RELEASE_TAG:
        raise RuntimeError('Release is not a public stable publication')
    verify_assets(published, expected)
    proof = {'release_id': release_id, 'release_tag': RELEASE_TAG, 'release_url': published['html_url'], 'published': True, 'published_at': published['published_at'], 'render_commit': report['render_commit'], 'publication_commit': target, 'run_url': f"https://github.com/{repo}/actions/runs/{os.environ['GITHUB_RUN_ID']}" if os.environ.get('GITHUB_RUN_ID') else None, 'verified_assets': expected, 'all_sizes_and_sha256_match': True}
    (folder / 'publication-restoration.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(proof, indent=2), flush=True)
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder', type=Path, required=True)
    parser.add_argument('--repo', default='ritmiro1/Game')
    parser.add_argument('--target', required=True)
    args = parser.parse_args()
    publish(args.folder, args.repo, args.target)


if __name__ == '__main__':
    main()
