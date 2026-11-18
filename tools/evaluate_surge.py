#!/usr/bin/env python3
"""Offline, non-mutating SURGE-style ambiguity/attention probe for decoder traces."""
import argparse
import json
from pathlib import Path


def analyze(payload):
    cases = payload.get("details", [])
    span_clusters = 0
    claims = 0
    questions = 0
    gold_covered = 0
    focus_questions = []
    for case in cases:
        spans = {}
        for position in case.get("candidate_generation", []):
            for match in position.get("matches", []):
                key = (match["start"], match["end"])
                surfaces = spans.setdefault(key, set())
                surfaces.update(c["surface"] for c in match.get("candidates", []))
        span_clusters += len(spans)
        case_claims = sum(len(values) for values in spans.values())
        claims += case_claims
        expected = case.get("expected", "")
        if any(expected in values for values in spans.values()):
            gold_covered += 1
        case_questions = []
        score_by_span = {}
        for trace in case.get("candidate_trace", []):
            key = (trace["start"], trace["end"])
            score_by_span.setdefault(key, {})[trace["surface"]] = max(
                trace["local_score"],
                score_by_span.setdefault(key, {}).get(trace["surface"], float("-inf")))
        for (start, end), values in spans.items():
            if len(values) < 2:
                continue
            questions += 1
            ranked = sorted(score_by_span.get((start, end), {}).items(),
                            key=lambda item: item[1], reverse=True)
            margin = ranked[0][1] - ranked[1][1] if len(ranked) > 1 else None
            case_questions.append({
                "span": [start, end],
                "claim_count": len(values),
                "score_margin": margin,
                "resolved": False,
            })
        if case_questions:
            # The least-separated alternative set receives attention first.
            focus = min(case_questions,
                        key=lambda q: (q["score_margin"] if q["score_margin"] is not None else 0,
                                       -q["claim_count"]))
            focus_questions.append({
                "reading": case.get("reading", ""),
                "focus_span": focus["span"],
                "question_count": len(case_questions),
                "claim_count": focus["claim_count"],
                "score_margin": focus["score_margin"],
                "resolved": False,
            })
    count = len(cases)
    return {
        "cases": count,
        "span_clusters": span_clusters,
        "claims": claims,
        "contradiction_questions": questions,
        "cases_with_focus": len(focus_questions),
        "gold_candidate_coverage": gold_covered / count if count else 0.0,
        "gold_candidate_coverage_count": gold_covered,
        "baseline_top1_exact_accuracy": payload.get("overall", {}).get("exact_accuracy", 0.0),
        "auto_resolved_questions": 0,
        "focus_questions": focus_questions,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("evaluation_json", type=Path)
    args = parser.parse_args()
    payload = json.loads(args.evaluation_json.read_text(encoding="utf-8"))
    print(json.dumps({"surge_probe": analyze(payload)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
