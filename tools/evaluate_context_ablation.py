#!/usr/bin/env python3
"""Compare token-local oracle context with causal, predicted-history evaluation.

The causal evaluator uses gold token boundaries/readings but never gold neighboring
candidate IDs. Each policy maintains its own predicted left history. This is not
full end-to-end decoding because segmentation remains gold-aligned.
"""
import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path


def read_lexicon(path):
    by_reading, by_pair = defaultdict(list), {}
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\r\n").split("\t")
            if len(cols) < 6:
                raise ValueError(f"{path}:{line_no}: expected at least six columns")
            try:
                item = {
                    "reading": cols[0], "surface": cols[1], "candidate_id": cols[5],
                    "word_cost": int(cols[6]) if len(cols) > 6 and cols[6] else 0,
                    "connection_cost": int(cols[7]) if len(cols) > 7 and cols[7] else 0,
                }
            except ValueError as exc:
                raise ValueError(f"{path}:{line_no}: invalid lexicon cost") from exc
            if not item["reading"] or not item["surface"] or not item["candidate_id"]:
                raise ValueError(f"{path}:{line_no}: empty reading, surface, or candidate ID")
            by_reading[item["reading"]].append(item)
            key = (item["reading"], item["surface"])
            old = by_pair.get(key)
            if old is None or (item["word_cost"], item["candidate_id"]) < (
                    old["word_cost"], old["candidate_id"]):
                by_pair[key] = item
    for reading in by_reading:
        by_reading[reading].sort(key=lambda x: (
            x["word_cost"], x["candidate_id"], x["surface"]))
    return by_reading, by_pair


def train_counts(path):
    unigram, left, right = Counter(), Counter(), Counter()
    left_total, right_total = Counter(), Counter()
    total = 0
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            tokens = [x for x in line.rstrip("\r\n").split("\t") if x]
            for i, token in enumerate(tokens):
                unigram[token] += 1
                total += 1
                if i:
                    left[(tokens[i - 1], token)] += 1
                    left_total[tokens[i - 1]] += 1
                if i + 1 < len(tokens):
                    right[(token, tokens[i + 1])] += 1
                    right_total[tokens[i + 1]] += 1
    return {"unigram": unigram, "left": left, "right": right,
            "left_total": left_total, "right_total": right_total,
            "unigram_total": total}


def _log_probability(count, total, vocab, alpha=0.5):
    return math.log((count + alpha) / (total + alpha * vocab))


def score_parts(candidate_id, left_id, right_id, counts):
    vocab = max(1, len(counts["unigram"]))
    uc = counts["unigram"][candidate_id]
    prior = _log_probability(uc, counts["unigram_total"], vocab)
    left_count = counts["left"][(left_id, candidate_id)] if left_id else 0
    right_count = counts["right"][(candidate_id, right_id)] if right_id else 0
    context = 0.0
    if left_id:
        context += _log_probability(
            left_count, counts["left_total"][left_id], vocab)
    if right_id:
        context += _log_probability(
            right_count, counts["right_total"][right_id], vocab)
    return prior, context, left_count + right_count


def morphology_score(candidate):
    return -float(candidate["word_cost"] + candidate["connection_cost"])


def best_per_surface(candidates, score_by_id):
    best = {}
    for candidate in candidates:
        candidate_id = candidate["candidate_id"]
        if candidate_id not in score_by_id:
            continue
        surface = candidate["surface"]
        score = score_by_id[candidate_id]
        current = best.get(surface)
        if current is None or score > current[0]:
            best[surface] = (score, candidate)
    return best


def ranked_surfaces(best):
    return sorted(best, key=lambda surface: (-best[surface][0], surface))


def read_test_sentences(corpus_path, by_pair):
    sentences, source_tokens = defaultdict(list), 0
    with corpus_path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        required = {"document_id", "sentence_id", "reading", "surface", "domain", "split"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError("corpus header is missing required columns")
        for line_no, row in enumerate(reader, 2):
            if row["split"] != "test":
                continue
            source_tokens += 1
            gold = by_pair.get((row["reading"], row["surface"]))
            if gold is None:
                raise ValueError(f"{corpus_path}:{line_no}: test token absent from lexicon")
            sentences[(row["document_id"], row["sentence_id"])].append(
                (row["reading"], row["surface"], gold))
    return list(sentences.values()), source_tokens


def evaluate_oracle(sentences, by_reading, counts, max_candidates, margin):
    totals = Counter()
    for sentence in sentences:
        for index, (reading, expected, _) in enumerate(sentence):
            candidates = by_reading.get(reading, [])[:max_candidates]
            if not candidates:
                totals["uncovered"] += 1
                continue
            totals["eligible"] += 1
            if expected not in {c["surface"] for c in candidates}:
                totals["candidate_misses"] += 1
                continue
            totals["covered"] += 1
            left_id = sentence[index - 1][2]["candidate_id"] if index else None
            right_id = sentence[index + 1][2]["candidate_id"] if index + 1 < len(sentence) else None
            parts = {c["candidate_id"]: score_parts(
                c["candidate_id"], left_id, right_id, counts) for c in candidates}
            morph = best_per_surface(candidates, {
                c["candidate_id"]: morphology_score(c) for c in candidates})
            combined = best_per_surface(candidates, {
                c["candidate_id"]: morphology_score(c) + sum(parts[c["candidate_id"]][:2])
                for c in candidates})
            supported = best_per_surface(candidates, {
                c["candidate_id"]: parts[c["candidate_id"]][1] for c in candidates
                if parts[c["candidate_id"]][2] > 0})
            if ranked_surfaces(morph)[0] == expected:
                totals["morph_correct"] += 1
            if ranked_surfaces(combined)[0] == expected:
                totals["combined_correct"] += 1
            order = ranked_surfaces(supported)
            if len(order) >= 2 and supported[order[0]][0] - supported[order[1]][0] >= margin:
                totals["surge_resolved"] += 1
                if order[0] == expected:
                    totals["surge_correct"] += 1
            else:
                totals["surge_abstained"] += 1
    covered = totals["covered"]
    if not covered:
        raise ValueError("no candidate-covered tokens in oracle evaluation")
    resolved = totals["surge_resolved"]
    return {
        "mode": "oracle_bidirectional_gold_neighbors",
        "candidate_covered_tokens": covered,
        "candidate_coverage": covered / totals["eligible"] if totals["eligible"] else None,
        "morphology_top1_accuracy": totals["morph_correct"] / covered,
        "combined_unigram_context_top1_accuracy": totals["combined_correct"] / covered,
        "surge_resolved_tokens": resolved,
        "surge_abstained_tokens": totals["surge_abstained"],
        "surge_resolution_precision": totals["surge_correct"] / resolved if resolved else None,
        "surge_correct_resolution_recall": totals["surge_correct"] / covered,
        "candidate_misses": totals["candidate_misses"],
        "uncovered_tokens": totals["uncovered"],
    }


def evaluate_causal(sentences, by_reading, counts, max_candidates, margin):
    totals = {name: Counter() for name in ("morphology", "combined", "surge_gated")}
    for sentence in sentences:
        histories = {name: [] for name in totals}
        for reading, expected, _gold in sentence:
            candidates = by_reading.get(reading, [])[:max_candidates]
            for policy in totals:
                stats = totals[policy]
                if not candidates:
                    stats["uncovered"] += 1
                    continue
                stats["eligible"] += 1
                if expected not in {c["surface"] for c in candidates}:
                    stats["candidate_misses"] += 1
                else:
                    stats["covered"] += 1
                left_id = histories[policy][-1] if histories[policy] else None
                parts = {c["candidate_id"]: score_parts(
                    c["candidate_id"], left_id, None, counts) for c in candidates}
                morph_scores = {c["candidate_id"]: morphology_score(c) for c in candidates}
                morph = best_per_surface(candidates, morph_scores)
                if policy == "morphology":
                    chosen_surface = ranked_surfaces(morph)[0]
                    chosen = morph[chosen_surface][1]
                elif policy == "combined":
                    combined = best_per_surface(candidates, {
                        c["candidate_id"]: morph_scores[c["candidate_id"]] +
                        parts[c["candidate_id"]][0] + parts[c["candidate_id"]][1]
                        for c in candidates})
                    chosen_surface = ranked_surfaces(combined)[0]
                    chosen = combined[chosen_surface][1]
                else:
                    supported = best_per_surface(candidates, {
                        c["candidate_id"]: parts[c["candidate_id"]][1]
                        for c in candidates if parts[c["candidate_id"]][2] > 0})
                    order = ranked_surfaces(supported)
                    if len(order) >= 2 and supported[order[0]][0] - supported[order[1]][0] >= margin:
                        chosen_surface = order[0]
                        chosen = supported[chosen_surface][1]
                        stats["resolved"] += 1
                        if chosen_surface == expected:
                            stats["correct_resolutions"] += 1
                    else:
                        chosen_surface = ranked_surfaces(morph)[0]
                        chosen = morph[chosen_surface][1]
                        stats["abstained"] += 1
                if chosen_surface == expected:
                    stats["correct"] += 1
                histories[policy].append(chosen["candidate_id"])
                if len(histories[policy]) > 2:
                    histories[policy].pop(0)
    output = {}
    for policy, stats in totals.items():
        covered, resolved = stats["covered"], stats["resolved"]
        output[policy] = {
            "eligible_tokens": stats["eligible"],
            "candidate_covered_tokens": covered,
            "candidate_coverage": covered / stats["eligible"] if stats["eligible"] else None,
            "top1_accuracy": stats["correct"] / covered if covered else None,
            "correct_tokens": stats["correct"],
            "candidate_misses": stats["candidate_misses"],
            "uncovered_tokens": stats["uncovered"],
        }
        if policy == "surge_gated":
            output[policy].update({
                "resolved_tokens": resolved,
                "abstained_tokens": stats["abstained"],
                "resolution_precision": stats["correct_resolutions"] / resolved if resolved else None,
                "correct_resolution_recall_over_covered":
                    stats["correct_resolutions"] / covered if covered else None,
                "fallback_to_morphology": True,
            })
    return {
        "mode": "causal_left_context_with_policy_specific_predicted_history",
        "gold_token_boundaries_and_readings": True,
        "gold_neighbor_candidate_ids_used": False,
        "future_context_used": False,
        "policies": output,
    }


def analyze(lexicon_path, corpus_path, sequences_path,
            context_margin=0.5, max_candidates=8, min_test_tokens=1):
    if not math.isfinite(context_margin) or context_margin < 0 or max_candidates < 1:
        raise ValueError("context margin must be finite/non-negative and candidate limit positive")
    by_reading, by_pair = read_lexicon(lexicon_path)
    counts = train_counts(sequences_path)
    sentences, source_tokens = read_test_sentences(corpus_path, by_pair)
    oracle = evaluate_oracle(sentences, by_reading, counts, max_candidates, context_margin)
    if oracle["candidate_covered_tokens"] < min_test_tokens:
        raise ValueError(
            f"only {oracle['candidate_covered_tokens']} candidate-covered test tokens; "
            f"required {min_test_tokens}")
    causal = evaluate_causal(sentences, by_reading, counts, max_candidates, context_margin)
    return {
        "test_source_tokens": source_tokens,
        "candidate_limit": max_candidates,
        "context_margin_threshold": context_margin,
        "train_unigram_types": len(counts["unigram"]),
        "train_unigram_tokens": counts["unigram_total"],
        "train_left_bigram_types": len(counts["left"]),
        "train_right_bigram_types": len(counts["right"]),
        "oracle_ablation": oracle,
        "causal_sequential": causal,
        "evaluation_boundary": (
            "causal mode uses gold token segmentation/readings but only each policy's "
            "own predicted left candidate history; it is not full end-to-end segmentation/decoding"
        ),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--train-sequences", type=Path, required=True)
    parser.add_argument("--context-margin", type=float, default=0.5)
    parser.add_argument("--max-candidates", type=int, default=8)
    parser.add_argument("--min-test-tokens", type=int, default=1)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = analyze(args.lexicon, args.corpus, args.train_sequences,
                         args.context_margin, args.max_candidates, args.min_test_tokens)
        payload = json.dumps({"context_evaluation": report}, ensure_ascii=False, indent=2)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(payload + "\n", encoding="utf-8")
        print(payload)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
