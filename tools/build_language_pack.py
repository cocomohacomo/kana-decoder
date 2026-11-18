#!/usr/bin/env python3
"""Build a deterministic offline Language Pack from normalized TSV inputs.

Dictionary TSV columns:
kana, surface, lemma, pos, reading_group[, candidate_id, word_cost,
connection_cost, left_id, right_id]
Training sequence TSV: one utterance per line, candidate IDs separated by tabs.
"""
import argparse
import hashlib
import json
import shutil
from collections import Counter
from pathlib import Path

MAX_I32 = 2**31 - 1
MAX_U32 = 2**32 - 1

def clean_field(value, label):
    value = value.strip()
    if not value or any(ch in value for ch in "\t\r\n"):
        raise ValueError(f"invalid {label}")
    return value

def parse_i32(value, label, minimum=-(2**31)):
    try:
        number = int(value)
    except ValueError as exc:
        raise ValueError(f"invalid integer {label}: {value}") from exc
    if number < minimum or number > MAX_I32:
        raise ValueError(f"integer out of range {label}: {value}")
    return number

def import_lexicon(source):
    rows = []
    seen = set()
    with source.open(encoding="utf-8", newline="") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\r\n").split("\t")
            if len(cols) < 5 or len(cols) > 10:
                raise ValueError(f"{source}:{line_no}: expected 5..10 columns")
            cols += [""] * (10 - len(cols))
            kana, surface, lemma, pos, group, candidate_id = cols[:6]
            kana = clean_field(kana, "reading")
            surface = clean_field(surface, "surface")
            lemma = clean_field(lemma or surface, "lemma")
            pos = clean_field(pos, "pos")
            group = clean_field(group, "reading_group")
            candidate_id = clean_field(candidate_id or group, "candidate_id")
            costs = [
                parse_i32(cols[6] or "0", "word_cost"),
                parse_i32(cols[7] or "0", "connection_cost"),
                parse_i32(cols[8] or "0", "left_id", 0),
                parse_i32(cols[9] or "0", "right_id", 0),
            ]
            row = [kana, surface, lemma, pos, group, candidate_id] + [str(x) for x in costs]
            key = tuple(row)
            if key not in seen:
                seen.add(key)
                rows.append(row)
    if not rows:
        raise ValueError("dictionary contains no entries")
    rows.sort(key=lambda row: (row[0], row[6], row[5], row[1]))
    return rows

def train_ngrams(sequence_file, known_candidates):
    counts = Counter()
    if sequence_file is None:
        return counts
    with sequence_file.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            tokens = [clean_field(x, "candidate token") for x in line.rstrip("\r\n").split("\t")]
            unknown = [token for token in tokens if token not in known_candidates]
            if unknown:
                raise ValueError(f"{sequence_file}:{line_no}: unknown candidate IDs: {unknown}")
            for i, token in enumerate(tokens):
                counts[("U", "", token)] += 1
                if i:
                    counts[("L", tokens[i - 1], token)] += 1
                if i + 1 < len(tokens):
                    counts[("R", tokens[i + 1], token)] += 1
    if any(value > MAX_U32 for value in counts.values()):
        raise ValueError("trained n-gram count exceeds uint32 pack format")
    return counts

def write_pack(args):
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        raise ValueError("output directory must be empty")
    out.mkdir(parents=True, exist_ok=True)
    rows = import_lexicon(args.lexicon.resolve())
    known_candidates = {row[5] for row in rows}
    counts = train_ngrams(args.sequences.resolve() if args.sequences else None, known_candidates)

    with (out / "lexicon.tsv").open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# kana\tsurface\tlemma\tpos\treading_group\tcandidate_id\tword_cost\tconnection_cost\tleft_id\tright_id\n")
        for row in rows:
            stream.write("\t".join(row) + "\n")
    with (out / "ngram.tsv").open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# direction\tcontext_token\tcandidate_id\tcount\n")
        for (direction, context, candidate), count in sorted(counts.items()):
            stream.write(f"{direction}\t{context}\t{candidate}\t{count}\n")

    payload_names = ["lexicon.tsv", "ngram.tsv"]
    if args.connections:
        connection_source = args.connections.resolve()
        if not connection_source.is_file():
            raise ValueError("connections file does not exist")
        shutil.copyfile(connection_source, out / "connections.tsv")
        payload_names.append("connections.tsv")
    license_source = args.license_file.resolve()
    if not license_source.is_file():
        raise ValueError("license file does not exist")
    shutil.copyfile(license_source, out / "LICENSE")
    payload_names.append("LICENSE")

    files = []
    for name in payload_names:
        data = (out / name).read_bytes()
        files.append({"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    manifest = {
        "pack_id": args.pack_id,
        "language_id": args.language_id,
        "pack_version": args.pack_version,
        "core_abi_version": "0.1",
        "dictionary_version": args.dictionary_version,
        "model_version": "candidate-ngram-counts-1",
        "format_version": 1,
        "reading_encoding_id": args.reading_encoding,
        "normalization_profile": args.normalization_profile,
        "orthography_profile": args.orthography_profile,
        "license": {"spdx": args.license_spdx, "file": "LICENSE", "upstream": args.upstream},
        "resource_limits": {"max_expanded_bytes": args.max_expanded_bytes, "max_candidates": 8, "max_edit_distance": 1},
        "files": files,
        "status": "imported-pack-requires-accuracy-validation",
    }
    (out / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"pack_id": args.pack_id, "entries": len(rows), "trained_ngram_records": len(counts), "files": payload_names}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--license-file", type=Path, required=True)
    parser.add_argument("--license-spdx", required=True)
    parser.add_argument("--upstream", required=True)
    parser.add_argument("--pack-id", required=True)
    parser.add_argument("--language-id", required=True)
    parser.add_argument("--pack-version", default="0.1.0")
    parser.add_argument("--dictionary-version", default="imported")
    parser.add_argument("--sequences", type=Path)
    parser.add_argument("--connections", type=Path)
    parser.add_argument("--reading-encoding", default="utf8")
    parser.add_argument("--normalization-profile", default="none-declared")
    parser.add_argument("--orthography-profile", default="language-default")
    parser.add_argument("--max-expanded-bytes", type=int, default=30 * 1024 * 1024)
    args = parser.parse_args()
    if args.max_expanded_bytes <= 0:
        parser.error("--max-expanded-bytes must be positive")
    if not args.license_spdx.strip() or not args.upstream.strip():
        parser.error("license SPDX and upstream metadata are required")
    print(json.dumps(write_pack(args), ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
