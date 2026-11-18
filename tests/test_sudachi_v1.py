import csv
import hashlib
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[1]
IMPORTER=ROOT/"tools"/"import_sudachi_v1.py"
VERIFY=ROOT/"tools"/"verify_sudachi_source.py"

class SudachiV1Tests(unittest.TestCase):
    def test_v1_header_lexicon_and_matrix(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp)
            source=root/"small_lex.csv"
            header=["SURFACE","LEFT_ID","RIGHT_ID","COST","WRITING","POS1","POS2","POS3","POS4","POS5","POS6","READING_FORM","NORMALIZED_FORM","DICTIONARY_FORM","MODE","SPLIT_A","SPLIT_B","WORD_STRUCTURE"]
            row=["食べる","100","200","-5000","食べる","動詞","一般","*","*","下一段-バ行","終止形-一般","タベル","食べる","食べる","A","","",""]
            with source.open("w",encoding="utf-8",newline="") as f:
                w=csv.writer(f);w.writerow(header);w.writerow(row)
            lex=root/"lexicon.tsv"
            run=subprocess.run([sys.executable,str(IMPORTER),"lexicon","--input",str(source),"--output",str(lex)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            cols=lex.read_text(encoding="utf-8").splitlines()[1].split("\t")
            self.assertEqual(cols[0],"たべる")
            self.assertEqual(cols[8:10],["100","200"])
            self.assertEqual(cols[10:12],["下一段-バ行","終止形-一般"])
            matrix=root/"matrix.def"
            matrix.write_text("3 4\n0 0 0\n1 2 -20\n",encoding="utf-8")
            conn=root/"connections.tsv"
            run=subprocess.run([sys.executable,str(IMPORTER),"matrix","--input",str(matrix),"--output",str(conn)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertEqual(conn.read_text(encoding="utf-8").splitlines()[1],"1\t2\t-20")

    def test_pinned_zip_hash_and_extract(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp)
            small=root/"small.zip"; matrix=root/"matrix.zip"
            with zipfile.ZipFile(small,"w") as z:z.writestr("nested/small_lex.csv","csv")
            with zipfile.ZipFile(matrix,"w") as z:z.writestr("matrix.def","1 1\n0 0 0\n")
            sh=hashlib.sha256(small.read_bytes()).hexdigest()
            mh=hashlib.sha256(matrix.read_bytes()).hexdigest()
            out=root/"out"
            run=subprocess.run([sys.executable,str(VERIFY),"--small-zip",str(small),"--small-sha256",sh,
                "--matrix-zip",str(matrix),"--matrix-sha256",mh,"--output",str(out)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertTrue((out/"small_lex.csv").is_file())
            self.assertTrue((out/"matrix.def").is_file())
            run=subprocess.run([sys.executable,str(VERIFY),"--small-zip",str(small),"--small-sha256","0"*64,
                "--matrix-zip",str(matrix),"--matrix-sha256",mh,"--output",str(root/"bad")],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)

    def test_matrix_rejects_bad_indices_and_duplicate_cells(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=pathlib.Path(tmp); source=root/"m.def"; output=root/"out.tsv"
            source.write_text("2 2\n2 0 0\n",encoding="utf-8")
            run=subprocess.run([sys.executable,str(IMPORTER),"matrix","--input",str(source),"--output",str(output)],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)
            source.write_text("2 2\n0 0 0\n0 0 1\n",encoding="utf-8")
            run=subprocess.run([sys.executable,str(IMPORTER),"matrix","--input",str(source),"--output",str(output)],capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)

if __name__=="__main__": unittest.main()
