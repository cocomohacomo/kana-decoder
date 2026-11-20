#!/usr/bin/env python3
"""Run paired rendered-surface-diversity vs legacy beam-pruning evaluation.

Input is the TSV emitted by prepare_ud_gsd_corpus.py. Only one explicit split
is evaluated. Covered contiguous token runs are joined by sentence_id, so
unknown/OOV tokens never create fabricated adjacency.
"""
import argparse
import csv
import json
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


def paired_bootstrap(new_cases, old_cases, samples=2000, seed=20261121):
    if samples < 1:
        raise ValueError("bootstrap samples must be positive")
    count = len(new_cases)
    if count == 0:
        raise ValueError("cannot bootstrap an empty evaluation")
    rng = random.Random(seed)
    exact_deltas = []
    cer_deltas = []
    for _ in range(samples):
        indices = [rng.randrange(count) for _ in range(count)]
        new_exact = old_exact = 0
        new_edits = old_edits = reference_chars = 0
        for index in indices:
            newer, older = new_cases[index], old_cases[index]
            expected = newer["expected"]
            reference_length = len(expected)
            reference_chars += reference_length
            new_edits += newer["edit_distance"]
            old_edits += older["edit_distance"]
            new_exact += newer["predicted"] == expected
            old_exact += older["predicted"] == expected
        exact_deltas.append((new_exact - old_exact) / count)
        if reference_chars:
            cer_deltas.append((new_edits - old_edits) / reference_chars)
        else:
            cer_deltas.append(0.0)

    def interval(values):
        ordered = sorted(values)
        lower = ordered[int(0.025 * (len(ordered) - 1))]
        upper = ordered[int(0.975 * (len(ordered) - 1))]
        return [lower, upper]

    return {
        "method": "paired percentile bootstrap over evaluation cases",
        "samples": samples,
        "seed": seed,
        "confidence_level": 0.95,
        "exact_accuracy_delta_ci95": interval(exact_deltas),
        "cer_delta_ci95": interval(cer_deltas),
        "interpretation": "Intervals are descriptive uncertainty estimates for this corpus split; they do not establish causality or generalization to other domains."
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evaluator", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--prepared-corpus", type=Path, required=True)
    parser.add_argument("--split", choices=("train", "dev", "test"), default="test")
    parser.add_argument("--output", type=Path)
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
        report = compare(diversity, legacy, rows, args.split)
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
