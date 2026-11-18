#!/usr/bin/env python3
"""Offline SURGE-inspired claim/question/focus analysis over decoder traces.

Only scorer context evidence may resolve a question. Morphology/word costs are
reported separately and never treated as semantic/context evidence.
"""
import argparse
import json
from pathlib import Path


def analyze(payload, context_margin_threshold=0.5):
    cases = payload.get("details", [])
    span_clusters = claims = questions = gold_covered = 0
    resolved_questions = 0
    focus_questions = []
    context_evidence_spans = 0
    for case in cases:
        spans = {}
        for position in case.get("candidate_generation", []):
            for match in position.get("matches", []):
                key = (match["start"], match["end"])
                spans.setdefault(key, set()).update(
                    c["surface"] for c in match.get("candidates", []))
        span_clusters += len(spans)
        claims += sum(len(values) for values in spans.values())
        expected = case.get("expected", "")
        if any(expected in values for values in spans.values()):
            gold_covered += 1

        # Preserve best observation per surface, but keep scorer context and
        # morphology/local scores in separate channels.
        observed = {}
        for trace in case.get("candidate_trace", []):
            key = (trace["start"], trace["end"])
            item = observed.setdefault(key, {})
            surface = trace["surface"]
            current = item.setdefault(surface, {
                "context": float("-inf"), "local": float("-inf"),
                "morphology": float("-inf"), "observations": 0})
            local_score = float(trace.get("local_score", 0.0))
            if local_score > current["local"]:
                # Keep context evidence from the same best local-path
                # observation; never compare maxima from incompatible beams.
                current["local"] = local_score
                current["context"] = float(trace.get("context_evidence_score", 0.0))
                current["morphology"] = float(trace.get("morphology_score", 0.0))
            current["observations"] += 1

        case_questions = []
        for (start, end), values in spans.items():
            if len(values) < 2:
                continue
            questions += 1
            scores = observed.get((start, end), {})
            ranked_context = sorted(
                ((surface, score["context"]) for surface, score in scores.items()
                 if surface in values),
                key=lambda item: item[1], reverse=True)
            # Zero is the empty-model score. Require actual non-zero scorer
            # evidence for at least two competing claims before resolution.
            evidence = [(surface, score) for surface, score in ranked_context
                        if score != 0.0]
            evidence_values = [score for _, score in evidence]
            has_context_evidence = (len(evidence_values) >= 2
                                    and max(evidence_values) - min(evidence_values) > 1e-9)
            margin = (ranked_context[0][1] - ranked_context[1][1]
                      if len(ranked_context) >= 2 else None)
            resolved = bool(has_context_evidence and margin is not None
                            and margin >= context_margin_threshold)
            if has_context_evidence:
                context_evidence_spans += 1
            if resolved:
                resolved_questions += 1
            case_questions.append({
                "span": [start, end],
                "claim_count": len(values),
                "context_evidence_claims": len(evidence),
                "context_score_margin": margin,
                "morphology_only_margin": _local_margin(scores, values),
                "resolved": resolved,
                "resolution_basis": "scorer_context" if resolved else "insufficient_context",
                "selected_surface": ranked_context[0][0] if resolved else None,
            })

        unresolved = [q for q in case_questions if not q["resolved"]]
        if unresolved:
            focus = min(unresolved, key=lambda q: (
                q["context_score_margin"] if q["context_score_margin"] is not None else float("-inf"),
                -q["claim_count"]))
            focus_questions.append({
                "reading": case.get("reading", ""),
                "focus_span": focus["span"],
                "question_count": len(case_questions),
                "unresolved_question_count": len(unresolved),
                "claim_count": focus["claim_count"],
                "context_score_margin": focus["context_score_margin"],
                "resolved": False,
            })
    count = len(cases)
    return {
        "cases": count,
        "span_clusters": span_clusters,
        "claims": claims,
        "contradiction_questions": questions,
        "context_evidence_spans": context_evidence_spans,
        "resolved_questions": resolved_questions,
        "unresolved_questions": questions - resolved_questions,
        "cases_with_focus": len(focus_questions),
        "gold_candidate_coverage": gold_covered / count if count else 0.0,
        "gold_candidate_coverage_count": gold_covered,
        "baseline_top1_exact_accuracy": payload.get("overall", {}).get("exact_accuracy", 0.0),
        "auto_resolved_questions": 0,
        "context_margin_threshold": context_margin_threshold,
        "focus_questions": focus_questions,
    }


def _local_margin(scores, values):
    ranked = sorted((score["local"] for surface, score in scores.items()
                     if surface in values), reverse=True)
    return ranked[0] - ranked[1] if len(ranked) >= 2 else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("evaluation_json", type=Path)
    parser.add_argument("--context-margin", type=float, default=0.5)
    args = parser.parse_args()
    if args.context_margin < 0:
        parser.error("--context-margin must be non-negative")
    payload = json.loads(args.evaluation_json.read_text(encoding="utf-8"))
    print(json.dumps({"surge_probe": analyze(payload, args.context_margin)},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
