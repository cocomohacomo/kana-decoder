import hashlib
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import compile_surge_clusters as compiler


class SurgeClusterSerializationTests(unittest.TestCase):
    def fixture(self):
        return (
            {"id-a": 0, "id-b": 0, "id-c": 1},
            {0: {("id-x", 1): 12, ("id-y", 2): 3},
             1: {("id-z", 1): 7}},
            {(0, 1): 20, (0, 2): 8, (1, 1): 9},
            {"train_sequences": 100, "vocabulary_considered": 20,
             "window": 2, "cluster_count": 2, "clustered_tokens": 3},
        )

    def test_serialization_is_deterministic_and_manifest_hash_matches(self):
        args = self.fixture()
        first, first_stats = compiler.serialize_model(*args, gap_threshold=0.05)
        second, second_stats = compiler.serialize_model(*args, gap_threshold=0.05)
        self.assertEqual(first, second)
        self.assertEqual(first_stats, second_stats)
        self.assertEqual(first_stats["sha256"], hashlib.sha256(first).hexdigest())
        self.assertIn(b"KANA_SURGE_CLUSTER\t1\n", first)
        self.assertIn(b"EVIDENCE\t0\t1\tid-x\t12\n", first)
        self.assertIn(b"EVIDENCE\t0\t2\tid-y\t3\n", first)

    def test_byte_limit_fails_closed(self):
        args = self.fixture()
        with self.assertRaisesRegex(ValueError, "exceeds byte limit"):
            compiler.serialize_model(*args, gap_threshold=0.05, max_bytes=8)

    def test_invalid_ids_and_distances_are_rejected(self):
        token_cluster, evidence, totals, metadata = self.fixture()
        token_cluster = {"bad\tid": 0}
        with self.assertRaisesRegex(ValueError, "invalid candidate_id"):
            compiler.serialize_model(token_cluster, evidence, totals, metadata, 0.05)
        evidence = {0: {("id-x", 3): 1}}
        with self.assertRaisesRegex(ValueError, "distance must be 1 or 2"):
            compiler.serialize_model({"id-a": 0}, evidence, totals, metadata, 0.05)

    def test_record_count_limits_are_enforced(self):
        token_cluster, evidence, totals, metadata = self.fixture()
        many = {f"id-{i}": 0 for i in range(compiler.HARD_MAX_MEMBERSHIPS + 1)}
        with self.assertRaisesRegex(ValueError, "membership count exceeds"):
            compiler.serialize_model(many, {}, {}, metadata, 0.05)


if __name__ == "__main__":
    unittest.main()
