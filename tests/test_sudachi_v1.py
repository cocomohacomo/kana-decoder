import csv,hashlib,json,pathlib,subprocess,sys,tempfile,unittest,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1];IMPORTER=ROOT/"tools"/"import_sudachi_v1.py";VERIFY=ROOT/"tools"/"verify_sudachi_source.py"
class SudachiV1Tests(unittest.TestCase):
 def test_actual_v1_header_layout_23_columns(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"lex.csv";out=root/"out.tsv"
   header=["Index_Form","Left_Id","Right_Id","Cost","Headword","POS1","POS2","POS3","POS4","POS5","POS6","Reading_Form","Normalized_Form","Dictionary_Form","Mode","Split_A","Split_B","Word_Structure","Synonym_Groups","Split_C","User_Data","POS_Id","Reference_Id"]
   row=["あめ","1","2","-5","雨","名詞","普通名詞","一般","*","*","*","アメ","雨","雨","A","","","","","","","",""]
   with src.open("w",encoding="utf-8",newline="") as f:
    w=csv.writer(f);w.writerow(header);w.writerow(row)
   r=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(src),"--output",str(out)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   cols=out.read_text(encoding="utf-8").splitlines()[1].split("\t")
   self.assertEqual(cols[0],"あめ");self.assertEqual(cols[1],"雨");self.assertEqual(cols[8:10],["1","2"])
 def test_headerless_v0_data_is_not_misclassified_as_v1(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"legacy.csv";out=root/"out.tsv"
   src.write_text(" ,5967,5967,10, ,空白,*,*,*,*,*,キゴウ, ,*,A,*,*,*,*\n",encoding="utf-8")
   r=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(src),"--output",str(out)],capture_output=True,text=True)
   self.assertNotEqual(r.returncode,0)
   self.assertIn("Headerless V0 CSV",r.stderr)

 def make_csv(self,path,header=None):
  header=header or ["SURFACE","LEFT_ID","RIGHT_ID","COST","WRITING","POS1","POS2","POS3","POS4","POS5","POS6","READING_FORM","NORMALIZED_FORM","DICTIONARY_FORM","MODE","SPLIT_A","SPLIT_B","WORD_STRUCTURE"]
  row=["食べる","100","200","-5000","食べる","動詞","一般","*","*","下一段-バ行","終止形-一般","タベル","食べる","食べる","A","","",""]
  with path.open("w",encoding="utf-8",newline="") as f:w=csv.writer(f);w.writerow(header);w.writerow(row)
 def test_v1_import(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"lex.csv";self.make_csv(src);out=root/"out.tsv"
   r=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(src),"--output",str(out)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr);cols=out.read_text().splitlines()[1].split("\t")
   self.assertEqual(cols[0],"たべる");self.assertEqual(cols[8:10],["100","200"])
 def test_pos5_pos6_are_not_misread_as_conjugation(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"lex.csv";out=root/"out.tsv"
   header=["Index_Form","Left_Id","Right_Id","Cost","Headword","POS1","POS2","POS3","POS4","POS5","POS6","Reading_Form","Normalized_Form","Dictionary_Form","Mode","Split_A","Split_B","Word_Structure","Synonym_Groups","Split_C","User_Data","POS_Id","Reference_Id"]
   row=["猫","1","2","-5","猫","名詞","普通名詞","一般","*","形状詞可能","*","ネコ","猫","猫","A","","","","","","","",""]
   with src.open("w",encoding="utf-8",newline="") as f:
    w=csv.writer(f);w.writerow(header);w.writerow(row)
   r=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(src),"--output",str(out)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   cols=out.read_text(encoding="utf-8").splitlines()[1].split("\\t")
   self.assertEqual(cols[10:12],["",""])

 def test_explicit_conjugation_columns_are_preserved(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"lex.csv";out=root/"out.tsv"
   header=["Index_Form","Left_Id","Right_Id","Cost","Headword","POS1","POS2","POS3","POS4","POS5","POS6","Reading_Form","Normalized_Form","Dictionary_Form","Mode","Conjugation_Type","Conjugation_Form"]
   row=["食べた","1","2","-5","食べる","動詞","一般","*","*","*","*","タベタ","食べた","食べる","A","下一段-バ行","連用形-促音便"]
   with src.open("w",encoding="utf-8",newline="") as f:
    w=csv.writer(f);w.writerow(header);w.writerow(row)
   r=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(src),"--output",str(out)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   cols=out.read_text(encoding="utf-8").splitlines()[1].split("\\t")
   self.assertEqual(cols[1],"食べる")
   self.assertEqual(cols[10:12],["下一段-バ行","連用形-促音便"])

 def test_header_case_underscore_and_extra(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"lex.csv"
   h=["surface","LEFTID","right_id","cost","writing","pos1","pos2","pos3","pos4","pos5","pos6","readingform","normalizedform","dictionaryform","mode","split_a","split_b","word_structure","EXTRA"]
   row=["食べる","100","200","-5000","食べる","動詞","一般","*","*","下一段-バ行","終止形-一般","タベル","食べる","食べる","A","","","","ignored"]
   with src.open("w",encoding="utf-8",newline="") as f:w=csv.writer(f);w.writerow(h);w.writerow(row)
   r=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(src),"--output",str(root/"out")],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr);self.assertIn("extra_columns_ignored",r.stdout)
 def test_malformed_width_rejected(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"bad.csv";self.make_csv(src)
   with src.open("a") as f:f.write("short,row\n")
   r=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(src),"--output",str(root/"out")],capture_output=True,text=True);self.assertNotEqual(r.returncode,0)
 def test_lock_driven_archive_verify(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);small=root/"s.zip";matrix=root/"m.zip"
   with zipfile.ZipFile(small,"w") as z:z.writestr("nested/small_lex.csv","csv")
   with zipfile.ZipFile(matrix,"w") as z:z.writestr("matrix.def","1 1\n0 0 0\n")
   lock={"schema_version":1,"format":"V1","sources":[{"id":"sudachi-small-lex","url":"https://example.invalid/s.zip","sha256":hashlib.sha256(small.read_bytes()).hexdigest(),"member":"small_lex.csv"},{"id":"unidic-2.1.2-matrix","url":"https://example.invalid/m.zip","sha256":hashlib.sha256(matrix.read_bytes()).hexdigest(),"member":"matrix.def"}]}
   lp=root/"lock.json";lp.write_text(json.dumps(lock));out=root/"out"
   r=subprocess.run([sys.executable,str(VERIFY),"--lock",str(lp),"--small-zip",str(small),"--matrix-zip",str(matrix),"--output",str(out)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr);self.assertTrue((out/"small_lex.csv").is_file())

 def test_matrix_conversion_and_invalid_cells(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp);src=root/"matrix.def";out=root/"connections.tsv"
   src.write_text("3 4\n0 0 0\n1 2 -20\n",encoding="utf-8")
   r=subprocess.run([sys.executable,str(IMPORTER),"matrix","--input",str(src),"--output",str(out)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(out.read_text().splitlines()[0],"# matrix 3 4");self.assertEqual(out.read_text().splitlines()[2],"1\t2\t-20")
   src.write_text("2 2\n2 0 1\n2 0 2\n",encoding="utf-8")
   r=subprocess.run([sys.executable,str(IMPORTER),"matrix","--input",str(src),"--output",str(out)],capture_output=True,text=True)
   self.assertNotEqual(r.returncode,0)

class CorpusSplitTests(unittest.TestCase):
 def run_tool(self,root):
  return subprocess.run([sys.executable,str(ROOT/"tools"/"validate_corpus_splits.py"),"--train",str(root/"train.tsv"),"--dev",str(root/"dev.tsv"),"--test",str(root/"test.tsv")],capture_output=True,text=True)
 def test_valid_and_leakage(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp)
   for split,group in [("train","a"),("dev","b"),("test","c")]: (root/f"{split}.tsv").write_text(f"あめ\t雨\tdialogue\t{group}\n")
   r=self.run_tool(root);self.assertEqual(r.returncode,0,r.stderr);self.assertEqual(json.loads(r.stdout)["status"],"valid")
   (root/"dev.tsv").write_text("あめ\t雨\tdialogue\ta\n");r=self.run_tool(root);self.assertNotEqual(r.returncode,0);self.assertIn("leakage",r.stderr)
if __name__=="__main__":unittest.main()
