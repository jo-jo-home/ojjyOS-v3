"""Pixel gate regression probes. Created by Jonas Lee."""
from pathlib import Path
import sys
import unittest
from PIL import Image
from vz_smoke import pointer_hotspot

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from mkcursor import render


class CaptureGateTests(unittest.TestCase):
    def setUp(self):
        self.before = Image.new('RGB', (1440, 900), (20, 90, 160))
        self.cursor = Image.frombytes('RGBA', (22, 30), bytes(render(1)))

    def image(self, x=None, y=100):
        # Tiny whole-background resampling differences must not be mistaken
        # for a cursor, nor obscure a correctly placed high-contrast stroke.
        result = Image.new('RGB', self.before.size, (22, 92, 162))
        if x is not None:
            result.paste(self.cursor, (x-2, y-2), self.cursor)
        return result

    def aligned(self, image):
        point = pointer_hotspot(self.before, image, 100, 100)
        return point is not None and abs(point[0]-100)<=3 and abs(point[1]-100)<=3

    def test_correct_hotspot_with_background_noise(self):
        self.assertTrue(self.aligned(self.image(100)))

    def test_ten_pixel_regression_still_fails(self):
        self.assertFalse(self.aligned(self.image(110)))

    def test_missing_cursor_fails(self):
        self.assertFalse(self.aligned(self.image()))

    def test_large_coordinate_regression_fails(self):
        self.assertFalse(self.aligned(self.image(500, 290)))

    def test_resize_is_not_compared_as_movement(self):
        with self.assertRaises(RuntimeError):
            pointer_hotspot(self.before, self.image(100).resize((1400, 900)), 100, 100)


if __name__ == '__main__':
    unittest.main()
