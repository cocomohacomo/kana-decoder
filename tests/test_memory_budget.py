import importlib.util
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "check_memory_budget.py"
SPEC = importlib.util.spec_from_file_location("check_memory_budget", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MemoryBudgetTests(unittest.TestCase):
    def test_reads_peak_rss(self):
        log = "Maximum resident set size (kbytes): 1048576\n"
        self.assertEqual(MODULE.peak_rss_kib(log), 1048576)

    def test_budget_boundary_and_overflow(self):
        self.assertTrue(MODULE.within_budget_kib(102400, 100))
        self.assertFalse(MODULE.within_budget_kib(102401, 100))

    def test_rejects_negative_values(self):
        with self.assertRaises(ValueError):
            MODULE.within_budget_kib(-1, 100)
        with self.assertRaises(ValueError):
            MODULE.within_budget_kib(1, -1)

    def test_requires_measurement(self):
        with self.assertRaises(ValueError):
            MODULE.peak_rss_kib("no measurement")


if __name__ == "__main__":
    unittest.main()
