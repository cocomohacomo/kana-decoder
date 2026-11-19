#!/usr/bin/env python3
"""Deterministic, dev-only calibration of SURGE evidence-family multipliers.

The held-out test split is never read by this script. It runs a small fixed
policy grid, minimizes CER, then maximizes sentence exact, then keeps the
earlier preset for deterministic ties.
"""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path

FAMILIES = (
    "decoder_prior", "lexical", "morphology", "transition",
    "ngram", "pos_ngram", "context_cluster",
)

PRESETS = (
    ("baseline", {family: 1.0 for family in FAMILIES}, 1.0),
    ("lexical_morphology", {
        **{family: 1.0 for family in FAMILIES},
        "decoder_prior": 0.5, "lexical": 1.5, "morphology": 1.25,
    }, 0.5),
    ("ngram_context", {
        **{family: 1.0 for family in FAMILIES},
        "decoder_prior": 0.5, "ngram": 1.5, "pos_ngram": 1.5,
        "context_cluster": 1.5,
    }, 0.5),
    ("balanced_context", {
        **{family: 1.0 for family in FAMILIES},
        "decoder_prior": 0.25, "lexical": 1.25, "ngram": 1.5,
        "pos_ngram": 1.25, "context_cluster": 1.5,
    }, 0.25),
    ("renderer_conservative", {family: 1.0 for family in FAMILIES}, 0.0),
)


def write_policy(path: Path, weights: dict[str, float], renderer: float) -> None:
    lines = [f"{family}\t{weights[family]:.8g}" for family in FAMILIES]
    lines.append(f"normalization_renderer\t{renderer:.8g}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evaluator", required=True)
    parser.add_argument("--pack", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--train-sequences", required=True)
    parser.add_argument("--output-policy", required=True)
    parser.add_argument("--output-report", required=True)
    args = parser.parse_args()

    output_policy = Path(args.output_policy)
    output_policy.parent.mkdir(parents=True, exist_ok=True)
    report_path = Path(args.output_report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, (name, weights, renderer) in enumerate(PRESETS):
        candidate_policy = output_policy.with_name(f".{output_policy.name}.{index}.tmp")
        write_policy(candidate_policy, weights, renderer)
        command = [
            args.evaluator, args.pack, args.corpus, "dev",
            args.train_sequences, str(candidate_policy),
        ]
        completed = subprocess.run(command, check=True, capture_output=True, text=True)
        metrics = json.loads(completed.stdout)
        rows.append({
            "name": name,
            "weights": weights,
            "normalization_renderer": renderer,
            "cer": metrics["surge_core"]["cer"],
            "sentence_exact": metrics["surge_core"]["sentence_exact"],
            "overrides": metrics["surge_core"]["overrides"],
            "deferred": metrics["surge_core"]["deferred"],
            "runtime_failures": metrics["surge_core"]["runtime_failures"],
        })
        candidate_policy.unlink(missing_ok=True)

    best_index = min(
        range(len(rows)),
        key=lambda i: (rows[i]["cer"], -rows[i]["sentence_exact"], i),
    )
    best = rows[best_index]
    write_policy(output_policy, best["weights"], best["normalization_renderer"])
    report = {
        "mode": "surge_dev_policy_calibration",
        "split": "dev",
        "selection": "minimum CER, then maximum sentence exact, then fixed preset order",
        "best_policy": best,
        "candidates": rows,
    }
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
