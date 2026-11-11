#!/usr/bin/env python3
"""Small same-origin, byte-range video player. No GitHub redirect during playback."""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

HERE = Path(__file__).resolve().parent
REPO = HERE.parent


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def byte_range(header: str, size: int) -> tuple[int, int]:
    match = re.fullmatch(r'bytes=(\d*)-(\d*)', header.strip())
    if not match or not any(match.groups()) or size < 1:
        raise ValueError('Invalid range')
    left, right = match.groups()
    if left:
        start = int(left)
        end = min(int(right), size - 1) if right else size - 1
    else:
        suffix = int(right)
        if suffix <= 0:
            raise ValueError('Empty suffix range')
        start, end = max(0, size - suffix), size - 1
    if start >= size or start > end:
        raise ValueError('Unsatisfiable range')
    return start, end


class Media:
    def __init__(self, path: Path):
        self.path = path
        self.sha256 = digest(path)
        self.etag = '"' + self.sha256 + '"'


class App:
    def __init__(self, page: bytes, script: bytes, media: dict[str, Media]):
        self.page = page
        self.script = script
        self.script_sha = hashlib.sha256(script).hexdigest()
        self.media = media


def create_app(repo: Path, economy_file: Path) -> App:
    report = json.loads((repo / 'downloads/playback-validation.json').read_text())
    if report.get('test_only') or report['source_sha256'] != 'a1e91b164efba3b1f7ae278b2c8158b56e560a7834be40afcc50de7c2a7356da':
        raise RuntimeError('A verified complete playback report is required')
    profiles = report['outputs']
    main = Media(repo / 'downloads' / profiles['720p']['file'])
    if main.sha256 != profiles['720p']['sha256']:
        raise RuntimeError('The main playback copy does not match its verified report')
    if not economy_file.is_file():
        raise RuntimeError('Prepare the local economy copy before starting the player')
    economy = Media(economy_file)
    media = {'/watch.mp4': main, '/economy.mp4': economy, '/preview.mp4': main}
    old = {
        '/video.mp4': repo / 'downloads/armorypass-ejoy-4k.mp4',
        '/wide.mp4': repo / 'downloads/armorypass-ejoy-4k-16x9.mp4',
        '/wide-preview.mp4': Path.home() / '.cache/video-tools/player-preview-wide-720p.mp4',
    }
    for url, path in old.items():
        if path.is_file():
            media[url] = Media(path)
    config = {'sources': {
        'hd': {'src': '/watch.mp4?v=' + main.sha256[:12], 'label': '720×1280 · H.264 Main 3.1'},
        'eco': {'src': '/economy.mp4?v=' + economy.sha256[:12], 'label': '432×768 · H.264 Baseline 3.0'},
    }}
    script = (HERE / 'player.js').read_bytes()
    script_sha = hashlib.sha256(script).hexdigest()
    replacements = {
        '__PLAYBACK_CONFIG__': json.dumps(config, ensure_ascii=False).replace('<', '\\u003c'),
        '__HD_SRC__': html.escape(config['sources']['hd']['src'], quote=True),
        '__SCRIPT_SRC__': '/player.js?v=' + script_sha[:12],
        '__FULLHD_URL__': html.escape(profiles['1080p']['download_url'], quote=True),
        '__HD_URL__': html.escape(profiles['720p']['download_url'], quote=True),
        '__ECO_URL__': html.escape(profiles['economy']['download_url'], quote=True),
        '__FULLHD_SIZE__': f"{profiles['1080p']['size'] / 1_000_000:.1f}".replace('.', ','),
        '__HD_SIZE__': f"{profiles['720p']['size'] / 1_000_000:.1f}".replace('.', ','),
        '__ECO_SIZE__': f"{profiles['economy']['size'] / 1_000_000:.1f}".replace('.', ','),
    }
    page = (HERE / 'player.html').read_text()
    for source, target in replacements.items():
        page = page.replace(source, target)
    return App(page.encode('utf-8'), script, media)


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def do_GET(self):
        self.respond(False)

    def do_HEAD(self):
        self.respond(True)

    def respond(self, head: bool):
        url = urlsplit(self.path)
        app = self.server.app
        if url.path == '/':
            self.send_bytes(app.page, 'text/html; charset=utf-8', head, 'no-store')
            return
        if url.path == '/player.js':
            self.send_bytes(app.script, 'application/javascript; charset=utf-8', head, self.cache_control(url.query, app.script_sha), '"' + app.script_sha + '"')
            return
        if url.path == '/favicon.ico':
            self.send_response(204)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        item = app.media.get(url.path)
        if item is None or not item.path.is_file():
            self.send_error(404, 'Not found')
            return
        cache = self.cache_control(url.query, item.sha256)
        requested_range = self.headers.get('Range')
        if requested_range and self.headers.get('If-Range') not in (None, item.etag):
            requested_range = None
        if not requested_range and self.headers.get('If-None-Match') == item.etag:
            self.send_response(304)
            self.send_header('ETag', item.etag)
            self.send_header('Cache-Control', cache)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        try:
            stream = item.path.open('rb')
        except OSError:
            self.send_error(404, 'Not found')
            return
        with stream:
            size = os.fstat(stream.fileno()).st_size
            try:
                start, end = byte_range(requested_range, size) if requested_range else (0, size - 1)
            except ValueError:
                self.send_response(416)
                self.send_header('Content-Range', f'bytes */{size}')
                self.send_header('Content-Length', '0')
                self.end_headers()
                return
            self.send_response(206 if requested_range else 200)
            self.send_header('Content-Type', 'video/mp4')
            self.send_header('Content-Length', str(end - start + 1))
            self.send_header('Content-Disposition', f'inline; filename="{item.path.name}"')
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('ETag', item.etag)
            self.send_header('Cache-Control', cache)
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Referrer-Policy', 'no-referrer')
            if requested_range:
                self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
            self.end_headers()
            if not head:
                stream.seek(start)
                remaining = end - start + 1
                try:
                    while remaining:
                        data = stream.read(min(256 * 1024, remaining))
                        if not data:
                            break
                        self.wfile.write(data)
                        remaining -= len(data)
                except (BrokenPipeError, ConnectionResetError):
                    pass

    @staticmethod
    def cache_control(query: str, sha: str) -> str:
        version = parse_qs(query).get('v', [''])[0]
        return 'public, max-age=31536000, immutable' if version in (sha, sha[:12]) else 'private, max-age=0, must-revalidate'

    def send_bytes(self, body: bytes, kind: str, head: bool, cache: str, etag: str | None = None):
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', cache)
        self.send_header('Referrer-Policy', 'no-referrer')
        if etag:
            self.send_header('ETag', etag)
        self.end_headers()
        if not head:
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

    def log_message(self, format, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8001)
    parser.add_argument('--economy-file', type=Path, default=Path.home() / '.cache/video-tools/playback/armorypass-ejoy-economy-432p.mp4')
    args = parser.parse_args()
    app = create_app(REPO, args.economy_file)
    server = ThreadingHTTPServer(('0.0.0.0', args.port), Handler)
    server.app = app
    print(f'Playback server ready on 0.0.0.0:{args.port}; same-origin H.264 720p and 432p; no automatic 16K download.', flush=True)
    if os.environ.get('E2B_SANDBOX_ID'):
        print(f'PLAYER_URL=https://{args.port}-{os.environ["E2B_SANDBOX_ID"]}.e2b.app/?v=smooth', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
