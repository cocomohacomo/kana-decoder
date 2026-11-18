#!/usr/bin/env python3
"""Build a reproducible Sudachi Small Language Pack from verified local archives."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def run_json(command):
 result=subprocess.run(command,check=True,capture_output=True,text=True)
 try:return json.loads(result.stdout)
 except json.JSONDecodeError as exc:raise RuntimeError(f"command did not return JSON: {' '.join(command)}\n{result.stdout}\n{result.stderr}") from exc
def sha256(path):
 h=hashlib.sha256()
 with path.open("rb") as f:
  for block in iter(lambda:f.read(1024*1024),b""):h.update(block)
 return h.hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument("--small-zip",type=Path,required=True);p.add_argument("--matrix-zip",type=Path,required=True)
 p.add_argument("--output",type=Path,required=True);p.add_argument("--work-dir",type=Path,required=True)
 p.add_argument("--lock",type=Path,default=ROOT/"licenses"/"sudachidict-small-20260723-source-lock.json")
 p.add_argument("--license-profile",type=Path,default=ROOT/"licenses"/"sudachi-small-20260723"/"license-profile.json")
 p.add_argument("--license-file",type=Path,default=ROOT/"licenses"/"sudachi-small-20260723"/"PACK-LICENSE-NOTICE.txt")
 p.add_argument("--upstream-legal-notice",type=Path,default=ROOT/"licenses"/"UPSTREAM_SUDACHIDICT_LEGAL.txt")
 p.add_argument("--sequences",type=Path);p.add_argument("--correction-rules",type=Path)
 p.add_argument("--pack-version",default="20260723.1");p.add_argument("--license-spdx",default="Apache-2.0 AND BSD-3-Clause");p.add_argument("--max-expanded-bytes",type=int,default=8*1024*1024*1024)
 a=p.parse_args()
 if a.max_expanded_bytes<=0:p.error("--max-expanded-bytes must be positive")
 for path in (a.small_zip,a.matrix_zip,a.lock,a.license_profile,a.license_file,a.upstream_legal_notice):
  if not path.is_file() or path.is_symlink():raise ValueError(f"required input missing or unsafe: {path}")
 work=a.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
 if work.is_symlink():raise ValueError("work directory must not be a symlink")
 source=work/"source";normalized=work/"normalized";source.mkdir(exist_ok=True);normalized.mkdir(exist_ok=True)
 verification=run_json([sys.executable,str(ROOT/"tools"/"verify_sudachi_source.py"),"--lock",str(a.lock.resolve()),"--small-zip",str(a.small_zip.resolve()),"--matrix-zip",str(a.matrix_zip.resolve()),"--output",str(source)])
 extracted_lex=Path(verification["small_lex_csv"]);matrix=Path(verification["matrix_def"])
 compatibility=run_json([sys.executable,str(ROOT/"tools"/"validate_sudachi_pack_inputs.py"),"--lexicon",str(extracted_lex),"--matrix",str(matrix)])
 lexicon=normalized/"lexicon.tsv";connections=normalized/"connections.tsv"
 lex_report=run_json([sys.executable,str(ROOT/"tools"/"import_sudachi_v1.py"),"lexicon","--input",str(extracted_lex),"--output",str(lexicon)])
 matrix_report=run_json([sys.executable,str(ROOT/"tools"/"import_sudachi_v1.py"),"matrix","--input",str(matrix),"--output",str(connections)])
 lock=json.loads(a.lock.read_text(encoding="utf-8"))
 provenance={
  "schema_version":1,"dictionary":"SudachiDict Small","release":lock.get("release"),
  "source_lock":lock,"archive_verification":verification,"compatibility":compatibility,
  "lexicon_import":lex_report,"matrix_import":matrix_report,
  "importer_sha256":{"import_sudachi_v1.py":sha256(ROOT/"tools"/"import_sudachi_v1.py"),
   "validate_sudachi_pack_inputs.py":sha256(ROOT/"tools"/"validate_sudachi_pack_inputs.py"),
   "build_language_pack.py":sha256(ROOT/"tools"/"build_language_pack.py")}
 }
 provenance_path=work/"provenance.json";provenance_path.write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
 command=[sys.executable,str(ROOT/"tools"/"build_language_pack.py"),"--lexicon",str(lexicon),"--connections",str(connections),
  "--output",str(a.output.resolve()),"--license-file",str(a.license_file.resolve()),"--license-profile",str(a.license_profile.resolve()),
  "--license-spdx",a.license_spdx,"--upstream","WorksApplications/SudachiDict v20260723 + UniDic 2.1.2 matrix",
  "--pack-id","ja-jp-sudachi-small","--language-id","ja-JP","--pack-version",a.pack_version,
  "--dictionary-version",str(lock.get("release")),"--reading-encoding","hiragana-utf8",
  "--normalization-profile","ja-kana-nfc-v1","--orthography-profile","ja-general-v1",
  "--max-expanded-bytes",str(a.max_expanded_bytes),"--provenance-file",str(provenance_path),
  "--upstream-legal-notice",str(a.upstream_legal_notice.resolve())]
 if a.sequences:command.extend(["--sequences",str(a.sequences.resolve())])
 if a.correction_rules:command.extend(["--correction-rules",str(a.correction_rules.resolve())])
 built=run_json(command)
 verified=run_json([sys.executable,str(ROOT/"tools"/"verify_pack.py"),str(a.output.resolve())])
 print(json.dumps({"status":"built-and-verified","pack":built,"verification":verified,
  "source_archives":{"small_lex_sha256":sha256(a.small_zip),"matrix_sha256":sha256(a.matrix_zip)}},ensure_ascii=False,indent=2))
if __name__=="__main__":main()
