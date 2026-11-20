#!/usr/bin/env python3
"""Offline Language Pack manifest, checksum and license verifier."""
import argparse,gzip,hashlib,json,re
from pathlib import Path,PurePosixPath
def _safe_relative(value):
 if not isinstance(value,str) or not value or "\\" in value:return False
 path=PurePosixPath(value)
 return not path.is_absolute() and all(part not in ("",".","..") for part in path.parts)
def verify_pack(root:Path,max_bytes=None):
 if max_bytes is not None and max_bytes<=0:raise ValueError("max_bytes must be positive or None")
 root=root.resolve();manifest_path=root/"manifest.json"
 if not manifest_path.is_file() or manifest_path.is_symlink():raise ValueError("manifest.json missing or unsafe")
 manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
 if not isinstance(manifest,dict):raise ValueError("manifest root must be an object")
 for key in ("pack_id","language_id","pack_version","core_abi_version"):
  if not isinstance(manifest.get(key),str) or not manifest[key].strip():raise ValueError(f"manifest field missing or invalid: {key}")
 if not isinstance(manifest.get("format_version"),int) or isinstance(manifest["format_version"],bool):raise ValueError("format_version must be an integer")
 license_meta=manifest.get("license")
 if not isinstance(license_meta,dict) or not all(isinstance(license_meta.get(k),str) and license_meta[k].strip() for k in ("file","spdx")):raise ValueError("license metadata incomplete")
 if not _safe_relative(license_meta["file"]):raise ValueError("license path is not a safe relative path")
 license_path=root/license_meta["file"]
 if license_path.is_symlink() or not license_path.is_file() or root not in license_path.resolve().parents:raise ValueError("license path escapes pack root or file missing")
 files=manifest.get("files")
 if not isinstance(files,list) or not files:raise ValueError("manifest files must be a non-empty array")
 seen=set();payload=0;verified=[]
 for item in files:
  if not isinstance(item,dict) or not all(k in item for k in ("path","size","sha256")):raise ValueError("malformed file entry")
  rel=item["path"]
  if not _safe_relative(rel) or rel in seen:raise ValueError(f"invalid or duplicate file path: {rel}")
  seen.add(rel);candidate=root/rel
  if candidate.is_symlink() or not candidate.is_file() or root not in candidate.resolve().parents:raise ValueError(f"path escapes pack root or file missing: {rel}")
  size=item["size"];digest=item["sha256"]
  if not isinstance(size,int) or isinstance(size,bool) or size<0:raise ValueError(f"invalid file size: {rel}")
  if not isinstance(digest,str) or not re.fullmatch("[0-9a-f]{64}",digest):raise ValueError(f"invalid SHA-256 value: {rel}")
  data=candidate.read_bytes();payload+=len(data)
  if len(data)!=size:raise ValueError(f"size mismatch: {rel}")
  if hashlib.sha256(data).hexdigest()!=digest:raise ValueError(f"checksum mismatch: {rel}")
  verified.append(rel)
 required={"ngram.tsv",license_meta["file"]}
 if not ({"lexicon.tsv","lexicon.tsv.gz"}&seen) or not required.issubset(seen):raise ValueError("manifest must include a lexicon, ngram.tsv and the license file")
 actual=set()
 for path in root.rglob("*"):
  if path.is_symlink():raise ValueError(f"symlink is not permitted in a Language Pack: {path.relative_to(root)}")
  if path.is_file() and path!=manifest_path:actual.add(path.relative_to(root).as_posix())
 if actual!=seen:raise ValueError(f"unlisted or missing payload files: {sorted(actual^seen)}")
 limits=manifest.get("resource_limits",{})
 if not isinstance(limits,dict):raise ValueError("resource_limits must be an object")
 declared=limits.get("max_expanded_bytes",8*1024*1024*1024)
 if not isinstance(declared,int) or isinstance(declared,bool) or declared<=0:raise ValueError("invalid declared max_expanded_bytes")
 effective=declared if max_bytes is None else min(max_bytes,declared)
 compression=manifest.get("dictionary_compression")
 if "lexicon.tsv.gz" in seen:
  if not isinstance(compression,dict) or compression.get("path")!="lexicon.tsv.gz" or compression.get("algorithm")!="gzip":raise ValueError("compressed lexicon metadata is missing or invalid")
  expanded=compression.get("uncompressed_size")
  if not isinstance(expanded,int) or isinstance(expanded,bool) or expanded<=0 or expanded>effective:raise ValueError("compressed lexicon expanded size exceeds resource limit")
  actual_expanded=0
  with gzip.open(root/"lexicon.tsv.gz","rb") as stream:
   for block in iter(lambda:stream.read(1024*1024),b""):
    actual_expanded+=len(block)
    if actual_expanded>effective:raise ValueError("compressed lexicon expands beyond resource limit")
  if actual_expanded!=expanded:raise ValueError("compressed lexicon expanded size mismatch")
 elif compression is not None:raise ValueError("compression metadata present without compressed lexicon")
 total=payload+manifest_path.stat().st_size
 if total>effective:raise ValueError(f"pack exceeds resource limit: {total} > {effective}")
 return {"pack_id":manifest["pack_id"],"payload_bytes":payload,"manifest_bytes":manifest_path.stat().st_size,"installed_bytes":total,"installed_mib":total/(1024*1024),"files_verified":verified,"limit_bytes":effective,"within_limit":True}
def main():
 p=argparse.ArgumentParser();p.add_argument("pack_dir",type=Path);p.add_argument("--max-mib",type=float,default=None,help="optional verification ceiling; omitted means use the Pack's declared resource limit")
 a=p.parse_args()
 if a.max_mib is not None and a.max_mib<=0:p.error("--max-mib must be positive")
 print(json.dumps(verify_pack(a.pack_dir,None if a.max_mib is None else int(a.max_mib*1024*1024)),ensure_ascii=False,indent=2))
if __name__=="__main__":main()
