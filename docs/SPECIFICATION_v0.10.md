# kana-decoder 仕様書 v0.10

- Status: Implementation milestone specification
- Version: 0.10.0
- Core license: MIT
- Runtime: CPU only / fully offline
- Third-party data: source-pinned, checksum-verified, per-component license inheritance

## 1. Scope and priority
v0.10 prioritizes completing a usable conversion library over premature size optimization. The former 30 MiB expanded-Pack ceiling is a provisional engineering target only. It MUST NOT block dictionary import, Pack construction, feature completion, or correctness testing. Build and verification tools report actual sizes; optimization follows measured accuracy, load time, and peak RSS. CPU-only, fully offline operation and upstream license inheritance remain mandatory.

## 2. Source lock driven import
The Sudachi source lock is the single source of truth for release, archive URL, SHA-256, archive member and license references. The verifier reads the lock directly and accepts local archive paths. It rejects unsupported schema/format, missing or duplicate source IDs, non-HTTPS URLs, malformed hashes, unexpected member names, unsafe ZIP paths, symlinks, duplicate requested members and oversized members. A successful local verification writes a machine-readable receipt. A pinned checksum is not evidence that an archive has been downloaded.

## 3. Sudachi V1 CSV compatibility
The importer recognizes required V1 columns case-insensitively and ignores underscores in header names. Required fields may be accompanied by extra upstream columns, which are ignored and counted. Duplicate normalized headers, missing required fields, short/long rows, invalid numbers and int32 overflow are rejected with line numbers. V0 and V1 remain separate import paths.

## 4. Corpus separation
Quality corpora use four TSV columns: input reading, expected orthographic surface, domain label, and group ID identifying the source document or speaker/session. Train, dev and test group IDs MUST be disjoint. Empty splits, empty fields, malformed rows and group leakage are rejected. The validator reports case count, unique groups, domains, unique readings and unique references. Test data is not used for training or threshold tuning. This is a leakage guard, not a guarantee of representativeness or annotation quality.

## 5. Size and performance policy
The 30 MiB expanded Pack ceiling is provisional and is not a development blocker. Actual expanded bytes, load time, peak RSS, decode latency and accuracy must be reported separately. No size or speed target is achieved without measurement on intended hardware and a real Pack. The builder's size parameter is a packaging policy, not a limit on development or source import.

## 6. Acceptance criteria
- Source verification derives expected hashes and members from the lock file.
- V1 headers accept case and underscore variations and optional extra columns.
- Malformed CSV row widths are rejected.
- Train/dev/test group leakage is rejected.
- Valid split reports are deterministic JSON.
- Existing Core, C API, Pack, importer and license tests remain passing.
- No production dictionary or accuracy claim is made until source artifacts and held-out corpora are available.

## 7. Next production work
1. Acquire exact pinned Sudachi raw archives and verify actual bytes and legal notices.
2. Generate the normalized Japanese Pack including connection matrix.
3. Audit POS and connection-class compatibility against runtime decoding.
4. Add error-class confusion reports and unknown-word / candidate-coverage analysis.
5. Add end-to-end C API streaming and partial-commit tests.
6. Measure accuracy, p50/p95/p99 latency, load time and peak RSS on target hardware.
7. Optimize matrix storage and Pack size only after measurements.
