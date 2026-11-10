import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("render_16k", Path(__file__).with_name("render_16k.py"))
render = importlib.util.module_from_spec(spec)
spec.loader.exec_module(render)


class GeometryTest(unittest.TestCase):
    def test_16k_preserves_existing_portrait_layout(self):
        self.assertEqual(render.geometry(8640, 15360), (11448, 1952))

    def test_4k_layout(self):
        self.assertEqual(render.geometry(2160, 3840), (2862, 488))

    def test_reject_invalid_canvas(self):
        for dimensions in [(0, 0), (8641, 15360), (3840, 2160), (720, 1278)]:
            with self.subTest(dimensions=dimensions):
                with self.assertRaises(ValueError):
                    render.geometry(*dimensions)

    def test_model_and_source_hashes_are_explicit(self):
        self.assertEqual(len(render.MODEL_SHA256), 64)
        self.assertEqual(len(render.SOURCE_SHA256), 64)
        self.assertEqual(render.FRAMES, 1518)


if __name__ == "__main__":
    unittest.main()
