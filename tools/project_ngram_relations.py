#!/usr/bin/env python3
"""Project train-only directional n-gram statistics into SURGE relation events."""
import argparse, hashlib, json, math
from collections import Counter
from pathlib import Path

SCHEMA = "SURGE_RELATION_EVENT"
VERSION = 1
HARD_MAX_DISTANCE = 4
HARD_MAX_EVENTS = 2_000_000
HARD_MAX_BYTES = 128 * 1024 * 1024
MAX_TOKEN_BYTES = 256
MAX_LINE_BYTES = 1024 * 1024

def _token(value):
    raw = value.encode("utf-8")
    if not raw or len(raw) > MAX_TOKEN_BYTES or any(c in value for c in "\t\r\n"):
        raise ValueError("token must be 1..256 UTF-8 bytes without TSV delimiters")
    return value

def read_sequences(path):
    sequences = []
    with Path(path).open(encoding="utf-8", newline="") as stream:
        for line_no, line in enumerate(stream, 1):
            if len(line.encode("utf-8")) > MAX_LINE_BYTES:
                raise ValueError(f"{path}:{line_no}: line exceeds byte limit")
            line = line.rstrip("\n")
            if line.endswith("\r"):
                raise ValueError(f"{path}:{line_no}: CR line endings are not supported")
            if not line:
                continue
            tokens = [_token(token) for token in line.split("\t")]
            if any(not token for token in tokens):
                raise ValueError(f"{path}:{line_no}: empty token")
            sequences.append(tokens)
    if not sequences:
        raise ValueError("training sequences are empty")
    return sequences

def build_relation_events(sequences, max_distance=3, min_count=2,
                          reliability_prior=5.0, distance_gamma=0.5, pmi_clip=8.0):
    if not 1 <= max_distance <= HARD_MAX_DISTANCE:
        raise ValueError("max_distance must be in [1, 4]")
    if min_count < 1 or reliability_prior <= 0 or distance_gamma < 0 or pmi_clip <= 0:
        raise ValueError("invalid relation extraction parameter")
    unigram, relation_counts, source_opportunities = Counter(), Counter(), Counter()
    total_tokens = 0
    for sequence in sequences:
        unigram.update(sequence)
        total_tokens += len(sequence)
        for index, source in enumerate(sequence):
            for distance in range(1, max_distance + 1):
                for direction, target_index in (("right", index + distance), ("left", index - distance)):
                    if 0 <= target_index < len(sequence):
                        target = sequence[target_index]
                        relation_counts[(source, target, direction, distance)] += 1
                        source_opportunities[(source, direction, distance)] += 1
    events = []
    for (source, target, direction, distance), count in sorted(relation_counts.items()):
        if count < min_count:
            continue
        opportunity = source_opportunities[(source, direction, distance)]
        p_cond, p_target = count / opportunity, unigram[target] / total_tokens
        pmi = math.log(max(p_cond, 1e-15) / max(p_target, 1e-15))
        clipped_pmi = max(-pmi_clip, min(pmi_clip, pmi))
        reliability = count / (count + reliability_prior)
        distance_weight = 1.0 / (distance ** distance_gamma)
        association = reliability * clipped_pmi * distance_weight
        events.append({
            "schema": SCHEMA, "version": VERSION,
            "source": {"namespace": "ja-JP", "key": source},
            "target": {"namespace": "ja-JP", "key": target},
            "relation": {
                "family": "ngram", "direction": direction, "distance": distance,
                "ordered": True, "count": count, "source_opportunities": opportunity,
                "conditional_probability": round(p_cond, 12),
                "target_probability": round(p_target, 12),
                "pmi": round(clipped_pmi, 12), "reliability": round(reliability, 12),
                "distance_weight": round(distance_weight, 12),
                "association": round(association, 12)
            }
        })
        if len(events) > HARD_MAX_EVENTS:
            raise ValueError("event count exceeds hard limit")
    events.sort(key=lambda e: (e["source"]["key"], e["relation"]["direction"],
                               e["relation"]["distance"], e["target"]["key"]))
    return events, {
        "train_sequences": len(sequences), "token_count": total_tokens,
        "vocabulary_size": len(unigram), "event_count": len(events),
        "max_distance": max_distance, "min_count": min_count,
        "reliability_prior": reliability_prior, "distance_gamma": distance_gamma,
        "pmi_clip": pmi_clip
    }

def serialize_events(events, metadata, max_bytes=HARD_MAX_BYTES):
    if not 1 <= max_bytes <= HARD_MAX_BYTES:
        raise ValueError("max_bytes outside supported range")
    lines = [json.dumps(event, ensure_ascii=False, sort_keys=True, separators=(",", ":")) for event in events]
    payload = (("\n".join(lines) + "\n") if lines else "").encode("utf-8")
    if len(payload) > max_bytes:
        raise ValueError(f"serialized events exceed byte limit: {len(payload)} > {max_bytes}")
    manifest = {
        "schema": SCHEMA, "schema_version": VERSION, "format": "jsonl",
        "sha256": hashlib.sha256(payload).hexdigest(), "bytes": len(payload),
        "runtime_integration": "experimental", "cluster_assignment": "surge_core",
        "training_split": "train", "test_used_for_training": False, **metadata
    }
    return payload, manifest

def compile_model(train_path, output_path, max_distance=3, min_count=2,
                  reliability_prior=5.0, distance_gamma=0.5, pmi_clip=8.0,
                  max_bytes=HARD_MAX_BYTES):
    sequences = read_sequences(train_path)
    events, metadata = build_relation_events(sequences, max_distance, min_count,
                                             reliability_prior, distance_gamma, pmi_clip)
    payload, manifest = serialize_events(events, metadata, max_bytes)
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)
    output_path.with_suffix(output_path.suffix + ".manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return manifest

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--train-sequences", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-distance", type=int, default=3)
    parser.add_argument("--min-count", type=int, default=2)
    parser.add_argument("--reliability-prior", type=float, default=5.0)
    parser.add_argument("--distance-gamma", type=float, default=0.5)
    parser.add_argument("--pmi-clip", type=float, default=8.0)
    parser.add_argument("--max-bytes", type=int, default=HARD_MAX_BYTES)
    args = parser.parse_args()
    try:
        result = compile_model(args.train_sequences, args.output, args.max_distance,
                               args.min_count, args.reliability_prior,
                               args.distance_gamma, args.pmi_clip, args.max_bytes)
        print(json.dumps({"relation_model": result}, ensure_ascii=False, indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")

if __name__ == "__main__":
    main()
