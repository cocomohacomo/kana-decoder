# Language Pack Import and Evaluation

## Purpose
This workflow turns license-cleared normalized lexicons into offline Language Packs and provides reproducible corpus measurements. It does not bundle third-party dictionaries or grant redistribution rights.

## Sudachi V1 source import
The lock file is authoritative for archive checksums and member names. Supply locally obtained ZIP files:

    python3 tools/verify_sudachi_source.py --small-zip ./small_lex.zip --matrix-zip ./matrix.def.zip --output ./work/sudachi-small

The verifier reads licenses/sudachidict-small-20260723-source-lock.json, verifies local files byte-for-byte, safely extracts small_lex.csv and matrix.def, and writes a verification receipt. It does not download files. A published checksum is only a pin until actual archive bytes are verified.

Import:

    python3 tools/import_sudachi_v1.py lexicon --input ./work/sudachi-small/small_lex.csv --output ./work/normalized/lexicon.tsv
    python3 tools/import_sudachi_v1.py matrix --input ./work/sudachi-small/matrix.def --output ./work/normalized/connections.tsv

V1 header matching is case-insensitive and ignores underscores. Additional columns are ignored and counted. Missing required fields, duplicate normalized names, malformed row widths and numeric overflow are rejected. V0 and V1 use separate adapters.

## Build a Pack
The Pack builder accepts a configurable expanded-byte budget. The former 30 MiB target is provisional and must not block feature development or a complete dictionary import. Record actual size, load time and peak RSS. Optimize after quality and runtime measurements identify a bottleneck.

## Evaluation corpus and leakage guard
Each train/dev/test TSV has exactly four columns:

    reading<TAB>expected_surface<TAB>domain<TAB>group_id

group_id identifies a source document, speaker/session, or other leakage boundary. Validate:

    python3 tools/validate_corpus_splits.py --train corpus/train.tsv --dev corpus/dev.tsv --test corpus/test.tsv --output corpus/split-report.json

The validator rejects empty splits, malformed rows, empty fields and group IDs shared across train/dev/test. It reports counts and domain distributions. This does not replace human annotation review or representativeness checks. Never train on dev/test. Tune on dev only and freeze before test.

## Current boundaries
- No production Sudachi archive has been locally verified or imported into this repository.
- The reference ja-lite Pack is a smoke fixture, not a representative dictionary.
- POS mapping, connection matrix compatibility and real held-out accuracy still require validation.


## Reproducible Sudachi source acquisition and compatibility checks

Fetch the two pinned source archives using the allowlisted HTTPS downloader:

    python3 tools/fetch_sudachi_source.py --output-dir ./work/sudachi-archives

The downloader reads the repository source lock, rejects unexpected URL hosts and redirects, enforces a 2 GiB per-archive safety cap, verifies SHA-256, and only then atomically publishes each file. It is a development-time acquisition tool; the runtime library remains offline.

Extract the verified members:

    python3 tools/verify_sudachi_source.py --small-zip ./work/sudachi-archives/small_lex.zip --matrix-zip ./work/sudachi-archives/matrix.def.zip --output ./work/sudachi-source

Before normalization, validate that every lexicon context ID fits the connection matrix:

    python3 tools/validate_sudachi_pack_inputs.py --lexicon ./work/sudachi-source/small_lex.csv --matrix ./work/sudachi-source/matrix.def --output ./work/sudachi-source/compatibility.json

Then import V1 lexicon and matrix with \`tools/import_sudachi_v1.py\`. Do not call the resulting files a production Pack until the release-specific LEGAL/NOTICE texts are verified and included in the generated Pack.
