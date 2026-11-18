import hashlib,importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
fetch=load("fetch_sudachi",ROOT/"tools"/"fetch_sudachi_source.py")
validate=load("validate_sudachi",ROOT/"tools"/"validate_sudachi_pack_inputs.py")
class SourcePipelineTests(unittest.TestCase):
 def test_url_allowlist(self):
  with tempfile.TemporaryDirectory() as d:
   p=pathlib.Path(d)/"lock.json"
   p.write_text('{"schema_version":1,"format":"V1","sources":[{"id":"sudachi-small-lex","url":"https://evil.example/a","sha256":"'+"a"*64+'"},{"id":"unidic-2.1.2-matrix","url":"https://d2ej7fkh96fzlu.cloudfront.net/m.zip","sha256":"'+"b"*64+'"}]}')
   with self.assertRaises(ValueError):fetch.load_sources(p)
 def test_context_id_matrix_compatibility(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);lex=root/"lex.csv";mat=root/"matrix.def"
   lex.write_text("SURFACE,LEFT_ID,RIGHT_ID\n語,1,2\n",encoding="utf-8")
   mat.write_text("3 4\n0 0 0\n",encoding="utf-8")
   self.assertEqual(validate.validate(lex,mat)["status"],"valid")
   mat.write_text("2 2\n0 0 0\n",encoding="utf-8")
   with self.assertRaises(ValueError):validate.validate(lex,mat)
   mat.write_text("3 4\n0 0 0\n0 0 1\n",encoding="utf-8")
   with self.assertRaises(ValueError):validate.validate(lex,mat)
 def test_sudachi_release_license_profile(self):
  profile=ROOT/"licenses"/"sudachi-small-20260723"/"license-profile.json"
  data=json.loads(profile.read_text())
  self.assertEqual(len(data["components"]),4)
  names=[item["license_file"] for item in data["components"]]
  self.assertEqual(len(set(names)),4)
  self.assertTrue(all((profile.parent/name).is_file() for name in names))
  lock=json.loads((ROOT/"licenses"/"sudachidict-small-20260723-source-lock.json").read_text())
  self.assertEqual(lock["sources"][1]["license_components"],["Apache-2.0","BSD-3-Clause"])
if __name__=="__main__":unittest.main()
