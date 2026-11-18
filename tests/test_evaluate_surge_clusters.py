import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_surge_clusters as evaluator


class SurgeClusterTests(unittest.TestCase):
    def test_cluster_generation_is_deterministic_and_bounded(self):
        sequences = [["a", "x", "b"], ["a", "x", "b"],
                     ["a", "y", "c"], ["a", "y", "c"]]
        first = evaluator.make_clusters(
            sequences, max_vocab=20, max_context_df=20,
            min_similarity=0.01, max_cluster_size=3)
        second = evaluator.make_clusters(
            sequences, max_vocab=20, max_context_df=20,
            min_similarity=0.01, max_cluster_size=3)
        self.assertEqual(first[0], second[0])
        self.assertEqual(first[3], second[3])
        self.assertLessEqual(first[3]["largest_cluster"], 3)
        self.assertTrue(all(0 <= cid < first[3]["cluster_count"]
                            for cid in first[0].values()))

    def test_cluster_context_can_resolve_and_tracks_predicted_history(self):
        candidates = {
            "きょう": [{"reading": "きょう", "surface": "今日",
                        "candidate_id": "id-weather", "word_cost": 0,
                        "connection_cost": 0}],
            "あめ": [
                {"reading": "あめ", "surface": "飴", "candidate_id": "id-candy",
                 "word_cost": 0, "connection_cost": 0},
                {"reading": "あめ", "surface": "雨", "candidate_id": "id-rain",
                 "word_cost": 10, "connection_cost": 0},
            ],
        }
        sentences = [[
            ("きょう", "今日", candidates["きょう"][0]),
            ("あめ", "雨", {"candidate_id": "id-rain"}),
        ]]
        metrics = evaluator.evaluate(
            sentences, candidates, {"id-weather": 0},
            {0: {("id-rain", 1): 10, ("id-candy", 1): 1}},
            {(0, 1): 11},
            beta=1.0, margin=0.0)
        self.assertEqual(metrics["morphology_accuracy"], 0.5)
        self.assertEqual(metrics["cluster_gated_accuracy"], 1.0)
        self.assertEqual(metrics["resolved_tokens"], 1)
        self.assertEqual(metrics["resolution_precision"], 1.0)

    def test_cluster_evidence_preserves_left_distance(self):
        candidates = {
            "ctx": [{"reading": "ctx", "surface": "Context",
                     "candidate_id": "id-context", "word_cost": 0,
                     "connection_cost": 0}],
            "x": [
                {"reading": "x", "surface": "X", "candidate_id": "id-x",
                 "word_cost": 0, "connection_cost": 0},
                {"reading": "x", "surface": "Y", "candidate_id": "id-y",
                 "word_cost": 1, "connection_cost": 0},
            ],
        }
        sentences = [[
            ("ctx", "Context", candidates["ctx"][0]),
            ("x", "X", {"candidate_id": "id-x"}),
        ]]
        # Evidence observed only at distance 2 must not resolve at distance 1.
        metrics = evaluator.evaluate(
            sentences, candidates, {"id-context": 0},
            {0: {("id-x", 2): 9, ("id-y", 2): 1}},
            {(0, 2): 10}, beta=1.0, margin=0.0)
        self.assertEqual(metrics["resolved_tokens"], 0)
        self.assertEqual(metrics["abstained_tokens"], 1)

if __name__ == "__main__":
    unittest.main()
