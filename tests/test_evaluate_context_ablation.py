import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_context_ablation as evaluator


class ContextEvaluationTests(unittest.TestCase):
    def _fixture(self, root, include_candy=True):
        lexicon = root / "lexicon.tsv"
        lexicon.write_text(
            "きょう\t今日\t今日\t名詞\t今日\tid-today\t0\t0\t1\t1\n"
            "あめ\t雨\t雨\t名詞\t雨\tid-rain\t1\t0\t1\t1\n"
            "あめ\t飴\t飴\t名詞\t飴\tid-candy\t0\t0\t1\t1\n"
            "ふる\t降る\t降る\t名詞\t降る\tid-fall\t0\t0\t1\t1\n",
            encoding="utf-8")
        seq = root / "train.tsv"
        rows = ["id-today\tid-rain\tid-fall"] * 4
        if include_candy:
            rows.append("id-today\tid-candy\tid-fall")
        seq.write_text("\n".join(rows) + "\n", encoding="utf-8")
        corpus = root / "corpus.tsv"
        corpus.write_text(
            "document_id\tsentence_id\treading\tsurface\tdomain\tsplit\n"
            "d1\ts1\tきょう\t今日\tnews-blog\ttest\n"
            "d1\ts1\tあめ\t雨\tnews-blog\ttest\n"
            "d1\ts1\tふる\t降る\tnews-blog\ttest\n",
            encoding="utf-8")
        return lexicon, corpus, seq

    def test_oracle_context_and_causal_context_are_reported_separately(self):
        with tempfile.TemporaryDirectory() as tmp:
            lexicon, corpus, seq = self._fixture(Path(tmp), True)
            result = evaluator.analyze(lexicon, corpus, seq)
            oracle = result["oracle_ablation"]
            causal = result["causal_sequential"]
            self.assertEqual(oracle["candidate_covered_tokens"], 3)
            self.assertLess(oracle["morphology_top1_accuracy"],
                            oracle["combined_unigram_context_top1_accuracy"])
            self.assertEqual(oracle["surge_resolved_tokens"], 1)
            self.assertEqual(oracle["surge_resolution_precision"], 1.0)
            self.assertFalse(causal["gold_neighbor_candidate_ids_used"])
            self.assertFalse(causal["future_context_used"])
            policies = causal["policies"]
            self.assertLess(policies["morphology"]["top1_accuracy"],
                            policies["combined"]["top1_accuracy"])
            self.assertEqual(policies["surge_gated"]["resolved_tokens"], 1)
            self.assertEqual(policies["surge_gated"]["resolution_precision"], 1.0)
            self.assertTrue(policies["surge_gated"]["fallback_to_morphology"])

    def test_surge_abstains_without_two_observed_left_context_candidates(self):
        with tempfile.TemporaryDirectory() as tmp:
            lexicon, corpus, seq = self._fixture(Path(tmp), False)
            result = evaluator.analyze(lexicon, corpus, seq)
            causal = result["causal_sequential"]["policies"]["surge_gated"]
            self.assertEqual(causal["resolved_tokens"], 0)
            self.assertGreaterEqual(causal["abstained_tokens"], 1)

    def test_candidate_cap_and_candidate_misses_are_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            lexicon, corpus, seq = self._fixture(Path(tmp), True)
            result = evaluator.analyze(lexicon, corpus, seq, max_candidates=1)
            self.assertEqual(result["candidate_limit"], 1)
            self.assertGreaterEqual(
                result["oracle_ablation"]["candidate_misses"], 1)
            self.assertGreaterEqual(
                result["causal_sequential"]["policies"]["combined"]["candidate_misses"], 1)

    def test_invalid_nonfinite_margin_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            lexicon, corpus, seq = self._fixture(Path(tmp), True)
            with self.assertRaises(ValueError):
                evaluator.analyze(lexicon, corpus, seq, context_margin=float("nan"))


if __name__ == "__main__":
    unittest.main()
