# kana-decoder 仕様書 v0.13

- Version: 0.13.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: functional Japanese conversion and reproducible real-dictionary integration; 30 MiB is a provisional optimization target, not a build-time cap.

## 1. Sparse connection matrix

The Sudachi/UniDic matrix is represented as a sorted sparse sequence of packed (right_id, left_id) keys and signed 32-bit costs. This replaces the former string-keyed hash map and avoids per-cell string allocation and hash-table node overhead.

The normalized connections.tsv begins with:
- # matrix <left_size> <right_size>
- a descriptive column header;
- sorted nonzero rows: right_id TAB left_id TAB cost.

Zero-cost cells are omitted and resolve to zero. The C++ loader:
- reads optional dimension metadata for backward compatibility with older fixtures;
- validates nonnegative IDs and declared bounds;
- rejects malformed rows and duplicate coordinates;
- sorts entries before publishing the model;
- swaps the fully validated model into place only after successful parsing.

The runtime performs binary search over sorted entries. The model is immutable during decode. Matrix dimensions are exposed for diagnostics.

## 2. Source compatibility validation

tools/validate_sudachi_pack_inputs.py scans the complete matrix.def, validates every row's arity, integer ranges, matrix bounds and duplicate coordinates, then checks every lexicon LEFT_ID/RIGHT_ID against the matrix dimensions. It rejects malformed CSV widths and duplicate normalized header names.

tools/import_sudachi_v1.py matrix emits the dimension header and sorted sparse output. It checks all source cells, rejects duplicates, and omits zero-cost entries only after duplicate validation.

## 3. One-command Pack build

After downloading the pinned archives with tools/fetch_sudachi_source.py, run:

    python3 tools/build_sudachi_pack.py \
      --small-zip ./work/archives/small_lex.zip \
      --matrix-zip ./work/archives/matrix.def.zip \
      --work-dir ./work/build \
      --output ./dist/ja-jp-sudachi-small

The orchestrator:
1. verifies source archive SHA-256 values against the source lock;
2. safely extracts only the expected source members;
3. validates every matrix cell and every lexicon context ID;
4. imports the V1 lexicon and connection matrix;
5. creates a provenance record containing source-lock, validation, importer results and tool hashes;
6. builds the Pack with the release-specific license profile and complete upstream LEGAL notice;
7. verifies the final Pack manifest and all payload hashes.

The tool accepts optional training sequences, correction rules, a separate work directory and a configurable expanded Pack budget. The default budget is 8 GiB. Runtime remains fully offline; the downloader is a development/build tool only.

## 4. Licensing and provenance

The source lock pins SudachiDict Small release 20260723 V1 and its UniDic 2.1.2 connection matrix. The upstream LEGAL notice for tag v20260723 and the exact upstream Apache-2.0 license text are preserved in the repository.

The source notice says the distribution is covered by Apache-2.0 and identifies both small_lex.csv and matrix.def.zip as UniDic-derived and subject to the UniDic BSD conditions. The release profile therefore records four distinct artifact/license relationships: Small lexicon + Apache-2.0, Small lexicon + BSD-3-Clause, matrix + Apache-2.0, and matrix + BSD-3-Clause. The Pack's combined data license expression is Apache-2.0 AND BSD-3-Clause. Core source code remains MIT. The profile is provenance/compliance metadata, not a legal opinion.

Core/Full dictionaries remain excluded because their additional NEologd-derived vocabulary has separate source datasets and conditions.

## 5. Validation and remaining release gates

Synthetic end-to-end tests verify the complete build orchestration, Pack manifest, license profile, provenance payload and legal notice inclusion. C++ tests cover matrix dimensions, out-of-range cells, duplicate cells, zero-cost updates and lookup.

The production Sudachi archive bytes have not yet been downloaded and verified in this repository. Therefore no production Pack is claimed. Remaining gates:
- obtain the exact official archives and confirm their pinned hashes;
- review all release-specific notices and resulting Pack contents;
- build and measure the real Pack;
- run held-out corpus accuracy and ambiguity evaluations;
- measure target CPU latency, peak RSS and streaming stability;
- optimize matrix/lexicon storage only after measuring real data.
