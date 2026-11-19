# kana-decoder SURGE Core統合仕様 v0.42

Version: 0.42.0
Parent: SPECIFICATION_v0.41.md
Normative status: integration architecture and runtime contract

## 1. Purpose

The independent SURGE Core is the authoritative context cognition, evidence, question, temporal validity, open-world memory and commit journal for kana-to-text normalization. kana-decoder is a language-specific recognition adapter and candidate generator. It MUST NOT maintain a second authoritative context graph.

The integration vendors the upstream SURGE repository at commit
`c8a0b5a5f8cf714b74892d1263bd53088fe51be3` (Core implementation 0.9.11,
SURGE specification 1.0) under `third_party/surge`. This is required for
offline, reproducible builds; it is not a network-time FetchContent dependency.

## 2. Runtime ownership

| Concern | Authority |
|---|---|
| Reading input, dictionary lookup, morphology lattice | kana-decoder language adapter |
| Candidate generation and bounded N-best | kana-decoder decoder |
| Context, immutable Evidence, Claim lifecycle | SURGE ContextGraph |
| Durable event order, idempotency, replay | SURGE EventJournal |
| Dependency evaluation | SURGE DependencyEngine invoked by ContextGraph |
| Contradiction to Question / Interest / Focus | SURGE QuestionRouter |
| Unresolved candidate set and competing interpretations | SURGE ChaosClusterLedger |
| Claim/Evidence valid-time and reassessment trace | SURGE ContextGraph temporal ledger |
| Provisional/final output commit | SURGE journal adapter events |
| Japanese number reading and initial orthographic rendering | Japanese LanguageAdapter |

The legacy local fixed-capacity LanguageState remains only as a compatibility and
isolated language-primitive test surface. It is not authoritative and is not
consulted by the production C/Python decode path.

## 3. Candidate-to-Core mapping

For each unique N-best token candidate, the adapter submits one atomic
`SemanticDelta` containing:

1. an Entity node describing the lexical or typed numeric candidate;
2. an Evidence node containing candidate identity, source reading span, POS,
   morphology/context score observations, source digest and observation time;
3. a Claim node referencing that Evidence;
4. Evidence `SUPPORTS` Claim relation;
5. Claim `ABOUT` context Cluster and Entity relations;
6. one bounded temporal Dependency for the packet's representative selected
   Claim, with predicate `CLAIM_TEMPORAL_STATE:VALID@timestamp`;
7. ClaimValidity and EvidenceValidity revisions tied to the source Evidence.

Candidate identity is the tuple of reading codepoint span, dictionary candidate ID
and surface. Distinct candidates sharing a span but producing different surfaces
are represented as competing Claims, never overwritten in place.

The current adapter records decoder morphology and context observations as
provenance-bearing Evidence content. It does not claim that the combined context
score is already decomposed into independent Word-Ngram, POS-Ngram and Cluster
Evidence channels. That decomposition remains a follow-up adapter task.

## 4. Contradiction, question and focus flow

When distinct candidate Claims occupy the same reading span, the adapter adds a
typed Claim-to-Claim `CONTRADICTS` relation. SURGE QuestionRouter converts each
such relation into Question and QuestionAspect nodes, Interest links to the
normalization context Cluster, and Focus when the target Cluster is unambiguous.

No contradiction is inferred merely because two candidates have different
candidate IDs; the current trigger requires a shared span and different surfaces.

## 5. Chaos Cluster retention

If N-best contains candidates outside the best path, the adapter creates or reuses
a persistent Chaos Cluster. One member records the source's unresolved candidate
set, and a provisional PatternHypothesis retains alternative Claim references.
This preserves uncertainty without asserting that all alternatives are true or
semantically equivalent. The retained hypothesis is not promoted automatically.

## 6. Temporal and dependency behavior

Claim and Evidence validity are written in the same SemanticDelta as the
candidate graph. ContextGraph runs DependencyEngine with bounded predicate
operations. One representative selected Claim per packet receives an explicit
TemporalReassessmentRecord; all candidate Claims still retain validity records
and can be reassessed on demand. This avoids one durable reassessment transaction
per token while preserving the evidence needed for later re-evaluation.

## 7. Streaming and commit lifecycle

- Streaming append: persist the current candidate graph and a
  `NORMALIZATION_PROPOSAL`; return provisional output only.
- Common-prefix commit: append `NORMALIZATION_STREAM_PREFIX_COMMIT`.
- Final commit: append `NORMALIZATION_STREAM_FINAL_COMMIT`.
- Batch decode: persist proposal and explicit final commit.
- N-best API: persist a final decision event with the selected path and alternatives.

Adapter-owned event kinds are recognized by the vendored ContextGraph replay
logic. Each custom event is one EventJournal transaction and advances the shared
state version exactly once. The adapter does not insert unrecognized journal
events into the shared journal.

## 8. Persistence and resource ceilings

Default runtime profile:

| Resource | Ceiling |
|---|---:|
| Unique candidates per decode | 64 |
| Contradiction relations per decode | 16 |
| Alternative refs in Chaos PatternHypothesis | 16 |
| N-best paths | 8 |
| Input reading | 256 codepoints |
| Journal events | 4,096 |
| Journal transactions | 2,048 |
| One journal payload | 256 KiB |
| Retained journal identity index | 4 MiB |

The default C API engine uses a unique ephemeral journal in the OS temporary
directory and removes it on engine destruction. Applications that need restart
recovery call `kd_engine_set_surge_journal` or pass
`surge_journal_path` to the Python wrapper. A persistent journal is caller-owned
and is not deleted by the library.

Resource exhaustion, invalid graph references, stale state versions and journal
commit failures fail closed. The runtime does not silently truncate candidates.

## 9. Compatibility

- C ABI remains version 1; `kd_engine_set_surge_journal` is additive.
- Existing language Pack schema and contents are unchanged.
- Static and shared libraries link the same pinned SURGE Core.
- The library remains offline and CPU-only.
- Existing low-level morphology APIs remain available for diagnostic/evaluation use.

## 10. Explicit non-goals of this integration stage

The integration does not fabricate semantic roles, full syntactic parses,
coreference, discourse-act recognition, date/time phrase extraction, unit
disambiguation, or style inference when no corresponding language recognizer
exists. The current number parser recognizes only a bounded subset of Japanese
integer/decimal readings. These are separate adapter extensions.

No CER, RTF or RSS improvement is claimed until the integrated Pack is
re-evaluated against the fixed dev/test protocol. The additional Core journal
and graph memory cost must be measured independently from decoder RSS.

## 11. Next stages

1. Split Morphological, Word-Ngram, POS-Ngram, Cluster and Orthographic
   Evidence into independently versioned Core Evidence producers.
2. Move evidence aggregation and candidate ranking into a generic SURGE
   RecognitionDecision API, with calibrated per-language policies.
3. Connect full syntax/semantic frames and scope spans from dictionary,
   connection matrix and language-specific recognizers.
4. Add date/time/range/unit/entity extraction and orthographic style policy.
5. Add snapshot/compaction and bounded retention for long-lived sessions.
6. Run full Pack quality and resource benchmarks; compare CER, exact sentence,
   RSS, RTF, journal growth and streaming commit correction rate.

