# kana-decoder 仕様書 v0.16

- Version: 0.16.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: complete functional Japanese conversion before optional size optimization. 30 MiB is not a development hard limit.

## 1. Large-source import memory behavior

The Sudachi V1 source validation and import pipeline must not retain the complete lexicon or connection matrix in Python lists, sets, or in-memory sort buffers.

- Matrix validation stores packed (right_id << 32) | left_id keys in a temporary SQLite database with a primary-key constraint. Duplicate cells are rejected, including zero-cost cells.
- Lexicon import stores normalized rows in a temporary SQLite database with a unique constraint over all emitted fields. Duplicate normalized rows are ignored deterministically.
- Matrix import stores cells in a temporary SQLite database, rejects duplicate coordinates, and emits nonzero cells in ascending packed-key order.
- Lexicon output is emitted in deterministic order: kana, numeric word cost, candidate ID, surface.
- SQLite cache is limited to 8 MiB; temporary database files are removed when the import or validation process exits.
- Temporary disk capacity must be sufficient for the imported source. The implementation does not claim a fixed peak RSS until measured on the production source archive.

## 2. Header compatibility and diagnostics

- Header normalization is case-insensitive and ignores underscores, as specified by Sudachi V1.
- Duplicate normalized headers are rejected to prevent ambiguous field selection.
- Diagnostic errors include the raw header and normalized duplicate names, allowing source-release drift to be identified without hiding the original validator error.
- Optional columns beyond the recognized V1 fields may be ignored by the importer; required V1 columns must be present.

## 3. Determinism and integrity

- Repeated imports of the same source and tool version must produce byte-identical normalized TSV files.
- Duplicate matrix coordinates are rejected before zero-cost cells are removed from the sparse output.
- Temporary SQLite databases are build-time working files only and are not included in the Language Pack.
- Pack source lock, archive SHA-256 checks, provenance, license-profile validation and native Pack resource limits remain mandatory.

## 4. Validation status

Synthetic regression tests cover disk-backed duplicate detection, 5,000-row validation, deterministic lexicon import and sorted sparse matrix output. The real Sudachi Small source archive has been downloaded and SHA-256 verified, but the production Pack is not yet built: the previous real-source run exposed a duplicate normalized CSV header. The exact raw header is not yet captured. A manual workflow rerun on the updated diagnostic code is required before claiming source compatibility or production Pack success.
