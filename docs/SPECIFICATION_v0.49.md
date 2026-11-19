# kana-decoder: Provider-neutral Language Pack decode policy v0.49

Version: 0.49.0
Parent: SPECIFICATION_v0.48.md

## 1. Objective

Move the host-to-provider decode contract away from Japanese implementation tuning types. A generic host should express bounded execution requirements without constructing or depending on Japanese morphology, dictionary, n-gram, POS, or connection-model objects.

## 2. LanguageDecodeOptions

The Language Pack contract now accepts `LanguageDecodeOptions`, containing only provider-neutral execution budgets:

- maximum returned paths;
- beam width;
- maximum input code points;
- maximum lattice edges;
- maximum retained lattice candidates;
- maximum expanded search states.

The Japanese provider translates these values into its private `DecoderOptions`. Scoring models, morphological policy, Japanese correction rules and language-specific resources remain owned by the provider.

## 3. Compatibility

A transitional C++ overload accepts the former `DecoderOptions` and forwards only the resource/path limits represented by `LanguageDecodeOptions`. This preserves current C++ callers while they migrate. Language-specific scoring pointers and policy switches are deliberately not forwarded through the generic Pack contract.

New host and conformance-test code must use `LanguageDecodeOptions`. The compatibility overload is temporary and must be removed after C API, tests and evaluation tools stop using Japanese implementation types.

The C ABI is unchanged.

## 4. Streaming

The Pack streaming provider accepts the same provider-neutral options and decodes the complete accumulated reading before passing paths to the generic `StreamingSession`. The session continues to enforce its own reading ceiling and the per-decode input ceiling transactionally.

## 5. Verification status

Conformance coverage is added for path-count limits, input ceilings, invalid UTF-8, compatibility forwarding, chunk accumulation, session isolation and transactional rejection.

The source change is committed to the kana-decoder development branch. Static/shared build, CTest, Python integration, golden-corpus parity, CER, latency and Peak RSS remain unverified until the corresponding CI and benchmark runs complete.

## 6. Remaining practical-runtime work

- Remove the transitional `DecoderOptions` overload and Japanese implementation declarations from the generic public header.
- Move complete Japanese rendering policy into the Language Set: orthography, okurigana, inflection, punctuation, spacing, numerals, dates, times, units and symbols.
- Ensure every selected surface is tied to its source interpretation Claim and that SURGE owns the single final decision.
- Add representative held-out corpora and candidate-recall measurements.
- Measure on the target edge PC and enforce explicit RSS and p95 latency budgets.
- Verify offline packaging and a clean-install smoke test.

The separate SURGE repository is outside this change.
