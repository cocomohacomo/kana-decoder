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
            self.assertEqual(
                dev_result["dev_ablation_scope"],
                "development_only_diagnostic_not_selection")
            for key in (
                "dev_ablation_left_context_only",
                "dev_ablation_pos_context_only",
                "dev_ablation_selected_policy",
            ):
                self.assertEqual(dev_result[key]["cases"], dev_result["morphology_only"]["cases"])
                self.assertGreaterEqual(dev_result[key]["cer"], 0.0)
                self.assertGreaterEqual(dev_result[key]["exact_accuracy"], 0.0)
                self.assertLessEqual(dev_result[key]["exact_accuracy"], 1.0)
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
                    result["surge_evidence_gated_left_context"]["cases"]
                - result["surge_evidence_gated_left_context"]["over_limit_sentences"])
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
            for grouped_key, overall_key in (
                ("morphology_metrics_by_domain", "morphology_only"),
                ("morphology_metrics_by_reading_length", "morphology_only"),
                ("surge_metrics_by_domain", "surge_evidence_gated_left_context"),
                ("surge_metrics_by_reading_length", "surge_evidence_gated_left_context")):
                groups = result[grouped_key]
                grouped_cases = sum(group["cases"] for group in groups.values())
                if grouped_key.endswith("_by_domain"):
                    self.assertEqual(grouped_cases, result[overall_key]["cases"])
                else:
                    # Empty and over-limit readings are intentionally excluded
                    # from reading-length strata.
                    self.assertLessEqual(grouped_cases, result[overall_key]["cases"])
                self.assertEqual(
                    sum(group["edit_distance"] for group in groups.values()),
                    result[overall_key]["edit_distance"])
                self.assertEqual(
                    sum(group["reference_codepoints"] for group in groups.values()),
                    result[overall_key]["reference_codepoints"])
            self.assertEqual(
                set(result["morphology_metrics_by_domain"]),
                set(result["surge_metrics_by_domain"]))
            self.assertEqual(
                set(result["morphology_metrics_by_reading_length"]),
                set(result["surge_metrics_by_reading_length"]))
            for paired_key, baseline_key, surge_key in (
                ("surge_vs_morphology_by_domain",
                 "morphology_metrics_by_domain", "surge_metrics_by_domain"),
                ("surge_vs_morphology_by_reading_length",
                 "morphology_metrics_by_reading_length", "surge_metrics_by_reading_length")):
                paired = result[paired_key]
                self.assertEqual(set(paired), set(result[baseline_key]))
                self.assertEqual(set(paired), set(result[surge_key]))
                for group_name, delta in paired.items():
                    baseline = result[baseline_key][group_name]
                    surge = result[surge_key][group_name]
                    self.assertEqual(delta["cases"], min(
                        baseline["cases"], surge["cases"]))
                    self.assertAlmostEqual(
                        delta["cer_delta"], surge["cer"] - baseline["cer"])
                    self.assertAlmostEqual(
                        delta["exact_accuracy_delta"],
                        surge["exact_accuracy"] - baseline["exact_accuracy"])
                    self.assertAlmostEqual(
                        delta["token_alignment_recall_delta"],
                        surge["token_alignment_recall"]
                        - baseline["token_alignment_recall"])
                    self.assertEqual(
                        delta["reference_codepoints"],
                        baseline["reference_codepoints"])
                    self.assertEqual(
                        delta["reference_codepoints"],
                        surge["reference_codepoints"])
            for combined_key, paired_key, survival_key in (
                ("surge_bottleneck_by_domain", "surge_vs_morphology_by_domain",
                 "candidate_survival_by_domain"),
                ("surge_bottleneck_by_reading_length",
                 "surge_vs_morphology_by_reading_length",
                 "candidate_survival_by_reading_length")):
                combined = result[combined_key]
                paired = result[paired_key]
                survival_groups_for_key = result[survival_key]
                self.assertEqual(set(combined), set(paired) & set(survival_groups_for_key))
                for name, diagnostic in combined.items():
                    self.assertEqual(diagnostic["cases"], paired[name]["cases"])
                    self.assertAlmostEqual(diagnostic["cer_delta"], paired[name]["cer_delta"])
                    self.assertAlmostEqual(
                        diagnostic["exact_accuracy_delta"],
                        paired[name]["exact_accuracy_delta"])
                    self.assertAlmostEqual(
                        diagnostic["token_alignment_recall_delta"],
                        paired[name]["token_alignment_recall_delta"])
                    self.assertEqual(
                        diagnostic["dominant_loss_stage"],
                        survival_groups_for_key[name]["dominant_loss_stage"])
                    self.assertEqual(
                        diagnostic["dominant_loss_tokens"],
                        survival_groups_for_key[name]["dominant_loss_tokens"])
                    self.assertEqual(
                        diagnostic["gold_tokens"],
                        survival_groups_for_key[name]["gold_tokens"])
                    self.assertAlmostEqual(
                        sum(diagnostic["loss_stage_rates_pct"].values()), 100.0,
                        delta=0.01)
            outcome_keys = (
                "cer_improved_exact_improved", "cer_improved_exact_tied",
                "cer_improved_exact_regressed", "cer_tied_exact_improved",
                "cer_tied_exact_tied", "cer_tied_exact_regressed",
                "cer_regressed_exact_improved", "cer_regressed_exact_tied",
                "cer_regressed_exact_regressed")
            overall_outcomes = result["surge_paired_sentence_outcomes"]["all"]
            self.assertEqual(
                sum(overall_outcomes["outcomes"][key] for key in outcome_keys),
                overall_outcomes["cases"])
            self.assertEqual(
                overall_outcomes["cases"],
                result["surge_evidence_gated_left_context"]["cases"])
            for grouped_key in (
                "surge_paired_sentence_outcomes_by_domain",
                "surge_paired_sentence_outcomes_by_reading_length"):
                groups = result[grouped_key]
                self.assertTrue(groups)
                for group in groups.values():
                    self.assertEqual(
                        sum(group["outcomes"][key] for key in outcome_keys),
                        group["cases"])
                grouped_cases = sum(group["cases"] for group in groups.values())
                if grouped_key.endswith("_by_domain"):
                    self.assertEqual(grouped_cases, overall_outcomes["cases"])
                else:
                    self.assertLessEqual(grouped_cases, overall_outcomes["cases"])
            self.assertIn(
                "news-blog", result["surge_paired_sentence_outcomes_by_domain"])
            self.assertIn(
                "1-4", result["surge_paired_sentence_outcomes_by_reading_length"])

            survival = result["candidate_survival_diagnostic"]
            survival_groups = [survival]
            for grouped_key in ("candidate_survival_by_domain",
                                "candidate_survival_by_reading_length"):
                groups = result[grouped_key]
                survival_groups.extend(groups.values())
            for group in survival_groups:
                losses = {
                    "lattice_missing": group["lattice_missing"],
                    "span_selection_drop": group["span_selection_drop"],
                    "shortlist_drop": group["shortlist_drop"],
                    "expansion_drop": group["expansion_drop"],
                    "trace_censored": group["trace_censored"],
                    "beam_pruning_drop": group["beam_pruning_drop"],
                    "final_selection_drop": group["final_selection_drop"],
                }
                dominant_stage = max(losses, key=losses.get)
                if losses[dominant_stage] == 0:
                    dominant_stage = "none"
                self.assertEqual(group["dominant_loss_stage"], dominant_stage)
                self.assertEqual(
                    group["dominant_loss_tokens"],
                    0 if dominant_stage == "none" else losses[dominant_stage])
                self.assertAlmostEqual(
                    group["dominant_loss_pct"],
                    100.0 * group["dominant_loss_tokens"] / group["gold_tokens"]
                    if group["gold_tokens"] else 0.0,
                    delta=0.01)
                self.assertEqual(
                    set(group["loss_stage_rates_pct"]),
                    set(losses) | {"final_selected"})
                self.assertAlmostEqual(
                    sum(group["loss_stage_rates_pct"].values()), 100.0
                    if group["gold_tokens"] else 0.0, delta=0.01)
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
                survival["lattice_missing"] + survival["span_selection_drop"] + survival["shortlist_drop"]
                + survival["expansion_drop"] + survival["trace_censored"]
                + survival["beam_pruning_drop"] + survival["final_selection_drop"]
                + survival["final_selected"])
            for key in ("lattice_present", "morphology_top8", "scored_or_expanded",
                        "beam_survived", "beam_pruning_drop", "final_selected",
                        "classified_gold_tokens", "unclassified_gold_tokens",
                        "final_aligned", "nbest_aligned", "nbest_recoverable",
                        "scored_not_in_nbest", "lattice_missing", "span_selection_drop", "shortlist_drop",
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
