import csv
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "evaluate_beam_ab.py"
SPEC = importlib.util.spec_from_file_location("evaluate_beam_ab", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BeamAbTests(unittest.TestCase):
    def _corpus(self, root):
        path = root / "prepared.tsv"
        with path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=[
                "document_id", "sentence_id", "reading", "surface", "domain", "split"
            ], delimiter="\t")
            writer.writeheader()
            writer.writerows([
                {"document_id": "d1", "sentence_id": "s1", "reading": "きょう",
                 "surface": "今日", "domain": "news-blog", "split": "test"},
                {"document_id": "d1", "sentence_id": "s1", "reading": "は",
                 "surface": "は", "domain": "news-blog", "split": "test"},
                {"document_id": "d2", "sentence_id": "s2", "reading": "あめ",
                 "surface": "雨", "domain": "news-blog", "split": "train"},
            ])
        return path

    def test_groups_contiguous_rows_and_keeps_split_isolated(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows = MODULE.build_eval_rows(self._corpus(Path(tmp)), "test")
        self.assertEqual(rows, [("d1", "s1", "きょうは", "今日は", "news-blog")])

    def test_paired_comparison_reports_exact_gain_and_cer_delta(self):
        rows = [("d", "s1", "きょう", "今日", "news"),
                ("d", "s2", "あめ", "雨", "news")]
        baseline = {
            "overall": {"cases": 2, "exact_accuracy": 0.5, "cer": 0.25, "decode_failures": 0},
            "domains": {"news": {"cases": 2, "exact_accuracy": 0.5, "cer": 0.25,
                                  "decode_failures": 0}},
            "details": [
                {"reading": "きょう", "expected": "今日", "predicted": "今日", "domain": "news"},
                {"reading": "あめ", "expected": "雨", "predicted": "飴", "domain": "news"},
            ]}
        newer = {
            "overall": {"cases": 2, "exact_accuracy": 1.0, "cer": 0.0, "decode_failures": 0},
            "domains": {"news": {"cases": 2, "exact_accuracy": 1.0, "cer": 0.0,
                                  "decode_failures": 0}},
            "details": [
                {"reading": "きょう", "expected": "今日", "predicted": "今日", "domain": "news"},
                {"reading": "あめ", "expected": "雨", "predicted": "雨", "domain": "news"},
            ]}
        report = MODULE.compare(newer, baseline, rows, "test")
        self.assertEqual(report["comparison"]["exact_gains"], 1)
        self.assertEqual(report["comparison"]["exact_regressions"], 0)
        self.assertEqual(report["comparison"]["delta_diversity_minus_legacy"]["cer"], -0.25)
        self.assertEqual(report["comparison"]["changed_output_cases"], 1)


    def test_paired_bootstrap_is_deterministic_and_reports_intervals(self):
        newer = [
            {"reading": "あ", "expected": "亜", "predicted": "亜", "edit_distance": 0},
            {"reading": "い", "expected": "伊", "predicted": "伊", "edit_distance": 0},
        ]
        older = [
            {"reading": "あ", "expected": "亜", "predicted": "阿", "edit_distance": 1},
            {"reading": "い", "expected": "伊", "predicted": "伊", "edit_distance": 0},
        ]
        first = MODULE.paired_bootstrap(newer, older, samples=300, seed=17)
        second = MODULE.paired_bootstrap(newer, older, samples=300, seed=17)
        self.assertEqual(first, second)
        self.assertEqual(first["samples"], 300)
        self.assertEqual(first["exact_accuracy_delta_ci95"], [0.0, 1.0])
        self.assertEqual(first["cer_delta_ci95"], [-1.0, 0.0])
\n    def test_evaluator_command_switch_is_explicit(self):
        payload = {"overall": {"cases": 0}, "details": []}
        runner = Mock(return_value=Mock(returncode=0, stdout=json.dumps(payload), stderr=""))
        MODULE._run("eval", "pack", "corpus", True, runner=runner)
        command = runner.call_args.args[0]
        self.assertIn("--legacy-beam-pruning", command)


if __name__ == "__main__":
    unittest.main()
