import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILDER = ROOT / "tools" / "build_language_pack.py"
VERIFIER = ROOT / "tools" / "verify_pack.py"

class PackBuilderTests(unittest.TestCase):
    def test_import_and_train_pack(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "ja-imported"
            subprocess.run([
                sys.executable, str(BUILDER),
                "--lexicon", str(ROOT / "packs" / "ja-lite" / "lexicon.tsv"),
                "--sequences", str(ROOT / "tests" / "data" / "ja-training-sequences.tsv"),
                "--license-file", str(ROOT / "packs" / "ja-lite" / "LICENSE"),
                "--license-spdx", "MIT", "--upstream", "original test fixture",
                "--pack-id", "ja-import-test", "--language-id", "ja-JP",
                "--output", str(out)
            ], check=True, capture_output=True, text=True)
            checked = subprocess.run([sys.executable, str(VERIFIER), str(out)],
                                     check=True, capture_output=True, text=True)
            report = json.loads(checked.stdout)
            self.assertTrue(report["within_limit"])
            self.assertTrue((out / "connections.tsv").exists() is False)
            ngram = (out / "ngram.tsv").read_text(encoding="utf-8")
            self.assertIn("L\train\tcandy\t1", ngram)
            manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["pack_id"], "ja-import-test")

    def test_malformed_dictionary_row_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            bad = root / "bad.tsv"
            bad.write_text("kana\tsurface\n", encoding="utf-8")
            command = [
                sys.executable, str(BUILDER), "--lexicon", str(bad),
                "--license-file", str(ROOT / "packs" / "ja-lite" / "LICENSE"),
                "--license-spdx", "MIT", "--upstream", "test",
                "--pack-id", "bad", "--language-id", "ja-JP",
                "--output", str(root / "out")
            ]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

if __name__ == "__main__":
    unittest.main()
