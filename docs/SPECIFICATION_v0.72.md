# kana-decoder: Compressed dictionary packs and streaming ingestion v0.72

Version: 0.72.0
Parent: SPECIFICATION_v0.71.md

## 1. Goal

Reduce the installed size of large Japanese language packs without introducing a full uncompressed dictionary copy at runtime. This is a storage and ingestion improvement, not a claim that the in-memory Trie itself has become compact.

## 2. Pack representation

- The language-pack builder writes the normalized lexicon as deterministic gzip data at `lexicon.tsv.gz`.
- The gzip stream uses a fixed timestamp and compression level 9 for reproducible output.
- The manifest records the compression algorithm, payload path, and exact uncompressed byte count.
- Other pack payloads retain their existing representation.
- Existing format-version-1 packs with an uncompressed `lexicon.tsv` remain supported.

## 3. Runtime ingestion

- The loader detects gzip by its magic bytes, not solely by filename.
- Gzip rows are decompressed incrementally with a fixed-size input buffer and passed directly to the existing row parser and Trie builder.
- The complete uncompressed TSV is never materialized as one in-memory string or temporary file.
- The loader retains the existing row validation, UTF-8 validation, candidate semantics, and fail-closed behavior.
- Pack validation checks the compressed payload hash and verifies the gzip stream, CRC, declared expanded byte count, and configured expanded-size ceiling before accepting the Pack.

## 4. Build and deployment

- CMake requires zlib and links it into the native runtime.
- Deployments must include the zlib runtime dependency appropriate to their platform.
- The Pack remains usable offline; no network access is introduced.
- The builder reports compressed bytes, uncompressed bytes, and compression ratio.

## 5. Memory and performance interpretation

Compression reduces installed dictionary bytes and I/O volume. It does not by itself reduce the resident memory required by the current Trie, candidate strings, scoring tables, or indexes. Streaming decompression avoids a second full-size expanded TSV copy, but adds a small decompressor buffer and CPU work.

The 100 MiB peak-RSS objective remains unachieved for the real Japanese dictionary. The next memory work must target in-memory representation: shared string storage, compact candidate records, packed node/edge arenas, and removal or lazy construction of redundant indexes.

No CER, throughput, or RSS improvement is claimed until exact-head CI and real-pack measurements complete.

## 6. Compatibility and safety tests

- Raw TSV packs remain loadable.
- Gzip packs produce the same lattice candidates as equivalent raw TSV.
- Gzip integrity and expanded-size metadata are checked.
- Malformed UTF-8 and malformed rows remain rejected.
- Existing manifest hashes, license verification, and resource ceilings remain enforced.
- SURGE main repository and vendored `third_party/surge` remain unchanged.
- Draft PR #2 remains unmerged pending explicit authorization.
