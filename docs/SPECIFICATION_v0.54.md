# kana-decoder: Japanese inflection-chain conformance v0.54

Version: 0.54.0
Parent: SPECIFICATION_v0.53.md

## 1. Goal

Verify that dictionary-supplied inflected surfaces survive multi-token morphological paths and final Japanese rendering without suffix guessing or okurigana mutation.

## 2. Conformance cases

- `まわっ + て` produces `回って`, preserving the dictionary-provided sound-change form and connective particle.
- `食べて + いる` produces `食べている`, preserving the inflected verb surface and auxiliary token.
- The decoder must cover the complete reading with contiguous token spans.
- The renderer must concatenate the selected dictionary surfaces exactly, subject only to separately configured punctuation/spacing/number policies.
- The selected path is still passed through SURGE UnifiedDecision; these tests validate realization of the chosen path, not an independent renderer-side lexical choice.

## 3. Verification boundaries

These are deterministic fixture-level regression tests. They do not establish broad conjugation coverage or candidate recall. Production qualification still requires a licensed full dictionary, held-out corpora and measured Candidate Recall@N for conjugated forms, auxiliaries, particles, contractions and sound changes.
