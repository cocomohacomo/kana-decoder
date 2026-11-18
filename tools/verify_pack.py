#!/usr/bin/env python3
"""Offline Language Pack manifest, checksum, license and size verifier."""
import argparse,hashlib,json
from pathlib import Path
def verify_pack(root:Path,max_bytes:int=30*1024*1024)->dict:
 mpath=root/"manifest.json"
 if not mpath.is_file():raise ValueError("manifest.json missing")
 root=root.resolve()
 m=json.loads(mpath.read_text(encoding="utf-8"))
 for k in ("pack_id","language_id","pack_version","core_abi_version","license"):
  if not m.get(k):raise ValueError(f"manifest field missing: {k}")
 license_meta=m["license"]
 if not isinstance(license_meta,dict) or not license_meta.get("file") or not license_meta.get("spdx"):
  raise ValueError("license metadata incomplete")
 lic=(root/license_meta["file"]).resolve()
 if root not in lic.parents or not lic.is_file():raise ValueError("license path escapes pack root or file missing")
 files=m.get("files")
 if not isinstance(files,list) or not files:raise ValueError("manifest files must be a non-empty array")
 seen=set();payload=0;verified=[]
 for item in files:
  if not isinstance(item,dict) or not all(k in item for k in ("path","size","sha256")):raise ValueError("malformed file entry")
  rel=item["path"]
  if not isinstance(rel,str) or not rel or rel in seen:raise ValueError(f"invalid or duplicate file path: {rel}")
  seen.add(rel);p=(root/rel).resolve()
  if root not in p.parents or not p.is_file():raise ValueError(f"path escapes pack root or file missing: {rel}")
  data=p.read_bytes();payload+=len(data)
  if not isinstance(item["size"],int) or item["size"]<0 or len(data)!=item["size"]:raise ValueError(f"size mismatch: {rel}")
  if not isinstance(item["sha256"],str) or hashlib.sha256(data).hexdigest()!=item["sha256"]:raise ValueError(f"checksum mismatch: {rel}")
  verified.append(rel)
 required={"lexicon.tsv","ngram.tsv"}
 if not required.issubset(seen):raise ValueError("required lexicon.tsv or ngram.tsv missing")
 declared=m.get("resource_limits",{}).get("max_expanded_bytes",max_bytes)
 if not isinstance(declared,int) or declared<=0:raise ValueError("invalid declared max_expanded_bytes")
 effective_limit=min(max_bytes,declared)
 total=payload+mpath.stat().st_size
 if total>effective_limit:raise ValueError(f"pack exceeds resource limit: {total} > {effective_limit}")
 if not verified:raise ValueError("manifest contains no payload files")
 return {"pack_id":m["pack_id"],"payload_bytes":payload,"manifest_bytes":mpath.stat().st_size,"installed_bytes":total,"installed_mib":total/(1024*1024),"files_verified":verified,"limit_bytes":effective_limit,"within_limit":True}
def main():
 p=argparse.ArgumentParser();p.add_argument("pack_dir",type=Path);p.add_argument("--max-mib",type=float,default=30.0);a=p.parse_args()
 print(json.dumps(verify_pack(a.pack_dir,int(a.max_mib*1024*1024)),ensure_ascii=False,indent=2))
if __name__=="__main__":main()
