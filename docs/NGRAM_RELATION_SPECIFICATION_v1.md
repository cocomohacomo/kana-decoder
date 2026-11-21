# N-gram Relation Projector v1

## Objective

Generate SURGE input evidence from Japanese n-gram observations with the explicit goal of improving contextual inference beyond a conventional n-gram candidate scorer. This is experimental; no accuracy gain is claimed until held-out evaluation confirms it.

The projector does not create or assign clusters. It emits directed relation events. SURGE Core owns classification fusion, cluster assignment, cluster importance, and temporal re-evaluation.

## Data flow

Train-only tokenized sequences -> directional position counts -> conditional probability / PMI / reliability -> deterministic JSONL SURGE relation events -> SURGE Core.

Semantic classifications and weights from external LLMs remain a separate evidence family. Relation keys are lexical identifiers, not semantic labels.

## Event semantics

Each event contains source and target lexical keys, direction (left/right), exact token distance (1..4; default 3), ordered marker, raw count, source-direction-distance opportunity count, conditional and target unigram probabilities, clipped PMI, count reliability, distance weight, and association.

For source A, direction D, distance d:

- P(B|A,D,d) = count(A,B,D,d) / opportunities(A,D,d)
- PMI = log(P(B|A,D,d) / P(B))
- reliability = count / (count + prior)
- association = reliability * clip(PMI) / d^gamma

Raw components are retained so SURGE can select its own evidence fusion. Distance weighting is a prior, not a hard rule.

## Why this can exceed plain n-gram

A conventional n-gram scorer primarily ranks candidates using local conditional likelihood. The projector instead supplies reusable relational evidence that SURGE can combine with independently supplied semantic classification, provenance, temporal relevance, contradiction/question routing, and cluster history. This offers a path to resolve cases where local surface likelihood is insufficient.

Improvement remains a hypothesis until held-out evaluation demonstrates it. The relation signal and existing masked n-gram score must not be naively added as independent evidence because they may originate from the same corpus. Runtime integration requires evidence-family de-duplication or a calibrated residual contribution.

## Data and safety contract

- Only training sequences build the relation model.
- Dev is for parameter selection; held-out test is evaluated once after freezing.
- Deterministic sorting and JSON serialization.
- Hard bounds: distance <= 4, 2,000,000 events, 128 MiB output, 256-byte token, 1 MiB input line. The CLI counts into a temporary SQLite database with a 2 MiB page cache and streams sorted JSONL to a temporary output file before atomic publication.
- Invalid or over-limit input fails closed.
- No cluster IDs are emitted.
- Raw corpus traces are not uploaded as CI artifacts.

## CLI

python tools/project_ngram_relations.py --train-sequences train_sequences.tsv --split train --output relations.jsonl

Options: max-distance, min-count, reliability-prior, distance-gamma, pmi-clip, max-bytes.

Output includes a .manifest.json file with SHA-256 and split provenance. The caller must ensure that the supplied sequence file is train-only.

## Evaluation gates

1. Unit tests verify direction, distance, order, rare-count shrinkage, deterministic output, and fail-closed limits.
2. Compare baseline n-gram-only against SURGE with projected relation events using pinned train/dev/test evaluation.
3. Tune only on dev. Freeze event model and SURGE policy before held-out test.
4. Promote to runtime only if held-out CER and sentence exact improve against the n-gram baseline without unacceptable latency/RSS regression.
