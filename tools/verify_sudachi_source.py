#!/usr/bin/env python3
"""Verify and safely unpack pinned Sudachi Small source artifacts."""
import argparse
import hashlib
import json
import stat
import zipfile
from pathlib import Path, PurePosixPath

def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""): h.update(block)
    return h.hexdigest()

def verify(path, expected):
    if not path.is_file() or path.is_symlink(): raise ValueError("artifact missing or symlink")
    actual=digest(path)
    if actual != expected.lower(): raise ValueError(f"SHA-256 mismatch: expected {expected}, got {actual}")
    return actual

def safe_extract(archive, destination, member):
    destination.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        matches=[]
        for info in z.infolist():
            p=PurePosixPath(info.filename)
            mode=info.external_attr >> 16
            if p.is_absolute() or any(x in ("..","") for x in p.parts) or "\\" in info.filename:
                raise ValueError("unsafe archive path: "+info.filename)
            if stat.S_ISLNK(mode): raise ValueError("symlink archive member rejected")
            if not info.is_dir() and p.name==member: matches.append(info)
        if len(matches)!=1: raise ValueError(f"expected exactly one {member} member, found {len(matches)}")
        target=destination/member
        with z.open(matches[0]) as src, target.open("wb") as dst:
            for block in iter(lambda:src.read(1024*1024),b""): dst.write(block)
        return target

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--small-zip",type=Path,required=True)
    p.add_argument("--small-sha256",required=True)
    p.add_argument("--matrix-zip",type=Path,required=True)
    p.add_argument("--matrix-sha256",required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    small_hash=verify(a.small_zip,a.small_sha256)
    matrix_hash=verify(a.matrix_zip,a.matrix_sha256)
    small=safe_extract(a.small_zip,a.output,"small_lex.csv")
    matrix=safe_extract(a.matrix_zip,a.output,"matrix.def")
    result={"small_lex_zip_sha256":small_hash,"matrix_zip_sha256":matrix_hash,
            "small_lex_csv":str(small),"matrix_def":str(matrix)}
    (a.output/"source-lock.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=="__main__": main()
