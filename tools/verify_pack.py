#!/usr/bin/env python3
"""Offline Language Pack manifest, checksum, license and size verifier."""
import argparse,hashlib,json
from pathlib import Path
def verify_pack(root:Path,max_bytes:int=30*1024*1024)->dict:
 mpath=root/"manifest.json"
 if not mpath.is_file():raise ValueError("manifest.json missing")
 m=json.loads(mpath.read_text(encoding="utf-8"))
 for k in ("pack_id","language_id","pack_version","core_abi_version","license"):
  if not m.get(k):raise ValueError(f"manifest field missing: {k}")
 lic=(root/m["license"].get("file","")).resolve()
 if root.resolve() not in lic.parents:raise ValueError("license path escapes pack root")
 if not m["license"].get("spdx") or not lic.is_file():raise ValueError("license metadata or license file missing")
 payload=0;verified=[]
 for item in m.get("files",[]):
  rel=item["path"];p=(root/rel).resolve()
  if root.resolve() not in p.parents:raise ValueError(f"path escapes pack root: {rel}")
  if not p.is_file():raise ValueError(f"required file missing: {rel}")
  data=p.read_bytes();payload+=len(data)
  if len(data)!=item["size"]:raise ValueError(f"size mismatch: {rel}")
  if hashlib.sha256(data).hexdigest()!=item["sha256"]:raise ValueError(f"checksum mismatch: {rel}")
  verified.append(rel)
 total=payload+mpath.stat().st_size
 if total>max_bytes:raise ValueError(f"pack exceeds resource limit: {total} > {max_bytes}")
 if not verified:raise ValueError("manifest contains no payload files")
 return {"pack_id":m["pack_id"],"payload_bytes":payload,"manifest_bytes":mpath.stat().st_size,"installed_bytes":total,"installed_mib":total/(1024*1024),"files_verified":verified,"limit_bytes":max_bytes,"within_limit":True}
def main():
 p=argparse.ArgumentParser();p.add_argument("pack_dir",type=Path);p.add_argument("--max-mib",type=float,default=30.0);a=p.parse_args()
 print(json.dumps(verify_pack(a.pack_dir,int(a.max_mib*1024*1024)),ensure_ascii=False,indent=2))
if __name__=="__main__":main()
