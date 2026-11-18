#!/usr/bin/env python3
"""Import Sudachi V1 CSV and MeCab matrix.def into normalized Pack TSV."""
import argparse
import csv
import hashlib
import json
import re
import sqlite3
import tempfile
from pathlib import Path

HEADER = ["surface", "left_id", "right_id", "cost", "writing", "pos1", "pos2", "pos3", "pos4", "pos5", "pos6", "reading_form", "normalized_form", "dictionary_form", "mode", "split_a", "split_b", "word_structure"]


def norm(value):
    return re.sub("_", "", value.strip()).casefold()


def hira(value):
    return "".join(chr(ord(char) - 0x60) if "\u30a1" <= char <= "\u30f6" else char for char in value)


def _database(directory, schema):
    db = sqlite3.connect(str(Path(directory) / "import.sqlite3"))
    db.execute("PRAGMA journal_mode=OFF")
    db.execute("PRAGMA synchronous=OFF")
    db.execute("PRAGMA temp_store=FILE")
    db.execute("PRAGMA cache_size=-8192")
    db.executescript(schema)
    return db


def import_lexicon(source, output):
    skipped = 0
    inserted = 0
    with tempfile.TemporaryDirectory(prefix="kana-decoder-lexicon-") as tmp:
        db = _database(tmp, """
            CREATE TABLE entries (
                kana TEXT NOT NULL, surface TEXT NOT NULL, lemma TEXT NOT NULL,
                pos TEXT NOT NULL, reading_group TEXT NOT NULL, candidate_id TEXT NOT NULL,
                word_cost TEXT NOT NULL, connection_cost TEXT NOT NULL, left_id TEXT NOT NULL,
                right_id TEXT NOT NULL, inflection_type TEXT NOT NULL, inflection_form TEXT NOT NULL,
                UNIQUE(kana, surface, lemma, pos, reading_group, candidate_id, word_cost,
                       connection_cost, left_id, right_id, inflection_type, inflection_form)
            );
        """)
        try:
            with source.open(encoding="utf-8-sig", newline="") as stream:
                reader = csv.DictReader(stream)
                raw = reader.fieldnames or []
                names = [norm(value) for value in raw]
                expected = {norm(value): value for value in HEADER}
                if len(set(names)) != len(names):
                    duplicates = sorted({name for name in names if names.count(name) > 1})
                    raise ValueError(f"duplicate CSV header after case/underscore normalization: {duplicates}; raw={raw!r}")
                missing = [value for value in HEADER if norm(value) not in names]
                if missing:
                    raise ValueError("missing required Sudachi V1 columns: " + ",".join(missing))
                mapping = {name: expected[normalized] for name, normalized in zip(raw, names) if normalized in expected}
                reader.fieldnames = [mapping.get(value, value) for value in raw]
                for line_no, row in enumerate(reader, 2):
                    try:
                        if None in row or any(value is None for value in row.values()):
                            raise ValueError("CSV row column count does not match header")
                        reading = hira((row["reading_form"] or "").strip())
                        surface = (row["writing"] or row["surface"] or "").strip()
                        lemma = (row["dictionary_form"] or row["normalized_form"] or surface).strip()
                        if not reading or reading == "*" or not surface:
                            skipped += 1
                            continue
                        pos = "/".join((row[f"pos{i}"] or "").strip() for i in range(1, 7)).rstrip("/")
                        left, right, cost = int(row["left_id"]), int(row["right_id"]), int(row["cost"])
                        if min(left, right) < 0 or max(left, right, cost) > 2**31 - 1 or cost < -(2**31):
                            raise ValueError("numeric field outside int32 range")
                        fields = [reading, surface, lemma, pos or "未知語", pos or "未知語"]
                        ident = "sudachi_" + hashlib.sha256("\x1f".join(fields + [str(left), str(right), str(cost)]).encode()).hexdigest()[:20]
                        item = fields + [ident, str(cost), "0", str(left), str(right), (row["pos5"] or "").strip(), (row["pos6"] or "").strip()]
                        cursor = db.execute("INSERT OR IGNORE INTO entries VALUES (" + ",".join("?" for _ in item) + ")", item)
                        inserted += cursor.rowcount
                    except Exception as exc:
                        raise ValueError(f"{source}:{line_no}: {exc}") from exc
            if not inserted:
                raise ValueError("no usable Sudachi V1 lexicon rows")
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write("# kana\tsurface\tlemma\tpos\treading_group\tcandidate_id\tword_cost\tconnection_cost\tleft_id\tright_id\tinflection_type\tinflection_form\n")
                cursor = db.execute("""
                    SELECT kana,surface,lemma,pos,reading_group,candidate_id,word_cost,
                           connection_cost,left_id,right_id,inflection_type,inflection_form
                    FROM entries
                    ORDER BY kana COLLATE BINARY, CAST(word_cost AS INTEGER), candidate_id COLLATE BINARY, surface COLLATE BINARY
                """)
                for row in cursor:
                    stream.write("\t".join(row) + "\n")
        finally:
            db.close()
    return {"entries": inserted, "skipped": skipped, "extra_columns_ignored": len(raw) - len(mapping), "import_storage": "temporary SQLite database; bounded application memory"}


def import_matrix(source, output):
    nonzero = 0
    input_cells = 0
    with tempfile.TemporaryDirectory(prefix="kana-decoder-matrix-") as tmp:
        db = _database(tmp, """
            CREATE TABLE cells (
                cell_key INTEGER PRIMARY KEY, right_id INTEGER NOT NULL,
                left_id INTEGER NOT NULL, cost INTEGER NOT NULL
            );
        """)
        try:
            with source.open(encoding="utf-8") as stream:
                first = stream.readline().split()
                if len(first) != 2:
                    raise ValueError("matrix.def must start with left_size right_size")
                try:
                    left_size, right_size = map(int, first)
                except ValueError as exc:
                    raise ValueError("matrix dimensions must be integers") from exc
                if min(left_size, right_size) <= 0 or max(left_size, right_size) > 2**31 - 1:
                    raise ValueError("invalid matrix dimensions")
                for line_no, line in enumerate(stream, 2):
                    if not line.strip():
                        continue
                    cols = line.split()
                    if len(cols) != 3:
                        raise ValueError(f"{source}:{line_no}: expected right_id left_id cost")
                    try:
                        right, left, cost = map(int, cols)
                    except ValueError as exc:
                        raise ValueError(f"{source}:{line_no}: invalid integer") from exc
                    if not (0 <= right < right_size and 0 <= left < left_size and -(2**31) <= cost < 2**31):
                        raise ValueError(f"{source}:{line_no}: matrix index or cost out of range")
                    key = (right << 32) | left
                    try:
                        db.execute("INSERT INTO cells VALUES (?,?,?,?)", (key, right, left, cost))
                    except sqlite3.IntegrityError as exc:
                        raise ValueError(f"{source}:{line_no}: duplicate matrix cell ({right}, {left})") from exc
                    input_cells += 1
                    if cost:
                        nonzero += 1
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write(f"# matrix {left_size} {right_size}\n")
                stream.write("# right_id\tleft_id\tcost\n")
                for right, left, cost in db.execute("SELECT right_id,left_id,cost FROM cells WHERE cost != 0 ORDER BY cell_key"):
                    stream.write(f"{right}\t{left}\t{cost}\n")
        finally:
            db.close()
    return {"left_size": left_size, "right_size": right_size, "input_cells": input_cells,
            "nonzero_cells": nonzero, "representation": "sorted sparse TSV with explicit matrix dimensions",
            "import_storage": "temporary SQLite database; bounded application memory"}


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name in ("lexicon", "matrix"):
        command = subparsers.add_parser(name)
        command.add_argument("--input", type=Path, required=True)
        command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = import_lexicon(args.input, args.output) if args.command == "lexicon" else import_matrix(args.input, args.output)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
