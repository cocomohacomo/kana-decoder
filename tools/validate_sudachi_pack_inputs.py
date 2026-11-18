#!/usr/bin/env python3
"""Validate Sudachi V1 lexicon IDs and every sparse/dense matrix.def cell."""
import argparse,csv,json
from pathlib import Path
def validate(lexicon,matrix):
 with matrix.open(encoding="utf-8") as f:
  header=f.readline().split()
  if len(header)!=2:raise ValueError("matrix.def must start with left_size right_size")
  try:left_size,right_size=map(int,header)
  except ValueError as exc:raise ValueError("matrix dimensions must be integers") from exc
  if min(left_size,right_size)<=0 or max(left_size,right_size)>2**31-1:raise ValueError("invalid matrix dimensions")
  matrix_keys=[];matrix_rows=0;nonzero=0
  for line_no,line in enumerate(f,2):
   if not line.strip():continue
   cols=line.split()
   if len(cols)!=3:raise ValueError(f"{matrix}:{line_no}: expected right_id left_id cost")
   try:right,left,cost=map(int,cols)
   except ValueError as exc:raise ValueError(f"{matrix}:{line_no}: invalid integer") from exc
   if not 0<=right<right_size or not 0<=left<left_size or not -(2**31)<=cost<2**31:
    raise ValueError(f"{matrix}:{line_no}: cell outside declared dimensions or int32")
   matrix_keys.append((right<<32)|left);matrix_rows+=1
   if cost:nonzero+=1
 matrix_keys.sort()
 if any(matrix_keys[i-1]==matrix_keys[i] for i in range(1,len(matrix_keys))):raise ValueError("duplicate connection matrix cell")
 with lexicon.open(encoding="utf-8-sig",newline="") as f:
  reader=csv.DictReader(f);raw=reader.fieldnames or []
  names=[x.strip().lower().replace("_","") for x in raw]
  if len(set(names))!=len(names):raise ValueError("duplicate lexicon header after normalization")
  mapping={name:raw[i] for i,name in enumerate(names)}
  for required in ("leftid","rightid"):
   if required not in mapping:raise ValueError(f"missing {required} column")
  max_left=max_right=-1;rows=0
  for line,row in enumerate(reader,2):
   try:
    if None in row or any(value is None for value in row.values()):raise ValueError("CSV row width mismatch")
    left=int(row[mapping["leftid"]]);right=int(row[mapping["rightid"]])
   except Exception as exc:raise ValueError(f"{lexicon}:{line}: invalid context ID or row width") from exc
   if not 0<=left<left_size or not 0<=right<right_size:raise ValueError(f"{lexicon}:{line}: context ID outside matrix dimensions")
   max_left=max(max_left,left);max_right=max(max_right,right);rows+=1
 if not rows:raise ValueError("empty lexicon")
 return {"status":"valid","lexicon_rows":rows,"matrix_rows":matrix_rows,"matrix_nonzero_cells":nonzero,
  "matrix_left_size":left_size,"matrix_right_size":right_size,"max_left_id":max_left,"max_right_id":max_right}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--lexicon",type=Path,required=True);p.add_argument("--matrix",type=Path,required=True);p.add_argument("--output",type=Path);a=p.parse_args()
 result=validate(a.lexicon,a.matrix);text=json.dumps(result,ensure_ascii=False,indent=2);print(text)
 if a.output:a.output.write_text(text+"\n",encoding="utf-8")
if __name__=="__main__":main()
