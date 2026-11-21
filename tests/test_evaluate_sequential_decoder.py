import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


EXECUTABLE = Path(sys.argv[1]) if len(sys.argv) > 2 else None
PACK = Path(sys.argv[2]) if len(sys.argv) > 2 else None


class SequentialDecoderEvaluatorTests(unittest.TestCase):
    def test_runtime_uses_predicted_left_history_without_gold_boundaries(self):
        if EXECUTABLE is None or PACK is None:
            self.skipTest("requires CTest-provided executable and Pack paths")
        executable = EXECUTABLE
        pack = PACK
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            train = root / "train_sequences.tsv"
            train.write_text(("rain\tchopsticks\n" * 20), encoding="utf-8")
            corpus = root / "test_tokens.tsv"
            corpus.write_text(
                "document_id\tsentence_id\treading\tsurface\tdomain\tsplit\n"
                "d1\tdev1\tあめ\t雨\tnews-blog\tdev\n"
                "d1\tdev1\tはし\t箸\tnews-blog\tdev\n"
                "d2\ttest1\tあめ\t雨\tnews-blog\ttest\n"
                "d2\ttest1\tはし\t箸\tnews-blog\ttest\n",
                encoding="utf-8")
            calibration = root / "surge-calibration.tsv"
            dev = subprocess.run(
                [str(executable), str(pack), str(train), str(corpus),
                 "dev-calibrate", str(calibration)],
                check=True, capture_output=True, text=True)
            dev_result = json.loads(dev.stdout)
            self.assertEqual(dev_result["split"], "dev")
            self.assertFalse(dev_result["test_split_used_for_selection"])
            self.assertEqual(dev_result["morphology_only"]["cases"], 1)
            self.assertEqual(dev_result["configurations_evaluated"], 36)  # no cluster sidecar: 3*3*2*2
            self.assertGreater(dev_result["selected_smoothing_alpha"], 0.0)
            self.assertGreaterEqual(dev_result["selected_pos_context_weight"], 0.0)
            self.assertGreaterEqual(dev_result["selected_cluster_weight"], 0.0)
            self.assertIn(dev_result["selected_policy"], {"morphology", "ngram_only", "cluster_only", "combined"})
            self.assertTrue(calibration.is_file())
            completed = subprocess.run(
                [str(executable), str(pack), str(train), str(corpus),
                 "test", str(calibration)],
                check=True, capture_output=True, text=True)
            try:
                result = json.loads(completed.stdout)
            except json.JSONDecodeError as error:
                start = max(0, error.pos - 120)
                end = min(len(completed.stdout), error.pos + 120)
                self.fail(f"invalid evaluator JSON at {error.pos}: "
                          f"{completed.stdout[start:end]!r}")
            self.assertFalse(result["gold_token_boundaries_used_for_decoding"])
            self.assertFalse(result["future_context_used"])
            self.assertEqual(result["split"], "test")
            self.assertGreaterEqual(result["dev_selected_context_weight"], 0.0)
            self.assertGreaterEqual(result["dev_selected_pos_context_weight"], 0.0)
            self.assertGreaterEqual(result["dev_selected_cluster_weight"], 0.0)
            self.assertEqual(result["source_test_tokens"], 2)
            self.assertEqual(result["morphology_only"]["exact_matches"], 0)
            self.assertEqual(result["trained_unigram_left_context"]["exact_matches"], 1)
            self.assertEqual(
                result["trained_unigram_left_context"]["exact_token_alignment"], 2)
            self.assertIn("surge_evidence_gated_left_context", result)
            self.assertEqual(
                result["surge_evidence_gated_left_context"]["cases"],
                result["morphology_only"]["cases"])
            self.assertIn("cer", result["surge_evidence_gated_left_context"])
            for grouped_key in ("surge_metrics_by_domain",
                                "surge_metrics_by_reading_length"):
                groups = result[grouped_key]
                self.assertTrue(groups)
                self.assertEqual(
                    sum(group["cases"] for group in groups.values()),
                    result["surge_evidence_gated_left_context"]["cases"])
                self.assertEqual(
                    sum(group["reference_codepoints"] for group in groups.values()),
                    result["surge_evidence_gated_left_context"]["reference_codepoints"])
                for group in groups.values():
                    self.assertGreaterEqual(group["cer"], 0.0)
                    self.assertGreaterEqual(group["exact_accuracy"], 0.0)
                    self.assertLessEqual(group["exact_accuracy"], 1.0)
            self.assertIn("news-blog", result["surge_metrics_by_domain"])
            self.assertIn("1-4", result["surge_metrics_by_reading_length"])
            self.assertEqual(
                result["surge_metrics_by_domain"]["news-blog"]["cases"],
                result["surge_evidence_gated_left_context"]["cases"])
            survival = result["candidate_survival_diagnostic"]
            for grouped_key in ("candidate_survival_by_domain",
                                "candidate_survival_by_reading_length"):
                groups = result[grouped_key]
                self.assertTrue(groups)
                self.assertEqual(
                    sum(group["gold_tokens"] for group in groups.values()),
                    survival["gold_tokens"])
                for group in groups.values():
                    self.assertEqual(group["classified_gold_tokens"], group["gold_tokens"])
                    self.assertEqual(group["unclassified_gold_tokens"], 0)
            self.assertIn("news-blog", result["candidate_survival_by_domain"])
            self.assertIn("1-4", result["candidate_survival_by_reading_length"])
            self.assertEqual(survival["gold_tokens"], result["source_test_tokens"])
            self.assertEqual(survival["classified_gold_tokens"], survival["gold_tokens"])
            self.assertEqual(survival["unclassified_gold_tokens"], 0)
            self.assertEqual(
                survival["classified_gold_tokens"],
                survival["lattice_missing"] + survival["shortlist_drop"]
                + survival["expansion_drop"] + survival["trace_censored"]
                + survival["beam_pruning_drop"] + survival["final_selection_drop"]
                + survival["final_selected"])
            for key in ("lattice_present", "morphology_top8", "scored_or_expanded",
                        "beam_survived", "beam_pruning_drop", "final_selected",
                        "classified_gold_tokens", "unclassified_gold_tokens",
                        "final_aligned", "nbest_aligned", "nbest_recoverable",
                        "scored_not_in_nbest", "lattice_missing", "shortlist_drop",
                        "expansion_drop", "trace_censored", "final_selection_drop"):
                self.assertGreaterEqual(survival[key], 0)
                self.assertLessEqual(survival[key], survival["gold_tokens"])
            self.assertGreaterEqual(survival["nbest_aligned"], survival["final_aligned"])
            self.assertLessEqual(survival["nbest_recoverable"], survival["nbest_aligned"])
            self.assertLessEqual(survival["scored_not_in_nbest"], survival["scored_or_expanded"])
            self.assertLessEqual(survival["beam_survived"], survival["scored_or_expanded"])
            self.assertLessEqual(survival["beam_pruning_drop"], survival["scored_or_expanded"])
            self.assertEqual(
                survival["scored_or_expanded"],
                survival["beam_survived"] + survival["beam_pruning_drop"])
            self.assertEqual(
                survival["nbest_aligned"],
                survival["final_aligned"] + survival["nbest_recoverable"])
            for key in ("lattice_coverage_pct", "morphology_top8_coverage_pct",
                        "scored_coverage_pct", "final_alignment_pct",
                        "nbest_alignment_pct"):
                self.assertGreaterEqual(survival[key], 0.0)
                self.assertLessEqual(survival[key], 100.0)

            # Calibration artifacts are strict five-number records. Reject trailing
            # fields and non-finite values rather than silently accepting corruption.
            for payload in ("1.0\t0.5\t0.5\t0.0\t0.5\textra\n",
                            "nan\t0.5\t0.5\t0.0\t0.5\n",
                            "1.0\t0.5\t0.5\t0.0\n"):
                malformed = root / "malformed-calibration.tsv"
                malformed.write_text(payload, encoding="utf-8")
                rejected = subprocess.run(
                    [str(executable), str(pack), str(train), str(corpus),
                     "test", str(malformed)],
                    capture_output=True, text=True)
                self.assertNotEqual(rejected.returncode, 0)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
