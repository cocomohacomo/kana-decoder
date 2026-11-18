#!/usr/bin/env python3
"""Download the exact Sudachi artifacts pinned by the repository source lock."""
import argparse,hashlib,json,os,re,tempfile,urllib.parse,urllib.request
from pathlib import Path
DEFAULT_LOCK=Path(__file__).resolve().parents[1]/"licenses"/"sudachidict-small-20260723-source-lock.json"
ALLOWED={"sudachi.s3.ap-northeast-1.amazonaws.com","d2ej7fkh96fzlu.cloudfront.net"}
MAX_BYTES=2*1024*1024*1024
def digest(path):
 h=hashlib.sha256()
 with path.open("rb") as f:
  for block in iter(lambda:f.read(1024*1024),b""):h.update(block)
 return h.hexdigest()
def load_sources(path):
 lock=json.loads(path.read_text(encoding="utf-8"))
 if not isinstance(lock,dict) or lock.get("schema_version")!=1 or lock.get("format")!="V1":raise ValueError("unsupported source lock")
 release=lock.get("release")
 if not isinstance(release,str) or not re.fullmatch(r"\d{8}",release):raise ValueError("source lock release must be YYYYMMDD")
 expected={
  "sudachi-small-lex":("small_lex.csv",f"https://sudachi.s3.ap-northeast-1.amazonaws.com/sudachidict-raw/{release}/small_lex.zip"),
  "unidic-2.1.2-matrix":("matrix.def","https://d2ej7fkh96fzlu.cloudfront.net/sudachidict-raw/matrix.def.zip")
 }
 out={}
 for item in lock.get("sources",[]):
  if not isinstance(item,dict):raise ValueError("source entry must be an object")
  ident=item.get("id")
  if ident not in expected or ident in out:raise ValueError("unknown or duplicate source")
  member,url=expected[ident]
  if item.get("member")!=member or item.get("url")!=url:raise ValueError(f"{ident}: source URL/member does not match pinned release convention")
  parsed=urllib.parse.urlparse(url)
  if parsed.scheme!="https" or parsed.hostname not in ALLOWED:raise ValueError(f"{ident}: URL host is not allowlisted")
  if not isinstance(item.get("sha256"),str) or not re.fullmatch(r"[0-9a-f]{64}",item["sha256"]):raise ValueError(f"{ident}: invalid SHA-256")
  out[ident]=item
 if set(out)!=set(expected):raise ValueError("source lock is incomplete")
 return out

def download(item,destination,timeout=60):
 destination.parent.mkdir(parents=True,exist_ok=True)
 request=urllib.request.Request(item["url"],headers={"User-Agent":"kana-decoder-source-fetch/1.0"})
 fd,tmp=tempfile.mkstemp(prefix=destination.name+".",suffix=".part",dir=destination.parent);os.close(fd)
 tmp_path=Path(tmp);total=0
 try:
  with urllib.request.urlopen(request,timeout=timeout) as response,tmp_path.open("wb") as out:
   final=urllib.parse.urlparse(response.geturl())
   if final.scheme!="https" or final.hostname not in ALLOWED:raise ValueError("redirect target is not allowlisted HTTPS")
   while True:
    block=response.read(1024*1024)
    if not block:break
    total+=len(block)
    if total>MAX_BYTES:raise ValueError("source archive exceeds 2 GiB safety limit")
    out.write(block)
  actual=digest(tmp_path)
  if actual!=item["sha256"]:raise ValueError(f"SHA-256 mismatch for {item['id']}: expected {item['sha256']}, got {actual}")
  os.replace(tmp_path,destination)
  return {"id":item["id"],"path":str(destination),"bytes":total,"sha256":actual,"status":"downloaded-and-verified"}
 finally:
  if tmp_path.exists():tmp_path.unlink()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--lock",type=Path,default=DEFAULT_LOCK);p.add_argument("--output-dir",type=Path,required=True);p.add_argument("--timeout",type=int,default=60);a=p.parse_args()
 sources=load_sources(a.lock);a.output_dir.mkdir(parents=True,exist_ok=True)
 result=[]
 for ident,name in (("sudachi-small-lex","small_lex.zip"),("unidic-2.1.2-matrix","matrix.def.zip")):
  result.append(download(sources[ident],a.output_dir/name,a.timeout))
 print(json.dumps({"lock":str(a.lock),"artifacts":result},ensure_ascii=False,indent=2))
if __name__=="__main__":main()
