#!/usr/bin/env python3
"""Deterministic, dev-only calibration of SURGE evidence-family multipliers."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

FAMILIES = ("decoder_prior", "lexical", "morphology", "transition", "ngram", "pos_ngram", "context_cluster", "typed_entity")
PRESETS = (
    ("baseline", {name: 1.0 for name in FAMILIES}, 1.0),
    ("lexical_morphology", {**{name: 1.0 for name in FAMILIES}, "decoder_prior": 0.5, "lexical": 1.5, "morphology": 1.25}, 0.5),
    ("ngram_context", {**{name: 1.0 for name in FAMILIES}, "decoder_prior": 0.5, "ngram": 1.5, "pos_ngram": 1.5, "context_cluster": 1.5}, 0.5),
    ("balanced_context", {**{name: 1.0 for name in FAMILIES}, "decoder_prior": 0.25, "lexical": 1.25, "ngram": 1.5, "pos_ngram": 1.25, "context_cluster": 1.5}, 0.25),
    ("renderer_conservative", {name: 1.0 for name in FAMILIES}, 0.0),
    ("typed_entity_disabled", {**{name: 1.0 for name in FAMILIES}, "typed_entity": 0.0}, 1.0),
    ("typed_entity_conservative", {**{name: 1.0 for name in FAMILIES}, "typed_entity": 0.5}, 1.0),
    ("typed_entity_strong", {**{name: 1.0 for name in FAMILIES}, "ngram": 1.5, "pos_ngram": 1.5, "context_cluster": 1.5, "typed_entity": 1.5}, 0.5),
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

def validate_diagnostic_metrics(metrics: dict) -> None:
    """Fail closed when evaluator diagnostic partitions are inconsistent."""
    candidate = metrics.get("candidate_generation")
    if candidate:
        r1 = candidate["surface_exact_recall_at_1"]
        r5 = candidate["surface_exact_recall_at_5"]
        r10 = candidate["surface_exact_recall_at_10"]
        if not 0.0 <= r1 <= r5 <= r10 <= 1.0:
            raise RuntimeError("candidate surface recall must be monotonic and within [0, 1]")
        if candidate["oracle_cer_at_10"] > candidate["oracle_cer_at_5"]:
            raise RuntimeError("candidate oracle CER must not increase from top-5 to top-10")
    classification = metrics.get("candidate_error_classification")
    if classification:
        cases = metrics["cases"]
        if classification["missing_candidate"] + classification["candidate_available"] != cases:
            raise RuntimeError("candidate error classes do not partition all cases")
        available = sum(classification[name] for name in (
            "candidate_available_runtime_failure", "candidate_available_deferred",
            "candidate_available_surge_exact", "candidate_available_surge_wrong"))
        if available != classification["candidate_available"]:
            raise RuntimeError("candidate-available outcomes do not form a complete partition")
        wrong = (classification["wrong_output_matches_raw_nbest"]
                 + classification["wrong_output_outside_raw_nbest"])
        if wrong != classification["candidate_available_surge_wrong"]:
            raise RuntimeError("wrong-output origin classes do not form a complete partition")
        if candidate and abs(classification["candidate_available"] / cases
                              - candidate["surface_exact_recall_at_10"]) > 1e-6:
            raise RuntimeError("candidate error partition disagrees with surface Recall@10")
    token = metrics.get("token_aligned_candidate_generation")
    if token:
        count = token["gold_tokens"]
        if count == 0:
            if any(token[key] is not None for key in ("recall_at_1", "recall_at_5", "recall_at_10")):
                raise RuntimeError("token recall must be null without trusted gold token boundaries")
        else:
            r1, r5, r10 = (token[key] for key in ("recall_at_1", "recall_at_5", "recall_at_10"))
            if not 0.0 <= r1 <= r5 <= r10 <= 1.0:
                raise RuntimeError("token-aligned recall must be monotonic and within [0, 1]")
            attribution = token["miss_attribution_at_10"]
            keys = ("exact_span_and_surface", "span_match_surface_mismatch",
                    "surface_match_span_mismatch", "no_surface_candidate")
            if sum(attribution[key] for key in keys) != count:
                raise RuntimeError("token miss attribution does not partition all gold tokens")
            if attribution["exact_span_and_surface"] != round(r10 * count):
                raise RuntimeError("token miss attribution disagrees with Recall@10")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evaluator", required=True)
    parser.add_argument("--pack", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--train-sequences", required=True)
    parser.add_argument("--source-lock", required=True)
    parser.add_argument("--max-rss-mib", type=int, default=1792)
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
        try:
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            metrics = json.loads(result.stdout)
        finally:
            candidate.unlink(missing_ok=True)
        if metrics.get("split") != "dev" or metrics.get("cases", 0) <= 0:
            raise RuntimeError("calibration requires a non-empty dev split")
        if metrics.get("surge_core", {}).get("runtime_failures", 0) != 0:
            raise RuntimeError("runtime failures make dev calibration invalid")
        validate_diagnostic_metrics(metrics)
        peak_rss = metrics.get("peak_rss_kib", -1)
        if peak_rss < 0 or peak_rss > args.max_rss_mib * 1024:
            raise RuntimeError("dev calibration peak RSS is missing or exceeds configured limit")
        rows.append({"name": name, "weights": weights, "normalization_renderer": renderer,
                     "cer": metrics["surge_core"]["cer"],
                     "sentence_exact": metrics["surge_core"]["sentence_exact"],
                     "overrides": metrics["surge_core"]["overrides"],
                     "deferred": metrics["surge_core"]["deferred"],
                     "runtime_failures": metrics["surge_core"]["runtime_failures"],
                     "decode_failures": metrics["surge_core"]["decode_failures"],
                     "typed_entities_found": metrics["surge_core"].get("typed_entities_found", 0),
                     "typed_entity_evidence_added": metrics["surge_core"].get("typed_entity_evidence_added", 0),
                     "selected_typed_entity_evidence": metrics["surge_core"].get("selected_typed_entity_evidence", 0),
                     "typed_entities_persisted": metrics["surge_core"].get("typed_entities_persisted", 0),
                     "elapsed_seconds": metrics["elapsed_seconds"],
                     "peak_rss_kib": metrics["peak_rss_kib"],
                     "candidate_generation": metrics.get("candidate_generation", {}),
                     "candidate_error_classification": metrics.get("candidate_error_classification", {}),
                     "token_aligned_candidate_generation": metrics.get("token_aligned_candidate_generation", {})})
    best_index = min(range(len(rows)), key=lambda i: (rows[i]["cer"], -rows[i]["sentence_exact"], i))
    best = rows[best_index]
    write_policy(policy_path, best["weights"], best["normalization_renderer"])
    report = {"mode": "surge_dev_policy_calibration", "split": "dev",
              "source_lock": str(Path(args.source_lock)),
              "source_lock_sha256": sha256_file(Path(args.source_lock)),
              "corpus_sha256": sha256_file(Path(args.corpus)),
              "train_sequences_sha256": sha256_file(Path(args.train_sequences)),
              "calibrator_sha256": sha256_file(Path(__file__)),
              "max_rss_mib": args.max_rss_mib,
        "selection": "minimum CER, then maximum sentence exact, then fixed preset order",
              "best_policy": best, "candidates": rows}
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
