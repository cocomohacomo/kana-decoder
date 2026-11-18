#!/usr/bin/env python3
"""Offline single-span candidate ablation for morphology, combined score and SURGE.

Only cases where a candidate span covers the entire reading can be aligned to the
case-level gold surface without an external token alignment. Multi-token cases
are counted as ineligible rather than guessed.
"""
import argparse
import json
import math
from pathlib import Path


def _best_observation(traces):
    observed = {}
    for item in traces:
        surface = item.get("surface")
        if not isinstance(surface, str) or not surface:
            continue
        local = float(item.get("local_score", 0.0))
        context = float(item.get("context_evidence_score", 0.0))
        morphology = float(item.get("morphology_score", 0.0))
        if not all(math.isfinite(value) for value in (local, context, morphology)):
            raise ValueError("candidate trace contains a non-finite score")
        current = observed.get(surface)
        if current is None or local > current["local"]:
            observed[surface] = {
                "local": local, "context": context, "morphology": morphology,
            }
    return observed


def _rank(observed, field):
    return sorted(observed, key=lambda surface: (-observed[surface][field], surface))


def _metric(correct, total):
    return correct / total if total else None


def analyze(payload, context_margin_threshold=0.5):
    if context_margin_threshold < 0:
        raise ValueError("context margin threshold must be non-negative")
    result = {
        "cases": len(payload.get("details", [])),
        "eligible_single_span_cases": 0,
        "candidate_covered_cases": 0,
        "candidate_coverage": None,
        "morphology_top1_correct": 0,
        "combined_local_top1_correct": 0,
        "surge_resolved_cases": 0,
        "surge_correct_resolutions": 0,
        "surge_unresolved_cases": 0,
        "morphology_top1_accuracy": None,
        "combined_local_top1_accuracy": None,
        "surge_resolution_precision": None,
        "surge_resolution_recall_over_covered": None,
        "context_margin_threshold": context_margin_threshold,
        "alignment_scope": "single candidate span covering the complete reading only",
    }
    for case in payload.get("details", []):
        reading = case.get("reading", "")
        expected = case.get("expected", "")
        codepoints = len(reading)
        spans = {}
        for position in case.get("candidate_generation", []):
            for match in position.get("matches", []):
                if match.get("start") == 0 and match.get("end") == codepoints:
                    spans.setdefault((0, codepoints), set()).update(
                        candidate.get("surface") for candidate in match.get("candidates", [])
                        if isinstance(candidate.get("surface"), str))
        if not spans:
            continue
        candidates = spans[(0, codepoints)]
        observed = _best_observation([
            item for item in case.get("candidate_trace", [])
            if item.get("start") == 0 and item.get("end") == codepoints
        ])
        candidates.intersection_update(observed)
        if not candidates:
            continue
        result["eligible_single_span_cases"] += 1
        if expected not in candidates:
            continue
        result["candidate_covered_cases"] += 1
        scored = {surface: observed[surface] for surface in candidates}
        morphology_rank = _rank(scored, "morphology")
        local_rank = _rank(scored, "local")
        if morphology_rank[0] == expected:
            result["morphology_top1_correct"] += 1
        if local_rank[0] == expected:
            result["combined_local_top1_correct"] += 1
        context_rank = _rank(scored, "context")
        evidence = [scored[surface]["context"] for surface in candidates
                    if scored[surface]["context"] != 0.0]
        has_evidence = len(evidence) >= 2 and max(evidence) - min(evidence) > 1e-9
        margin = (scored[context_rank[0]]["context"] - scored[context_rank[1]]["context"]
                  if len(context_rank) >= 2 else None)
        if has_evidence and margin is not None and margin >= context_margin_threshold:
            result["surge_resolved_cases"] += 1
            if context_rank[0] == expected:
                result["surge_correct_resolutions"] += 1
        else:
            result["surge_unresolved_cases"] += 1
    covered = result["candidate_covered_cases"]
    eligible = result["eligible_single_span_cases"]
    result["candidate_coverage"] = _metric(covered, eligible)
    result["morphology_top1_accuracy"] = _metric(result["morphology_top1_correct"], covered)
    result["combined_local_top1_accuracy"] = _metric(result["combined_local_top1_correct"], covered)
    resolved = result["surge_resolved_cases"]
    result["surge_resolution_precision"] = _metric(result["surge_correct_resolutions"], resolved)
    result["surge_resolution_recall_over_covered"] = _metric(
        result["surge_correct_resolutions"], covered)
    result["end_to_end_decoder"] = payload.get("overall", {})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evaluation_json", type=Path)
    parser.add_argument("--context-margin", type=float, default=0.5)
    args = parser.parse_args()
    try:
        payload = json.loads(args.evaluation_json.read_text(encoding="utf-8"))
        print(json.dumps({"ablation": analyze(payload, args.context_margin)},
                         ensure_ascii=False, indent=2))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
