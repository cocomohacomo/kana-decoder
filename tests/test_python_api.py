import pathlib,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"python"))
from kana_decoder import KanaDecoder,DecodedPath
class PythonApiTests(unittest.TestCase):
 def test_decode_nbest_and_streaming(self):
  with KanaDecoder(sys.argv[1],library_path=sys.argv[2]) as decoder:
   result=decoder.decode("あめ")
   self.assertIn(result,("雨","飴","あめ"))
   paths=decoder.decode_nbest("あめ",8)
   self.assertGreaterEqual(len(paths),1)
   self.assertIsInstance(paths[0],DecodedPath)
   self.assertGreaterEqual(len(paths[0].tokens),1)
   token=paths[0].tokens[0]
   self.assertGreater(token.end_codepoint,token.start_codepoint)
   self.assertTrue(token.surface)
   with decoder.streaming(32) as session:
    session.append("あ")
    session.append("め")
    committed=session.commit_best()
    self.assertIn(committed,("雨","飴","あめ"))
 def test_nbest_limit(self):
  with KanaDecoder(sys.argv[1],library_path=sys.argv[2]) as decoder:
   with self.assertRaises(ValueError):decoder.decode_nbest("あめ",9)
if __name__=="__main__":
 pack,library=sys.argv[1],sys.argv[2]
 sys.argv=sys.argv[:1]
 unittest.main()
