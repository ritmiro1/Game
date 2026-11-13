import copy
import json
from pathlib import Path
import tempfile
import unittest

from optimize_playback import sha256
from publish_restoration import bundle_files, find_release, verify_assets
from restore_strong import EXPORTS, FRAMES, PROFILES, RELEASE_TAG, SAMPLES, SOURCE_SHA256


class RestorationPublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        outputs = {}
        for key, (width, height, name) in EXPORTS.items():
            path = self.folder / name
            path.write_bytes(('fixture for ' + name).encode())
            outputs[key] = {'file': name, 'width': width, 'height': height, 'frames': FRAMES, 'full_decode_passed': True, 'aac_unchanged': True, 'faststart': True, 'bt709': True, 'size': path.stat().st_size, 'sha256': sha256(path)}
        self.report = {'release_tag': RELEASE_TAG, 'source_sha256': SOURCE_SHA256, 'frames': FRAMES, 'test_only': False, 'profile': 'strong', 'render_commit': 'fixture-render-revision', 'outputs': outputs}
        (self.folder / 'restoration-validation.json').write_text(json.dumps(self.report))
        (self.folder / 'restoration-sample-qa.json').write_text(json.dumps({'source_sha256': SOURCE_SHA256, 'test_only': False, 'selected_profile': 'strong', 'selected_parameters': PROFILES['strong'], 'sample_frame_indexes': list(SAMPLES), 'candidate_summary': {'strong': {'guardrails_passed': True}}}))
        (self.folder / 'restoration-analysis.json').write_text(json.dumps({'source_sha256': SOURCE_SHA256, 'test_only': False, 'frames': FRAMES, 'profile': 'strong', 'parameters': PROFILES['strong'], 'render_commit': 'fixture-render-revision'}))
        (self.folder / 'RESTORATION-README-RU.md').write_text('fixture notes')

    def tearDown(self):
        self.temp.cleanup()

    def test_complete_consistent_bundle_is_accepted(self):
        files = bundle_files(self.folder, self.report)
        self.assertEqual(len(files), 7)
        self.assertEqual(len({p.name for p in files}), 7)

    def test_test_and_partial_reports_are_rejected(self):
        for field, value in (('test_only', True), ('frames', 6), ('source_sha256', 'wrong')):
            report = copy.deepcopy(self.report)
            report[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                bundle_files(self.folder, report)

    def test_missing_delivery_profile_is_rejected(self):
        self.report['outputs'].pop('4k')
        with self.assertRaises(ValueError):
            bundle_files(self.folder, self.report)

    def test_path_in_output_name_is_rejected(self):
        self.report['outputs']['720p']['file'] = '../file.mp4'
        with self.assertRaises(ValueError):
            bundle_files(self.folder, self.report)

    def test_changed_or_audio_modified_file_is_rejected(self):
        self.report['outputs']['720p']['aac_unchanged'] = False
        with self.assertRaises(ValueError):
            bundle_files(self.folder, self.report)
        self.report['outputs']['720p']['aac_unchanged'] = True
        (self.folder / EXPORTS['720p'][2]).write_bytes(b'changed')
        with self.assertRaises(ValueError):
            bundle_files(self.folder, self.report)

    def test_failed_sample_guardrails_are_rejected(self):
        path = self.folder / 'restoration-sample-qa.json'
        data = json.loads(path.read_text())
        data['candidate_summary']['strong']['guardrails_passed'] = False
        path.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            bundle_files(self.folder, self.report)

    def test_asset_digest_and_size_must_both_match(self):
        metadata = {'assets': [{'name': 'file.mp4', 'state': 'uploaded', 'size': 12, 'digest': 'sha256:abc'}]}
        expected = {'file.mp4': {'size': 12, 'sha256': 'abc'}}
        verify_assets(metadata, expected)
        for field, value in (('size', 13), ('digest', 'sha256:wrong'), ('state', 'new')):
            changed = copy.deepcopy(metadata)
            changed['assets'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                verify_assets(changed, expected)

    def test_release_lookup_includes_drafts_and_rejects_ambiguity(self):
        releases = [{'tag_name': RELEASE_TAG, 'draft': True}]
        self.assertTrue(find_release(releases, RELEASE_TAG)['draft'])
        self.assertIsNone(find_release(releases, 'unrelated'))
        with self.assertRaises(ValueError):
            find_release(releases * 2, RELEASE_TAG)


if __name__ == '__main__':
    unittest.main()
