import gzip
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
                "--connections", str(ROOT / "packs" / "ja-lite" / "connections.tsv"),
                "--correction-rules", str(ROOT / "packs" / "ja-lite" / "correction_rules.tsv"),
                "--license-file", str(ROOT / "packs" / "ja-lite" / "LICENSE"),
                "--license-spdx", "MIT", "--upstream", "original test fixture",
                "--pack-id", "ja-import-test", "--language-id", "ja-JP",
                "--output", str(out)
            ], check=True, capture_output=True, text=True)
            checked = subprocess.run([sys.executable, str(VERIFIER), str(out)],
                                     check=True, capture_output=True, text=True)
            report = json.loads(checked.stdout)
            self.assertTrue(report["within_limit"])
            self.assertTrue((out / "lexicon.tsv.gz").is_file())
            self.assertFalse((out / "lexicon.tsv").exists())
            with gzip.open(out / "lexicon.tsv.gz", "rt", encoding="utf-8") as compressed_lexicon:
                self.assertIn("あめ", compressed_lexicon.read())
            manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["dictionary_compression"]["algorithm"], "gzip")
            self.assertGreater(manifest["dictionary_compression"]["uncompressed_size"], 0)
            self.assertTrue((out / "connections.tsv").is_file())
            self.assertIn("connections.tsv", report["files_verified"])
            self.assertIn("correction_rules.tsv", report["files_verified"])
            ngram = (out / "ngram.tsv").read_text(encoding="utf-8")
            self.assertIn("L\train\tcandy\t1", ngram)
            manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["pack_id"], "ja-import-test")

    def test_component_license_profile_is_copied_and_validated(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            out = root / "ja-licensed"
            (root / "notices").mkdir()
            (root / "notices" / "apache.txt").write_text("Apache notice\\n", encoding="utf-8")
            (root / "notices" / "unidic.txt").write_text("UniDic BSD notice\\n", encoding="utf-8")
            profile = {
                "schema_version": 1,
                "components": [
                    {"id": "sudachidict", "version": "v20261015", "source_sha256": "1" * 64, "spdx": "Apache-2.0",
                     "commercial_use": True, "redistribution": True, "modified_redistribution": True,
                     "upstream_url": "https://github.com/WorksApplications/SudachiDict",
                     "license_file": "notices/apache.txt"},
                    {"id": "unidic-small", "version": "2011-2013", "source_sha256": "2" * 64, "spdx": "BSD-3-Clause",
                     "commercial_use": True, "redistribution": True, "modified_redistribution": True,
                     "upstream_url": "https://clrd.ninjal.ac.jp/unidic/",
                     "license_file": "notices/unidic.txt"}
                ]
            }
            profile_path = root / "profile.json"
            profile_path.write_text(json.dumps(profile), encoding="utf-8")
            subprocess.run([
                sys.executable, str(BUILDER),
                "--lexicon", str(ROOT / "packs" / "ja-lite" / "lexicon.tsv"),
                "--license-file", str(ROOT / "packs" / "ja-lite" / "LICENSE"),
                "--license-profile", str(profile_path),
                "--license-spdx", "Apache-2.0 AND BSD-3-Clause",
                "--upstream", "SudachiDict Small plus UniDic component",
                "--pack-id", "ja-licensed-test", "--language-id", "ja-JP",
                "--output", str(out)
            ], check=True, capture_output=True, text=True)
            subprocess.run([sys.executable, str(VERIFIER), str(out)],
                           check=True, capture_output=True, text=True)
            audit = subprocess.run([
                sys.executable, str(ROOT / "tools" / "validate_license_profile.py"), str(out)
            ], check=True, capture_output=True, text=True)
            result = json.loads(audit.stdout)
            self.assertEqual(result["components"], 2)
            self.assertTrue((out / "THIRD_PARTY_LICENSES" / "unidic-small.txt").is_file())
            manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(len(manifest["license"]["components"]), 2)

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
