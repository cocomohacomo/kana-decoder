#!/usr/bin/env python3
"""Convert UD Japanese-GSD CoNLL-U into dictionary-covered token sequences.

The upstream corpus is fetched by CI at a pinned commit and is never committed
or uploaded as an artifact. UniDicInfo.pron is used as the surface reading.
"""
import argparse
import csv
import hashlib
import json
import unicodedata
from collections import Counter
from pathlib import Path
from urllib.parse import unquote

SPLITS = ("train", "dev", "test")


def to_hiragana(value):
    result = []
    for char in value:
        code = ord(char)
        if 0x30A1 <= code <= 0x30F6:
            result.append(chr(code - 0x60))
        elif code == 0x30F7:
            result.append("わ" + "゙")
        elif code == 0x30F8:
            result.append("ゐ" + "゙")
        elif code == 0x30F9:
            result.append("ゑ" + "゙")
        elif code == 0x30FA:
            result.append("を" + "゙")
        else:
            result.append(char)
    return "".join(result)


def parse_misc(misc):
    values = {}
    if misc == "_" or not misc:
        return values
    for item in misc.split("|"):
        if "=" in item:
            key, value = item.split("=", 1)
            values[key.casefold()] = unquote(value)
    return values


def token_reading(form, misc):
    values = parse_misc(misc)
    info = values.get("unidicinfo")
    if info:
        fields = info.split(",")
        if len(fields) >= 4 and fields[3] not in ("", "*", "？"):
            return to_hiragana(fields[3])
    reading = values.get("reading") or values.get("pron")
    if reading and reading not in ("*", "？"):
        return to_hiragana(reading)
    if form and all(unicodedata.category(ch)[0] in ("P", "S", "N") for ch in form):
        return form
    return None


def iter_sentences(path):
    sentence = []
    sentence_id = None
    index = 0
    with path.open(encoding="utf-8") as stream:
        for raw in stream:
            line = raw.rstrip("\r\n")
            if not line:
                if sentence:
                    index += 1
                    yield sentence_id or str(index), sentence
                    sentence = []
                    sentence_id = None
                continue
            if line.startswith("#"):
                if line.startswith("# sent_id ="):
                    sentence_id = line.split("=", 1)[1].strip()
                continue
            cols = line.split("\t")
            if len(cols) != 10:
                raise ValueError(f"{path}: malformed CoNLL-U row with {len(cols)} columns")
            token_id = cols[0]
            if "-" in token_id or "." in token_id:
                continue
            if not token_id.isdigit():
                raise ValueError(f"{path}: invalid token ID {token_id!r}")
            form = cols[1]
            reading = token_reading(form, cols[9])
            sentence.append((form, reading))
    if sentence:
        index += 1
        yield sentence_id or str(index), sentence


def read_lexicon(path):
    pairs = set()
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\r\n").split("\t")
            if len(cols) < 6:
                raise ValueError(f"{path}:{line_no}: expected at least six lexicon columns")
            if cols[0] and cols[1] and cols[5]:
                pairs.add((cols[0], cols[1]))
    if not pairs:
        raise ValueError("normalized lexicon is empty")
    return pairs


def prepare(input_dir, lexicon_path, output_path, source_commit):
    known = read_lexicon(lexicon_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    stats = {split: Counter() for split in SPLITS}
    digests = {}
    with output_path.open("w", encoding="utf-8", newline="") as out:
        writer = csv.writer(out, delimiter="\t", lineterminator="\n")
        writer.writerow(["document_id", "sentence_id", "reading", "surface", "domain", "split"])
        for split in SPLITS:
            source = input_dir / f"ja_gsd-ud-{split}.conllu"
            if not source.is_file():
                raise ValueError(f"missing UD split file: {source}")
            digests[source.name] = hashlib.sha256(source.read_bytes()).hexdigest()
            for sent_id, tokens in iter_sentences(source):
                stats[split]["source_sentences"] += 1
                stats[split]["source_tokens"] += len(tokens)
                run = []
                run_number = 0

                def flush():
                    nonlocal run_number
                    if not run:
                        return
                    run_number += 1
                    document_id = f"ud-gsd-{split}-{sent_id}"
                    segment_id = f"{sent_id}-{run_number}"
                    for reading, surface in run:
                        writer.writerow([document_id, segment_id, reading, surface,
                                         "news-blog", split])
                    stats[split]["covered_tokens"] += len(run)
                    stats[split]["covered_segments"] += 1
                    run.clear()

                for surface, reading in tokens:
                    if not reading:
                        stats[split]["missing_reading_tokens"] += 1
                        flush()
                        continue
                    reading = to_hiragana(reading)
                    if (reading, surface) not in known:
                        stats[split]["out_of_dictionary_tokens"] += 1
                        flush()
                        continue
                    run.append((reading, surface))
                flush()
                if not run_number:
                    stats[split]["fully_excluded_sentences"] += 1
                else:
                    stats[split]["sentences_with_coverage"] += 1

    summary = {
        "dataset": "UniversalDependencies/UD_Japanese-GSD",
        "source_commit": source_commit,
        "license": "CC-BY-SA-4.0",
        "license_url": "https://creativecommons.org/licenses/by-sa/4.0/legalcode",
        "source_files_sha256": digests,
        "reading_source": "CoNLL-U MISC UniDicInfo.pron, converted from katakana to hiragana",
        "coverage_policy": "unknown or unreadable tokens break a sentence into contiguous covered runs; no false adjacency is introduced",
        "splits": {key: dict(value) for key, value in stats.items()},
        "output": str(output_path.name),
        "redistribution": "source corpus and derived n-gram pack are not uploaded or committed",
    }
    output_path.with_name("ud_gsd_manifest.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args.input_dir, args.lexicon, args.output,
                                 args.source_commit), ensure_ascii=False, indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
