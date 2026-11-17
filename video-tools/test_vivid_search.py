import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

import imageio_ffmpeg
import numpy as np

from optimize_playback import sha256
from render_vivid import EXPORTS, export_command, read_search
import vivid_search as vivid


PARAMETERS = dict(contrast=1.1133333, saturation=1.3, vibrance=.1555556, exposure_stops=.0742857, warmth=.004, highlight_protection=.35)


class VividGradeTests(unittest.TestCase):
    def test_grid_really_contains_120000_distinct_combinations(self):
        values = vivid.candidate_parameters()
        self.assertEqual(values.shape, (120000, 6))
        self.assertEqual(len(np.unique(values, axis=0)), 120000)

    def test_black_white_and_finite_gamut_are_preserved(self):
        rgb = np.concatenate((np.array([[0,0,0],[1,1,1]], np.float32), np.random.default_rng(1).random((1024,3), dtype=np.float32)))
        for row in (vivid.candidate_parameters()[0], vivid.candidate_parameters()[-1], vivid.parameter_row(PARAMETERS)):
            output = vivid.apply_grade(rgb, vivid.parameter_dict(row))
            self.assertTrue(np.isfinite(output).all())
            self.assertGreaterEqual(output.min(), 0)
            self.assertLessEqual(output.max(), 1)
            np.testing.assert_allclose(output[:2], rgb[:2], atol=1e-6)

    def test_neutral_ramp_is_monotone(self):
        ramp = np.repeat(np.linspace(0,1,2048, dtype=np.float32)[:,None],3,axis=1)
        for index in (0, 119999):
            output = vivid.apply_grade(ramp, vivid.parameter_dict(vivid.candidate_parameters()[index]))
            y = output @ np.array([.2126,.7152,.0722])
            self.assertGreaterEqual(np.diff(y).min(), -1e-6)

    def test_neutrals_have_no_tint_without_warmth(self):
        ramp = np.repeat(np.linspace(0,1,64, dtype=np.float32)[:,None],3,axis=1)
        output = vivid.apply_grade(ramp, {**PARAMETERS,'warmth':0})
        np.testing.assert_allclose(output[:,0], output[:,1], atol=1e-6)
        np.testing.assert_allclose(output[:,2], output[:,1], atol=1e-6)

    def test_gamut_compression_preserves_chroma_direction_without_warmth(self):
        rgb = np.random.default_rng(2).random((256,3),dtype=np.float32)
        output = vivid.apply_grade(rgb, {**PARAMETERS,'warmth':0})
        weights = np.array([.2126,.7152,.0722],np.float32)
        a = rgb-(rgb@weights)[:,None]
        b = output-(output@weights)[:,None]
        self.assertLess(np.linalg.norm(np.cross(a,b),axis=-1).max(), 1e-6)
        self.assertGreaterEqual(np.sum(a*b,axis=-1).min(), -1e-6)

    def test_parameter_batches_match_individual_evaluations(self):
        rgb = np.random.default_rng(3).random((2,128,3),dtype=np.float32)
        context = vivid.Context.make(rgb)
        rows = vivid.candidate_parameters()[[0,20000,119999]]
        batched, scores, passed = vivid.evaluate(context, rows)
        for index,row in enumerate(rows):
            single, score, flag = vivid.evaluate(context,row[None])
            np.testing.assert_allclose(batched[index],single[0],atol=1e-6)
            np.testing.assert_allclose(scores[index],score[0],atol=1e-6)
            self.assertEqual(passed[index],flag[0])

    def test_each_guardrail_rejects_invalid_values(self):
        valid = np.array([[[1.2,1.1,2,4,0,0,.1,.01]]],np.float32)
        self.assertTrue(vivid.passes(valid)[0])
        for index,value in enumerate((1.8,.5,20,20,.1,.1,.8,.2)):
            bad = valid.copy()
            bad[0,0,index] = value
            self.assertFalse(vivid.passes(bad)[0])
        invalid = valid.copy()
        invalid[0,0,0] = np.nan
        self.assertFalse(vivid.passes(invalid)[0])

    def test_invalid_rgb_and_parameters_are_rejected(self):
        for rgb in (np.array([[[-.1,0,0]]]), np.array([[[np.nan,0,0]]]), np.zeros((3,3))):
            with self.assertRaises(ValueError):
                vivid.Context.make(rgb)
        with self.assertRaises(ValueError):
            vivid.parameter_row({**PARAMETERS,'saturation':10})
        with self.assertRaises(ValueError):
            vivid.parameter_row({'contrast':1.1})

    def test_stratified_grid_retains_real_pixel_colors(self):
        frame = np.zeros((636,480,3),np.uint8)
        frame[...,0] = np.arange(480,dtype=np.uint16)[None]%251
        proxy = vivid.stratified_proxy(frame)
        self.assertEqual(proxy.shape,(336,3))
        self.assertEqual(proxy.dtype,np.uint8)
        xs = ((np.arange(16)+.5)*480/16).astype(int)
        np.testing.assert_array_equal(proxy[:16,0],xs%251)

    def test_ffmpeg_lut_agrees_with_formula_including_rgb_axis_order(self):
        rgb = np.random.default_rng(4).integers(0,256,(32,32,3),dtype=np.uint8)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'grade.cube'
            vivid.write_lut(path, PARAMETERS)
            command = [imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-threads','1','-filter_threads','1','-f','rawvideo','-pixel_format','rgb24','-video_size','32x32','-i','pipe:0','-vf',f'lut3d=file={path}:interp=tetrahedral','-frames:v','1','-f','rawvideo','-pix_fmt','rgb24','pipe:1']
            result = subprocess.run(command,input=rgb.tobytes(),capture_output=True,check=True)
        output = np.frombuffer(result.stdout,np.uint8).reshape(rgb.shape).astype(float)
        expected = vivid.apply_grade(rgb.astype(np.float32)/255,PARAMETERS)*255
        # Tetrahedral interpolation approximates the continuous formula, then
        # FFmpeg quantizes to 8 bits. This exercises 1024 fixed RGB samples,
        # not a mathematical error bound for every possible color.
        error = np.abs(output-expected)
        self.assertLessEqual(error.max(), 4.0)
        self.assertLess(np.percentile(error, 99), 1.5)
        self.assertLess(error.mean(), .60)


class VividPipelineTests(unittest.TestCase):
    def test_exports_use_lut_before_scale_and_copy_original_audio(self):
        args = SimpleNamespace(output_dir=Path('/tmp/vivid-test'), source=Path('/tmp/source.mp4'),test_frames=0)
        for key, (_,_,name) in EXPORTS.items():
            command = export_command(args,'ffmpeg',key)
            filters = command[command.index('-vf')+1]
            self.assertLess(filters.index('lut3d='),filters.index('scale='))
            self.assertIn('interp=tetrahedral',filters)
            self.assertIn('-copyts',command)
            self.assertEqual(command[command.index('-c:a')+1],'copy')
            self.assertEqual(command[-1],str(args.output_dir/name))

    def test_test_export_has_no_original_audio_and_unsafe_paths_are_rejected(self):
        args = SimpleNamespace(output_dir=Path('/tmp/vivid-test'),source=Path('/tmp/source.mp4'),test_frames=6)
        self.assertIn('-an',export_command(args,'ffmpeg','720p'))
        args.output_dir = Path("/tmp/unsafe'grade")
        with self.assertRaises(ValueError):
            export_command(args,'ffmpeg','720p')

    def test_selection_reader_rejects_test_and_changed_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder/vivid.ARCHIVE).write_bytes(b'explicit unit fixture, not candidate evidence')
            vivid.write_lut(folder/'vivid-grade.cube',PARAMETERS,size=33)
            metrics = dict(zip(vivid.METRICS,(1.2,1.1,2,4,0,0,.1,.01),strict=True))
            selection = {'source_sha256':vivid.base.SOURCE_SHA256,'release_tag':vivid.RELEASE_TAG,'test_only':True,'selected':{'parameters':PARAMETERS,'native_guardrails_passed':True,'native_sample_metrics':[metrics]},'sample_frame_indexes':[2],'archive':{'file':vivid.ARCHIVE,'sha256':sha256(folder/vivid.ARCHIVE)},'lut':{'sha256':sha256(folder/'vivid-grade.cube')}}
            (folder/'vivid-search.json').write_text(json.dumps(selection))
            with self.assertRaises(ValueError):
                read_search(folder)
            self.assertTrue(read_search(folder,allow_test=True)['test_only'])
            (folder/vivid.ARCHIVE).write_bytes(b'changed fixture')
            with self.assertRaises(ValueError):
                read_search(folder,allow_test=True)


if __name__ == '__main__':
    unittest.main()
