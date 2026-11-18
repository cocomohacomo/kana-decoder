import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class SequentialDecoderEvaluatorTests(unittest.TestCase):
    def test_runtime_uses_predicted_left_history_without_gold_boundaries(self):
        executable = Path(sys.argv[1])
        pack = Path(sys.argv[2])
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            train = root / "train_sequences.tsv"
            train.write_text(("rain\tchopsticks\n" * 20), encoding="utf-8")
            corpus = root / "test_tokens.tsv"
            corpus.write_text(
                "document_id\tsentence_id\treading\tsurface\tdomain\tsplit\n"
                "d1\ts1\tあめ\t雨\tnews-blog\ttest\n"
                "d1\ts1\tはし\t箸\tnews-blog\ttest\n",
                encoding="utf-8")
            completed = subprocess.run(
                [str(executable), str(pack), str(train), str(corpus)],
                check=True, capture_output=True, text=True)
            result = json.loads(completed.stdout)
            self.assertFalse(result["gold_token_boundaries_used_for_decoding"])
            self.assertFalse(result["future_context_used"])
            self.assertEqual(result["source_test_tokens"], 2)
            self.assertEqual(result["morphology_only"]["exact_matches"], 0)
            self.assertEqual(result["trained_unigram_left_context"]["exact_matches"], 1)
            self.assertEqual(
                result["trained_unigram_left_context"]["exact_token_alignment"], 2)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
