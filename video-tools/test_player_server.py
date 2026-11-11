import http.client
import importlib.util
from pathlib import Path
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location('player_server', Path(__file__).with_name('player_server.py'))
player = importlib.util.module_from_spec(spec)
spec.loader.exec_module(player)


class RangeTest(unittest.TestCase):
    def test_prefix_open_suffix_and_clamping(self):
        for header, expected in [('bytes=0-9', (0, 9)), ('bytes=90-', (90, 99)), ('bytes=-7', (93, 99)), ('bytes=90-999', (90, 99)), ('bytes=-999', (0, 99))]:
            with self.subTest(header=header):
                self.assertEqual(player.byte_range(header, 100), expected)

    def test_reject_invalid_ranges(self):
        for header in ['bytes=-0', 'bytes=-', 'bytes=100-', 'bytes=20-10', 'bytes=0-1,3-4', 'items=0-10', 'bytes=hello']:
            with self.subTest(header=header):
                with self.assertRaises(ValueError):
                    player.byte_range(header, 100)


class HTTPTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.file = Path(cls.directory.name) / 'test.mp4'
        cls.data = b'\x00\x00\x00\x18ftyp' + bytes(range(100))
        cls.file.write_bytes(cls.data)
        cls.media = player.Media(cls.file)
        cls.server = player.ThreadingHTTPServer(('0.0.0.0', 0), player.Handler)
        cls.server.app = player.App(b'<html>local player</html>', b'console.log("test")', {'/watch.mp4': cls.media})
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)
        cls.directory.cleanup()

    def request(self, path, headers=None, method='GET'):
        connection = http.client.HTTPConnection('127.0.0.1', self.port, timeout=5)
        connection.request(method, path, headers={'Host': '8001-example.e2b.app', **(headers or {})})
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result

    def test_host_html_and_no_frame_block(self):
        status, headers, body = self.request('/')
        self.assertEqual(status, 200)
        self.assertEqual(headers['Cache-Control'], 'no-store')
        self.assertNotIn('X-Frame-Options', headers)
        self.assertEqual(body, b'<html>local player</html>')

    def test_full_and_head(self):
        status, headers, body = self.request('/watch.mp4')
        self.assertEqual(status, 200)
        self.assertEqual(headers['Accept-Ranges'], 'bytes')
        self.assertEqual(headers['Content-Type'], 'video/mp4')
        self.assertEqual(body, self.data)
        status, headers, body = self.request('/watch.mp4', method='HEAD')
        self.assertEqual(status, 200)
        self.assertEqual(int(headers['Content-Length']), len(self.data))
        self.assertEqual(body, b'')

    def test_ranges_and_invalid_range(self):
        status, headers, body = self.request('/watch.mp4', {'Range': 'bytes=8-15'})
        self.assertEqual(status, 206)
        self.assertEqual(headers['Content-Range'], f'bytes 8-15/{len(self.data)}')
        self.assertEqual(body, self.data[8:16])
        status, _, body = self.request('/watch.mp4', {'Range': 'bytes=-10'})
        self.assertEqual(status, 206)
        self.assertEqual(body, self.data[-10:])
        status, headers, body = self.request('/watch.mp4', {'Range': 'bytes=9999-'})
        self.assertEqual(status, 416)
        self.assertEqual(headers['Content-Range'], f'bytes */{len(self.data)}')
        self.assertEqual(body, b'')

    def test_version_cache_and_not_modified(self):
        path = '/watch.mp4?v=' + self.media.sha256[:12]
        status, headers, _ = self.request(path)
        self.assertEqual(status, 200)
        self.assertIn('immutable', headers['Cache-Control'])
        status, _, body = self.request(path, {'If-None-Match': self.media.etag})
        self.assertEqual(status, 304)
        self.assertEqual(body, b'')

    def test_if_range_does_not_mix_versions(self):
        status, _, body = self.request('/watch.mp4', {'Range': 'bytes=0-7', 'If-Range': '"old-version"'})
        self.assertEqual(status, 200)
        self.assertEqual(body, self.data)

    def test_unknown_paths_are_not_filesystem_access(self):
        status, _, _ = self.request('/../../etc/passwd')
        self.assertEqual(status, 404)


if __name__ == '__main__':
    unittest.main()
