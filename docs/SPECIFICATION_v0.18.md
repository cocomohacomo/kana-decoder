# kana-decoder 仕様書 v0.18

- Version: 0.18.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: complete functional Japanese conversion before optional size optimization. 30 MiB is not a development hard limit.

## 1. Sudachi V1 source pin

- SudachiDict release 20260723 V1 raw lexicon is fetched only from \`/sudachidict-raw/v1/20260723/\`.
- The unversioned \`/sudachidict-raw/20260723/\` endpoint is V0 raw CSV and is incompatible with the V1 importer.
- Pinned V1 archive SHA-256: \`e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706\`.
- The source fetcher checks exact URL convention and SHA-256 before extraction.

## 2. V1 header and field mapping

- V1 lexicon is named-header CSV; header matching is case-insensitive and ignores underscores.
- \`Index_Form\` maps to the lookup surface and \`Headword\` to the display surface. If Headword is absent or empty, Index_Form is used.
- Required fields: Index_Form, Left_Id, Right_Id, Cost, Reading_Form.
- POS1-POS6 and V1 metadata columns may be present; unknown extra columns are ignored.
- Duplicate normalized headers and multiple aliases mapping to the same canonical field are rejected.
- Headerless V0 CSV is explicitly rejected.
- Reading is converted from katakana to hiragana. Empty and \`*\` readings are skipped.
- Lemma selection order: Dictionary_Form, Normalized_Form, Headword.

## 3. Composition-only entries and context IDs

- Sudachi V1 permits Left_Id or Right_Id to be \`-1\` for words used only as components of split expressions and not as standalone analysis candidates.
- The source validator accepts exactly \`-1\` as this sentinel, reports the number of such non-decodable rows, and continues to reject values below \`-1\` or IDs outside matrix dimensions.
- The normalized kana-decoder lexicon excludes entries where either context ID is \`-1\`. They remain counted in the source validation report and importer skip count, but are not emitted as candidate edges.
- All non-sentinel context IDs must fit the declared matrix dimensions.

## 4. Bounded import and determinism

- Source lexicon and matrix rows are streamed into temporary SQLite storage; no full-source Python list/set/sort buffer is permitted.
- SQLite cache is capped at 8 MiB. Temporary databases are removed when each stage exits.
- Duplicate matrix coordinates are rejected, including zero-cost cells.
- Lexicon duplicates are deterministically removed. Output ordering is stable across repeated imports.

## 5. Regression gates and status

- Tests cover the 23-column V1 header, explicit V0 headerless rejection, exact V1 source URL/hash, valid \`-1\` component-only IDs, invalid lower IDs, and filtering component-only rows from decoder candidates.
- The corrected V1 archive download and SHA-256 verification succeeded in Workflow run 44373076130. That run then stopped at the first \`-1\` context ID because the validator had not yet implemented the documented sentinel.
- Production Pack generation, artifact upload, runtime load, peak RSS and accuracy remain unverified until a subsequent full Workflow succeeds.
