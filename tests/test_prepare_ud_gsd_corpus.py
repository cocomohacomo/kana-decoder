import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_ud_gsd_corpus as adapter


class UdGsdAdapterTests(unittest.TestCase):
    def test_katakana_pronunciation_and_oov_breaks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "ud"
            source.mkdir()
            lexicon = root / "lexicon.tsv"
            lexicon.write_text(
                "きょう\t今日\t今日\t名詞\t今日\tid-today\t0\n"
                "あめ\t雨\t雨\t名詞\t雨\tid-rain\t10\n"
                "ふる\t降る\t降る\t動詞\t降る\tid-fall\t0\n",
                encoding="utf-8")
            line = (
                "1\t今日\t今日\tNOUN\t_\t_\t0\troot\t_\t"
                "UniDicInfo=キョウ,今日,今日,キョウ,今日,キョウ,*,*\n"
                "2\t未知\t未知\tNOUN\t_\t_\t1\tdep\t_\t"
                "UniDicInfo=ミチ,未知,未知,ミチ,未知,ミチ,*,*\n"
                "3\t雨\t雨\tNOUN\t_\t_\t1\tdep\t_\t"
                "UniDicInfo=アメ,雨,雨,アメ,雨,アメ,*,*\n"
                "4\t降る\t降る\tVERB\t_\t_\t1\tdep\t_\t"
                "UniDicInfo=フル,降る,降る,フル,降る,フル,*,*\n\n"
            )
            for split in ("train", "dev", "test"):
                (source / f"ja_gsd-ud-{split}.conllu").write_text(
                    "# sent_id = sample\n" + line, encoding="utf-8")
            output = root / "corpus.tsv"
            report = adapter.prepare(source, lexicon, output, "fixture")
            with output.open(encoding="utf-8") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(rows), 6)
            self.assertEqual([r["reading"] for r in rows[:2]], ["あめ", "ふる"])
            self.assertEqual(rows[0]["split"], "train")
            self.assertEqual(report["splits"]["test"]["out_of_dictionary_tokens"], 1)
            manifest = json.loads((root / "ud_gsd_manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["source_commit"], "fixture")

    def test_missing_reading_breaks_run_without_fabricating_reading(self):
        self.assertIsNone(adapter.token_reading("未知語", "_"))
        self.assertEqual(adapter.token_reading("。", "_"), "。")


if __name__ == "__main__":
    unittest.main()
