#!/usr/bin/env python3
"""Strict Sudachi V1 CSV and MeCab matrix.def importer."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

HEADER = ["surface", "left_id", "right_id", "cost", "writing",
          "pos1", "pos2", "pos3", "pos4", "pos5", "pos6",
          "reading_form", "normalized_form", "dictionary_form",
          "mode", "split_a", "split_b", "word_structure"]

def hira(value):
    return "".join(chr(ord(c)-0x60) if "\u30a1" <= c <= "\u30f6" else c for c in value)

def import_lexicon(source, output):
    rows, seen, skipped = [], set(), 0
    with source.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        names = [x.strip().lower() for x in (reader.fieldnames or [])]
        if names != HEADER:
            raise ValueError("expected exact Sudachi V1 18-column header: " + ",".join(HEADER))
        for line, row in enumerate(reader, 2):
            try:
                reading = hira((row["reading_form"] or "").strip())
                surface = (row["writing"] or row["surface"] or "").strip()
                lemma = (row["dictionary_form"] or row["normalized_form"] or surface).strip()
                if not reading or reading == "*" or not surface:
                    skipped += 1
                    continue
                pos = "/".join((row[f"pos{i}"] or "").strip() for i in range(1,7)).rstrip("/")
                left, right, cost = int(row["left_id"]), int(row["right_id"]), int(row["cost"])
                if min(left,right) < 0 or max(left,right,cost) > 2**31-1 or cost < -(2**31):
                    raise ValueError("numeric field outside int32 range")
                fields = [reading,surface,lemma,pos or "未知語",pos or "未知語"]
                ident = "sudachi_" + hashlib.sha256("\x1f".join(fields+[str(left),str(right),str(cost)]).encode()).hexdigest()[:20]
                item = fields+[ident,str(cost),"0",str(left),str(right),
                               (row["pos5"] or "").strip(),(row["pos6"] or "").strip()]
                key = tuple(item)
                if key not in seen:
                    seen.add(key); rows.append(item)
            except Exception as exc:
                raise ValueError(f"{source}:{line}: {exc}") from exc
    if not rows:
        raise ValueError("no usable Sudachi V1 lexicon rows")
    rows.sort(key=lambda x:(x[0],int(x[8]),int(x[9]),x[5]))
    with output.open("w",encoding="utf-8",newline="\n") as stream:
        stream.write("# kana\tsurface\tlemma\tpos\treading_group\tcandidate_id\tword_cost\tconnection_cost\tleft_id\tright_id\tinflection_type\tinflection_form\n")
        for row in rows: stream.write("\t".join(row)+"\n")
    return {"entries":len(rows),"skipped":skipped}

def import_matrix(source, output):
    rows, seen = [], set()
    with source.open(encoding="utf-8") as stream:
        first = stream.readline().split()
        if len(first) != 2: raise ValueError("matrix.def must start with left_size right_size")
        left_size,right_size = map(int,first)
        if min(left_size,right_size) <= 0: raise ValueError("invalid matrix dimensions")
        for line_no,line in enumerate(stream,2):
            if not line.strip(): continue
            cols=line.split()
            if len(cols)!=3: raise ValueError(f"{source}:{line_no}: expected right_id left_id cost")
            right,left,cost=map(int,cols)
            if not (0<=right<right_size and 0<=left<left_size and -(2**31)<=cost<2**31):
                raise ValueError(f"{source}:{line_no}: matrix index or cost out of range")
            key=(right,left)
            if key in seen: raise ValueError(f"{source}:{line_no}: duplicate matrix cell")
            seen.add(key)
            # ConnectionModel defaults absent cells to zero, so omit explicit
            # zero-cost cells to keep the normalized representation sparse.
            if cost != 0: rows.append((right,left,cost))
    with output.open("w",encoding="utf-8",newline="\n") as stream:
        stream.write("# right_id\tleft_id\tcost\n")
        for right,left,cost in rows: stream.write(f"{right}\t{left}\t{cost}\n")
    return {"left_size":left_size,"right_size":right_size,"nonzero_cells":len(rows), "representation":"sparse-tsv; absent cells resolve to zero"}

def main():
    p=argparse.ArgumentParser()
    sub=p.add_subparsers(dest="command",required=True)
    lex=sub.add_parser("lexicon"); lex.add_argument("--input",type=Path,required=True); lex.add_argument("--output",type=Path,required=True)
    mat=sub.add_parser("matrix"); mat.add_argument("--input",type=Path,required=True); mat.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    result=import_lexicon(a.input,a.output) if a.command=="lexicon" else import_matrix(a.input,a.output)
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=="__main__": main()
