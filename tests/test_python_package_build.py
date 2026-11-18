import os,pathlib,subprocess,sys,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class PythonPackageBuildTests(unittest.TestCase):
 def test_wheel_metadata_and_importable_install(self):
  with tempfile.TemporaryDirectory() as tmp:
   target=pathlib.Path(tmp)/"site"
   command=[sys.executable,"-m","pip","install","--no-deps","--no-build-isolation","--target",str(target),str(ROOT)]
   result=subprocess.run(command,capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   env=os.environ.copy();env["PYTHONPATH"]=str(target)
   probe=subprocess.run([sys.executable,"-c","import kana_decoder; assert kana_decoder.__version__ == '0.14.0'"],
    capture_output=True,text=True,env=env)
   self.assertEqual(probe.returncode,0,probe.stdout+probe.stderr)
if __name__=="__main__":unittest.main()
