import copy
import unittest

from enhance_variants import FRAMES, SOURCE_SHA256, STAGES, filename
from verify_existing_variants import check_report, safe_name


class ResumeTests(unittest.TestCase):
    def setUp(self):
        outputs = {key: {'file': filename(key), 'width': 720, 'height': 1280, 'frames': FRAMES, 'aac_unchanged': True, 'full_decode_passed': True} for key, _ in STAGES}
        for key, name, width, height in (
            ('balanced-watch', 'armorypass-ejoy-balanced-watch-720p.mp4', 720, 1280),
            ('balanced-1080p', 'armorypass-ejoy-balanced-1080p.mp4', 1080, 1920),
            ('balanced-4k', 'armorypass-ejoy-balanced-4k.mp4', 2160, 3840),
        ):
            outputs[key] = {'file': name, 'width': width, 'height': height, 'frames': FRAMES, 'aac_unchanged': True, 'full_decode_passed': True}
        self.report = {'test_only': False, 'source_sha256': SOURCE_SHA256, 'frames': FRAMES, 'outputs': outputs, 'archive': {'file': 'armorypass-ejoy-all-variants-720p.zip'}}

    def test_complete_manifest_accepted(self):
        check_report(self.report)

    def test_test_frames_cannot_be_published(self):
        self.report['test_only'] = True
        with self.assertRaises(ValueError):
            check_report(self.report)

    def test_wrong_source_rejected(self):
        self.report['source_sha256'] = '0' * 64
        with self.assertRaises(ValueError):
            check_report(self.report)

    def test_missing_profile_rejected(self):
        self.report['outputs'].pop('balanced-4k')
        with self.assertRaises(ValueError):
            check_report(self.report)

    def test_incomplete_or_unverified_video_rejected(self):
        for field, value in (('frames', 6), ('aac_unchanged', False), ('full_decode_passed', False)):
            report = copy.deepcopy(self.report)
            report['outputs']['01-reference'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_report(report)

    def test_swapped_filename_or_dimensions_rejected(self):
        for field, value in (('file', 'unrelated.mp4'), ('width', 480), ('height', 636)):
            report = copy.deepcopy(self.report)
            report['outputs']['balanced-4k'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_report(report)

    def test_asset_paths_cannot_escape_folder(self):
        for name in ('../file.mp4', '/tmp/file.mp4', 'a/b', 'a\\b', '', None):
            with self.subTest(name=name), self.assertRaises(ValueError):
                safe_name(name)


if __name__ == '__main__':
    unittest.main()
