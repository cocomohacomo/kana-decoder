#!/usr/bin/env python3
"""Verify pinned Sudachi source archives and safely extract selected members."""
import argparse,hashlib,json,re,stat,zipfile
from pathlib import Path,PurePosixPath
DEFAULT_LOCK=Path(__file__).resolve().parents[1]/"licenses"/"sudachidict-small-20260723-source-lock.json"
MAX_MEMBER_BYTES=512*1024*1024
def digest(path):
 h=hashlib.sha256()
 with path.open("rb") as f:
  for block in iter(lambda:f.read(1024*1024),b""):h.update(block)
 return h.hexdigest()
def load_lock(path):
 lock=json.loads(path.read_text(encoding="utf-8"))
 if not isinstance(lock,dict) or lock.get("schema_version")!=1 or lock.get("format")!="V1":raise ValueError("source lock must use schema_version 1 and V1")
 required={"sudachi-small-lex":"small_lex.csv","unidic-2.1.2-matrix":"matrix.def"};found={}
 for item in lock.get("sources",[]):
  if not isinstance(item,dict):raise ValueError("source entry must be an object")
  ident=item.get("id")
  if ident not in required or ident in found:raise ValueError("unknown or duplicate source id")
  if item.get("member")!=required[ident]:raise ValueError(f"{ident}: unexpected archive member")
  if not isinstance(item.get("url"),str) or not item["url"].startswith("https://"):raise ValueError(f"{ident}: source URL must use HTTPS")
  if not isinstance(item.get("sha256"),str) or not re.fullmatch("[0-9a-f]{64}",item["sha256"]):raise ValueError(f"{ident}: invalid SHA-256")
  found[ident]=item
 if set(found)!=set(required):raise ValueError("source lock must contain both pinned artifacts")
 return lock,found
def verify(path,expected):
 if not path.is_file() or path.is_symlink():raise ValueError("artifact missing or symlink")
 actual=digest(path)
 if actual!=expected:raise ValueError(f"SHA-256 mismatch: expected {expected}, got {actual}")
 return actual
def safe_extract(archive,destination,member):
 destination.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(archive) as z:
  matches=[]
  for info in z.infolist():
   p=PurePosixPath(info.filename);mode=info.external_attr>>16
   if p.is_absolute() or any(x in ("..","") for x in p.parts) or "\\" in info.filename or ":" in info.filename or info.file_size>MAX_MEMBER_BYTES:raise ValueError("unsafe or oversized archive member: "+info.filename)
   if stat.S_ISLNK(mode):raise ValueError("symlink archive member rejected")
   if not info.is_dir() and p.name==member:matches.append(info)
  if len(matches)!=1:raise ValueError(f"expected exactly one {member} member, found {len(matches)}")
  target=destination/member
  with z.open(matches[0]) as src,target.open("wb") as dst:
   for block in iter(lambda:src.read(1024*1024),b""):dst.write(block)
  return target
def main():
 p=argparse.ArgumentParser();p.add_argument("--lock",type=Path,default=DEFAULT_LOCK);p.add_argument("--small-zip",type=Path,required=True);p.add_argument("--matrix-zip",type=Path,required=True);p.add_argument("--output",type=Path,required=True);a=p.parse_args()
 lock,sources=load_lock(a.lock);sm=sources["sudachi-small-lex"];mm=sources["unidic-2.1.2-matrix"]
 sh=verify(a.small_zip,sm["sha256"]);mh=verify(a.matrix_zip,mm["sha256"])
 small=safe_extract(a.small_zip,a.output,sm["member"]);matrix=safe_extract(a.matrix_zip,a.output,mm["member"])
 result={"lock_file":str(a.lock),"dictionary":lock.get("dictionary"),"release":lock.get("release"),"format":lock["format"],"small_lex_zip_sha256":sh,"matrix_zip_sha256":mh,"small_lex_csv":str(small),"matrix_def":str(matrix),"verification":"sha256-verified-local-artifacts"}
 (a.output/"source-lock.json").write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8");print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=="__main__":main()
