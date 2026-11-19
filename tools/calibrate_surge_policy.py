#!/usr/bin/env python3
"""Deterministic, dev-only calibration of SURGE evidence-family multipliers."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

FAMILIES = ("decoder_prior", "lexical", "morphology", "transition", "ngram", "pos_ngram", "context_cluster")
PRESETS = (
    ("baseline", {name: 1.0 for name in FAMILIES}, 1.0),
    ("lexical_morphology", {**{name: 1.0 for name in FAMILIES}, "decoder_prior": 0.5, "lexical": 1.5, "morphology": 1.25}, 0.5),
    ("ngram_context", {**{name: 1.0 for name in FAMILIES}, "decoder_prior": 0.5, "ngram": 1.5, "pos_ngram": 1.5, "context_cluster": 1.5}, 0.5),
    ("balanced_context", {**{name: 1.0 for name in FAMILIES}, "decoder_prior": 0.25, "lexical": 1.25, "ngram": 1.5, "pos_ngram": 1.25, "context_cluster": 1.5}, 0.25),
    ("renderer_conservative", {name: 1.0 for name in FAMILIES}, 0.0),
)

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

def write_policy(path: Path, weights: dict[str, float], renderer: float) -> None:
    lines = [f"{name}\t{weights[name]:.8g}" for name in FAMILIES]
    lines.append(f"normalization_renderer\t{renderer:.8g}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evaluator", required=True)
    parser.add_argument("--pack", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--train-sequences", required=True)
    parser.add_argument("--source-lock", required=True)
    parser.add_argument("--output-policy", required=True)
    parser.add_argument("--output-report", required=True)
    args = parser.parse_args()
    policy_path = Path(args.output_policy)
    policy_path.parent.mkdir(parents=True, exist_ok=True)
    report_path = Path(args.output_report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for index, (name, weights, renderer) in enumerate(PRESETS):
        candidate = policy_path.with_name(f".{policy_path.name}.{index}.tmp")
        write_policy(candidate, weights, renderer)
        command = [args.evaluator, args.pack, args.corpus, "dev", args.train_sequences, str(candidate)]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        metrics = json.loads(result.stdout)
        if metrics.get("split") != "dev" or metrics.get("cases", 0) <= 0:
            raise RuntimeError("calibration requires a non-empty dev split")
        if metrics.get("surge_core", {}).get("runtime_failures", 0) != 0:
            raise RuntimeError("runtime failures make dev calibration invalid")
        rows.append({"name": name, "weights": weights, "normalization_renderer": renderer,
                     "cer": metrics["surge_core"]["cer"],
                     "sentence_exact": metrics["surge_core"]["sentence_exact"],
                     "overrides": metrics["surge_core"]["overrides"],
                     "deferred": metrics["surge_core"]["deferred"],
                     "runtime_failures": metrics["surge_core"]["runtime_failures"],
                     "decode_failures": metrics["surge_core"]["decode_failures"],
                     "elapsed_seconds": metrics["elapsed_seconds"],
                     "peak_rss_kib": metrics["peak_rss_kib"]})
        candidate.unlink(missing_ok=True)
    best_index = min(range(len(rows)), key=lambda i: (rows[i]["cer"], -rows[i]["sentence_exact"], i))
    best = rows[best_index]
    write_policy(policy_path, best["weights"], best["normalization_renderer"])
    report = {"mode": "surge_dev_policy_calibration", "split": "dev",
              "source_lock": str(Path(args.source_lock)),
              "source_lock_sha256": sha256_file(Path(args.source_lock)),
              "corpus_sha256": sha256_file(Path(args.corpus)),
              "train_sequences_sha256": sha256_file(Path(args.train_sequences)),
              "calibrator_sha256": sha256_file(Path(__file__)),
              "selection": "minimum CER, then maximum sentence exact, then fixed preset order",
              "best_policy": best, "candidates": rows}
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
