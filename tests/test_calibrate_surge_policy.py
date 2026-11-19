import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CALIBRATOR = ROOT / "tools" / "calibrate_surge_policy.py"


class SurgePolicyCalibrationTests(unittest.TestCase):
    def test_dev_only_grid_selection_and_provenance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evaluator = root / "fake-evaluator"
            evaluator.write_text(
                "#!/usr/bin/env python3\n"
                "import json, pathlib, sys\n"
                "assert sys.argv[3] == 'dev', 'calibrator must only use dev'\n"
                "policy = {}\n"
                "for line in pathlib.Path(sys.argv[5]).read_text().splitlines():\n"
                "    key, value = line.split('\\t')\n"
                "    policy[key] = float(value)\n"
                "cer = (0.5 - 0.1 * policy['ngram'] - 0.1 * policy['context_cluster']\n"
                "       - 0.01 * policy['typed_entity'])\n"
                "exact = 0.1 + 0.05 * policy['pos_ngram']\n"
                "print(json.dumps({'split':'dev','cases':10,"
                "'surge_core':{'cer':cer,'sentence_exact':exact,'overrides':2,"
                "'deferred':1,'runtime_failures':0,'decode_failures':0,'typed_entities_found':3,'typed_entity_evidence_added':2,'selected_typed_entity_evidence':1,'typed_entities_persisted':3},"
                "'elapsed_seconds':0.2,'peak_rss_kib':1000,\n"
                "'candidate_generation':{'mean_paths':4.0,'surface_exact_recall_at_1':0.2,\n"
                "'surface_exact_recall_at_5':0.4,'surface_exact_recall_at_10':0.5,\n"
                "'oracle_cer_at_5':0.3,'oracle_cer_at_10':0.25}}))\n",
                encoding="utf-8",
            )
            evaluator.chmod(0o755)
            corpus = root / "corpus.tsv"
            train = root / "train.tsv"
            lock = root / "source-lock.json"
            corpus.write_text("dev-corpus\n", encoding="utf-8")
            train.write_text("train-sequences\n", encoding="utf-8")
            lock.write_text('{"revision":"fixed"}\n', encoding="utf-8")
            policy = root / "selected-policy.tsv"
            report = root / "calibration.json"
            subprocess.run(
                [
                    sys.executable, str(CALIBRATOR),
                    "--evaluator", str(evaluator),
                    "--pack", str(root / "pack"),
                    "--corpus", str(corpus),
                    "--train-sequences", str(train),
                    "--source-lock", str(lock),
                    "--output-policy", str(policy),
                    "--output-report", str(report),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = json.loads(report.read_text(encoding="utf-8"))
            self.assertEqual(result["split"], "dev")
            self.assertEqual(len(result["candidates"]), 8)
            self.assertEqual(
                result["source_lock_sha256"],
                hashlib.sha256(lock.read_bytes()).hexdigest(),
            )
            self.assertEqual(result["best_policy"]["name"], "typed_entity_strong")
            self.assertTrue(policy.is_file())
            self.assertIn("typed_entity\t1.5", policy.read_text(encoding="utf-8"))
            self.assertEqual(result["best_policy"]["typed_entities_found"], 3)
            self.assertEqual(result["best_policy"]["typed_entity_evidence_added"], 2)
            self.assertEqual(result["best_policy"]["selected_typed_entity_evidence"], 1)
            self.assertEqual(result["best_policy"]["typed_entities_persisted"], 3)
            self.assertEqual(result["best_policy"]["candidate_generation"]["surface_exact_recall_at_5"], 0.4)
            self.assertEqual(result["best_policy"]["candidate_generation"]["surge_exact_given_candidate_at_10"], 0.6)
            self.assertIn("normalization_renderer\t", policy.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
