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
    classification = payload["candidate_error_classification"]
    required_classes = {
        "missing_candidate", "candidate_available",
        "candidate_available_runtime_failure", "candidate_available_deferred",
        "candidate_available_surge_exact", "candidate_available_surge_wrong",
        "wrong_output_matches_raw_nbest", "wrong_output_outside_raw_nbest",
    }
    assert required_classes <= set(classification), classification
    cases = payload["cases"]
    assert classification["missing_candidate"] + classification["candidate_available"] == cases, classification
    available_partition = (
        classification["candidate_available_runtime_failure"]
        + classification["candidate_available_deferred"]
        + classification["candidate_available_surge_exact"]
        + classification["candidate_available_surge_wrong"]
    )
    assert available_partition == classification["candidate_available"], classification
    wrong_partition = (
        classification["wrong_output_matches_raw_nbest"]
        + classification["wrong_output_outside_raw_nbest"]
    )
    assert wrong_partition == classification["candidate_available_surge_wrong"], classification
    assert classification["candidate_available"] == round(recall10 * cases), classification
    assert payload["cases"] > 0
    print("SURGE candidate Recall@N contract tests passed")


if __name__ == "__main__":
    main()
