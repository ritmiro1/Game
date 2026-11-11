import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("optimize_playback", Path(__file__).with_name("optimize_playback.py"))
optimize = importlib.util.module_from_spec(spec)
spec.loader.exec_module(optimize)


class PlaybackProfilesTest(unittest.TestCase):
    def test_all_profiles_are_portrait_and_even(self):
        for p in optimize.PROFILES.values():
            self.assertEqual(p["width"] * 16, p["height"] * 9)
            self.assertEqual(p["width"] % 2, 0)
            self.assertEqual(p["height"] % 2, 0)

    def test_avc_levels_allow_30_fps(self):
        limits = {30: (1620, 40500), 31: (3600, 108000), 40: (8192, 245760)}
        for p in optimize.PROFILES.values():
            macroblocks = ((p["width"] + 15) // 16) * ((p["height"] + 15) // 16)
            max_frame, max_second = limits[p["level_idc"]]
            self.assertLessEqual(macroblocks, max_frame)
            self.assertLessEqual(macroblocks * 30, max_second)

    def test_economy_avoids_b_frames(self):
        p = optimize.PROFILES["economy"]
        self.assertEqual(p["profile"], "baseline")
        self.assertEqual(p["bframes"], 0)
        self.assertEqual(p["refs"], 1)

    def test_all_encodes_are_faststart_and_8bit(self):
        for p in optimize.PROFILES.values():
            options = optimize.encode_options(p)
            self.assertIn("+faststart", options)
            self.assertIn("yuv420p", options)
            self.assertIn("cpu-independent=1", options)


if __name__ == "__main__":
    unittest.main()
