import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("prepare_context_corpus", ROOT / "tools" / "prepare_context_corpus.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PrepareContextCorpusTests(unittest.TestCase):
    def test_document_disjoint_split_and_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            lex = root / "lexicon.tsv"
            lex.write_text("# header\nあ\t亜\t亜\t名詞\tg1\tid-a\t10\nめ\t目\t目\t名詞\tg2\tid-me\t20\n", encoding="utf-8")
            corpus = root / "tokens.tsv"
            corpus.write_text(
                "document_id\tsentence_id\treading\tsurface\tdomain\n"
                "doc-a\ts1\tあ\t亜\tgeneral\n"
                "doc-a\ts1\tめ\t目\tgeneral\n"
                "doc-b\ts2\tあ\t亜\tgeneral\n"
                "doc-c\ts3\tめ\t目\tmeeting\n", encoding="utf-8")
            out = root / "out"
            summary = MODULE.compile_corpus(lex, corpus, out)
            self.assertEqual(summary["documents"], 3)
            self.assertEqual(sum(x["documents"] for x in summary["splits"].values()), 3)
            train = (out / "train_sequences.tsv").read_text(encoding="utf-8")
            self.assertTrue(all(token in {"id-a", "id-me"} for token in train.strip().split("\t")))
            self.assertTrue((out / "dev_gold.tsv").exists())
            self.assertTrue((out / "test_gold.tsv").exists())
            for name in ("train", "dev", "test"):
                self.assertGreaterEqual(summary["splits"][name]["documents"], 1)

    def test_explicit_document_split_conflict_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            corpus = root / "tokens.tsv"
            corpus.write_text(
                "document_id\tsentence_id\treading\tsurface\tdomain\tsplit\n"
                "doc-a\ts1\tあ\t亜\tgeneral\ttrain\n"
                "doc-a\ts2\tめ\t目\tgeneral\ttest\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "multiple splits"):
                MODULE.load_corpus(corpus)

    def test_unknown_dictionary_token_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            lex = root / "lexicon.tsv"
            lex.write_text("あ\t亜\t亜\t名詞\tg1\tid-a\n", encoding="utf-8")
            corpus = root / "tokens.tsv"
            corpus.write_text(
                "document_id\tsentence_id\treading\tsurface\tdomain\tsplit\n"
                "doc-a\ts1\tめ\t目\tgeneral\ttrain\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "absent from dictionary"):
                MODULE.compile_corpus(lex, corpus, root / "out")


if __name__ == "__main__":
    unittest.main()
