import unittest

import numpy as np

from tests.quality_metrics import luma_ssim


class QualityMetricsTests(unittest.TestCase):
    def test_identity_and_degradation(self):
        reference = np.arange(15 * 17, dtype=np.uint8).reshape(15, 17)
        self.assertAlmostEqual(luma_ssim(reference, reference), 1.0)
        degraded = np.zeros_like(reference)
        self.assertLess(luma_ssim(reference, degraded), 0.5)

    def test_validation(self):
        plane = np.zeros((8, 8), dtype=np.uint8)
        with self.assertRaises(ValueError):
            luma_ssim(plane, plane[:, :4])
        with self.assertRaises(ValueError):
            luma_ssim(plane, plane, 0)


if __name__ == "__main__":
    unittest.main()
