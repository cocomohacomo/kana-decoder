#!/usr/bin/env python3
"""Run paired rendered-surface-diversity vs legacy beam-pruning evaluation.

Input is the TSV emitted by prepare_ud_gsd_corpus.py. Only one explicit split
is evaluated. Covered contiguous token runs are joined by sentence_id, so
unknown/OOV tokens never create fabricated adjacency.
"""
import argparse
import csv
import hashlib
import json
import os
import platform
import random
import subprocess
import sys
import tempfile
from pathlib import Path


def build_eval_rows(source, split):
    grouped = {}
    with source.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        required = {"document_id", "sentence_id", "reading", "surface", "domain", "split"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError("prepared corpus is missing required columns: " +
                             ", ".join(sorted(required)))
        for line_number, row in enumerate(reader, 2):
            if row["split"] != split:
                continue
            key = (row["document_id"], row["sentence_id"], row["domain"])
            reading, surface = row["reading"], row["surface"]
            if not reading or not surface:
                raise ValueError(f"empty reading/surface at prepared corpus line {line_number}")
            if key not in grouped:
                grouped[key] = [[], []]
            grouped[key][0].append(reading)
            grouped[key][1].append(surface)
    if not grouped:
        raise ValueError(f"no rows found for split {split!r}")
    return [
        (key[0], key[1], "".join(values[0]), "".join(values[1]), key[2])
        for key, values in grouped.items()
    ]


def _run(evaluator, pack, corpus, legacy, runner=subprocess.run):
    command = [str(evaluator), str(pack), str(corpus), "--details"]
    if legacy:
        command.append("--legacy-beam-pruning")
    completed = runner(command, text=True, capture_output=True, check=False)
    if completed.returncode:
        raise RuntimeError(
            f"evaluator failed ({completed.returncode}): {completed.stderr.strip()}")
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise ValueError(f"evaluator returned invalid JSON: {exc}") from exc
    if not isinstance(payload.get("overall"), dict) or not isinstance(payload.get("details"), list):
        raise ValueError("evaluator JSON is missing overall metrics or case details")
    if payload["overall"].get("cases") != len(payload["details"]):
        raise ValueError("evaluator case count does not match detail rows")
    return payload


def _delta(new, old, key):
    lhs, rhs = new.get(key), old.get(key)
    if isinstance(lhs, (int, float)) and isinstance(rhs, (int, float)):
        return lhs - rhs
    return None


def paired_bootstrap(new_cases, old_cases, samples=2000, seed=20261121,
                     source_rows=None):
    if samples < 1:
        raise ValueError("bootstrap samples must be positive")
    count = len(new_cases)
    if count == 0:
        raise ValueError("cannot bootstrap an empty evaluation")
    if len(old_cases) != count:
        raise ValueError("paired bootstrap inputs have different case counts")
    if source_rows is not None and len(source_rows) != count:
        raise ValueError("bootstrap source rows do not align with evaluator cases")

    # Documents, not sentences, are the independent resampling units. All
    # covered segments from a document stay together in each bootstrap draw.
    if source_rows is None:
        clusters = {str(index): [index] for index in range(count)}
        method = "paired percentile bootstrap over cases"
        resampling_unit = "case"
    else:
        clusters = {}
        for index, row in enumerate(source_rows):
            document_id = row[0]
            clusters.setdefault(document_id, []).append(index)
        method = "paired percentile cluster bootstrap over documents"
        resampling_unit = "document"

    cluster_indices = list(clusters.values())
    rng = random.Random(seed)
    exact_deltas = []
    cer_deltas = []
    for _ in range(samples):
        sampled = [rng.randrange(len(cluster_indices))
                   for _ in range(len(cluster_indices))]
        indices = [case_index for cluster_index in sampled
                   for case_index in cluster_indices[cluster_index]]
        new_exact = old_exact = 0
        new_edits = old_edits = reference_chars = 0
        for index in indices:
            newer, older = new_cases[index], old_cases[index]
            expected = newer["expected"]
            if older.get("expected") != expected:
                raise ValueError("paired bootstrap references differ")
            reference_length = len(expected)
            reference_chars += reference_length
            new_distance = newer.get("edit_distance")
            old_distance = older.get("edit_distance")
            if (not isinstance(new_distance, int) or new_distance < 0 or
                    not isinstance(old_distance, int) or old_distance < 0):
                raise ValueError("paired bootstrap requires non-negative integer edit distances")
            new_edits += new_distance
            old_edits += old_distance
            new_exact += newer["predicted"] == expected
            old_exact += older["predicted"] == expected
        exact_deltas.append((new_exact - old_exact) / len(indices))
        cer_deltas.append(
            (new_edits - old_edits) / reference_chars if reference_chars else 0.0)

    def interval(values):
        ordered = sorted(values)
        lower = ordered[int(0.025 * (len(ordered) - 1))]
        upper = ordered[int(0.975 * (len(ordered) - 1))]
        return [lower, upper]

    return {
        "method": method,
        "resampling_unit": resampling_unit,
        "warning": ("Fewer than two independent resampling units; interval "
                    "is not informative." if len(cluster_indices) < 2 else None),
        "cluster_count": len(cluster_indices),
        "samples": samples,
        "seed": seed,
        "confidence_level": 0.95,
        "exact_accuracy_delta_ci95": interval(exact_deltas),
        "cer_delta_ci95": interval(cer_deltas),
        "interpretation": (
            "Intervals resample whole documents and quantify descriptive uncertainty "
            "within this corpus split; they do not establish causality or "
            "generalization to other domains."
        )
    }

def compare(new, old, rows, split, bootstrap_samples=2000, bootstrap_seed=20261121):
    new_cases = new["details"]
    old_cases = old["details"]
    if len(new_cases) != len(old_cases) or len(rows) != len(new_cases):
        raise ValueError("A/B runs did not evaluate the same number of cases")
    if [(x["reading"], x["expected"], x["domain"]) for x in new_cases] != [
            (x["reading"], x["expected"], x["domain"]) for x in old_cases]:
        raise ValueError("A/B evaluator case order or inputs differ")

    changed = []
    exact_gains = exact_regressions = both_correct = both_wrong = 0
    for source_row, candidate, baseline in zip(rows, new_cases, old_cases):
        if (candidate["reading"], candidate["expected"], candidate["domain"]) != (
                source_row[2], source_row[3], source_row[4]):
            raise ValueError("prepared corpus rows do not match evaluator case details")
        candidate_correct = candidate["predicted"] == candidate["expected"]
        baseline_correct = baseline["predicted"] == baseline["expected"]
        both_correct += candidate_correct and baseline_correct
        both_wrong += not candidate_correct and not baseline_correct
        exact_gains += candidate_correct and not baseline_correct
        exact_regressions += baseline_correct and not candidate_correct
        if candidate["predicted"] != baseline["predicted"]:
            changed.append({
                "document_id": source_row[0],
                "sentence_id": source_row[1],
                "reading": candidate["reading"],
                "expected": candidate["expected"],
                "legacy_prediction": baseline["predicted"],
                "diversity_prediction": candidate["predicted"],
                "legacy_correct": baseline_correct,
                "diversity_correct": candidate_correct,
            })

    domain_deltas = {}
    for domain in sorted(new.get("domains", {})):
        newer, older = new["domains"][domain], old.get("domains", {}).get(domain, {})
        domain_deltas[domain] = {
            "cases": newer.get("cases", 0),
            "exact_accuracy_delta": _delta(newer, older, "exact_accuracy"),
            "cer_delta": _delta(newer, older, "cer"),
            "decode_failure_delta": _delta(newer, older, "decode_failures"),
        }
    return {
        "split": split,
        "case_count": len(rows),
        "paired_bootstrap": paired_bootstrap(
            new_cases, old_cases, samples=bootstrap_samples, seed=bootstrap_seed,\n            source_rows=rows),
        "comparison": {
            "legacy_beam_pruning": old["overall"],
            "rendered_surface_diversity": new["overall"],
            "delta_diversity_minus_legacy": {
                "exact_accuracy": _delta(new["overall"], old["overall"], "exact_accuracy"),
                "cer": _delta(new["overall"], old["overall"], "cer"),
                "decode_failures": _delta(new["overall"], old["overall"], "decode_failures"),
            },
            "exact_gains": exact_gains,
            "exact_regressions": exact_regressions,
            "both_correct": both_correct,
            "both_wrong": both_wrong,
            "changed_output_cases": len(changed),
        },
        "by_domain": domain_deltas,
        "changed_cases": changed,
        "interpretation": (
            "Negative CER delta and positive exact-accuracy delta favor rendered-surface "
            "diversity. This is a paired evaluation, not a statistical significance test."
        ),
    }


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fingerprint_path(path):
    """Hash a file or a deterministic recursive file manifest."""
    path = path.resolve()
    if not path.exists():
        raise FileNotFoundError(f"input path does not exist: {path}")
    if path.is_file():
        return {
            "kind": "file",
            "name": path.name,
            "size_bytes": path.stat().st_size,
            "sha256": sha256_file(path),
            "file_count": 1,
        }
    entries = []
    for child in sorted(p for p in path.rglob("*") if p.is_file()):
        if child.is_symlink():
            raise ValueError(f"symlink is not allowed in fingerprinted input: {child}")
        relative = child.relative_to(path).as_posix()
        entries.append({
            "path": relative,
            "size_bytes": child.stat().st_size,
            "sha256": sha256_file(child),
        })
    manifest = json.dumps(entries, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":")).encode("utf-8")
    return {
        "kind": "directory",
        "name": path.name,
        "file_count": len(entries),
        "total_size_bytes": sum(item["size_bytes"] for item in entries),
        "manifest_sha256": hashlib.sha256(manifest).hexdigest(),
    }


def source_revision():
    if os.environ.get("GITHUB_SHA"):
        return os.environ["GITHUB_SHA"]
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"], text=True, capture_output=True,
            check=False)
        if result.returncode == 0:
            return result.stdout.strip()
    except OSError:
        pass
    return None


def sanitized_command(command):
    path_options = {"--evaluator", "--pack", "--prepared-corpus", "--output"}
    result = []
    redact_next = False
    for argument in command:
        if redact_next:
            result.append(Path(argument).name)
            redact_next = False
            continue
        result.append(argument)
        if argument in path_options:
            redact_next = True
        else:
            for option in path_options:
                prefix = option + "="
                if argument.startswith(prefix):
                    result[-1] = prefix + Path(argument[len(prefix):]).name
                    break
    return result


def build_provenance(evaluator, pack, prepared_corpus, split, command,
                     bootstrap_samples, bootstrap_seed):
    return {
        "schema_version": 1,
        "source_revision": source_revision(),
        "python_version": platform.python_version(),
        "platform": platform.platform(),
        "command": sanitized_command(command),
        "split": split,
        "bootstrap": {
            "method": "paired percentile bootstrap over evaluation cases",
            "samples": bootstrap_samples,
            "seed": bootstrap_seed,
            "confidence_level": 0.95,
        },
        "inputs": {
            "evaluator": fingerprint_path(evaluator),
            "pack": fingerprint_path(pack),
            "prepared_corpus": fingerprint_path(prepared_corpus),
        },
        "reproducibility_note": (
            "Input fingerprints identify exact file contents. Local path arguments "
            "are reduced to basenames. Reproduction also requires a compatible "
            "runtime and the recorded source revision."
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evaluator", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--prepared-corpus", type=Path, required=True)
    parser.add_argument("--split", choices=("train", "dev", "test"), default="test")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--bootstrap-samples", type=int, default=2000)
    parser.add_argument("--bootstrap-seed", type=int, default=20261121)
    args = parser.parse_args()
    try:
        rows = build_eval_rows(args.prepared_corpus, args.split)
        with tempfile.TemporaryDirectory(prefix="kana-beam-ab-") as tmp:
            corpus = Path(tmp) / "evaluation.tsv"
            with corpus.open("w", encoding="utf-8", newline="") as stream:
                for _, _, reading, surface, domain in rows:
                    stream.write(f"{reading}\t{surface}\t{domain}\n")
            diversity = _run(args.evaluator, args.pack, corpus, False)
            legacy = _run(args.evaluator, args.pack, corpus, True)
        report = compare(diversity, legacy, rows, args.split,
                         bootstrap_samples=args.bootstrap_samples,
                         bootstrap_seed=args.bootstrap_seed)
        report["provenance"] = build_provenance(
            args.evaluator, args.pack, args.prepared_corpus, args.split,
            sys.argv, args.bootstrap_samples, args.bootstrap_seed)
        rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        else:
            sys.stdout.write(rendered)
    except (OSError, ValueError, RuntimeError, csv.Error) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
