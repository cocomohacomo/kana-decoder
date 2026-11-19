#!/usr/bin/env python3
"""Compile the train-only SURGE lexical cluster probe into a bounded optional TSV model.

The output is an experimental sidecar, not a Language Pack member and not loaded by
the production decoder. Its manifest binds the exact bytes with SHA-256.
"""
import argparse
import hashlib
import json
from pathlib import Path

import evaluate_surge_clusters as evaluator

SCHEMA = "KANA_SURGE_CLUSTER"
SCHEMA_VERSION = 1
HARD_MAX_BYTES = 64 * 1024 * 1024
HARD_MAX_EVIDENCE = 500_000
HARD_MAX_MEMBERSHIPS = 100_000
HARD_MAX_CLUSTERS = 4_096
MAX_ID_BYTES = 128
UINT32_MAX = (1 << 32) - 1


def _field(value, label):
    value = str(value)
    encoded = value.encode("utf-8")
    if not encoded or len(encoded) > MAX_ID_BYTES or "\t" in value or "\n" in value or "\r" in value:
        raise ValueError(f"invalid {label}: expected 1..{MAX_ID_BYTES} UTF-8 bytes without TSV delimiters")
    return value


def serialize_model(token_cluster, cluster_candidates, cluster_totals,
                    metadata, gap_threshold, max_bytes=HARD_MAX_BYTES):
    if not 1 <= max_bytes <= HARD_MAX_BYTES:
        raise ValueError(f"max_bytes must be between 1 and {HARD_MAX_BYTES}")
    cluster_ids = sorted(set(token_cluster.values()))
    if len(cluster_ids) > HARD_MAX_CLUSTERS:
        raise ValueError("cluster count exceeds hard limit")
    memberships = sorted(token_cluster.items(), key=lambda item: (item[1], item[0]))
    if len(memberships) > HARD_MAX_MEMBERSHIPS:
        raise ValueError("cluster membership count exceeds hard limit")

    evidence = []
    for cluster_id, counts in cluster_candidates.items():
        for (candidate_id, distance), count in counts.items():
            if not isinstance(cluster_id, int) or cluster_id < 0 or cluster_id >= HARD_MAX_CLUSTERS:
                raise ValueError("cluster id outside supported range")
            if not isinstance(distance, int) or distance not in (1, 2):
                raise ValueError("distance must be 1 or 2")
            if not isinstance(count, int) or not 1 <= count <= UINT32_MAX:
                raise ValueError("evidence count outside uint32 range")
            evidence.append((cluster_id, distance, _field(candidate_id, "candidate_id"), count))
    evidence.sort()
    if len(evidence) > HARD_MAX_EVIDENCE:
        raise ValueError("evidence record count exceeds hard limit")

    totals = []
    for (cluster_id, distance), count in cluster_totals.items():
        if not isinstance(cluster_id, int) or cluster_id < 0 or cluster_id >= HARD_MAX_CLUSTERS:
            raise ValueError("cluster total id outside supported range")
        if not isinstance(distance, int) or distance not in (1, 2):
            raise ValueError("cluster total distance must be 1 or 2")
        if not isinstance(count, int) or not 1 <= count <= UINT32_MAX:
            raise ValueError("cluster total outside uint32 range")
        totals.append((cluster_id, distance, count))
    totals.sort()

    lines = [f"{SCHEMA}\t{SCHEMA_VERSION}"]
    scalar_meta = {
        "train_sequences": int(metadata["train_sequences"]),
        "vocabulary_considered": int(metadata["vocabulary_considered"]),
        "cluster_count": len(cluster_ids),
        "clustered_tokens": len(memberships),
        "window": int(metadata["window"]),
        "max_history": 2,
        "gap_threshold": float(gap_threshold),
        "smoothing_alpha": 0.5,
    }
    if scalar_meta["window"] != 2 or not 0.0 <= scalar_meta["gap_threshold"] <= 1_000_000:
        raise ValueError("unsupported window or gap threshold")
    for key, value in scalar_meta.items():
        if isinstance(value, float):
            if not (value == value and abs(value) != float("inf")):
                raise ValueError(f"non-finite metadata: {key}")
            rendered = format(value, ".9g")
        else:
            if value < 0 or value > UINT32_MAX:
                raise ValueError(f"metadata outside uint32 range: {key}")
            rendered = str(value)
        lines.append(f"META\t{key}\t{rendered}")
    for candidate_id, cluster_id in sorted(token_cluster.items(), key=lambda item: (item[1], item[0])):
        if not isinstance(cluster_id, int) or cluster_id < 0 or cluster_id >= HARD_MAX_CLUSTERS:
            raise ValueError("token cluster id outside supported range")
        lines.append(f"TOKEN\t{cluster_id}\t{_field(candidate_id, 'candidate_id')}")
    for cluster_id, distance, candidate_id, count in evidence:
        lines.append(f"EVIDENCE\t{cluster_id}\t{distance}\t{candidate_id}\t{count}")
    for cluster_id, distance, count in totals:
        lines.append(f"TOTAL\t{cluster_id}\t{distance}\t{count}")

    payload = ("\n".join(lines) + "\n").encode("utf-8")
    if len(payload) > max_bytes:
        raise ValueError(f"serialized model exceeds byte limit: {len(payload)} > {max_bytes}")
    stats = {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "bytes": len(payload),
        "cluster_count": len(cluster_ids),
        "membership_count": len(memberships),
        "evidence_count": len(evidence),
        "total_count": len(totals),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "max_bytes": max_bytes,
    }
    return payload, stats


def compile_model(lexicon_path, corpus_path, sequences_path, output_path,
                  max_bytes=HARD_MAX_BYTES):
    # Reuse the evaluator's strict split handling and dev-only threshold selection.
    evaluation = evaluator.analyze(lexicon_path, corpus_path, sequences_path)
    threshold = evaluation["calibration"]["selected_gap_threshold"]
    _by_reading, _by_pair = evaluator.read_lexicon(lexicon_path)
    sequences = evaluator.read_sequences(sequences_path)
    token_cluster, cluster_candidates, cluster_totals, metadata = evaluator.make_clusters(sequences)
    payload, stats = serialize_model(token_cluster, cluster_candidates,
                                    cluster_totals, metadata, threshold, max_bytes)
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(payload)
    manifest = dict(stats)
    manifest.update({
        "model_file": output_path.name,
        "calibration_split": "dev",
        "cluster_generation_split": "train",
        "test_used_for_calibration": False,
        "runtime_integration": "disabled",
    })
    manifest_path = output_path.with_suffix(output_path.suffix + ".manifest.json")
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
                              encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--train-sequences", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-bytes", type=int, default=HARD_MAX_BYTES)
    args = parser.parse_args()
    try:
        result = compile_model(args.lexicon, args.corpus, args.train_sequences,
                               args.output, args.max_bytes)
        print(json.dumps({"surge_cluster_model": result}, ensure_ascii=False, indent=2))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
