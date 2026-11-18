#!/usr/bin/env python3
"""Validate Sudachi V1 lexicon IDs and matrix.def with disk-backed duplicate detection."""
import argparse
import csv
import json
import sqlite3
import tempfile
from pathlib import Path


def validate(lexicon, matrix):
    matrix_rows = 0
    nonzero = 0
    with tempfile.TemporaryDirectory(prefix="kana-decoder-validate-") as tmp:
        db = sqlite3.connect(str(Path(tmp) / "validation.sqlite3"))
        db.execute("PRAGMA journal_mode=OFF")
        db.execute("PRAGMA synchronous=OFF")
        db.execute("PRAGMA temp_store=FILE")
        db.execute("PRAGMA cache_size=-8192")
        db.execute("CREATE TABLE matrix_cells (cell_key INTEGER PRIMARY KEY)")
        try:
            with matrix.open(encoding="utf-8") as stream:
                header = stream.readline().split()
                if len(header) != 2:
                    raise ValueError("matrix.def must start with left_size right_size")
                try:
                    left_size, right_size = map(int, header)
                except ValueError as exc:
                    raise ValueError("matrix dimensions must be integers") from exc
                if min(left_size, right_size) <= 0 or max(left_size, right_size) > 2**31 - 1:
                    raise ValueError("invalid matrix dimensions")
                for line_no, line in enumerate(stream, 2):
                    if not line.strip():
                        continue
                    cols = line.split()
                    if len(cols) != 3:
                        raise ValueError(f"{matrix}:{line_no}: expected right_id left_id cost")
                    try:
                        right, left, cost = map(int, cols)
                    except ValueError as exc:
                        raise ValueError(f"{matrix}:{line_no}: invalid integer") from exc
                    if not 0 <= right < right_size or not 0 <= left < left_size or not -(2**31) <= cost < 2**31:
                        raise ValueError(f"{matrix}:{line_no}: cell outside declared dimensions or int32")
                    key = (right << 32) | left
                    try:
                        db.execute("INSERT INTO matrix_cells(cell_key) VALUES (?)", (key,))
                    except sqlite3.IntegrityError as exc:
                        raise ValueError(f"{matrix}:{line_no}: duplicate connection matrix cell ({right}, {left})") from exc
                    matrix_rows += 1
                    if cost:
                        nonzero += 1
            with lexicon.open(encoding="utf-8-sig", newline="") as stream:
                reader = csv.DictReader(stream)
                raw = reader.fieldnames or []
                names = [value.strip().lower().replace("_", "") for value in raw]
                if len(set(names)) != len(names):
                    duplicates = sorted({name for name in names if names.count(name) > 1})
                    raise ValueError(f"duplicate lexicon header after normalization: {duplicates}; raw={raw!r}")
                mapping = {name: raw[index] for index, name in enumerate(names)}
                for required in ("leftid", "rightid"):
                    if required not in mapping:
                        raise ValueError(f"missing {required} column")
                max_left = max_right = -1
                rows = 0
                for line_no, row in enumerate(reader, 2):
                    try:
                        if None in row or any(value is None for value in row.values()):
                            raise ValueError("CSV row width mismatch")
                        left = int(row[mapping["leftid"]])
                        right = int(row[mapping["rightid"]])
                    except Exception as exc:
                        raise ValueError(f"{lexicon}:{line_no}: invalid context ID or row width") from exc
                    if not 0 <= left < left_size or not 0 <= right < right_size:
                        raise ValueError(f"{lexicon}:{line_no}: context ID outside matrix dimensions")
                    max_left = max(max_left, left)
                    max_right = max(max_right, right)
                    rows += 1
            if not rows:
                raise ValueError("empty lexicon")
        finally:
            db.close()
    return {
        "status": "valid", "lexicon_rows": rows, "matrix_rows": matrix_rows,
        "matrix_nonzero_cells": nonzero, "matrix_left_size": left_size,
        "matrix_right_size": right_size, "max_left_id": max_left,
        "max_right_id": max_right,
        "validation_storage": "temporary SQLite database; bounded application memory",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    rendered = json.dumps(validate(args.lexicon, args.matrix), ensure_ascii=False, indent=2)
    print(rendered)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
