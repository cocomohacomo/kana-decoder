#!/usr/bin/env python3
"""Stream train-only directional n-gram statistics into SURGE relation events.

The CLI path is disk-backed: it never retains the training corpus or the complete
relation-event set in RAM. SQLite is used as a bounded working store and JSONL
is emitted in deterministic order.
"""
import argparse
import hashlib
import json
import math
import os
import sqlite3
import tempfile
from collections import Counter
from pathlib import Path

SCHEMA = "SURGE_RELATION_EVENT"
VERSION = 1
HARD_MAX_DISTANCE = 4
HARD_MAX_EVENTS = 2_000_000
HARD_MAX_BYTES = 128 * 1024 * 1024
MAX_TOKEN_BYTES = 256
MAX_LINE_BYTES = 1024 * 1024
SQL_BATCH_SIZE = 4096


def _token(value):
    raw = value.encode("utf-8")
    if not raw or len(raw) > MAX_TOKEN_BYTES or any(c in value for c in "\t\r\n\0"):
        raise ValueError("token must be 1..256 UTF-8 bytes without TSV/control delimiters")
    return value


def iter_sequences(path, digest=None):
    """Yield one validated token sequence at a time, optionally hashing raw bytes."""
    with Path(path).open("rb") as stream:
        for line_no, raw in enumerate(stream, 1):
            if digest is not None:
                digest.update(raw)
            if len(raw) > MAX_LINE_BYTES:
                raise ValueError(f"{path}:{line_no}: line exceeds byte limit")
            if raw.endswith(b"\r\n") or b"\r" in raw:
                raise ValueError(f"{path}:{line_no}: CR line endings are not supported")
            try:
                line = raw[:-1] if raw.endswith(b"\n") else raw
                decoded = line.decode("utf-8", errors="strict")
            except UnicodeDecodeError as exc:
                raise ValueError(f"{path}:{line_no}: invalid UTF-8") from exc
            if not decoded:
                continue
            tokens = [_token(token) for token in decoded.split("\t")]
            if any(not token for token in tokens):
                raise ValueError(f"{path}:{line_no}: empty token")
            yield tokens


def read_sequences(path):
    """Compatibility helper for small unit fixtures; production CLI is streaming."""
    sequences = list(iter_sequences(path))
    if not sequences:
        raise ValueError("training sequences are empty")
    return sequences


def _validate_parameters(max_distance, min_count, reliability_prior, distance_gamma, pmi_clip):
    if not 1 <= max_distance <= HARD_MAX_DISTANCE:
        raise ValueError("max_distance must be in [1, 4]")
    if (min_count < 1 or not math.isfinite(reliability_prior) or reliability_prior <= 0
            or not math.isfinite(distance_gamma) or distance_gamma < 0
            or not math.isfinite(pmi_clip) or pmi_clip <= 0):
        raise ValueError("invalid relation extraction parameter")


def _event(source, target, direction, distance, count, opportunity, target_count,
           total_tokens, reliability_prior, distance_gamma, pmi_clip):
    p_cond = count / opportunity
    p_target = target_count / total_tokens
    pmi = math.log(max(p_cond, 1e-15) / max(p_target, 1e-15))
    clipped_pmi = max(-pmi_clip, min(pmi_clip, pmi))
    reliability = count / (count + reliability_prior)
    distance_weight = 1.0 / (distance ** distance_gamma)
    association = reliability * clipped_pmi * distance_weight
    return {
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
    }


def build_relation_events(sequences, max_distance=3, min_count=2,
                          reliability_prior=5.0, distance_gamma=0.5, pmi_clip=8.0):
    """In-memory compatibility API used by small unit tests and notebooks."""
    _validate_parameters(max_distance, min_count, reliability_prior, distance_gamma, pmi_clip)
    unigram, relation_counts, source_opportunities = Counter(), Counter(), Counter()
    total_tokens = 0
    sequence_count = 0
    for sequence in sequences:
        sequence_count += 1
        unigram.update(sequence)
        total_tokens += len(sequence)
        for index, source in enumerate(sequence):
            for distance in range(1, max_distance + 1):
                for direction, target_index in (("right", index + distance), ("left", index - distance)):
                    if 0 <= target_index < len(sequence):
                        target = sequence[target_index]
                        relation_counts[(source, target, direction, distance)] += 1
                        source_opportunities[(source, direction, distance)] += 1
    if total_tokens == 0:
        raise ValueError("training sequences are empty")
    events = []
    for (source, target, direction, distance), count in sorted(relation_counts.items()):
        if count < min_count:
            continue
        events.append(_event(source, target, direction, distance, count,
            source_opportunities[(source, direction, distance)], unigram[target],
            total_tokens, reliability_prior, distance_gamma, pmi_clip))
        if len(events) > HARD_MAX_EVENTS:
            raise ValueError("event count exceeds hard limit")
    events.sort(key=lambda e: (e["source"]["key"], e["relation"]["direction"],
                               e["relation"]["distance"], e["target"]["key"]))
    return events, {
        "train_sequences": sequence_count, "token_count": total_tokens,
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


def _flush_batches(connection, unigram_batch, relation_batch, opportunity_batch):
    if unigram_batch:
        connection.executemany(
            "INSERT INTO unigram(token,count) VALUES(?,?) "
            "ON CONFLICT(token) DO UPDATE SET count=count+excluded.count", unigram_batch.items())
        unigram_batch.clear()
    if relation_batch:
        connection.executemany(
            "INSERT INTO relation_count(source,target,direction,distance,count) VALUES(?,?,?,?,?) "
            "ON CONFLICT(source,target,direction,distance) DO UPDATE SET count=count+excluded.count",
            [(s, t, d, n, c) for (s, t, d, n), c in relation_batch.items()])
        relation_batch.clear()
    if opportunity_batch:
        connection.executemany(
            "INSERT INTO opportunity(source,direction,distance,count) VALUES(?,?,?,?) "
            "ON CONFLICT(source,direction,distance) DO UPDATE SET count=count+excluded.count",
            [(s, d, n, c) for (s, d, n), c in opportunity_batch.items()])
        opportunity_batch.clear()


def _stream_count_to_sqlite(train_path, connection, max_distance):
    digest = hashlib.sha256()
    unigram_batch, relation_batch, opportunity_batch = Counter(), Counter(), Counter()
    total_tokens = sequence_count = pending = 0
    for sequence in iter_sequences(train_path, digest):
        sequence_count += 1
        total_tokens += len(sequence)
        for token in sequence:
            unigram_batch[token] += 1
            pending += 1
        for index, source in enumerate(sequence):
            for distance in range(1, max_distance + 1):
                for direction, target_index in (("right", index + distance), ("left", index - distance)):
                    if 0 <= target_index < len(sequence):
                        relation_batch[(source, sequence[target_index], direction, distance)] += 1
                        opportunity_batch[(source, direction, distance)] += 1
                        pending += 1
                        if pending >= SQL_BATCH_SIZE:
                            _flush_batches(connection, unigram_batch, relation_batch, opportunity_batch)
                            connection.commit()
                            pending = 0
    _flush_batches(connection, unigram_batch, relation_batch, opportunity_batch)
    connection.commit()
    if sequence_count == 0 or total_tokens == 0:
        raise ValueError("training sequences are empty")
    return sequence_count, total_tokens, digest.hexdigest()


def _write_streaming_events(connection, output_path, total_tokens, sequence_count,
                            input_sha256, max_distance, min_count, reliability_prior,
                            distance_gamma, pmi_clip, max_bytes):
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=output_path.name + ".", suffix=".tmp", dir=str(output_path.parent))
    digest = hashlib.sha256()
    written = event_count = 0
    try:
        with os.fdopen(fd, "wb") as target:
            rows = connection.execute(
                "SELECT r.source,r.target,r.direction,r.distance,r.count,o.count,u.count "
                "FROM relation_count r "
                "JOIN opportunity o ON o.source=r.source AND o.direction=r.direction AND o.distance=r.distance "
                "JOIN unigram u ON u.token=r.target "
                "ORDER BY r.source,r.direction,r.distance,r.target")
            for source, target, direction, distance, count, opportunity, target_count in rows:
                if count < min_count:
                    continue
                event = _event(source, target, direction, distance, count, opportunity,
                    target_count, total_tokens, reliability_prior, distance_gamma, pmi_clip)
                encoded = (json.dumps(event, ensure_ascii=False, sort_keys=True,
                                      separators=(",", ":")) + "\n").encode("utf-8")
                if event_count >= HARD_MAX_EVENTS:
                    raise ValueError("event count exceeds hard limit")
                if written + len(encoded) > max_bytes:
                    raise ValueError(f"serialized events exceed byte limit: {written + len(encoded)} > {max_bytes}")
                target.write(encoded)
                digest.update(encoded)
                written += len(encoded)
                event_count += 1
            target.flush()
            os.fsync(target.fileno())
        manifest = {
            "schema": SCHEMA, "schema_version": VERSION, "format": "jsonl",
            "sha256": digest.hexdigest(), "input_sha256": input_sha256, "bytes": written,
            "runtime_integration": "experimental", "cluster_assignment": "surge_core",
            "training_split": "train", "test_used_for_training": False,
            "train_sequences": sequence_count, "token_count": total_tokens,
            "vocabulary_size": connection.execute("SELECT COUNT(*) FROM unigram").fetchone()[0],
            "event_count": event_count, "max_distance": max_distance, "min_count": min_count,
            "reliability_prior": reliability_prior, "distance_gamma": distance_gamma,
            "pmi_clip": pmi_clip
        }
        os.replace(temp_name, output_path)
        manifest_path = output_path.with_suffix(output_path.suffix + ".manifest.json")
        manifest_tmp = manifest_path.with_name(manifest_path.name + ".tmp")
        manifest_tmp.write_text(json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
                                encoding="utf-8")
        os.replace(manifest_tmp, manifest_path)
        return manifest
    except Exception:
        try:
            os.unlink(temp_name)
        except OSError:
            pass
        raise


def compile_model(train_path, output_path, max_distance=3, min_count=2,
                  reliability_prior=5.0, distance_gamma=0.5, pmi_clip=8.0,
                  max_bytes=HARD_MAX_BYTES, split="train"):
    _validate_parameters(max_distance, min_count, reliability_prior, distance_gamma, pmi_clip)
    if split != "train":
        raise ValueError("only --split train is accepted; dev/test must never build relation memory")
    if not 1 <= max_bytes <= HARD_MAX_BYTES:
        raise ValueError("max_bytes outside supported range")
    train_path, output_path = Path(train_path), Path(output_path)
    if train_path.resolve() == output_path.resolve():
        raise ValueError("input and output paths must differ")
    with tempfile.TemporaryDirectory(prefix="surge-ngram-") as temporary:
        database = Path(temporary) / "relations.sqlite3"
        connection = sqlite3.connect(str(database))
        try:
            connection.execute("PRAGMA journal_mode=OFF")
            connection.execute("PRAGMA synchronous=OFF")
            connection.execute("PRAGMA temp_store=FILE")
            connection.executescript(
                "CREATE TABLE unigram(token TEXT PRIMARY KEY,count INTEGER NOT NULL);"
                "CREATE TABLE relation_count(source TEXT,target TEXT,direction TEXT,distance INTEGER,count INTEGER NOT NULL,"
                "PRIMARY KEY(source,target,direction,distance));"
                "CREATE TABLE opportunity(source TEXT,direction TEXT,distance INTEGER,count INTEGER NOT NULL,"
                "PRIMARY KEY(source,direction,distance));")
            sequences, total_tokens, input_sha256 = _stream_count_to_sqlite(
                train_path, connection, max_distance)
            return _write_streaming_events(connection, output_path, total_tokens, sequences,
                input_sha256, max_distance, min_count, reliability_prior,
                distance_gamma, pmi_clip, max_bytes)
        finally:
            connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--train-sequences", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--split", choices=("train",), required=True,
                        help="explicitly assert that the source is the training split")
    parser.add_argument("--max-distance", type=int, default=3)
    parser.add_argument("--min-count", type=int, default=2)
    parser.add_argument("--reliability-prior", type=float, default=5.0)
    parser.add_argument("--distance-gamma", type=float, default=0.5)
    parser.add_argument("--pmi-clip", type=float, default=8.0)
    parser.add_argument("--max-bytes", type=int, default=HARD_MAX_BYTES)
    args = parser.parse_args()
    try:
        result = compile_model(args.train_sequences, args.output, args.max_distance,
            args.min_count, args.reliability_prior, args.distance_gamma,
            args.pmi_clip, args.max_bytes, args.split)
        print(json.dumps({"relation_model": result}, ensure_ascii=False, indent=2))
    except (OSError, ValueError, sqlite3.Error) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
