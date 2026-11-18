#!/usr/bin/env python3
"""Import Sudachi V1 CSV and MeCab matrix.def into normalized Pack TSV."""
import argparse,csv,hashlib,json,re
from pathlib import Path
HEADER=["surface","left_id","right_id","cost","writing","pos1","pos2","pos3","pos4","pos5","pos6","reading_form","normalized_form","dictionary_form","mode","split_a","split_b","word_structure"]
def norm(s): return re.sub("_","",s.strip()).casefold()
def hira(s): return "".join(chr(ord(c)-0x60) if "\u30a1"<=c<="\u30f6" else c for c in s)
def import_lexicon(source,output):
 rows=[];seen=set();skipped=0
 with source.open(encoding="utf-8-sig",newline="") as f:
  reader=csv.DictReader(f);raw=reader.fieldnames or [];names=[norm(x) for x in raw]
  expected={norm(x):x for x in HEADER}
  if len(set(names))!=len(names): raise ValueError("duplicate CSV header after case/underscore normalization")
  missing=[x for x in HEADER if norm(x) not in names]
  if missing: raise ValueError("missing required Sudachi V1 columns: "+",".join(missing))
  mapping={r:expected[n] for r,n in zip(raw,names) if n in expected}
  reader.fieldnames=[mapping.get(x,x) for x in raw]
  for line,row in enumerate(reader,2):
   try:
    if None in row or any(v is None for v in row.values()): raise ValueError("CSV row column count does not match header")
    reading=hira((row["reading_form"] or "").strip());surface=(row["writing"] or row["surface"] or "").strip()
    lemma=(row["dictionary_form"] or row["normalized_form"] or surface).strip()
    if not reading or reading=="*" or not surface: skipped+=1;continue
    pos="/".join((row[f"pos{i}"] or "").strip() for i in range(1,7)).rstrip("/")
    left,right,cost=int(row["left_id"]),int(row["right_id"]),int(row["cost"])
    if min(left,right)<0 or max(left,right,cost)>2**31-1 or cost<-(2**31): raise ValueError("numeric field outside int32 range")
    fields=[reading,surface,lemma,pos or "未知語",pos or "未知語"]
    ident="sudachi_"+hashlib.sha256("\x1f".join(fields+[str(left),str(right),str(cost)]).encode()).hexdigest()[:20]
    item=fields+[ident,str(cost),"0",str(left),str(right),(row["pos5"] or "").strip(),(row["pos6"] or "").strip()]
    key=tuple(item)
    if key not in seen: seen.add(key);rows.append(item)
   except Exception as exc: raise ValueError(f"{source}:{line}: {exc}") from exc
 if not rows: raise ValueError("no usable Sudachi V1 lexicon rows")
 rows.sort(key=lambda x:(x[0],int(x[8]),int(x[9]),x[5]));output.parent.mkdir(parents=True,exist_ok=True)
 with output.open("w",encoding="utf-8",newline="\n") as f:
  f.write("# kana\tsurface\tlemma\tpos\treading_group\tcandidate_id\tword_cost\tconnection_cost\tleft_id\tright_id\tinflection_type\tinflection_form\n")
  for row in rows:f.write("\t".join(row)+"\n")
 return {"entries":len(rows),"skipped":skipped,"extra_columns_ignored":len(raw)-len(mapping)}
def import_matrix(source,output):
 rows=[];seen=set()
 with source.open(encoding="utf-8") as f:
  first=f.readline().split()
  if len(first)!=2:raise ValueError("matrix.def must start with left_size right_size")
  left_size,right_size=map(int,first)
  if min(left_size,right_size)<=0:raise ValueError("invalid matrix dimensions")
  for line_no,line in enumerate(f,2):
   if not line.strip():continue
   cols=line.split()
   if len(cols)!=3:raise ValueError(f"{source}:{line_no}: expected right_id left_id cost")
   right,left,cost=map(int,cols)
   if not(0<=right<right_size and 0<=left<left_size and -(2**31)<=cost<2**31):raise ValueError(f"{source}:{line_no}: matrix index or cost out of range")
   key=(right,left)
   if key in seen:raise ValueError(f"{source}:{line_no}: duplicate matrix cell")
   seen.add(key)
   if cost!=0:rows.append((right,left,cost))
 output.parent.mkdir(parents=True,exist_ok=True)
 with output.open("w",encoding="utf-8",newline="\n") as f:
  f.write("# right_id\tleft_id\tcost\n")
  for right,left,cost in rows:f.write(f"{right}\t{left}\t{cost}\n")
 return {"left_size":left_size,"right_size":right_size,"nonzero_cells":len(rows),"representation":"sparse-tsv; absent cells resolve to zero"}
def main():
 p=argparse.ArgumentParser();sub=p.add_subparsers(dest="command",required=True)
 for name in ("lexicon","matrix"):
  q=sub.add_parser(name);q.add_argument("--input",type=Path,required=True);q.add_argument("--output",type=Path,required=True)
 a=p.parse_args();r=import_lexicon(a.input,a.output) if a.command=="lexicon" else import_matrix(a.input,a.output)
 print(json.dumps(r,ensure_ascii=False,indent=2))
if __name__=="__main__":main()
