#!/usr/bin/env python3
"""Exercise the public C ABI through ctypes against the shared library."""
import ctypes, pathlib, sys, unittest
LIB=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>2 and pathlib.Path(sys.argv[1]).is_file() else None
PACK=pathlib.Path(sys.argv[2]).resolve() if len(sys.argv)>2 and pathlib.Path(sys.argv[2]).is_dir() else None
sys.argv=sys.argv[:1]  # hide CTest-provided paths from unittest discovery
class TokenView(ctypes.Structure):
    _fields_=[("start_codepoint",ctypes.c_size_t),("end_codepoint",ctypes.c_size_t),
      ("candidate_id",ctypes.c_char_p),("surface",ctypes.c_char_p),("pos",ctypes.c_char_p),
      ("inflection_type",ctypes.c_char_p),("inflection_form",ctypes.c_char_p),
      ("morphology_score",ctypes.c_double),("context_score",ctypes.c_double)]
@unittest.skipUnless(LIB is not None and PACK is not None, "requires CTest-provided shared-library and Pack paths")
class ApiTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.lib=ctypes.CDLL(str(LIB))
  L=cls.lib
  L.kd_c_api_version.restype=ctypes.c_uint32
  L.kd_engine_create.restype=ctypes.c_void_p
  L.kd_engine_destroy.argtypes=[ctypes.c_void_p]
  L.kd_engine_load_pack.argtypes=[ctypes.c_void_p,ctypes.c_char_p];L.kd_engine_load_pack.restype=ctypes.c_int
  L.kd_engine_decode_nbest.argtypes=[ctypes.c_void_p,ctypes.c_char_p,ctypes.c_size_t];L.kd_engine_decode_nbest.restype=ctypes.c_void_p
  L.kd_decode_result_destroy.argtypes=[ctypes.c_void_p]
  L.kd_decode_result_path_count.argtypes=[ctypes.c_void_p];L.kd_decode_result_path_count.restype=ctypes.c_size_t
  L.kd_decode_result_path_score.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_double)];L.kd_decode_result_path_score.restype=ctypes.c_int
  L.kd_decode_result_token_count.argtypes=[ctypes.c_void_p,ctypes.c_size_t];L.kd_decode_result_token_count.restype=ctypes.c_size_t
  L.kd_decode_result_token.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_size_t,ctypes.POINTER(TokenView)];L.kd_decode_result_token.restype=ctypes.c_int
 @classmethod
 def tearDownClass(cls): pass
 def test_nbest_metadata(self):
  L=self.lib;self.assertEqual(L.kd_c_api_version(),1);engine=L.kd_engine_create();self.assertTrue(engine)
  try:
   self.assertEqual(L.kd_engine_load_pack(engine,str(PACK).encode()),0)
   result=L.kd_engine_decode_nbest(engine,"あめ".encode(),8);self.assertTrue(result)
   try:
    self.assertGreaterEqual(L.kd_decode_result_path_count(result),1)
    self.assertGreaterEqual(L.kd_decode_result_token_count(result,0),1)
    score=ctypes.c_double();self.assertEqual(L.kd_decode_result_path_score(result,0,ctypes.byref(score)),0)
    token=TokenView();self.assertEqual(L.kd_decode_result_token(result,0,0,ctypes.byref(token)),0)
    self.assertGreater(token.end_codepoint,token.start_codepoint)
    self.assertEqual(token.surface.decode(), "雨")
    self.assertTrue(token.candidate_id and token.pos is not None)
   finally:L.kd_decode_result_destroy(result)
  finally:L.kd_engine_destroy(engine)
if __name__=="__main__":unittest.main()
