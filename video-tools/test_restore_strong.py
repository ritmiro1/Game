import unittest
from unittest.mock import patch

import cv2
import numpy as np

import enhance_variants as base
import restore_strong as strong


class StrongCleanupTests(unittest.TestCase):
    def setUp(self):
        cv2.setNumThreads(1)
        self.flat = base.to_planes(np.full((64, 80, 3), 128, np.uint8))
        rng = np.random.default_rng(105)
        self.raw = base.to_planes(rng.integers(0, 256, (64, 80, 3), dtype=np.uint8))
        self.zero = np.zeros((64, 80, 2), np.float32)

    def test_compose_constant_translation(self):
        a, b = self.zero.copy(), self.zero.copy()
        a[:, :, 0] = 2
        b[:, :, 1] = -3
        expected = a + b
        np.testing.assert_array_equal(strong.compose(a, b), expected)

    def test_forward_backward_inconsistency_is_rejected(self):
        forward = self.zero.copy()
        forward[:, :, 0] = 6
        confidence = strong.confidence(self.flat, self.flat, forward, self.zero)
        self.assertLess(float(confidence.max()), 1e-10)

    def test_different_scene_rejected(self):
        black = self.flat.copy()
        black[:, :, 0] = 0
        confidence = strong.confidence(self.flat, black, self.zero, self.zero)
        self.assertEqual(float(confidence.max()), 0)

    def test_outside_image_rejected(self):
        forward = self.zero.copy()
        forward[:, :, 0] = 1000
        confidence = strong.confidence(self.flat, self.flat, forward, -forward)
        self.assertEqual(float(confidence.max()), 0)

    def test_flat_surface_not_given_new_texture(self):
        for key, p in strong.PROFILES.items():
            with self.subTest(profile=key):
                out = strong.native(self.flat, [], p)
                np.testing.assert_allclose(out, self.flat, atol=5e-6)

    def test_every_profile_is_bounded_against_current_frame(self):
        neighbor = self.raw.copy()
        neighbor[:, :, 0] += .1
        obs = base.Observation(neighbor, self.zero, self.zero, np.ones((64, 80), np.float32))
        for key, p in strong.PROFILES.items():
            with self.subTest(profile=key):
                out = strong.native(self.raw, [(1, obs), (2, obs)], p)
                self.assertLessEqual(float(np.max(np.abs(out[:, :, 0] - self.raw[:, :, 0]))), p['luma_cap'] / 255 + 1e-6)
                self.assertLessEqual(float(np.max(np.abs(out[:, :, 1:] - self.raw[:, :, 1:]))), p['chroma_cap'] / 255 + 1e-6)
                self.assertTrue(np.isfinite(out).all())

    def test_rejected_neighbor_does_not_change_processing(self):
        obs = base.Observation(self.flat, self.zero, self.zero, np.zeros((64, 80), np.float32))
        a = strong.native(self.raw, [], strong.PROFILES['strong'])
        b = strong.native(self.raw, [(1, obs)], strong.PROFILES['strong'])
        np.testing.assert_allclose(a, b, atol=1e-7)

    def test_window_never_accumulates_whole_video(self):
        def decoded(*unused):
            for i in range(12):
                yield np.full((64, 80, 3), 128 + i, np.uint8)
        with patch.object(base, 'decode_frames', decoded):
            window = strong.Window('unused', 'unused', 12)
            previous_raw = None
            for n in range(12):
                raw, neighbors = window.get(n)
                self.assertLessEqual(len(window.frames), 5)
                self.assertLessEqual(len(window.pairs), 4)
                self.assertLessEqual(len(neighbors), 4)
                self.assertAlmostEqual(float(raw[5, 5, 0]), (128 + n) / 255, places=6)
                if previous_raw is not None:
                    self.assertTrue(any(obs.planes is previous_raw for _, obs in neighbors))
                previous_raw = raw
            window.close(complete=True)

    def test_metrics_are_finite_for_constant_image(self):
        metrics = strong.sample_metrics(self.flat, self.flat)
        self.assertTrue(all(np.isfinite(v) for v in metrics.values()))
        self.assertEqual(metrics['mean_luma_change_8bit'], 0)
        self.assertEqual(metrics['flat_high_frequency_reduction'], 0)

    @staticmethod
    def candidate(reduction=.4, retention=.97, mean=1, clipping=0):
        return {'flat_high_frequency_reduction': reduction, 'strong_edge_gradient_retention': retention, 'mean_luma_change_8bit': mean, 'rgb_clipping_fraction_increase': clipping}

    def test_selector_refuses_destructive_candidates(self):
        results = {key: [self.candidate(retention=.70)] for key in strong.PROFILES}
        with self.assertRaises(RuntimeError):
            strong.choose_profile(results)

    def test_selector_refuses_additional_clipping(self):
        results = {key: [self.candidate(clipping=.02)] for key in strong.PROFILES}
        with self.assertRaises(RuntimeError):
            strong.choose_profile(results)

    def test_selector_rejects_unsupported_forced_profile(self):
        results = {key: [self.candidate(retention=.70 if key == 'strong' else .98)] for key in strong.PROFILES}
        with self.assertRaises(RuntimeError):
            strong.choose_profile(results, 'strong')

    def test_selector_only_ranks_eligible_candidates(self):
        results = {'moderate': [self.candidate(.30)], 'clean': [self.candidate(.45)], 'strong': [self.candidate(.80, retention=.70)]}
        chosen, summaries = strong.choose_profile(results)
        self.assertEqual(chosen, 'clean')
        self.assertFalse(summaries['strong']['guardrails_passed'])

    def test_every_delivery_has_portrait_geometry_and_unique_name(self):
        self.assertEqual(len(strong.EXPORTS), 3)
        self.assertEqual(len({p[2] for p in strong.EXPORTS.values()}), 3)
        for width, height, _ in strong.EXPORTS.values():
            active, top = base.geometry(width, height)
            self.assertLessEqual(top + active, height)
            self.assertNotIn('crop=', base.canvas_filter(width, height))


if __name__ == '__main__':
    unittest.main()
