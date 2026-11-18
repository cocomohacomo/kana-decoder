import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import evaluate_context_ablation as evaluator


class ContextAblationTests(unittest.TestCase):
    def _fixture(self, root, include_candy=True):
        lexicon = root / "lexicon.tsv"
        lexicon.write_text(
            "きょう\t今日\t今日\t名詞\t今日\tid-today\t0\t0\t1\t1\n"
            "あめ\t雨\t雨\t名詞\t雨\tid-rain\t1\t0\t1\t1\n"
            "あめ\t飴\t飴\t名詞\t飴\tid-candy\t0\t0\t1\t1\n"
            "ふる\t降る\t降る\t動詞\t降る\tid-fall\t0\t0\t1\t1\n",
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

    def test_context_resolves_morphology_error_and_surge_is_evidence_gated(self):
        with tempfile.TemporaryDirectory() as tmp:
            lexicon, corpus, seq = self._fixture(Path(tmp), True)
            result = evaluator.analyze(lexicon, corpus, seq)
            self.assertEqual(result["candidate_covered_tokens"], 3)
            self.assertLess(result["morphology_top1_accuracy"],
                            result["combined_unigram_context_top1_accuracy"])
            self.assertEqual(result["surge_resolved_tokens"], 1)
            self.assertEqual(result["surge_resolution_precision"], 1.0)

    def test_surge_abstains_without_two_supported_candidates(self):
        with tempfile.TemporaryDirectory() as tmp:
            lexicon, corpus, seq = self._fixture(Path(tmp), False)
            result = evaluator.analyze(lexicon, corpus, seq)
            self.assertEqual(result["surge_resolved_tokens"], 0)
            self.assertGreaterEqual(result["surge_abstained_tokens"], 1)

    def test_context_scoring_matches_smoothing_and_candidate_cap(self):
        with tempfile.TemporaryDirectory() as tmp:
            lexicon, corpus, seq = self._fixture(Path(tmp), True)
            result = evaluator.analyze(lexicon, corpus, seq, context_margin=0.5,
                                       max_candidates=1)
            self.assertEqual(result["candidate_limit"], 1)
            self.assertEqual(result["candidate_misses"], 1)


if __name__ == "__main__":
    unittest.main()
