import importlib.util
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "evaluate_surge.py"
SPEC = importlib.util.spec_from_file_location("evaluate_surge", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SurgeProbeTests(unittest.TestCase):
    def test_conflicting_claims_create_unresolved_question(self):
        payload = {
            "overall": {"exact_accuracy": 0.0},
            "details": [{
                "reading": "あめ", "expected": "雨", "predicted": "アメ",
                "candidate_generation": [{
                    "matches": [{"start": 0, "end": 2, "candidates": [
                        {"surface": "アメ"}, {"surface": "雨"}, {"surface": "飴"}
                    ]}]
                }],
                "candidate_trace": [
                    {"start": 0, "end": 2, "surface": "アメ", "local_score": 10.0},
                    {"start": 0, "end": 2, "surface": "雨", "local_score": 9.0}
                ]
            }]
        }
        result = MODULE.analyze(payload)
        self.assertEqual(result["contradiction_questions"], 1)
        self.assertEqual(result["gold_candidate_coverage_count"], 1)
        self.assertEqual(result["baseline_top1_exact_accuracy"], 0.0)
        self.assertEqual(result["auto_resolved_questions"], 0)
        self.assertFalse(result["focus_questions"][0]["resolved"])

    def test_no_conflict_creates_no_question(self):
        result = MODULE.analyze({"details": [{
            "expected": "ぴ", "candidate_generation": [{
                "matches": [{"start": 0, "end": 1, "candidates": [{"surface": "ぴ"}]}]
            }]
        }]})
        self.assertEqual(result["contradiction_questions"], 0)
        self.assertEqual(result["gold_candidate_coverage_count"], 1)


if __name__ == "__main__":
    unittest.main()
