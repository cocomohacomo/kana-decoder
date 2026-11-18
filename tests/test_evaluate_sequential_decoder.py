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
            self.assertEqual(dev_result["configurations_evaluated"], 72)
            self.assertGreater(dev_result["selected_smoothing_alpha"], 0.0)
            self.assertTrue(calibration.is_file())
            completed = subprocess.run(
                [str(executable), str(pack), str(train), str(corpus),
                 "test", str(calibration)],
                check=True, capture_output=True, text=True)
            result = json.loads(completed.stdout)
            self.assertFalse(result["gold_token_boundaries_used_for_decoding"])
            self.assertFalse(result["future_context_used"])
            self.assertEqual(result["split"], "test")
            self.assertGreaterEqual(result["dev_selected_context_weight"], 0.0)
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

            # Calibration artifacts are strict three-number records. Reject trailing
            # fields and non-finite values rather than silently accepting corruption.
            for payload in ("1.0\t0.5\t0.5\textra\n",
                            "nan\t0.5\t0.5\n"):
                malformed = root / "malformed-calibration.tsv"
                malformed.write_text(payload, encoding="utf-8")
                rejected = subprocess.run(
                    [str(executable), str(pack), str(train), str(corpus),
                     "test", str(malformed)],
                    capture_output=True, text=True)
                self.assertNotEqual(rejected.returncode, 0)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
