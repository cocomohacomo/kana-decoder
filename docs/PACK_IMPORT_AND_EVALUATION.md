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


## v0.13: verified end-to-end Sudachi Small Pack build

The matrix importer now writes a dimension header and sorted sparse nonzero cells. The C++ connection model uses sorted packed-coordinate entries rather than a string-keyed hash map. Its loader checks matrix dimensions and rejects malformed, duplicate, or out-of-range cells.

First download and verify the pinned archives:

    python3 tools/fetch_sudachi_source.py --output-dir ./work/archives

Then build and verify the complete Pack in one command:

    python3 tools/build_sudachi_pack.py \
      --small-zip ./work/archives/small_lex.zip \
      --matrix-zip ./work/archives/matrix.def.zip \
      --work-dir ./work/build \
      --output ./dist/ja-jp-sudachi-small

The orchestrator verifies both archive hashes, extracts only the expected members, validates all matrix rows and lexicon context IDs, imports the V1 dictionary and matrix, builds the Pack, and runs the final manifest/hash verifier. The generated Pack includes source-lock and tool provenance, the complete upstream LEGAL notice, full Apache-2.0 and BSD-3-Clause license texts, and a component-level license profile.

The source lock records the upstream LEGAL notice's Apache-2.0 distribution license and UniDic BSD-3-Clause conditions for both the Small lexicon and the matrix. The Pack data license expression is therefore Apache-2.0 AND BSD-3-Clause; the repository's original Core code remains MIT.

No production dictionary Pack is claimed until the real source archives are downloaded and their pinned checksums verified. The 30 MiB size is only a provisional optimization target. The build's default declared expanded-size budget is 8 GiB.


## Build the real Sudachi Pack through GitHub Actions

The repository also provides a manual workflow at .github/workflows/sudachi-pack.yml. In GitHub, open Actions, select Build Sudachi Small Pack, choose the expanded Pack budget (default 8 GiB), and run the workflow. It downloads the pinned archives, verifies both SHA-256 values, builds and verifies the Pack, then uploads a 30-day Actions artifact. The Pack binary is not committed to the source repository.

This workflow has been added but has not yet been manually dispatched in this development session. Its success must be confirmed by a completed workflow run and inspection of the uploaded artifact before treating the real dictionary Pack as available.
