# kana-decoder 仕様書 v0.9

- Status: Implementation milestone specification
- Version: 0.9.0
- Core license: MIT
- Runtime: CPU only / fully offline
- Third-party data: source-pinned, checksum-verified, per-component license inheritance

## 1. Scope

v0.9 specifies reproducible import of SudachiDict Small V1 source data and conversion of its connection matrix into kana-decoder's normalized Pack inputs. It does not assert that a production dictionary has been bundled or that production accuracy targets have been met.

## 2. Source pinning

The initial reproducible source candidate is SudachiDict Small 20260723, V1:
- small_lex.zip SHA-256: b578ac9545899783d5d7e30d5d78d5d9dcf40b36965d4af0663ec2eb041c1093
- matrix.def.zip SHA-256: 3ccaed582481b31be170c7200cf1cb2856f2d14c2fa601b3692d97dbad5a3bd1

The lock is stored in licenses/sudachidict-small-20260723-source-lock.json. The published hashes are reported by the Lindera raw-source mirror. Before a production Pack is released, the downloaded artifact bytes, official release metadata, upstream legal notices, and source provenance must be independently verified. The source archives are intentionally not committed.

## 3. V1 lexicon mapping

The importer accepts the official 18-column V1 CSV header:
SURFACE, LEFT_ID, RIGHT_ID, COST, WRITING, POS1..POS6, READING_FORM, NORMALIZED_FORM, DICTIONARY_FORM, MODE, SPLIT_A, SPLIT_B, WORD_STRUCTURE.

Mapping:
- READING_FORM -> hiragana reading (Katakana converted by code-point offset).
- WRITING -> orthographic surface; SURFACE is retained as fallback when WRITING is empty.
- DICTIONARY_FORM, then NORMALIZED_FORM, then surface -> lemma fallback.
- POS1..POS6 -> slash-separated POS string.
- POS5/POS6 -> inflection type/form.
- COST -> word cost.
- LEFT_ID/RIGHT_ID -> left/right connection class IDs.
- Missing reading and empty-reading markers are skipped.
- Malformed column counts, numeric values and int32 overflow are rejected with source line numbers.

The V0 importer remains a distinct format; V0 and V1 are never guessed from row width or silently interchanged.

## 4. Connection matrix

The source matrix begins with left_size and right_size. Each following row is right_id, left_id, cost. The importer validates dimensions, index ranges, signed int32 costs and duplicate cells. Zero-cost cells are omitted from the normalized sparse TSV because ConnectionModel defines absent cells as zero.

The current unordered-map runtime representation is functionally compatible with this sparse TSV but is not considered memory-optimal for a dense morphological matrix. Compact binary/mmap representation and target RSS measurement remain required before production packaging.

## 5. Secure source archive handling

Source artifacts are verified by SHA-256 before extraction. Extraction rejects absolute paths, parent traversal, Windows path separators/drive prefixes, symlinks, duplicate target members and oversized members. Only the requested small_lex.csv and matrix.def members are extracted. The tool records a source-lock result file.

## 6. Acceptance criteria

- Strict current Sudachi V1 CSV header mapping is covered by fixture tests.
- Connection matrix dimension and cell validation is covered by tests.
- Zero-cost cells are omitted without changing ConnectionModel lookup semantics.
- Wrong source hashes and unsafe ZIP members are rejected.
- Existing Core, C API, Pack builder, license and corpus tests remain passing.
- No claim of production accuracy, 30 MiB installed size, or target-device RSS is made until the real source Pack is built and measured.

## 7. Remaining production gates

1. Download and verify exact raw source archives against the lock.
2. Verify source license texts and notices for the precise pinned release.
3. Build the normalized lexicon and sparse matrix, then run the Pack builder with a complete component license profile.
4. Measure installed Pack size, runtime RSS, load time, candidate coverage and unknown-word rate.
5. Split real corpora by source document/speaker/session into train/dev/test; do not tune on test.
6. Add POS mapping, connection compatibility audit and error-class reporting.
7. Replace unordered-map matrix storage with a bounded compact representation if RSS/size is unacceptable.
