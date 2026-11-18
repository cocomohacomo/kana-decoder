# kana-decoder 仕様書 v0.19

- Version: 0.19.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: complete functional Japanese conversion before optional size optimization. 30 MiB is not a development hard limit.

## 1. Sudachi V1 source contract

- SudachiDict release 20260723 V1 raw lexicon is fetched only from \`/sudachidict-raw/v1/20260723/\`.
- The unversioned \`/sudachidict-raw/20260723/\` endpoint is V0 raw CSV and is incompatible with the V1 importer.
- Pinned V1 archive SHA-256: \`e49936daef64043657752eb9f4ada912cf0316e26dd158a6200c770e2f93e706\`.
- Header is named CSV; matching is case-insensitive and ignores underscores. \`Index_Form\` is lookup surface; \`Headword\` is display surface.
- Headerless V0 input is rejected by the V1 importer.

## 2. Composition-only entries

- Sudachi V1 permits Left_Id or Right_Id = -1 for entries used only as components of split expressions.
- Validation accepts exactly -1, counts such entries, and rejects IDs below -1 or IDs outside the matrix dimensions.
- Such entries are not emitted as standalone kana-decoder candidates.

## 3. Bounded import

- Lexicon and matrix import use temporary SQLite storage, 8 MiB cache, deterministic sorted output, and duplicate detection.
- Duplicate matrix cells are rejected before zero-cost cells are omitted from the sparse output.

## 4. First production Pack

- Workflow run: 44373492197, completed success.
- Pack ID: \`ja-jp-sudachi-small\`, Pack version \`20260723.1\`.
- Final dictionary entries: 754,602.
- Installed size: 693,542,002 bytes (661.413 MiB).
- Matrix: 5,981 x 5,981; 35,759,450 nonzero cells.
- Artifact ZIP: 149,644,217 bytes; SHA-256 \`de7512aa0c387e3ca44921794d9e64dfbcf218574e1326941462d88b7ecc7341\`.
- No n-gram training corpus was supplied; ngram.tsv contains only its header. This Pack does not claim learned context ranking quality.

## 5. Required runtime acceptance

Pack creation and static verification are necessary but not sufficient. Before calling this a usable Japanese conversion Pack, the CI workflow must:
1. Build the native C++ runtime.
2. Load the full Pack including the 35.7-million-cell sparse connection matrix.
3. Run the smoke corpus and report exact-match/CER/decode-failure metrics.
4. Capture elapsed load/evaluation time and peak RSS.
5. Keep the actual production Pack artifact only if runtime load and Pack verification both pass.

## 6. Current limitations

- The first Pack is 661.413 MiB installed, not a 30 MiB product. This is expected under the user's completion-first development policy; later optimization must be evidence-driven.
- Current smoke corpus has only four lines and is a wiring test, not a representative quality benchmark.
- Runtime load, peak RSS, and accuracy on held-out natural Japanese data are not yet established.
