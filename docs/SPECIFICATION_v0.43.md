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
-> SURGE Recognition Core (Claims, Evidence, ContextGraph, Question/Interest/Focus, Dependency, Chaos Cluster)
-> RecognitionDecision
-> NormalizationHypothesis generation
-> SURGE NormalizationDecision
-> Language Set surface renderer
-> Core validation, source/output span alignment and journal commit
-> kana-decoder output transport

Recognition and normalization are related but distinct decisions:
- Recognition determines the lexical/semantic interpretation.
- Normalization determines the appropriate written representation of that interpretation.
- Rendering realizes the selected representation into output characters.
- Rendering cannot silently replace the semantic or lexical interpretation.

The Core may defer either decision independently. A contradiction discovered during rendering returns as a typed conflict and can trigger Question/Focus reassessment.

## 4. Normalization coverage

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
- evidence provenance, validity and deduplication;
- context-conditioned selection and defer/reassess;
- ambiguity retention in Chaos Cluster;
- source-to-output span alignment;
- revision and final commit authority;
- resource caps and deterministic fallback.

## 5. Migration sources

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

## 6. Pack replacement and session guarantees

A session pins Language Set ID, version, digest, ABI and normalization policy. A pack replacement is activated only at a safe session boundary. Historical claims/evidence retain their original pack provenance. Reprocessing under another pack creates new evidence and explicit revisions; it never rewrites historical records.

A data-only Language Set must be replaceable without rebuilding kana-decoder or SURGE Core. Native modules, if needed, must use the stable SURGE Language Runtime ABI. The default deployment remains offline, CPU-only and bounded by explicit memory, candidate, lattice, output and per-event time limits.

## 7. Required tests

- Japanese pack conversion preserves all dictionary, morphology, connection, n-gram and normalization resources.
- Kanji/kana/katakana/Latin/digit/symbol/punctuation selection is context-sensitive and traceable.
- Semantic choice and orthographic choice remain independently inspectable.
- Renderer conflicts return to Core and can trigger reassessment.
- Source/output alignment remains valid after streaming revisions and journal replay.
- Identical input, pack digest and policy produce deterministic output.
- Host has no production-side dictionary, morphology, n-gram, Japanese normalizer or renderer.
- A second language set with a different script/grammar can be installed and used without rebuilding the host.
- Recognition and normalization decisions both respect resource caps and fail without partial journal commits.
- Existing C/Python APIs remain compatible.
- CER, sentence exact match, RTF, peak RSS, pack size, load time and switching cost are measured against the current baseline.

## 8. Completion criteria

Language and Normalization Adapter migration is complete only when all language-specific recognition and surface realization execute through SURGE Language Runtime and a replaceable Language Set; Core owns both recognition and normalization decisions; kana-decoder is a transport/compatibility/evaluation host; and cross-language pack replacement passes executable conformance tests.

Documentation alone does not constitute implementation completion.
