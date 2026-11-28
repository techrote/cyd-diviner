"""Formatter edge cases; no board or fake measurement artifact is involved."""
import unittest
from check_output_bounds import link_record_bound


class OutputBoundsTests(unittest.TestCase):
    def test_integer_widths_and_strings_are_type_specific(self):
        self.assertEqual(link_record_bound("%s %u %d %llu %02x", string_bytes=7),
                         7 + 10 + 11 + 20 + 2 + 4 + 1)

    def test_histogram_includes_32_full_width_counts_closure_and_nul(self):
        self.assertEqual(link_record_bound("[", histogram_entries=32), 1 + 32 * 11 + 3 + 1)

    def test_unreviewed_conversions_fail_closed(self):
        for conversion in ("%p", "%x", "%f", "%zu", "%%"):
            with self.subTest(conversion=conversion):
                if conversion == "%zu":
                    # size_t is 32-bit on targets; 10 digits is conservative.
                    self.assertEqual(link_record_bound(conversion), 11)
                else:
                    with self.assertRaises(ValueError):
                        link_record_bound(conversion)


if __name__ == "__main__":
    unittest.main()
