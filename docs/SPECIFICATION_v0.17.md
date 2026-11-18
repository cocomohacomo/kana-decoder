# kana-decoder 仕様書 v0.17

- Version: 0.17.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: complete functional Japanese conversion before optional size optimization. 30 MiB is not a development hard limit.

## 1. Source format pinning

- SudachiDict release 20260723 V1 raw lexicon must be fetched from the explicit \`/sudachidict-raw/v1/20260723/\` endpoint.
- The unversioned \`/sudachidict-raw/20260723/\` endpoint is V0 raw CSV and is incompatible with this V1 importer, even though the archive filename is also \`small_lex.zip\`.
- The pinned V1 archive SHA-256 is \`e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706\`.
- The source fetcher validates the exact URL convention and SHA-256 before extraction. The release identifier, source URL, checksum and license-profile provenance must agree.

## 2. V1 lexicon header and field mapping

- V1 CSV is named-header CSV. Header normalization is case-insensitive and ignores underscores.
- \`Index_Form\` maps to normalized lookup surface; \`Headword\` maps to display surface.
- Required source columns: Index_Form, Left_Id, Right_Id, Cost, Reading_Form. Headword is optional and falls back to Index_Form.
- POS1-POS6, Normalized_Form, Dictionary_Form, Mode, Split_A, Split_B, Word_Structure and the V1 extension columns Synonym_Groups, Split_C, User_Data, POS_Id, Reference_Id may be present.
- Unknown extra columns are ignored. Duplicate normalized headers or two aliases mapping to one canonical field are rejected.
- Headerless V0 rows are explicitly rejected by the V1 importer. A V0 source must never be silently interpreted as a V1 header.
- Reading is normalized from katakana to hiragana. Entries with missing or \`*\` readings are skipped.
- Display surface, lemma fallback, POS, word cost and left/right context IDs are preserved in normalized TSV. Dictionary_Form is preferred for lemma; Normalized_Form and Headword are fallback sources when the dictionary form is blank or \`*\`.

## 3. Bounded import and determinism

- The lexicon and matrix import pipeline must not retain all source rows in Python lists, sets or in-memory sort buffers.
- SQLite temporary storage is used for duplicate detection and deterministic sorted output, with cache capped at 8 MiB.
- Matrix duplicate coordinates are rejected before zero-cost cells are removed from sparse output.
- Repeated imports of the same source and tool version must produce byte-identical normalized TSV output.

## 4. Regression gates

- Test official 23-column V1 header layout, legacy named-header fixture compatibility, and explicit rejection of headerless V0 rows.
- Test the exact V1 source URL and SHA-256 pin in the source lock.
- The production Pack is not considered built until the pinned V1 archive has been downloaded and hash-verified, source validation/import has completed, license checks pass, Pack verification succeeds, and an artifact is uploaded.
- Archive checksum sourced from an independent mirror is a pin, not proof of a successful official-endpoint download. Workflow success must confirm it.

## 5. Status

The previous production Workflow failed because the lock said V1 while its URL selected the V0 raw archive. The updated source lock now selects the V1 endpoint and importer field names match the V1 header model. Production archive download, checksum verification, full import, Pack creation, runtime load, peak RSS and accuracy remain to be confirmed by the next workflow run.
