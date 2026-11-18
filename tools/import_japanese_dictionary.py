#!/usr/bin/env python3
"""Import licensed Japanese dictionary CSV into kana-decoder normalized TSV.

Supported formats:
- sudachi-csv: 17-column Sudachi dictionary source CSV
- ipadic-csv: 13-column IPADIC-compatible MeCab CSV
- header-csv: CSV with explicit named columns (see --help)

This tool converts data only. It does not bundle dictionaries or determine license rights.
"""
import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path

def to_hiragana(text):
    out = []
    for ch in text:
        cp = ord(ch)
        if 0x30A1 <= cp <= 0x30F6:
            out.append(chr(cp - 0x60))
        else:
            out.append(ch)
    return "".join(out)

def clean(value):
    value = (value or "").strip()
    if not value or value == "*":
        return ""
    if any(ch in value for ch in "\t\r\n"):
        raise ValueError("field contains TSV control characters")
    return value

def candidate_id(fields):
    raw = "\x1f".join(fields).encode("utf-8")
    return "ja_" + hashlib.sha256(raw).hexdigest()[:20]

def parse_row(row, fmt, header):
    if fmt == "sudachi-csv":
        if len(row) != 17:
            raise ValueError(f"Sudachi row must have 17 columns, got {len(row)}")
        surface, left, right, cost = row[:4]
        pos = row[4:8]
        reading, lemma = row[10], row[12]
        inflection_type, inflection_form = row[8], row[9]
    elif fmt == "ipadic-csv":
        if len(row) != 13:
            raise ValueError(f"IPADIC row must have 13 columns, got {len(row)}")
        surface, left, right, cost = row[:4]
        pos = row[4:8]
        lemma, reading = row[9], row[11]
        inflection_type, inflection_form = row[8], row[9]
    else:
        indexes = {name: header.index(name) for name in header}
        required = ("surface", "reading", "pos", "word_cost", "left_id", "right_id")
        missing = [name for name in required if name not in indexes]
        if missing:
            raise ValueError(f"header CSV missing columns: {missing}")
        get = lambda name, default="": row[indexes[name]] if name in indexes and indexes[name] < len(row) else default
        surface, reading, lemma = get("surface"), get("reading"), get("lemma", "")
        pos = [get("pos")]
        left, right, cost = get("left_id"), get("right_id"), get("word_cost")
        explicit_id, group = get("candidate_id"), get("reading_group")
        inflection_type, inflection_form = get("inflection_type"), get("inflection_form")
    surface = clean(surface)
    reading = to_hiragana(clean(reading))
    if not surface or not reading:
        return None
    pos_parts = [clean(x) for x in pos if clean(x)]
    pos_label = "/".join(pos_parts) or "未知語"
    lemma = clean(lemma) or surface
    inflection_type = clean(inflection_type)
    inflection_form = clean(inflection_form)
    try:
        left_id, right_id, word_cost = int(left), int(right), int(cost)
    except (TypeError, ValueError) as exc:
        raise ValueError("connection IDs and word cost must be integers") from exc
    if not (0 <= left_id <= 2**31-1 and 0 <= right_id <= 2**31-1 and -(2**31) <= word_cost <= 2**31-1):
        raise ValueError("numeric field outside int32 range")
    group = (group if fmt == "header-csv" else "/".join(pos_parts)) or pos_label
    ident = explicit_id if fmt == "header-csv" and explicit_id else candidate_id(
        [reading, surface, lemma, pos_label, str(left_id), str(right_id), inflection_type, inflection_form])
    return [reading, surface, lemma, pos_label, group, ident, str(word_cost), "0", str(left_id), str(right_id), inflection_type, inflection_form]

def import_csv(source, output, fmt):
    rows, seen, skipped = [], set(), 0
    with source.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.reader(stream)
        header = []
        if fmt == "header-csv":
            try:
                header = [x.strip() for x in next(reader)]
            except StopIteration:
                raise ValueError("header CSV is empty")
            if len(set(header)) != len(header):
                raise ValueError("header CSV contains duplicate column names")
        for line_no, row in enumerate(reader, 2 if fmt == "header-csv" else 1):
            if not row or all(not x.strip() for x in row):
                continue
            try:
                item = parse_row(row, fmt, header)
            except Exception as exc:
                raise ValueError(f"{source}:{line_no}: {exc}") from exc
            if item is None:
                skipped += 1
                continue
            key = tuple(item)
            if key not in seen:
                seen.add(key)
                rows.append(item)
    if not rows:
        raise ValueError("no importable entries (all readings may be missing)")
    rows.sort(key=lambda x: (x[0], x[8], x[9], x[5]))
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# kana\tsurface\tlemma\tpos\treading_group\tcandidate_id\tword_cost\tconnection_cost\tleft_id\tright_id\tinflection_type\tinflection_form\n")
        for row in rows:
            stream.write("\t".join(row) + "\n")
    return {"input_format": fmt, "imported_entries": len(rows), "skipped_unreadable": skipped,
            "unique_readings": len({x[0] for x in rows}), "output": str(output)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--format", choices=("sudachi-csv", "ipadic-csv", "header-csv"), required=True)
    args = parser.parse_args()
    if not args.input.is_file():
        parser.error("input CSV does not exist")
    try:
        print(json.dumps(import_csv(args.input, args.output, args.format), ensure_ascii=False, indent=2))
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
