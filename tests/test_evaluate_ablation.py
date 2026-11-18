import importlib.util
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "evaluate_ablation.py"
SPEC = importlib.util.spec_from_file_location("evaluate_ablation", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class AblationTests(unittest.TestCase):
    def test_context_can_resolve_when_morphology_prefers_wrong_candidate(self):
        payload = {"overall": {"exact_accuracy": 0.5, "cer": 0.2}, "details": [{
            "reading": "はし", "expected": "橋",
            "candidate_generation": [{"matches": [{"start": 0, "end": 2, "candidates": [
                {"surface": "箸"}, {"surface": "橋"}]}]}],
            "candidate_trace": [
                {"start": 0, "end": 2, "surface": "箸", "morphology_score": -1,
                 "local_score": -1, "context_evidence_score": -1.4},
                {"start": 0, "end": 2, "surface": "橋", "morphology_score": -3,
                 "local_score": -3, "context_evidence_score": -0.2},
            ]}]}
        result = MODULE.analyze(payload, 0.5)
        self.assertEqual(result["candidate_coverage"], 1.0)
        self.assertEqual(result["morphology_top1_accuracy"], 0.0)
        self.assertEqual(result["combined_local_top1_accuracy"], 0.0)
        self.assertEqual(result["surge_resolved_cases"], 1)
        self.assertEqual(result["surge_correct_resolutions"], 1)
        self.assertEqual(result["surge_resolution_precision"], 1.0)

    def test_empty_context_abstains_instead_of_using_morphology(self):
        payload = {"details": [{
            "reading": "あめ", "expected": "雨",
            "candidate_generation": [{"matches": [{"start": 0, "end": 2, "candidates": [
                {"surface": "アメ"}, {"surface": "雨"}]}]}],
            "candidate_trace": [
                {"start": 0, "end": 2, "surface": "アメ", "morphology_score": -1,
                 "local_score": -1, "context_evidence_score": 0},
                {"start": 0, "end": 2, "surface": "雨", "morphology_score": -9,
                 "local_score": -9, "context_evidence_score": 0},
            ]}]}
        result = MODULE.analyze(payload)
        self.assertEqual(result["morphology_top1_accuracy"], 0.0)
        self.assertEqual(result["surge_resolved_cases"], 0)
        self.assertEqual(result["surge_unresolved_cases"], 1)

    def test_multispan_case_is_not_misaligned_to_sentence_gold(self):
        payload = {"details": [{
            "reading": "きしゃ", "expected": "汽車",
            "candidate_generation": [{"matches": [{"start": 0, "end": 2, "candidates": [
                {"surface": "汽"}, {"surface": "記"}]}]}],
            "candidate_trace": []}]}
        result = MODULE.analyze(payload)
        self.assertEqual(result["eligible_single_span_cases"], 0)
        self.assertIsNone(result["candidate_coverage"])


if __name__ == "__main__":
    unittest.main()
