#!/usr/bin/env python3
"""Compile tokenized Japanese corpus into leakage-safe Pack training/evaluation TSVs.

Input TSV header:
document_id<TAB>sentence_id<TAB>reading<TAB>surface<TAB>domain[<TAB>split]
Rows are tokens in sentence order. A document is assigned to exactly one split.
"""
import argparse
import csv
import hashlib
import json
from collections import OrderedDict
from pathlib import Path

SPLITS = ("train", "dev", "test")


def read_lexicon(path):
    index = {}
    with path.open(encoding="utf-8", newline="") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\r\n").split("\t")
            if len(cols) < 6:
                raise ValueError(f"{path}:{line_no}: normalized lexicon needs at least 6 columns")
            reading, surface, candidate_id = cols[0], cols[1], cols[5]
            if not reading or not surface or not candidate_id:
                raise ValueError(f"{path}:{line_no}: empty reading/surface/candidate_id")
            try:
                cost = int(cols[6]) if len(cols) > 6 and cols[6] else 0
            except ValueError as exc:
                raise ValueError(f"{path}:{line_no}: invalid word_cost") from exc
            key = (reading, surface)
            value = (cost, candidate_id)
            if key not in index or value < index[key]:
                index[key] = value
    if not index:
        raise ValueError("normalized lexicon is empty")
    return {key: value[1] for key, value in index.items()}


def assign_document_splits(document_ids):
    ordered = sorted(document_ids, key=lambda value: hashlib.sha256(value.encode("utf-8")).digest())
    count = len(ordered)
    if count == 1:
        sizes = (1, 0, 0)
    elif count == 2:
        sizes = (1, 0, 1)
    else:
        test = max(1, round(count * 0.1))
        dev = max(1, round(count * 0.1))
        train = count - dev - test
        while train < 1:
            if test > 1:
                test -= 1
            elif dev > 1:
                dev -= 1
            else:
                break
            train = count - dev - test
        sizes = (train, dev, test)
    result = {}
    offset = 0
    for split, size in zip(SPLITS, sizes):
        for document_id in ordered[offset:offset + size]:
            result[document_id] = split
        offset += size
    return result


def load_corpus(path):
    documents = OrderedDict()
    doc_splits = {}
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        required = {"document_id", "sentence_id", "reading", "surface", "domain"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError("corpus header must contain document_id, sentence_id, reading, surface, domain")
        has_split = "split" in reader.fieldnames
        for line_no, row in enumerate(reader, 2):
            doc = (row.get("document_id") or "").strip()
            sentence = (row.get("sentence_id") or "").strip()
            reading = (row.get("reading") or "").strip()
            surface = (row.get("surface") or "").strip()
            domain = (row.get("domain") or "unspecified").strip() or "unspecified"
            if not all((doc, sentence, reading, surface)):
                raise ValueError(f"{path}:{line_no}: document_id/sentence_id/reading/surface must be non-empty")
            split = (row.get("split") or "").strip().lower() if has_split else ""
            if split and split not in SPLITS:
                raise ValueError(f"{path}:{line_no}: split must be train, dev, or test")
            if doc in doc_splits and split and doc_splits[doc] != split:
                raise ValueError(f"{path}:{line_no}: document appears in multiple splits")
            if split:
                doc_splits[doc] = split
            key = (doc, sentence)
            if key not in documents:
                documents[key] = {"domain": domain, "tokens": []}
            item = documents[key]
            if item["domain"] != domain:
                raise ValueError(f"{path}:{line_no}: sentence has inconsistent domain labels")
            item["tokens"].append((reading, surface, line_no))
    if not documents:
        raise ValueError("tokenized corpus is empty")
    return documents, doc_splits


def compile_corpus(lexicon_path, corpus_path, output_dir):
    lexicon = read_lexicon(lexicon_path)
    sentences, explicit_splits = load_corpus(corpus_path)
    all_docs = {doc for doc, _ in sentences}
    inferred = assign_document_splits(all_docs - set(explicit_splits))
    assignments = {**inferred, **explicit_splits}
    output_dir.mkdir(parents=True, exist_ok=True)
    outputs = {name: [] for name in SPLITS}
    stats = {name: {"documents": 0, "sentences": 0, "tokens": 0} for name in SPLITS}
    seen_docs = {name: set() for name in SPLITS}
    for (doc, sentence_id), item in sentences.items():
        split = assignments[doc]
        ids = []
        readings = []
        surfaces = []
        for reading, surface, line_no in item["tokens"]:
            candidate_id = lexicon.get((reading, surface))
            if candidate_id is None:
                raise ValueError(
                    f"{corpus_path}:{line_no}: token absent from dictionary: reading={reading!r}, surface={surface!r}")
            ids.append(candidate_id)
            readings.append(reading)
            surfaces.append(surface)
        if split == "train":
            outputs[split].append("\t".join(ids))
        else:
            outputs[split].append("\t".join(("".join(readings), "".join(surfaces), item["domain"])))
        seen_docs[split].add(doc)
        stats[split]["sentences"] += 1
        stats[split]["tokens"] += len(ids)
    for split in SPLITS:
        stats[split]["documents"] = len(seen_docs[split])
        filename = "train_sequences.tsv" if split == "train" else f"{split}_gold.tsv"
        content = "\n".join(outputs[split])
        (output_dir / filename).write_text(content + ("\n" if content else ""), encoding="utf-8")
    if set().union(*seen_docs.values()) != all_docs:
        raise AssertionError("internal split assignment lost documents")
    for doc in all_docs:
        if sum(doc in values for values in seen_docs.values()) != 1:
            raise AssertionError(f"document leakage across splits: {doc}")
    summary = {
        "documents": len(all_docs),
        "sentences": len(sentences),
        "splits": stats,
        "files": {
            "train": "train_sequences.tsv",
            "dev": "dev_gold.tsv",
            "test": "test_gold.tsv",
        },
        "split_policy": "document-disjoint; explicit split column honored, otherwise deterministic SHA-256 ordering",
    }
    (output_dir / "corpus_manifest.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(compile_corpus(args.lexicon, args.corpus, args.output_dir),
                         ensure_ascii=False, indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
