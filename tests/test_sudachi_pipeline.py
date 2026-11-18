import hashlib,importlib.util,json,pathlib,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
def load(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
fetch=load("fetch_sudachi",ROOT/"tools"/"fetch_sudachi_source.py")
validate=load("validate_sudachi",ROOT/"tools"/"validate_sudachi_pack_inputs.py")
class SourcePipelineTests(unittest.TestCase):
 def test_pinned_source_uses_v1_archive_endpoint_and_hash(self):
  lock=ROOT/"licenses"/"sudachidict-small-20260723-source-lock.json"
  sources=fetch.load_sources(lock)
  item=sources["sudachi-small-lex"]
  self.assertEqual(item["url"],"https://sudachi.s3.ap-northeast-1.amazonaws.com/sudachidict-raw/v1/20260723/small_lex.zip")
  self.assertEqual(item["sha256"],"e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706")
 def test_validator_accepts_v1_named_header_layout(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);lex=root/"lex.csv";mat=root/"matrix.def"
   lex.write_text("Index_Form,Left_Id,Right_Id,Cost,Headword,POS1,POS2,POS3,POS4,POS5,POS6,Reading_Form,Normalized_Form,Dictionary_Form,Mode,Split_A,Split_B,Word_Structure,Synonym_Groups,Split_C,User_Data,POS_Id,Reference_Id\nあめ,1,2,-5,雨,名詞,普通名詞,一般,*,*,*,アメ,雨,雨,A,,,,,,,,\n",encoding="utf-8")
   mat.write_text("3 4\n0 0 0\n",encoding="utf-8")
   result=validate.validate(lex,mat)
   self.assertEqual(result["status"],"valid");self.assertEqual(result["lexicon_rows"],1)

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
