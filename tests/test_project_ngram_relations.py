import hashlib
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import project_ngram_relations as projector

class NgramRelationProjectorTests(unittest.TestCase):
    def test_direction_distance_and_order_are_preserved(self):
        events, meta = projector.build_relation_events(
            [["a", "b", "c"], ["a", "b", "c"], ["a", "b", "c"]],
            max_distance=2, min_count=1)
        indexed = {(e["source"]["key"], e["target"]["key"],
                    e["relation"]["direction"], e["relation"]["distance"]): e for e in events}
        self.assertIn(("a", "b", "right", 1), indexed)
        self.assertIn(("b", "a", "left", 1), indexed)
        self.assertIn(("a", "c", "right", 2), indexed)
        self.assertTrue(indexed[("a", "b", "right", 1)]["relation"]["ordered"])
        self.assertEqual(meta["train_sequences"], 3)

    def test_rare_evidence_is_shrunk_and_reproducible(self):
        seqs = [["rare", "target"], ["common", "target"]]
        a, _ = projector.build_relation_events(seqs, min_count=1, reliability_prior=5)
        b, _ = projector.build_relation_events(seqs, min_count=1, reliability_prior=5)
        self.assertEqual(a, b)
        rare = next(e for e in a if e["source"]["key"] == "rare")
        self.assertLess(rare["relation"]["reliability"], 0.5)

    def test_min_count_filters_singletons(self):
        events, _ = projector.build_relation_events(
            [["one", "unique"], ["two", "different"]], min_count=2)
        self.assertEqual(events, [])

    def test_serialization_manifest_digest_and_determinism(self):
        events, meta = projector.build_relation_events([["a", "b"], ["a", "b"]], min_count=1)
        p1, m1 = projector.serialize_events(events, meta)
        p2, m2 = projector.serialize_events(events, meta)
        self.assertEqual(p1, p2)
        self.assertEqual(m1, m2)
        self.assertEqual(m1["sha256"], hashlib.sha256(p1).hexdigest())
        decoded = [json.loads(line) for line in p1.decode().splitlines()]
        self.assertTrue(all(x["schema"] == "SURGE_RELATION_EVENT" for x in decoded))
        self.assertEqual(m1["cluster_assignment"], "surge_core")

    def test_bounds_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "max_distance"):
            projector.build_relation_events([["a", "b"]], max_distance=5)
        events, meta = projector.build_relation_events([["a", "b"]], min_count=1)
        with self.assertRaisesRegex(ValueError, "byte limit"):
            projector.serialize_events(events, meta, max_bytes=1)

if __name__ == "__main__":
    unittest.main()
