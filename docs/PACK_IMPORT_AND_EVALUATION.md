# Language Pack Import and Evaluation

## Purpose
This workflow turns a license-cleared, normalized lexicon into an offline Language Pack and provides reproducible corpus measurements. It does not bundle third-party dictionaries or grant redistribution rights.

## Normalized dictionary input
UTF-8 TSV, one candidate per row:
1. reading
2. surface
3. lemma
4. POS label
5. reading group
6. candidate ID (optional)
7. word cost (optional signed int32)
8. candidate connection cost (optional signed int32)
9. left connection class ID (optional non-negative int32)
10. right connection class ID (optional non-negative int32)
11. inflection / conjugation type (optional)
12. inflection / conjugation form (optional)

Five-column inputs are accepted. Optional fields are padded with defaults. Duplicate complete rows are removed and output is sorted deterministically. Reading and surface must be non-empty. A language-specific upstream importer must normalize the source dictionary's own schema into this contract; this generic tool does not parse arbitrary Sudachi/UniDic/MeCab database formats.

## Japanese source dictionary adapters

`tools/import_japanese_dictionary.py` converts licensed CSV exports into the normalized TSV above:
- `--format sudachi-csv`: strict 17-column Sudachi source CSV mapping. Uses reading_form, dictionary_form, POS1-4, conjugation type/form, source word cost, and left/right connection IDs.
- `--format ipadic-csv`: strict 13-column IPADIC-compatible MeCab CSV mapping. Uses reading, base form, POS1-4, conjugation type/form, word cost, and connection IDs.
- `--format header-csv`: named-column CSV requiring surface, reading, pos, word_cost, left_id, and right_id; optional lemma, candidate_id, reading_group, inflection_type and inflection_form.

Example:

    python3 tools/import_japanese_dictionary.py \
      --input licensed-dictionary.csv \
      --format sudachi-csv \
      --output normalized-lexicon.tsv

The adapter converts Katakana readings to Hiragana, skips rows without a usable reading (e.g. `*`), emits deterministic candidate IDs when none are supplied, and preserves connection class IDs and inflection metadata into decoded tokens. It does not import the source connection matrix; provide a compatible normalized `connections.tsv` separately. Sudachi/UniDic/IPADIC releases have differing schemas and licensing terms. Confirm the exact source version and export layout before import. UniDic distributions are not assumed to be IPADIC-compatible; use header-csv with an explicit, audited field mapping for a UniDic export. No third-party dictionary data is included in this repository.

Then build and verify the Pack using the existing builder. The optional `--correction-rules` argument includes a five-column correction rule TSV; the native loader validates and loads at most 256 rules. The builder's SPDX and upstream fields are declarations only, not a license compliance check.

## Training sequence input
One tokenized utterance per line. Candidate IDs are separated by TAB. Every ID must exist in the imported lexicon. The builder emits:
- U: unigram counts
- L: previous-candidate to current-candidate counts
- R: next-candidate context to current-candidate counts

The model uses count-based additive smoothing and bounded two-token contexts. These are ranking scores, not calibrated probabilities. The initial trainer is a transparent bigram trainer, not a neural language model or full Kneser-Ney implementation.

## Build a pack
Example (only with a dictionary whose license permits this use and redistribution):

    python3 tools/build_language_pack.py \
      --lexicon normalized-lexicon.tsv \
      --sequences tokenized-training.tsv \
      --connections connections.tsv \
      --license-file THIRD_PARTY_LICENSE.txt \
      --license-spdx "MIT" \
      --upstream "upstream project and exact version" \
      --pack-id ja-domain-pack \
      --language-id ja-JP \
      --dictionary-version "source-version" \
      --output ./dist/ja-domain-pack

The output directory must be empty. The tool copies the supplied license text, writes deterministic lexicon/n-gram data and generates a manifest containing file sizes and SHA-256 hashes. It never downloads data. Review the upstream license, notices, attribution, modification and redistribution obligations yourself; the SPDX identifier is metadata, not a legal determination.

Validate the resulting pack before use:

    python3 tools/verify_pack.py ./dist/ja-domain-pack

The Python verifier and C++ runtime loader both check ABI/format metadata, license provenance, relative paths, symlinks, unlisted payloads, file sizes and SHA-256 hashes before Pack use. The native verifier accepts the repository's current builder Manifest schema (not arbitrary JSON extensions), with 1 MiB manifest, 64-file and 30 MiB expanded-payload ceilings. Run the Python verifier as an additional preflight check.

## Component-level license profiles

A production Pack with third-party data should be built with a component profile. Each source license is copied into the Pack, and the normalized profile plus notices are included in the manifest checksum list.

Input profile example (paths are relative to the profile JSON file):

```json
{
  "schema_version": 1,
  "components": [
    {
      "id": "sudachidict",
      "version": "v20261015",
      "spdx": "Apache-2.0",
      "commercial_use": true,
      "redistribution": true,
      "modified_redistribution": true,
      "upstream_url": "https://github.com/WorksApplications/SudachiDict",
      "license_file": "notices/LICENSE-2.0.txt"
    },
    {
      "id": "unidic-small",
      "version": "2011-2013",
      "spdx": "BSD-3-Clause",
      "commercial_use": true,
      "redistribution": true,
      "modified_redistribution": true,
      "upstream_url": "https://clrd.ninjal.ac.jp/unidic/",
      "license_file": "notices/UNIDIC-BSD.txt"
    }
  ]
}
```

Build with `--license-profile license-input.json` and a combined SPDX expression such as `--license-spdx "Apache-2.0 AND BSD-3-Clause"`. The builder copies each component's license file into `THIRD_PARTY_LICENSES/`, writes `license-profile.json`, and hashes both in `manifest.json`. It rejects profile files and notice paths outside the profile directory.

Run `python3 tools/validate_license_profile.py PACK_DIR` after building. The validator requires exact pinned versions, a reviewed permissive SPDX identifier, HTTPS upstream URLs, explicit commercial-use / redistribution / modified-redistribution permissions, and present notice files. The validator checks declarations and packaging only; a human must verify those declarations against the exact upstream artifact.

See `docs/LICENSING_POLICY.md` and `docs/THIRD_PARTY_COMPONENTS.md` for source-selection decisions. The profile is not a legal opinion.

## Evaluation corpus
UTF-8 TSV with exactly two columns:
- reading
- expected final surface string

Run:

    ./build/kana_decoder_eval ./packs/ja-lite tests/data/ja-smoke-gold.tsv

The JSON result includes overall and per-domain case count, exact-match accuracy, total edit distance, reference Unicode code-point count, CER and decode failures. Two-column legacy corpora are grouped under `unspecified`. CER is computed over Unicode code points, not UTF-8 bytes. The smoke corpus only checks wiring and regressions; it is not a representative quality benchmark.

For meaningful evaluation, keep train/dev/test corpora separate by source document or speaker/session to prevent leakage. Report exact-match accuracy and CER by domain, sentence length, unknown-word rate, ambiguity class and script type. Tune costs and thresholds on development data only, then freeze them before test evaluation. Include confidence intervals or bootstrap resampling for small test sets. Report p50/p95/p99 latency and peak RSS separately from accuracy.

## Current boundaries
- No production Japanese dictionary has been imported. The repository's ja-lite data remains an original minimal fixture.
- The generic TSV adapter is a normalized interchange format, not a parser for arbitrary dictionary distributions.
- POS label mapping and connection-class mapping must be supplied by a language-specific adapter.
- Corpus sequence training is a simple count model. Real corpus preparation, tokenization, calibration and held-out evaluation are still required.
- CER and exact accuracy are measured by the new evaluation CLI; per-token confusion matrices, confidence calibration, bootstrap confidence intervals and unknown-word breakdown are future work.
