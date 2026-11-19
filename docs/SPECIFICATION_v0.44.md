# kana-decoder: Immutable Language Pack Runtime v0.44

Version: 0.44.0
Parent: SPECIFICATION_v0.43.md

## 1. Product objective

The product converts pure kana input into natural Japanese text containing context-appropriate kanji, hiragana, katakana, Latin text, numerals, symbols and punctuation. SURGE is the shared cognition and decision core serving this conversion goal. Language-pack infrastructure is valuable only insofar as it improves normalization quality, replaceability, streaming behavior and edge-device resource use.

## 2. Stage objective

Establish an immutable, replaceable Language Pack runtime boundary without breaking the existing C/Python compatibility APIs or the current Japanese decoding path.

This stage is an incremental migration. It does not claim that all Japanese recognition, morphology, rendering or correction logic has already moved out of kana-decoder.

## 3. Language Pack contract

The internal C++ contract is `kana_decoder::LanguagePack`:

- immutable identity: pack ID, language tag, version and manifest digest;
- read-only lexicon provider;
- read-only n-gram scorer;
- read-only connection model;
- correction policy/resources;
- language adapter for candidate interpretation and entity realization.

The contract is deliberately read-only after loading. A decoder engine owns a `shared_ptr<const LanguagePack>`; each streaming session pins the same snapshot. Session creation therefore shares the large dictionary and model resources rather than copying them.

The manifest digest is SHA-256 over the exact manifest bytes. The manifest's existing payload hashes continue to validate the individual pack resources before the snapshot is published.

## 4. Session and replacement semantics

- A pack is fully verified and loaded before it becomes visible to the engine.
- An engine binds one immutable pack snapshot.
- A streaming session captures that exact shared snapshot at creation and continues using it even if the owning engine is later destroyed.
- The current C API rejects a second pack load into an already-loaded engine. This prevents accidental mid-session mutation and cross-pack contamination while the compatibility API has no explicit safe-boundary replacement operation.
- A future replacement API must activate a new snapshot only at an explicit session boundary and create a new SURGE context or an explicit provenance-preserving revision. It must not silently rewrite historical evidence.

## 5. Language adapter ownership

The SURGE normalization runtime no longer parses Japanese number readings directly. It delegates numeric entity creation and rendering to the adapter pinned by the active Language Pack.

Japanese numeric parsing and rendering remain implemented by `JapaneseLanguageAdapter` in this migration stage. The generic runtime knows only the adapter contract and the generic Entity/NumericStyle data model.

## 6. Host responsibilities

The C API remains a compatibility facade. It holds a shared immutable pack reference and passes its providers into the existing decoder. It does not copy lexicon, n-gram or connection resources into every streaming session.

The following are still migration work and must not be considered complete:

- moving lattice construction and morphology decoding behind the Language Pack provider contract;
- moving all candidate scoring and POS/ngram evidence generation into the language pack;
- moving orthographic variants, kana/kanji/katakana/Latin selection, punctuation, spacing, okurigana and inflection realization behind the adapter;
- providing a stable cross-language runtime ABI and a second independently implemented language pack;
- eliminating remaining language-specific code from the host-side decoder implementation.

## 7. Resource and correctness requirements

- Pack verification must complete before publication.
- The pack snapshot is immutable after publication.
- Streaming sessions share pack resources; they do not deep-copy the lexicon, scorer or connection matrix.
- Pack identity and digest are available for provenance and benchmark reporting.
- Existing per-decode input, lattice, candidate, expansion, journal and retrieval ceilings remain in force.
- Failed pack loading must leave the engine's previous state unchanged.
- Existing C/Python API behavior remains compatible, except that reloading a pack into an already-loaded engine is rejected rather than silently changing its pinned runtime.

## 8. Conformance tests

- Verify the Japanese Pack manifest and resource hashes before snapshot creation.
- Assert pack ID, language tag, version and 64-character manifest digest.
- Assert the adapter language tag matches the Pack language tag.
- Decode a Japanese fixture using providers obtained only through the Language Pack interface.
- Create multiple references to one snapshot and verify shared object identity.
- Verify Japanese numeric entity creation and realization through the adapter interface.
- Verify C API one-time pack binding and streaming session operation after engine destruction.
- Build both static and shared libraries with the new runtime source.
- Measure actual peak RSS and load-time effects on production-size dictionaries; small fixture tests do not establish production memory performance.

## 9. Completion status

This stage establishes the immutable Pack snapshot and session-pinning boundary. It is not completion of SPECIFICATION_v0.43 migration. A generic external ABI, non-Japanese pack implementation, full candidate-provider migration and complete surface renderer migration remain required before the Language Set architecture can be considered complete.
