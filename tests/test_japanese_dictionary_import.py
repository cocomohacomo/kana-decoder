import csv
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
IMPORTER = ROOT / "tools" / "import_japanese_dictionary.py"

class JapaneseDictionaryImportTests(unittest.TestCase):
    def run_import(self, fmt, rows, header=None):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = pathlib.Path(tmp.name)
        source, output = root / "source.csv", root / "lexicon.tsv"
        with source.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            if header:
                writer.writerow(header)
            writer.writerows(rows)
        result = subprocess.run([sys.executable, str(IMPORTER), "--input", str(source),
                                 "--output", str(output), "--format", fmt],
                                capture_output=True, text=True)
        return result, output

    def test_sudachi_csv(self):
        row = ["食べる", "100", "200", "-5000", "動詞", "一般", "*", "*",
               "下一段-バ行", "終止形-一般", "タベル", "食べる", "食べる",
               "A", "食べる", "食べ", "*"]
        result, output = self.run_import("sudachi-csv", [row])
        self.assertEqual(result.returncode, 0, result.stderr)
        fields = output.read_text(encoding="utf-8").splitlines()[1].split("\t")
        self.assertEqual(fields[0], "たべる")
        self.assertEqual(fields[1], "食べる")
        self.assertEqual(fields[6:], ["-5000", "0", "100", "200", "下一段-バ行", "終止形-一般"])

    def test_ipadic_csv(self):
        row = ["橋", "11", "12", "-100", "名詞", "一般", "*", "*",
               "*", "*", "橋", "ハシ", "ハシ"]
        result, output = self.run_import("ipadic-csv", [row])
        self.assertEqual(result.returncode, 0, result.stderr)
        fields = output.read_text(encoding="utf-8").splitlines()[1].split("\t")
        self.assertEqual(fields[0], "はし")
        self.assertEqual(fields[1], "橋")
        self.assertEqual(fields[2], "橋")
        self.assertEqual(fields[10:12], ["", ""])

    def test_header_csv_and_unreadable_skip(self):
        header = ["surface", "reading", "lemma", "pos", "word_cost", "left_id", "right_id"]
        rows = [["雨", "アメ", "雨", "名詞/一般", "10", "1", "2"],
                ["記号", "*", "記号", "記号", "0", "0", "0"]]
        result, output = self.run_import("header-csv", rows, header)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("雨", output.read_text(encoding="utf-8"))
        self.assertNotIn("記号", output.read_text(encoding="utf-8"))

    def test_malformed_row_fails_with_line_number(self):
        result, _ = self.run_import("sudachi-csv", [["too", "few"]])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(":1:", result.stderr)

if __name__ == "__main__":
    unittest.main()
