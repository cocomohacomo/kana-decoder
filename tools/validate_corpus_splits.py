#!/usr/bin/env python3
"""Validate train/dev/test group separation. TSV: reading, expected, domain, group_id."""
import argparse,json
from collections import Counter
from pathlib import Path
def read_split(path):
 rows=[]
 with path.open(encoding="utf-8") as f:
  for no,line in enumerate(f,1):
   if not line.strip() or line.startswith("#"):continue
   cols=line.rstrip("\r\n").split("\t")
   if len(cols)!=4:raise ValueError(f"{path}:{no}: expected 4 columns: reading, expected, domain, group_id")
   if not all(cols):raise ValueError(f"{path}:{no}: fields must be non-empty")
   rows.append(cols)
 if not rows:raise ValueError(f"{path}: split is empty")
 return rows
def validate(paths):
 data={name:read_split(path) for name,path in paths.items()};owners={}
 for split,rows in data.items():
  for row in rows:
   group=row[3];owner=owners.setdefault(group,split)
   if owner!=split:raise ValueError(f"leakage: group_id {group!r} appears in both {owner} and {split}")
 report={}
 for split,rows in data.items():
  report[split]={"cases":len(rows),"unique_groups":len({r[3] for r in rows}),"domains":dict(sorted(Counter(r[2] for r in rows).items())),"unique_readings":len({r[0] for r in rows}),"unique_references":len({r[1] for r in rows})}
 return {"schema_version":1,"status":"valid","splits":report}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ("train","dev","test"):p.add_argument("--"+n,type=Path,required=True)
 p.add_argument("--output",type=Path);a=p.parse_args()
 result=json.dumps(validate({n:getattr(a,n) for n in ("train","dev","test")}),ensure_ascii=False,indent=2)+"\n"
 if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(result,encoding="utf-8")
 print(result,end="")
if __name__=="__main__":main()
