#!/usr/bin/env python3
"""Build train-only lexical context clusters and evaluate cluster-gated candidate selection.

This is a lightweight SURGE-cluster feasibility probe, not the production SURGE Core.
Gold token boundaries/readings are used only to isolate candidate ranking; candidate
history is predicted independently by each policy.
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
            if len(cols) < 8:
                raise ValueError(f"{path}:{line_no}: expected at least eight columns")
            item = {"reading": cols[0], "surface": cols[1], "candidate_id": cols[5],
                    "word_cost": int(cols[6] or 0), "connection_cost": int(cols[7] or 0)}
            by_reading[item["reading"]].append(item)
            key = (item["reading"], item["surface"])
            if key not in by_pair or item["word_cost"] < by_pair[key]["word_cost"]:
                by_pair[key] = item
    for values in by_reading.values():
        values.sort(key=lambda x: (x["word_cost"] + x["connection_cost"], x["candidate_id"]))
    return by_reading, by_pair


def read_sequences(path):
    sequences = []
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            tokens = [x for x in line.rstrip("\r\n").split("\t") if x]
            if tokens:
                sequences.append(tokens)
    if not sequences:
        raise ValueError("training sequences are empty")
    return sequences


def make_clusters(sequences, max_vocab=2000, window=2, min_similarity=0.32,
                  max_cluster_size=24, max_context_df=100):
    frequency = Counter(token for seq in sequences for token in seq)
    vocab = {token for token, _ in frequency.most_common(max_vocab)}
    contexts = defaultdict(Counter)
    context_df = Counter()
    for seq in sequences:
        filtered = [x for x in seq if x in vocab]
        for i, token in enumerate(filtered):
            for j in range(max(0, i-window), min(len(filtered), i+window+1)):
                if i != j:
                    contexts[token][filtered[j]] += 1
        for token in set(filtered):
            context_df[token] += 1
    # Sparse TF-IDF vectors, with common sentence context suppressed.
    vectors = {}
    for token, counts in contexts.items():
        weighted = {}
        for context, count in counts.items():
            df = context_df[context]
            if df > max_context_df:
                continue
            weighted[context] = (1.0 + math.log(count)) * math.log(
                (1.0 + len(sequences)) / (1.0 + df))
        norm = math.sqrt(sum(value * value for value in weighted.values()))
        if norm:
            vectors[token] = {key: value / norm for key, value in weighted.items()}
    # Inverted index avoids a full vocabulary-squared similarity matrix.
    inverted = defaultdict(list)
    for token, vector in vectors.items():
        for feature, weight in vector.items():
            inverted[feature].append((token, weight))
    similarities = defaultdict(float)
    for posting in inverted.values():
        if len(posting) > max_context_df:
            continue
        for i, (left, lw) in enumerate(posting):
            for right, rw in posting[i+1:]:
                key = (left, right) if left < right else (right, left)
                similarities[key] += lw * rw
    parent = {token: token for token in vocab}
    sizes = {token: 1 for token in vocab}

    def root(token):
        while parent[token] != token:
            parent[token] = parent[parent[token]]
            token = parent[token]
        return token

    edges = sorted(((score, pair) for pair, score in similarities.items()
                    if score >= min_similarity), key=lambda x: (-x[0], x[1]))
    for score, (left, right) in edges:
        a, b = root(left), root(right)
        if a == b or sizes[a] + sizes[b] > max_cluster_size:
            continue
        # Stable root selection makes the output reproducible.
        if a > b:
            a, b = b, a
        parent[b] = a
        sizes[a] += sizes[b]
    members = defaultdict(list)
    for token in sorted(vocab):
        members[root(token)].append(token)
    groups = [sorted(items) for items in members.values() if len(items) >= 2]
    groups.sort(key=lambda items: (items[0], len(items)))
    token_cluster = {}
    for cluster_id, items in enumerate(groups):
        for token in items:
            token_cluster[token] = cluster_id
    # Directional, distance-preserving counts: a cluster at position i
    # supports the candidate at i + distance. This avoids sentence-wide
    # topic co-occurrence being mistaken for local decoding evidence.
    cluster_candidates = defaultdict(Counter)
    cluster_totals = Counter()
    for seq in sequences:
        for index, context_token in enumerate(seq):
            cluster_id = token_cluster.get(context_token)
            if cluster_id is None:
                continue
            for distance in range(1, window + 1):
                target_index = index + distance
                if target_index >= len(seq):
                    break
                target = seq[target_index]
                cluster_candidates[cluster_id][(target, distance)] += 1
                cluster_totals[(cluster_id, distance)] += 1
    return token_cluster, cluster_candidates, cluster_totals, {
        "train_sequences": len(sequences), "vocabulary_considered": len(vocab),
        "cluster_count": len(groups), "clustered_tokens": len(token_cluster),
        "largest_cluster": max((len(x) for x in groups), default=0),
        "min_similarity": min_similarity, "window": window,
    }


def read_split(path, by_pair, split):
    sentences = defaultdict(list)
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        for line_no, row in enumerate(reader, 2):
            if row["split"] != split:
                continue
            gold = by_pair.get((row["reading"], row["surface"]))
            if gold is None:
                raise ValueError(f"{path}:{line_no}: token absent from lexicon")
            sentences[(row["document_id"], row["sentence_id"])].append(
                (row["reading"], row["surface"], gold))
    return list(sentences.values())


def rank_by_surface(candidates, scores):
    best = {}
    for candidate in candidates:
        cid = candidate["candidate_id"]
        if cid not in scores:
            continue
        surface = candidate["surface"]
        if surface not in best or scores[cid] > best[surface][0]:
            best[surface] = (scores[cid], candidate)
    return sorted(best, key=lambda s: (-best[s][0], s)), best


def evaluate(sentences, by_reading, token_cluster, cluster_candidates,
             cluster_totals, beta, margin, alpha=0.5):
    totals = Counter()
    vocab_size = max(1, len(token_cluster) * 4)
    for sentence in sentences:
        history = []
        for reading, expected, _gold in sentence:
            candidates = by_reading.get(reading, [])[:8]
            if not candidates:
                continue
            totals["eligible"] += 1
            if expected not in {c["surface"] for c in candidates}:
                totals["candidate_misses"] += 1
            else:
                totals["covered"] += 1
            morph_scores = {c["candidate_id"]:
                -float(c["word_cost"] + c["connection_cost"]) for c in candidates}
            order, morph = rank_by_surface(candidates, morph_scores)
            if not order:
                continue
            chosen_surface = order[0]
            if chosen_surface == expected:
                totals["morph_correct"] += 1
            cluster_scores, evidence = {}, {}
            for candidate in candidates:
                cid = candidate["candidate_id"]
                values, support = [], 0
                # history is causal and bounded to the previous two predictions.
                # Distance 1 is the immediately preceding predicted token.
                for distance, context_candidate_id in enumerate(reversed(history), 1):
                    cluster_id = token_cluster.get(context_candidate_id)
                    if cluster_id is None:
                        continue
                    count = cluster_candidates[cluster_id][(cid, distance)]
                    total = cluster_totals[(cluster_id, distance)]
                    if count:
                        support += count
                        values.append(math.log((count + alpha) /
                            (total + alpha * vocab_size)))
                if values:
                    cluster_scores[cid] = sum(values) / len(values)
                    evidence[cid] = support
            supported_order, supported = rank_by_surface(candidates, cluster_scores)
            resolved = False
            if len(supported_order) >= 2:
                gap = supported[supported_order[0]][0] - supported[supported_order[1]][0]
                if beta * gap >= margin:
                    chosen_surface = supported_order[0]
                    resolved = True
            if resolved:
                totals["resolved"] += 1
                if chosen_surface == expected:
                    totals["correct_resolutions"] += 1
            else:
                totals["abstained"] += 1
            if chosen_surface == expected:
                totals["cluster_correct"] += 1
            selected = (supported[chosen_surface][1] if resolved
                        else morph[chosen_surface][1])
            history.append(selected["candidate_id"])
            if len(history) > 2:
                history.pop(0)
    covered = totals["covered"]
    resolved = totals["resolved"]
    return {
        "eligible_tokens": totals["eligible"], "candidate_covered_tokens": covered,
        "candidate_misses": totals["candidate_misses"],
        "morphology_accuracy": totals["morph_correct"] / covered if covered else None,
        "cluster_gated_accuracy": totals["cluster_correct"] / covered if covered else None,
        "resolved_tokens": resolved, "abstained_tokens": totals["abstained"],
        "resolution_precision": totals["correct_resolutions"] / resolved if resolved else None,
        "correct_resolution_recall": totals["correct_resolutions"] / covered if covered else None,
    }


def analyze(lexicon_path, corpus_path, sequences_path):
    by_reading, by_pair = read_lexicon(lexicon_path)
    sequences = read_sequences(sequences_path)
    token_cluster, cluster_candidates, cluster_totals, cluster_meta = make_clusters(sequences)
    dev = read_split(corpus_path, by_pair, "dev")
    test = read_split(corpus_path, by_pair, "test")
    if not dev or not test:
        raise ValueError("both dev and test splits must contain covered sentences")
    grid = []
    for beta in (0.25, 0.5, 1.0, 2.0):
        for margin in (0.0, 0.05, 0.1, 0.25, 0.5):
            # beta scales evidence gap equivalently by scaling the acceptance margin.
            metrics = evaluate(dev, by_reading, token_cluster, cluster_candidates,
                               cluster_totals, beta, margin)
            grid.append((metrics["cluster_gated_accuracy"] or 0.0, beta, margin, metrics))
    _, beta, margin, dev_metrics = max(grid, key=lambda x: (
        x[0], x[3]["resolution_precision"] or 0.0, -x[1], -x[2]))
    test_metrics = evaluate(test, by_reading, token_cluster, cluster_candidates,
                            cluster_totals, beta, margin)
    return {
        "model": "train-only lexical context-profile clusters",
        "cluster_model": cluster_meta,
        "calibration": {"split": "dev", "grid_size": len(grid),
                        "selected_beta": beta, "selected_margin": margin,
                        "dev_metrics": dev_metrics},
        "test_metrics": test_metrics,
        "evaluation_limits": {
            "gold_token_boundaries_and_readings": True,
            "gold_neighbor_candidate_ids_used": False,
            "future_context_used": False,
            "cluster_generation_split": "train only",
            "note": "Feasibility probe only; not production SURGE Core or full raw-kana end-to-end evaluation."
        }
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lexicon", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--train-sequences", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.lexicon, args.corpus, args.train_sequences)
        payload = json.dumps({"surge_cluster_evaluation": result}, ensure_ascii=False, indent=2)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(payload + "\n", encoding="utf-8")
        print(payload)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"error: {exc}\n")


if __name__ == "__main__":
    main()
