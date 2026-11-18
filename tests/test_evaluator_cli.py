#!/usr/bin/env python3
"""Black-box contract tests for the corpus evaluation CLI."""
import json
import pathlib
import subprocess
import sys
import tempfile


def run(command):
    return subprocess.run(command, text=True, capture_output=True, check=False)


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: test_evaluator_cli.py EVALUATOR PACK_DIR CORPUS_TSV")
    evaluator, pack_dir, corpus = sys.argv[1:]

    normal = run([evaluator, pack_dir, corpus])
    assert normal.returncode == 0, normal.stderr
    normal_result = json.loads(normal.stdout)
    assert normal_result["overall"]["cases"] > 0
    assert "details" not in normal_result

    detailed = run([evaluator, pack_dir, corpus, "--details"])
    assert detailed.returncode == 0, detailed.stderr
    detailed_result = json.loads(detailed.stdout)
    assert detailed_result["overall"] == normal_result["overall"]
    assert detailed_result["domains"] == normal_result["domains"]
    assert len(detailed_result["details"]) == normal_result["overall"]["cases"]
    for item in detailed_result["details"]:
        assert set(item) == {
            "reading", "expected", "predicted", "domain",
            "edit_distance", "decode_failure",
        }
        assert isinstance(item["decode_failure"], bool)
        assert item["edit_distance"] >= 0

    traced = run([evaluator, pack_dir, corpus, "--trace"])
    assert traced.returncode == 0, traced.stderr
    traced_result = json.loads(traced.stdout)
    assert traced_result["overall"] == normal_result["overall"]
    assert len(traced_result["details"]) == normal_result["overall"]["cases"]
    assert all("candidate_trace" in item for item in traced_result["details"])
    for item in traced_result["details"]:
        for candidate in item["candidate_trace"]:
            assert candidate["local_score"] == (
                candidate["morphology_score"] + candidate["weighted_context_score"]
            )
            assert isinstance(candidate["unknown_fallback"], bool)

    # CRLF input must not contaminate the domain key.
    with tempfile.TemporaryDirectory() as directory:
        crlf = pathlib.Path(directory) / "crlf.tsv"
        crlf.write_bytes("あめ\t雨\tfixture\r\n".encode("utf-8"))
        crlf_result = run([evaluator, pack_dir, str(crlf), "--details"])
        assert crlf_result.returncode == 0, crlf_result.stderr
        parsed = json.loads(crlf_result.stdout)
        assert list(parsed["domains"]) == ["fixture"]

        malformed = pathlib.Path(directory) / "malformed.tsv"
        malformed.write_text("missing-separator\n", encoding="utf-8")
        rejected = run([evaluator, pack_dir, str(malformed)])
        assert rejected.returncode == 2
        assert "line 1" in rejected.stderr

        empty = pathlib.Path(directory) / "empty.tsv"
        empty.write_text("# comment only\n", encoding="utf-8")
        rejected_empty = run([evaluator, pack_dir, str(empty)])
        assert rejected_empty.returncode == 2
        assert "empty" in rejected_empty.stderr

    print("evaluator CLI contract tests passed")


if __name__ == "__main__":
    main()
