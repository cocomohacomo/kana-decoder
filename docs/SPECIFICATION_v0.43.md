# kana-decoder: SURGE Language and Normalization Runtime Migration v0.43

Version: 0.43.0
Parent: SPECIFICATION_v0.42.md

## 1. Goal

Move both Language Adapter and Normalization Adapter completely into SURGE. kana-decoder must not retain language-specific recognition or normalization logic. Its final role is a thin host/API layer for kana input, normalized output transport, compatibility APIs, and evaluation.

Normative framework contract: SURGE Language Runtime and Replaceable Language Packs v0.1, including its Normalization Runtime section.

## 2. Target ownership

| Component | Ownership |
|---|---|
| Streaming ingress, source spans, event sequencing | SURGE Core |
| Candidate/evidence/claim graph, Questions, Interest/Focus, Dependencies, Chaos Cluster | SURGE Core |
| RecognitionDecision, temporal reassessment, revisions and commit journal | SURGE Core |
| Generic Language Runtime ABI, pack loader, validation, session pinning | SURGE Language Runtime |
| Japanese lexicon, morphology, POS, connection matrix, readings, n-grams | Japanese Language Set |
| Japanese semantic/syntactic feature providers and constraints | Japanese Language Set |
| Japanese normalization hypotheses, script conversion and surface realization | Japanese Language Set |
| Japanese orthography, okurigana, inflection realization, punctuation and spacing | Japanese Language Set |
| Japanese number/date/time/unit/currency formatting and transliteration | Japanese Language Set |
| Generic normalization hypothesis/decision lifecycle, provenance, ambiguity, alignment and commit | SURGE Core |
| C/Python compatibility, input/output buffers, application I/O | kana-decoder Host |

No Japanese-specific normalizer or renderer is placed in generic Core. The generic Core owns the contracts and decision lifecycle; the Japanese Language Set owns Japanese rules and realization algorithms. This distinction allows a different language set to provide different scripts, grammar and orthography without changing Core.

## 3. End-to-end execution

Input Kana Stream
-> SURGE Stream Ingress
-> selected Language Set
-> candidate lattice / lexicon / morphology / n-gram / syntax / semantic evidence
-> SURGE Core ingress and Cluster classification/retention
-> replaceable Language Set candidate/evidence providers and surface renderer
-> SURGE UnifiedDecision (interpretation + surface hypotheses in one transaction)
-> Core validation, source/output span alignment, temporal reassessment and journal commit
-> kana-decoder output transport

Recognition and normalization are not separate decision engines. SURGE exposes one
UnifiedDecision call and one decision trace/commit boundary:
- The interpretation phase ranks lexical/semantic candidate paths.
- The surface phase ranks original and Language Set-rendered realizations for the selected path.
- Both phases share one Core policy, provenance model, budget, disposition and transaction.
- If surface selection defers or rejects, the unified decision returns no partial selected surfaces.
- Rendering cannot silently replace the semantic or lexical interpretation. A rendering conflict returns to the Core as evidence and can trigger Question/Interest/Focus reassessment.

The phases remain separately inspectable in the unified trace, but there is no
independent RecognitionDecision or NormalizationDecision API or host-side selector.

## 4. Clustered input retention

All incoming information is retained in SURGE's internal cluster graph rather
than treated as disposable decoder-local scores. The input stream is anchored
under a context cluster. SURGE classifies and retains candidate interpretations
in typed interpretation clusters and source/rendered surface hypotheses in
surface-realization clusters. Cluster membership is explicit through graph
Containment relations; Claims, Evidence, Entities, provenance, validity and
contradiction relations remain queryable after the input event. Unresolved
alternatives are retained in Chaos Cluster and can be revisited through
Question/Interest/Focus and temporal reassessment. Cluster classification and
retention are Core responsibilities, not a parallel kana-decoder memory.

## 5. Normalization coverage

The Japanese Language Set must own:
- kanji/kana/katakana/Latin/digit/symbol/punctuation candidate generation;
- orthographic variants, okurigana, inflection and conjugation realization;
- word spacing and punctuation policy;
- Arabic numerals, kanji numerals, decimals, ranges, dates, times, units and currencies;
- abbreviations, transliteration and script conversion;
- locale/style-specific rendering rules;
- output surface validation and deterministic realization.

The SURGE Core must own:
- NormalizationHypothesis identity and lifecycle;
- UnifiedDecision combining interpretation and surface selection;
- evidence provenance, validity and deduplication;
- context-conditioned selection and defer/reassess;
- ambiguity retention in Chaos Cluster;
- source-to-output span alignment;
- revision and final commit authority;
- resource caps and deterministic fallback.

## 6. Migration sources

Move the following application-owned elements into SURGE Runtime or Japanese Language Set:
- src/surge_language.cpp and its public header;
- lexicon trie and dictionary lookup;
- morphology decoder and candidate lattice providers;
- local scoring and word/POS n-gram providers;
- connection matrix and correction rules;
- Japanese number/date/time/unit parsers;
- Japanese orthographic conversion, punctuation, spacing and rendering;
- any normalization adapter, postprocessor or output selector;
- pack builder/loader and Japanese-specific pack resources.

Keep in kana-decoder only:
- C ABI and Python API compatibility facade;
- input event transport and output buffer transport;
- application integration;
- corpus preparation and evaluation/benchmark harnesses;
- compatibility conversion commands during migration, with no parallel production runtime.

## 7. Pack replacement and session guarantees

A session pins Language Set ID, version, digest, ABI and normalization policy. A pack replacement is activated only at a safe session boundary. Historical claims/evidence retain their original pack provenance. Reprocessing under another pack creates new evidence and explicit revisions; it never rewrites historical records.

A data-only Language Set must be replaceable without rebuilding kana-decoder or SURGE Core. Native modules, if needed, must use the stable SURGE Language Runtime ABI. The default deployment remains offline, CPU-only and bounded by explicit memory, candidate, lattice, output and per-event time limits.

## 8. Required tests

- Japanese pack conversion preserves all dictionary, morphology, connection, n-gram and normalization resources.
- Kanji/kana/katakana/Latin/digit/symbol/punctuation selection is context-sensitive and traceable.
- UnifiedDecision chooses interpretation and surface realization in one transaction; phase-level traces remain inspectable.
- Every input candidate is assigned to a typed SURGE Cluster and retained with evidence/provenance; no host-local shadow memory is used.
- Renderer conflicts return to Core and can trigger reassessment.
- Source/output alignment remains valid after streaming revisions and journal replay.
- Identical input, pack digest and policy produce deterministic output.
- Host has no production-side dictionary, morphology, n-gram, Japanese normalizer or renderer.
- A second language set with a different script/grammar can be installed and used without rebuilding the host.
- Recognition and normalization decisions both respect resource caps and fail without partial journal commits.
- Existing C/Python APIs remain compatible.
- CER, sentence exact match, RTF, peak RSS, pack size, load time and switching cost are measured against the current baseline.

## 9. Completion criteria

Language and Normalization Adapter migration is complete only when all language-specific recognition and surface realization execute through SURGE Language Runtime and a replaceable Language Set; Core owns both recognition and normalization decisions; kana-decoder is a transport/compatibility/evaluation host; and cross-language pack replacement passes executable conformance tests.

Documentation alone does not constitute implementation completion.
