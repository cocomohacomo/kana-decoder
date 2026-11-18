#!/usr/bin/env python3
"""Evaluate morphology, unigram+context and evidence-gated SURGE on held-out tokens.

This is a teacher-forced oracle-context ablation: neighboring gold token IDs are
provided to isolate candidate scoring. It is not an end-to-end conversion score.
The n-gram equations mirror MaskedNgramScorer in src/scoring.cpp.
"""
import argparse
import csv
import json
import math
from collections import Counter, defaultdict
from pathlib import Path


def read_lexicon(path):
    by_reading = defaultdict(list)
    by_pair = {}
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\r\n").split("\t")
            if len(cols) < 6:
                raise ValueError(f"{path}:{line_no}: expected at least six columns")
            try:
                word_cost = int(cols[6]) if len(cols) > 6 and cols[6] else 0
                connection_cost = int(cols[7]) if len(cols) > 7 and cols[7] else 0
                left_id = int(cols[8]) if len(cols) > 8 and cols[8] else 0
                right_id = int(cols[9]) if len(cols) > 9 and cols[9] else 0
            except ValueError as exc:
                raise ValueError(f"{path}:{line_no}: invalid lexicon cost or connection ID") from exc
            item = {
                "reading": cols[0], "surface": cols[1], "candidate_id": cols[5],
                "word_cost": word_cost, "connection_cost": connection_cost,
                "left_id": left_id, "right_id": right_id,
            }
            by_reading[item["reading"]].append(item)
            key = (item["reading"], item["surface"])
            current = by_pair.get(key)
            if current is None or (word_cost, item["candidate_id"]) < (
                    current["word_cost"], current["candidate_id"]):
                by_pair[key] = item
    for reading in by_reading:
        by_reading[reading].sort(key=lambda x: (
            x["word_cost"], x["candidate_id"], x["surface"]))
    return by_reading, by_pair


def train_counts(path):
    unigram = Counter()
    left = Counter()
    right = Counter()
    left_total = Counter()
    right_total = Counter()
    unigram_total = 0
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            tokens = [x for x in line.rstrip("\r\n").split("\t") if x]
            if not tokens:
                continue
            for i, token in enumerate(tokens):
                unigram[token] += 1
                unigram_total += 1
                if i:
                    left[(tokens[i - 1], token)] += 1
                    left_total[tokens[i - 1]] += 1
                if i + 1 < len(tokens):
                    right[(token, tokens[i + 1])] += 1
                    right_total[tokens[i + 1]] += 1
    return unigram, left, right, left_total, right_total, unigram_total


def read_connections(path, required_pairs=None):
    result = {}
    if path is None or not required_pairs:
        return result
    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\r\n").split("\t")
            if len(cols) != 3:
                raise ValueError(f"{path}:{line_no}: connection row must have three columns")
            right_id, left_id, cost = map(int, cols)
            key = (right_id, left_id)
            if key in required_pairs:
                result[key] = cost
    return result


def context_score(candidate_id, left_id, right_id, counts):
    unigram, left, right, left_total, right_total, unigram_total = counts
    vocab = max(1, len(unigram))
    alpha = 0.5
    score = 0.0
    support = 0
    if left_id is not None:
        count = left[(left_id, candidate_id)]
        support += count
        score += math.log((count + alpha) /
                          (left_total[left_id] + alpha * vocab))
    if right_id is not None:
        count = right[(candidate_id, right_id)]
        support += count
        score += math.log((count + alpha) /
                          (right_total[right_id] + alpha * vocab))
    uc = unigram[candidate_id]
    prior = math.log((uc + alpha) / (unigram_total + alpha * vocab))
    return prior, score, support


def _best_by_surface(candidates, score_fn):
    best = {}
    for candidate in candidates:
        score = score_fn(candidate)
        surface = candidate["surface"]
        if surface not in best or score > best[surface][0]:
            best[surface] = (score, candidate)
    return best


def _top(best):
    return sorted(best, key=lambda surface: (-best[surface][0], surface))


def analyze(lexicon_path, corpus_path, sequences_path, connections_path=None,
            context_margin=0.5, max_candidates=8, min_test_tokens=1):
    if context_margin < 0 or max_candidates < 1:
        raise ValueError("context margin must be non-negative and max candidates positive")
    by_reading, by_pair = read_lexicon(lexicon_path)
    counts = train_counts(sequences_path)
    sentences = defaultdict(list)
    source_tokens = 0
    with corpus_path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        required = {"document_id", "sentence_id", "reading", "surface", "domain", "split"}
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError("corpus header must contain document_id, sentence_id, reading, surface, domain, split")
        for line_no, row in enumerate(reader, 2):
            if row["split"] != "test":
                continue
            source_tokens += 1
            key = (row["document_id"], row["sentence_id"])
            pair = (row["reading"], row["surface"])
            gold = by_pair.get(pair)
            if gold is None:
                raise ValueError(f"{corpus_path}:{line_no}: test token absent from normalized lexicon")
            sentences[key].append((row["reading"], row["surface"], gold))

    # The imported matrix can contain millions of sparse transitions. Retain only
    # transitions needed by the held-out gold-neighbor candidate windows.
    required_connections = set()
    for sentence in sentences.values():
        for index, (reading, _, _) in enumerate(sentence):
            previous = sentence[index - 1][2] if index else None
            following = sentence[index + 1][2] if index + 1 < len(sentence) else None
            previous_right = previous["right_id"] if previous else None
            following_left = following["left_id"] if following else None
            for candidate in by_reading.get(reading, [])[:max_candidates]:
                if previous_right is not None:
                    required_connections.add((previous_right, candidate["left_id"]))
                if following_left is not None:
                    required_connections.add((candidate["right_id"], following_left))
    connections = read_connections(connections_path, required_connections)

    totals = Counter()
    for sentence in sentences.values():
        for index, (reading, expected, gold) in enumerate(sentence):
            candidates = by_reading.get(reading, [])[:max_candidates]
            if not candidates:
                totals["uncovered"] += 1
                continue
            gold_surfaces = {candidate["surface"] for candidate in candidates}
            totals["eligible"] += 1
            if expected not in gold_surfaces:
                totals["candidate_misses"] += 1
                continue
            totals["covered"] += 1
            prev_id = sentence[index - 1][2]["candidate_id"] if index else None
            next_id = sentence[index + 1][2]["candidate_id"] if index + 1 < len(sentence) else None
            prev_right = sentence[index - 1][2]["right_id"] if index else None
            next_left = sentence[index + 1][2]["left_id"] if index + 1 < len(sentence) else None

            def components(candidate):
                prior, context, support = context_score(
                    candidate["candidate_id"], prev_id, next_id, counts)
                morph_cost = (candidate["word_cost"] + candidate["connection_cost"])
                if prev_right is not None:
                    morph_cost += connections.get((prev_right, candidate["left_id"]), 0)
                if next_left is not None:
                    morph_cost += connections.get((candidate["right_id"], next_left), 0)
                morphology = -float(morph_cost)
                return morphology, prior, context, support

            per_id = {candidate["candidate_id"]: components(candidate)
                      for candidate in candidates}
            morphology = _best_by_surface(candidates, lambda c: per_id[c["candidate_id"]][0])
            combined = _best_by_surface(
                candidates, lambda c: sum(per_id[c["candidate_id"]][:3]))
            context = _best_by_surface(candidates, lambda c: per_id[c["candidate_id"]][2])
            if _top(morphology)[0] == expected:
                totals["morphology_correct"] += 1
            if _top(combined)[0] == expected:
                totals["combined_correct"] += 1

            supported = {}
            for candidate in candidates:
                item = per_id[candidate["candidate_id"]]
                if item[3] > 0:
                    surface = candidate["surface"]
                    if surface not in supported or item[2] > supported[surface][0]:
                        supported[surface] = (item[2], candidate)
            ranked_support = sorted(supported, key=lambda s: (-supported[s][0], s))
            if len(ranked_support) >= 2:
                margin = supported[ranked_support[0]][0] - supported[ranked_support[1]][0]
                if margin >= context_margin:
                    totals["surge_resolved"] += 1
                    if ranked_support[0] == expected:
                        totals["surge_correct"] += 1
                else:
                    totals["surge_abstained"] += 1
            else:
                totals["surge_abstained"] += 1

    if totals["covered"] < min_test_tokens:
        raise ValueError(
            f"only {totals['covered']} candidate-covered test tokens; required {min_test_tokens}")
    covered = totals["covered"]
    resolved = totals["surge_resolved"]
    result = {
        "evaluation_mode": "teacher-forced gold neighboring tokens; candidate-scoring ablation, not end-to-end decoding",
        "context_margin_threshold": context_margin,
        "candidate_limit": max_candidates,
        "test_source_tokens": source_tokens,
        "eligible_tokens": totals["eligible"],
        "candidate_covered_tokens": covered,
        "candidate_coverage": covered / totals["eligible"] if totals["eligible"] else None,
        "morphology_top1_accuracy": totals["morphology_correct"] / covered,
        "combined_unigram_context_top1_accuracy": totals["combined_correct"] / covered,
        "surge_resolved_tokens": resolved,
        "surge_abstained_tokens": totals["surge_abstained"],
        "surge_resolution_precision": totals["surge_correct"] / resolved if resolved else None,
        "surge_correct_resolution_recall_over_covered": totals["surge_correct"] / covered,
        "surge_resolved_accuracy_over_covered": resolved / covered,
        "candidate_misses": totals["candidate_misses"],
        "uncovered_tokens": totals["uncovered"],
        "train_unigram_types": len(counts[0]),
        "train_unigram_tokens": counts[5],
        "train_left_bigram_types": len(counts[1]),
        "train_right_bigram_types": len(counts[2]),
        "corpus_splits": "train/dev/test supplied by UD Japanese-GSD; test labels used only for scoring",
    }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--train-sequences", type=Path, required=True)
    parser.add_argument("--connections", type=Path)
    parser.add_argument("--context-margin", type=float, default=0.5)
    parser.add_argument("--max-candidates", type=int, default=8)
    parser.add_argument("--min-test-tokens", type=int, default=1)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        report = analyze(args.lexicon, args.corpus, args.train_sequences,
                         args.connections, args.context_margin,
                         args.max_candidates, args.min_test_tokens)
        payload = json.dumps({"context_ablation": report}, ensure_ascii=False, indent=2)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(payload + "\n", encoding="utf-8")
        print(payload)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
