"""Safety, geometry and observation-consistency tests for the experiments."""
import unittest

import cv2
import numpy as np

import enhance_variants as v


class VariantTests(unittest.TestCase):
    def setUp(self):
        cv2.setNumThreads(1)
        self.rng = np.random.default_rng(17)
        self.image = self.rng.integers(0, 256, (64, 80, 3), dtype=np.uint8)
        self.planes = v.to_planes(self.image)
        self.flat = v.to_planes(np.full((64, 80, 3), 128, np.uint8))
        self.zero_flow = np.zeros((64, 80, 2), np.float32)

    def observation(self, planes, confidence=1.0):
        return v.Observation(planes, self.zero_flow.copy(), self.zero_flow.copy(), np.full((64, 80), confidence, np.float32))

    def test_bt709_roundtrip(self):
        np.testing.assert_array_equal(v.from_planes(self.planes), self.image)

    def test_zero_flow_is_identity(self):
        np.testing.assert_array_equal(v.warp(self.planes, self.zero_flow), self.planes)

    def test_out_of_bounds_observations_rejected(self):
        flow = self.zero_flow.copy()
        flow[:, :, 0] = 1000
        confidence = v.confidence_for(self.flat, self.flat, flow, -flow)
        self.assertEqual(float(confidence.max()), 0)

    def test_inconsistent_forward_backward_motion_rejected(self):
        outward = self.zero_flow.copy()
        outward[:, :, 0] = 8
        confidence = v.confidence_for(self.flat, self.flat, outward, self.zero_flow)
        self.assertLess(float(confidence.max()), 1e-10)

    def test_photometric_mismatch_rejected(self):
        neighbor = self.flat.copy()
        neighbor[:, :, 0] += 0.3
        confidence = v.confidence_for(self.flat, neighbor, self.zero_flow, self.zero_flow)
        self.assertLess(float(confidence.max()), 1e-10)

    def test_scene_cut_rejected(self):
        a = v.to_planes(np.zeros((64, 80, 3), np.uint8))
        b = v.to_planes(np.full((64, 80, 3), 255, np.uint8))
        ab, ba = v.Motion().pair(a, b)
        self.assertEqual(float(ab.confidence.max()), 0)
        self.assertEqual(float(ba.confidence.max()), 0)

    def test_flat_field_not_textured_by_spatial_filters(self):
        for function in (v.deblock, v.chroma, v.deblur, v.sharpen):
            with self.subTest(function=function.__name__):
                np.testing.assert_allclose(function(self.flat), self.flat, atol=1e-6)

    def test_deblock_does_not_smear_strong_boundary(self):
        p = self.flat.copy()
        p[:, :8, 0] = 0.2
        p[:, 8:, 0] = 0.8
        np.testing.assert_allclose(v.deblock(p), p, atol=1e-7)

    def test_deblock_reduces_small_grid_step(self):
        p = self.flat.copy()
        p[:, :8, 0] = 0.4
        p[:, 8:, 0] = 0.42
        result = v.deblock(p)
        self.assertLess(float(np.mean(result[:, 8, 0] - result[:, 7, 0])), 0.02)
        np.testing.assert_allclose(result[:, :6], p[:, :6])

    def test_denoise_delta_is_bounded(self):
        neighbor = self.planes + 0.10
        output = v.denoise(self.planes, [self.observation(neighbor), self.observation(neighbor)])
        self.assertLessEqual(float(np.max(np.abs(output - self.planes))), 2 / 255 + 1e-7)

    def test_rejected_neighbor_cannot_affect_denoising(self):
        output = v.denoise(self.planes, [self.observation(self.flat, 0)])
        np.testing.assert_array_equal(output, self.planes)

    def test_multiframe_has_exact_double_dimensions_and_bounded_luma(self):
        output = v.multiframe(self.planes, [self.observation(self.flat)])
        self.assertEqual(output.shape, (128, 160, 3))
        # Cubic overshoot is clipped at the valid image range by design.
        baseline = np.clip(v.resize(self.planes[:, :, 0]), 0, 1)
        self.assertLessEqual(float(np.max(np.abs(output[:, :, 0] - baseline))), 3 / 255 + 1e-6)
        np.testing.assert_allclose(output[:, :, 1:], v.resize(self.planes)[:, :, 1:])

    def test_multiframe_preserves_flat_field(self):
        output = v.multiframe(self.flat, [self.observation(self.flat)])
        np.testing.assert_allclose(output, v.resize(self.flat), atol=1e-6)

    def test_rejected_neighbors_have_no_multiframe_effect(self):
        a = v.multiframe(self.planes, [])
        b = v.multiframe(self.planes, [self.observation(self.flat, 0)])
        np.testing.assert_allclose(a, b, atol=1e-7)

    def test_accepted_observation_actually_affects_multiframe_result(self):
        neighbor = self.flat.copy()
        neighbor[:, :, 0] += 2 / 255
        alone = v.multiframe(self.flat, [])
        fused = v.multiframe(self.flat, [self.observation(neighbor)])
        self.assertGreater(float(np.mean(fused[:, :, 0] - alone[:, :, 0])), 0.0001)

    def test_stabilization_reduces_processing_residual_disagreement(self):
        previous = self.flat + 1 / 255
        current = self.flat - 1 / 255
        stable = v.stabilize(self.flat, current, self.flat, previous, self.observation(self.flat))
        before = np.mean(np.abs((current - self.flat) - (previous - self.flat)))
        after = np.mean(np.abs((stable - self.flat) - (previous - self.flat)))
        self.assertLess(float(after), float(before))

    def test_stabilization_does_not_blend_actual_scene_motion(self):
        changed_scene = self.planes.copy()
        # Both frames have zero processing residual; their content is different.
        output = v.stabilize(changed_scene, changed_scene, self.flat, self.flat, self.observation(self.flat))
        np.testing.assert_array_equal(output, changed_scene)

    def test_stabilization_residual_delta_is_bounded(self):
        processed = self.planes + 0.1
        output = v.stabilize(self.planes, processed, self.flat, self.flat - 0.1, self.observation(self.flat))
        self.assertLessEqual(float(np.max(np.abs(output - processed))), 1 / 255 + 1e-7)

    def test_combined_native_delta_is_bounded(self):
        output = v.combined_native(self.planes, [self.observation(self.flat)], np.ones(3, np.float32))
        self.assertLessEqual(float(np.max(np.abs(output - self.planes))), 5 / 255 + 1e-7)
        self.assertTrue(np.isfinite(output).all())

    def test_color_experiment_has_small_fixed_gains(self):
        output = v.color(self.flat, np.ones(3, np.float32))
        self.assertLess(float(np.max(np.abs(output - self.flat))), 1 / 255)

    def test_all_portrait_profiles_keep_full_source_geometry(self):
        for width, height in ((720, 1280), (1080, 1920), (2160, 3840)):
            active, top = v.geometry(width, height)
            self.assertLessEqual(abs(active - width * 636 / 480), 1)
            self.assertGreaterEqual(top, 0)
            self.assertLessEqual(top + active, height)
            self.assertNotIn('crop=', v.canvas_filter(width, height))

    def test_profiles_and_names_are_distinct(self):
        self.assertEqual(len(v.STAGES), 10)
        self.assertEqual(len({v.filename(k) for k, _ in v.STAGES}), 10)
        self.assertEqual(v.profile(2160, 3840)['level_idc'], 51)
        self.assertEqual(v.profile(720, 1280)['profile_idc'], 77)

    def test_encode_options_do_not_interpolate_or_replace_audio(self):
        options = v.encode_args(v.profile(720, 1280))
        self.assertNotIn('-r', options)
        self.assertNotIn('-c:a', options)
        self.assertNotIn('minterpolate', ' '.join(options))


if __name__ == '__main__':
    unittest.main()
