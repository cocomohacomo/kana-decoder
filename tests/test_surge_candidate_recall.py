#!/usr/bin/env python3
"""Contract test for N-best candidate recall and oracle CER metrics."""
import json
import subprocess
import sys


def main():
    if len(sys.argv) != 6:
        raise SystemExit("usage: test_surge_candidate_recall.py EVALUATOR PACK CORPUS TRAIN POLICY")
    evaluator, pack, corpus, train, policy = sys.argv[1:]
    result = subprocess.run(
        [evaluator, pack, corpus, "general", train, policy],
        text=True, capture_output=True, check=False,
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    metrics = payload["candidate_generation"]
    required = {
        "mean_paths", "surface_exact_recall_at_1", "surface_exact_recall_at_5",
        "surface_exact_recall_at_10", "oracle_cer_at_5", "oracle_cer_at_10", "surge_exact_given_candidate_at_10",
    }
    assert required <= set(metrics), metrics
    recall1 = metrics["surface_exact_recall_at_1"]
    recall5 = metrics["surface_exact_recall_at_5"]
    recall10 = metrics["surface_exact_recall_at_10"]
    assert 0.0 <= recall1 <= recall5 <= recall10 <= 1.0, metrics
    assert metrics["mean_paths"] >= 1.0, metrics
    assert 0.0 <= metrics["oracle_cer_at_10"] <= metrics["oracle_cer_at_5"], metrics
    assert 0.0 <= metrics["surge_exact_given_candidate_at_10"] <= 1.0, metrics
    assert payload["cases"] > 0
    print("SURGE candidate Recall@N contract tests passed")


if __name__ == "__main__":
    main()
