# kana-decoder: Japanese Language Set build isolation v0.45

Version: 0.45.0
Parent: SPECIFICATION_v0.44.md

## 1. Product objective

Convert pure kana input into natural Japanese text containing context-appropriate kanji, hiragana, katakana, Latin text, numerals, symbols and punctuation. SURGE remains the sole context cognition and decision authority. Build modularity is subordinate to conversion quality, streaming behavior and bounded edge-device resources.

## 2. Stage objective

Move the Japanese lattice morphology search implementation out of the generic `src/` tree and compile it as a dedicated Japanese Language Set object target. Preserve the existing C++ behavior and expose the implementation only through the existing shared decoder/provider contract.

## 3. Implemented build boundary

- `language_sets/ja-JP/morphology_decoder.cpp` contains the moved `MorphologyDecoder::decode` implementation.
- `language_sets/ja-JP/japanese_language_pack.cpp` remains the Japanese Pack provider implementation.
- CMake builds both files in `kana_decoder_ja_language_set`, an independently addressable OBJECT target.
- The object target has private include paths, the same strict warning policy, and position-independent code enabled so its objects can be linked into both static and shared library targets.
- `kana_decoder_core` and `kana_decoder_shared` consume the same object target, avoiding duplicate source ownership and keeping static/shared behavior aligned.
- The old `src/morphology_decoder.cpp` source path is removed.

## 4. Compatibility and invariants

This is a physical source/build ownership move, not a morphology algorithm rewrite. It must preserve:
- lattice edge and candidate ordering;
- hard ceilings on input length, lattice edges, candidates, beam width, paths, expanded states and trace records;
- transition, word, n-gram, POS and SURGE cluster score calculations;
- deterministic stable sorting and output ordering;
- existing C/Python API behavior.

The Japanese Language Set target is still compiled into the overall kana-decoder binaries. This stage does not yet provide a separately distributed shared module or stable external plugin ABI.

## 5. Verification requirements

- Configure and build `kana_decoder_ja_language_set`.
- Build both `kana_decoder_core` and `kana_decoder_shared`.
- Run all CTest and Python integration tests.
- Compare golden-corpus output before and after the move; expected output must be byte-identical.
- Run the bounded decoder benchmark and memory-budget test.
- Do not claim quality, latency or RSS improvement from this relocation alone.

## 6. Remaining migration

- Move Japanese lexicon/lattice construction, unknown-token fallback, connection model, n-gram/POS scorers and correction generation behind the Language Set provider.
- Separate generic data contracts from Japanese-specific implementation headers.
- Complete the Language Adapter renderer for orthography, inflection, okurigana, punctuation, numerals, dates, times and units.
- Introduce a stable versioned external ABI and a second independently implemented Language Set.
- Measure held-out CER, sentence exact match, streaming commit quality, CPU, Peak RSS, Pack size and load time.

## 7. Scope boundary

All changes in this stage are limited to the kana-decoder repository and its PR branch. The separate SURGE main repository is not modified.
