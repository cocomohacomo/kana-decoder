import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "validate_license_profile.py"

class LicenseProfileTests(unittest.TestCase):
    def make_profile(self, root, version="v1.2.3", commercial=True, redistribution=True):
        notices = root / "THIRD_PARTY_LICENSES"
        notices.mkdir(exist_ok=True)
        (notices / "component.txt").write_text("license notice\n", encoding="utf-8")
        profile = {
            "schema_version": 1,
            "components": [{
                "id": "sample-dictionary",
                "version": version,
                "source_sha256": "0" * 64,
                "spdx": "BSD-3-Clause",
                "commercial_use": commercial,
                "redistribution": redistribution,
                "modified_redistribution": True,
                "upstream_url": "https://example.org/source",
                "notice_file": "THIRD_PARTY_LICENSES/component.txt"
            }]
        }
        (root / "license-profile.json").write_text(json.dumps(profile), encoding="utf-8")

    def run_validator(self, root):
        return subprocess.run([sys.executable, str(VALIDATOR), str(root)],
                              capture_output=True, text=True)

    def test_valid_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.make_profile(root)
            result = self.run_validator(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(result.stdout)["release_profile_valid"])

    def test_floating_version_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.make_profile(root, version="main")
            self.assertNotEqual(self.run_validator(root).returncode, 0)

    def test_noncommercial_component_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.make_profile(root, commercial=False)
            self.assertNotEqual(self.run_validator(root).returncode, 0)

    def test_missing_redistribution_permission_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            self.make_profile(root, redistribution=False)
            self.assertNotEqual(self.run_validator(root).returncode, 0)

if __name__ == "__main__":
    unittest.main()
