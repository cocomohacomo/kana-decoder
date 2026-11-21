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
                {"reading": "きょう", "expected": "今日", "predicted": "今日", "domain": "news", "edit_distance": 0},
                {"reading": "あめ", "expected": "雨", "predicted": "飴", "domain": "news", "edit_distance": 1},
            ]}
        newer = {
            "overall": {"cases": 2, "exact_accuracy": 1.0, "cer": 0.0, "decode_failures": 0},
            "domains": {"news": {"cases": 2, "exact_accuracy": 1.0, "cer": 0.0,
                                  "decode_failures": 0}},
            "details": [
                {"reading": "きょう", "expected": "今日", "predicted": "今日", "domain": "news", "edit_distance": 0},
                {"reading": "あめ", "expected": "雨", "predicted": "雨", "domain": "news", "edit_distance": 0},
            ]}
        report = MODULE.compare(newer, baseline, rows, "test")
        self.assertEqual(report["comparison"]["exact_gains"], 1)
        self.assertEqual(report["comparison"]["exact_regressions"], 0)
        self.assertEqual(report["comparison"]["delta_diversity_minus_legacy"]["cer"], -0.25)
        self.assertEqual(report["comparison"]["changed_output_cases"], 1)
        self.assertIn("exact_accuracy_delta_ci95", report["paired_bootstrap"])


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

    def test_cluster_bootstrap_resamples_whole_documents(self):
        newer = [
            {"expected": "甲", "predicted": "甲", "edit_distance": 0},
            {"expected": "乙", "predicted": "乙", "edit_distance": 0},
            {"expected": "丙", "predicted": "丙", "edit_distance": 0},
        ]
        older = [
            {"expected": "甲", "predicted": "阿", "edit_distance": 1},
            {"expected": "乙", "predicted": "伊", "edit_distance": 1},
            {"expected": "丙", "predicted": "丙", "edit_distance": 0},
        ]
        source_rows = [
            ("doc-a", "s1", "a", "甲", "news"),
            ("doc-a", "s2", "b", "乙", "news"),
            ("doc-b", "s1", "c", "丙", "news"),
        ]
        result = MODULE.paired_bootstrap(
            newer, older, samples=100, seed=9, source_rows=source_rows)
        self.assertEqual(result["resampling_unit"], "document")
        self.assertEqual(result["cluster_count"], 2)
        self.assertEqual(
            result["method"], "paired percentile cluster bootstrap over documents")

    def test_cluster_bootstrap_rejects_misaligned_source_rows(self):
        cases = [{"expected": "甲", "predicted": "甲", "edit_distance": 0}]
        with self.assertRaises(ValueError):
            MODULE.paired_bootstrap(cases, cases, source_rows=[])

    def test_paired_bootstrap_rejects_zero_samples(self):
        with self.assertRaises(ValueError):
            MODULE.paired_bootstrap([], [], samples=0)


    def test_input_fingerprints_are_content_sensitive_and_deterministic(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pack = root / "pack"
            pack.mkdir()
            (pack / "a.txt").write_text("alpha", encoding="utf-8")
            (pack / "b.txt").write_text("beta", encoding="utf-8")
            first = MODULE.fingerprint_path(pack)
            second = MODULE.fingerprint_path(pack)
            self.assertEqual(first, second)
            self.assertEqual(first["file_count"], 2)
            (pack / "b.txt").write_text("changed", encoding="utf-8")
            third = MODULE.fingerprint_path(pack)
            self.assertNotEqual(first["manifest_sha256"], third["manifest_sha256"])

    def test_provenance_records_bootstrap_and_all_input_fingerprints(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            evaluator = root / "evaluator"
            corpus = root / "corpus.tsv"
            pack = root / "pack"
            pack.mkdir()
            evaluator.write_bytes(b"binary")
            corpus.write_text("reading\\texpected\\tdomain\\n", encoding="utf-8")
            (pack / "dict.tsv").write_text("あ\\t亜\\n", encoding="utf-8")
            evaluator_hash = MODULE.sha256_file(evaluator)
            result = MODULE.build_provenance(
                evaluator, pack, corpus, "dev", ["evaluate_beam_ab.py", "--split", "dev"],
                5000, 23)
        self.assertEqual(result["schema_version"], 1)
        self.assertEqual(result["bootstrap"]["samples"], 5000)
        self.assertEqual(result["bootstrap"]["seed"], 23)
        self.assertEqual(result["inputs"]["evaluator"]["sha256"],
                         evaluator_hash)
        self.assertEqual(result["inputs"]["pack"]["file_count"], 1)
        self.assertEqual(result["inputs"]["prepared_corpus"]["kind"], "file")
        self.assertEqual(result["command"][-1], "dev")
        self.assertEqual(
            MODULE.sanitized_command(["--pack", "/private/user/data/pack"]),
            ["--pack", "pack"])

    def test_evaluator_command_switch_is_explicit(self):
        payload = {"overall": {"cases": 0}, "details": []}
        runner = Mock(return_value=Mock(returncode=0, stdout=json.dumps(payload), stderr=""))
        MODULE._run("eval", "pack", "corpus", True, runner=runner)
        command = runner.call_args.args[0]
        self.assertIn("--legacy-beam-pruning", command)


if __name__ == "__main__":
    unittest.main()
