#!/usr/bin/env python3
"""Check Sudachi V1 lexicon context IDs against the pinned matrix dimensions."""
import argparse,csv,json
from pathlib import Path
def validate(lexicon,matrix):
 with matrix.open(encoding="utf-8") as f:
  header=f.readline().split()
 if len(header)!=2:raise ValueError("matrix.def must start with left_size right_size")
 left_size,right_size=map(int,header)
 if min(left_size,right_size)<=0:raise ValueError("invalid matrix dimensions")
 max_left=max_right=-1;rows=0
 with lexicon.open(encoding="utf-8-sig",newline="") as f:
  reader=csv.DictReader(f);names={x.strip().lower().replace("_",""):x for x in (reader.fieldnames or [])}
  for required in ("leftid","rightid"):
   if required not in names:raise ValueError(f"missing {required} column")
  for line,row in enumerate(reader,2):
   try:left=int(row[names["leftid"]]);right=int(row[names["rightid"]])
   except Exception as exc:raise ValueError(f"{lexicon}:{line}: invalid context ID") from exc
   if not 0<=left<left_size or not 0<=right<right_size:raise ValueError(f"{lexicon}:{line}: context ID outside matrix dimensions")
   max_left=max(max_left,left);max_right=max(max_right,right);rows+=1
 if not rows:raise ValueError("empty lexicon")
 return {"status":"valid","lexicon_rows":rows,"matrix_left_size":left_size,"matrix_right_size":right_size,"max_left_id":max_left,"max_right_id":max_right}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--lexicon",type=Path,required=True);p.add_argument("--matrix",type=Path,required=True);p.add_argument("--output",type=Path);a=p.parse_args()
 result=validate(a.lexicon,a.matrix);text=json.dumps(result,ensure_ascii=False,indent=2);print(text)
 if a.output:a.output.write_text(text+"\n",encoding="utf-8")
if __name__=="__main__":main()
