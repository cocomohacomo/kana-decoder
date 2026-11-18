import csv,hashlib,json,pathlib,subprocess,sys,tempfile,unittest,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1];TOOL=ROOT/"tools"/"build_sudachi_pack.py"
class SudachiPackBuildTests(unittest.TestCase):
 def test_end_to_end_synthetic_pack(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);archives=root/"archives";archives.mkdir()
   header=["SURFACE","LEFT_ID","RIGHT_ID","COST","WRITING","POS1","POS2","POS3","POS4","POS5","POS6","READING_FORM","NORMALIZED_FORM","DICTIONARY_FORM","MODE","SPLIT_A","SPLIT_B","WORD_STRUCTURE"]
   lex=root/"small_lex.csv"
   with lex.open("w",encoding="utf-8",newline="") as f:
    w=csv.writer(f);w.writerow(header);w.writerow(["雨","1","2","-5000","雨","名詞","普通名詞","一般","*","*","*","アメ","雨","雨","A","","",""])
   matrix=root/"matrix.def";matrix.write_text("3 4\n2 1 -20\n",encoding="utf-8")
   small_zip=archives/"small_lex.zip";matrix_zip=archives/"matrix.def.zip"
   with zipfile.ZipFile(small_zip,"w") as z:z.write(lex,"nested/small_lex.csv")
   with zipfile.ZipFile(matrix_zip,"w") as z:z.write(matrix,"matrix.def")
   sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
   lock={"schema_version":1,"dictionary":"test","release":"20261119","format":"V1","sources":[
    {"id":"sudachi-small-lex","url":"https://example.invalid/small.zip","sha256":sha(small_zip),"member":"small_lex.csv"},
    {"id":"unidic-2.1.2-matrix","url":"https://example.invalid/matrix.zip","sha256":sha(matrix_zip),"member":"matrix.def"}]}
   lock_path=root/"lock.json";lock_path.write_text(json.dumps(lock))
   license_dir=root/"licenses";license_dir.mkdir();(license_dir/"MIT.txt").write_text("MIT test notice\n")
   profile={"schema_version":1,"components":[{"id":"test-dictionary","version":"2026.11","source_sha256":sha(small_zip),"spdx":"MIT","commercial_use":True,"redistribution":True,"modified_redistribution":True,"upstream_url":"https://example.invalid/source.zip","license_file":"MIT.txt"}]}
   profile_path=license_dir/"profile.json";profile_path.write_text(json.dumps(profile))
   notice=license_dir/"PACK-LICENSE.txt";notice.write_text("MIT\n")
   legal=license_dir/"UPSTREAM_LEGAL.txt";legal.write_text("Upstream legal notice\n")
   output=root/"pack";work=root/"work"
   result=subprocess.run([sys.executable,str(TOOL),"--small-zip",str(small_zip),"--matrix-zip",str(matrix_zip),
    "--lock",str(lock_path),"--license-profile",str(profile_path),"--license-file",str(notice),
    "--upstream-legal-notice",str(legal),"--license-spdx","MIT","--output",str(output),"--work-dir",str(work)],
    capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stderr+result.stdout)
   report=json.loads(result.stdout);self.assertEqual(report["status"],"built-and-verified")
   self.assertTrue((output/"provenance.json").is_file());self.assertTrue((output/"UPSTREAM_LEGAL.txt").is_file())
   manifest=json.loads((output/"manifest.json").read_text());self.assertIn("connections.tsv",[x["path"] for x in manifest["files"]])

 def test_subcommand_failure_includes_captured_diagnostics(self):
  import importlib.util
  spec=importlib.util.spec_from_file_location("build_sudachi_pack",TOOL)
  module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  with self.assertRaisesRegex(RuntimeError,"validator-detail-from-stderr"):
   module.run_json([sys.executable,"-c","import sys; print('validator-detail-from-stderr', file=sys.stderr); sys.exit(1)"])

if __name__=="__main__":unittest.main()
