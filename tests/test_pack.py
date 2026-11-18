import importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("verify_pack",ROOT/"tools"/"verify_pack.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class PackTests(unittest.TestCase):
 def test_fixture_pack_verifies_under_30_mib(self):
  r=module.verify_pack(ROOT/"packs"/"ja-lite");self.assertTrue(r["within_limit"]);self.assertEqual(r["pack_id"],"ja-lite-reference");self.assertEqual(len(r["files_verified"]),5)
 def test_checksum_mismatch_is_rejected(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=pathlib.Path(tmp);src=ROOT/"packs"/"ja-lite"
   for p in src.iterdir():
    if p.is_file():(d/p.name).write_bytes(p.read_bytes())
   with (d/"lexicon.tsv").open("ab") as f:f.write(b"corrupt")
   with self.assertRaisesRegex(ValueError,"size mismatch|checksum mismatch"):module.verify_pack(d)
 def test_missing_license_rejected(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=pathlib.Path(tmp);src=ROOT/"packs"/"ja-lite"
   for p in src.iterdir():
    if p.is_file() and p.name!="LICENSE":(d/p.name).write_bytes(p.read_bytes())
   with self.assertRaisesRegex(ValueError,"license"):module.verify_pack(d)

 def test_duplicate_manifest_paths_rejected(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=pathlib.Path(tmp);src=ROOT/"packs"/"ja-lite"
   for p in src.iterdir():
    if p.is_file():(d/p.name).write_bytes(p.read_bytes())
   m=json.loads((d/"manifest.json").read_text(encoding="utf-8"));m["files"].append(dict(m["files"][0]))
   (d/"manifest.json").write_text(json.dumps(m),encoding="utf-8")
   with self.assertRaisesRegex(ValueError,"duplicate"):module.verify_pack(d)
 def test_declared_resource_limit_is_enforced(self):
  with tempfile.TemporaryDirectory() as tmp:
   d=pathlib.Path(tmp);src=ROOT/"packs"/"ja-lite"
   for p in src.iterdir():
    if p.is_file():(d/p.name).write_bytes(p.read_bytes())
   m=json.loads((d/"manifest.json").read_text(encoding="utf-8"));m["resource_limits"]["max_expanded_bytes"]=1
   (d/"manifest.json").write_text(json.dumps(m),encoding="utf-8")
   with self.assertRaisesRegex(ValueError,"resource limit"):module.verify_pack(d)
if __name__=="__main__":unittest.main()
