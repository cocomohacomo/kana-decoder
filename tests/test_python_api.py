import pathlib,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"python"))
from kana_decoder import KanaDecoder,DecodedPath
PACK,LIB=(sys.argv[1],sys.argv[2]) if len(sys.argv)>2 else (None,None)
sys.argv=sys.argv[:1]
@unittest.skipUnless(PACK is not None and LIB is not None, "requires CTest-provided shared-library and Pack paths")
class PythonApiTests(unittest.TestCase):
 def test_decode_nbest_and_streaming(self):
  with KanaDecoder(PACK,library_path=LIB) as decoder:
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
 def test_streaming_snapshot_survives_engine_close(self):
  decoder=KanaDecoder(PACK,library_path=LIB)
  session=decoder.streaming(32)
  decoder.close()
  try:
   session.append("あめ")
   self.assertIn(session.commit_best(),("雨","飴","あめ"))
  finally:session.close()
 def test_nbest_limit(self):
  with KanaDecoder(PACK,library_path=LIB) as decoder:
   with self.assertRaises(ValueError):decoder.decode_nbest("あめ",9)
if __name__=="__main__":
 unittest.main()
