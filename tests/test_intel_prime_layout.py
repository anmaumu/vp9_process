"""Reject malformed PRIME metadata without mapping or touching a GPU."""
import unittest
from intel_va_prime_support import Prime, describe


class PrimeTest(unittest.TestCase):
    def test_linear_and_tiled_distinct(self):
        p = Prime()
        p.num_objects, p.num_layers = 1, 1
        p.objects[0].size = 4096
        p.layers[0].planes = 1
        p.layers[0].pitches[0] = 128
        self.assertTrue(describe(p)["linear_modifier"])
        p.objects[0].modifier = 0x0100000000000009
        self.assertFalse(describe(p)["linear_modifier"])
        p.layers[0].objects[0] = 1
        with self.assertRaises(ValueError):
            describe(p)

    def test_bad_counts(self):
        for count in (0, 5, 0xFFFFFFFF):
            p = Prime()
            p.num_objects = count
            with self.assertRaises(ValueError):
                describe(p)


if __name__ == "__main__":
    unittest.main()
