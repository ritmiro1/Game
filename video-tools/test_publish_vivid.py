from contextlib import redirect_stdout
import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from optimize_playback import sha256
import publish_vivid as publication
import vivid_search as vivid


class VividPublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.asset = self.folder/'fixture.mp4'
        self.asset.write_bytes(b'unit fixture, not a real video')
        (self.folder/'VIVID-README-RU.md').write_text('unit fixture notes')
        self.expected = {self.asset.name:{'size':self.asset.stat().st_size,'sha256':sha256(self.asset)}}
        self.assets = [{'name':name,'size':item['size'],'digest':'sha256:'+item['sha256'],'state':'uploaded'} for name,item in self.expected.items()]
        self.draft = {'id':123,'tag_name':vivid.RELEASE_TAG,'draft':True,'prerelease':False,'assets':self.assets}
        self.public = {**self.draft,'draft':False,'html_url':'https://github.com/ritmiro1/Game/releases/tag/'+vivid.RELEASE_TAG,'published_at':'fixture-time'}

    def tearDown(self):
        self.temp.cleanup()

    def api(self, path, payload=None, method=None):
        if path.endswith('/releases?per_page=100'):
            return []
        if method == 'PATCH':
            self.assertFalse(payload['draft'])
            self.assertEqual(payload['make_latest'],'false')
            return self.public
        if payload is not None:
            self.assertTrue(payload['draft'])
            self.assertEqual(payload['make_latest'],'false')
        return self.draft

    def test_new_draft_uses_authoritative_post_and_patch_responses(self):
        with patch('publish_vivid.bundle_files',return_value=([self.asset],{'render_commit':'fixture-render'})), patch('publish_vivid.api',side_effect=self.api) as api, patch('publish_vivid.run_gh',return_value='') as gh, redirect_stdout(io.StringIO()):
            proof = publication.publish(self.folder,'ritmiro1/Game','fixture-revision')
        self.assertTrue(proof['published'])
        self.assertEqual(len(gh.call_args_list),1)
        self.assertEqual(sum('/releases?per_page=' in call.args[0] for call in api.call_args_list),1)
        self.assertEqual(api.call_args_list[-1].kwargs['method'],'PATCH')

    def test_public_release_is_never_reuploaded_or_overwritten(self):
        with patch('publish_vivid.bundle_files',return_value=([self.asset],{'render_commit':'fixture-render'})), patch('publish_vivid.api',return_value=self.public), patch('publish_vivid.run_gh') as gh, redirect_stdout(io.StringIO()):
            publication.publish(self.folder,'ritmiro1/Game','fixture-revision',123)
        gh.assert_not_called()

    def test_different_public_asset_stops_without_mutation(self):
        metadata = copy.deepcopy(self.public)
        metadata['assets'][0]['size'] += 1
        with patch('publish_vivid.bundle_files',return_value=([self.asset],{'render_commit':'fixture-render'})), patch('publish_vivid.api',return_value=metadata), patch('publish_vivid.run_gh') as gh:
            with self.assertRaises(ValueError):
                publication.publish(self.folder,'ritmiro1/Game','fixture-revision',123)
        gh.assert_not_called()

    def test_wrong_explicit_release_id_cannot_be_used_for_upload(self):
        metadata = {**self.draft,'tag_name':'different-release'}
        with patch('publish_vivid.bundle_files',return_value=([self.asset],{'render_commit':'fixture-render'})), patch('publish_vivid.api',return_value=metadata), patch('publish_vivid.run_gh') as gh:
            with self.assertRaises(ValueError):
                publication.publish(self.folder,'ritmiro1/Game','fixture-revision',123)
        gh.assert_not_called()

    def test_partial_test_or_short_search_report_is_rejected(self):
        valid = {'test_only':False,'source_sha256':vivid.base.SOURCE_SHA256,'frames':1518,'actual_evaluated_candidates':120000}
        for field,value in [('test_only',True),('source_sha256','wrong'),('frames',6),('actual_evaluated_candidates',100)]:
            report = {**valid,field:value}
            (self.folder/'vivid-validation.json').write_text(json.dumps(report))
            with self.subTest(field=field), self.assertRaises(ValueError):
                publication.bundle_files(self.folder)

    def test_archive_contains_actual_distinct_rows_and_finite_per_sample_metrics(self):
        path = self.folder/vivid.ARCHIVE
        parameters = vivid.candidate_parameters()
        values = np.broadcast_to(np.array([1.2,1.1,2,4,0,0,.1,.01],np.float32),(vivid.CANDIDATES,len(vivid.SAMPLES),len(vivid.METRICS)))
        np.savez_compressed(path,parameters=parameters,parameter_names=np.array(vivid.NAMES),metrics=values,metric_names=np.array(vivid.METRICS),source_frame_indexes=np.array(vivid.SAMPLES),eligible=np.ones(vivid.CANDIDATES,bool),preference_scores=np.zeros(vivid.CANDIDATES,np.float32))
        search = {'archive':{'size':path.stat().st_size,'sha256':sha256(path)},'coarse_guardrail_pass_count':vivid.CANDIDATES,'selected':{'candidate_row':0,'parameters':vivid.parameter_dict(parameters[0])}}
        publication.check_archive(self.folder,search)
        bad = copy.deepcopy(search)
        bad['selected']['parameters']['contrast'] = 1.1
        with self.assertRaises(ValueError):
            publication.check_archive(self.folder,bad)
        search['coarse_guardrail_pass_count'] -= 1
        with self.assertRaises(ValueError):
            publication.check_archive(self.folder,search)


if __name__ == '__main__':
    unittest.main()
