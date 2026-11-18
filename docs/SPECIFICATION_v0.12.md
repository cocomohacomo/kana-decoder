# kana-decoder 仕様書 v0.12

- Version: 0.12.0
- Core license: MIT
- Runtime: CPU-only / offline
- Scope: structured N-best C ABI and reproducible Japanese dictionary acquisition

## 1. Structured N-best result API

The C ABI exposes \`kd_engine_decode_nbest(engine, reading, max_paths)\` returning an opaque owned result. \`max_paths=0\` selects the default maximum of 8; values above 8 are rejected because the Core decoder has a hard beam/path ceiling of 8.

The result provides:
- path count and per-path total score;
- token count per path;
- token start/end offsets measured in Unicode code points of the input reading;
- candidate ID, surface, POS, inflection type and inflection form;
- morphology and context score contributions.

\`kd_token_view\` contains borrowed string pointers. They remain valid only while the owning \`kd_decode_result\` remains alive and unmodified. Callers must not free or retain these pointers after destroying the result. Returned scores are ranking features, not calibrated probabilities or confidence percentages.

Invalid handles/indices return \`KD_ERR_INVALID_ARGUMENT\`. Result creation failure returns null. The API does not return a partially initialized result.

## 2. Python FFI

CMake builds both the existing linkable core target and a shared library named \`kana_decoder\`. The Python ctypes integration test loads the shared library by exact build path and verifies Pack loading, N-best result ownership, path scores, Unicode code-point spans, and candidate metadata.

The Python wrapper is a consumer of the stable C ABI, not a second implementation of decoding.

## 3. Sudachi V1 source acquisition

\`tools/fetch_sudachi_source.py\` reads the versioned source lock and downloads only the two pinned HTTPS hosts. It verifies SHA-256 before atomically publishing each local archive. Redirect targets must remain HTTPS and on the allowlist. Each archive is limited to 2 GiB.

Then run:
1. \`tools/verify_sudachi_source.py\` to verify and safely extract the locked members;
2. \`tools/validate_sudachi_pack_inputs.py\` to confirm every lexicon LEFT_ID/RIGHT_ID fits the matrix dimensions;
3. \`tools/import_sudachi_v1.py\` to normalize lexicon and sparse matrix TSV;
4. \`tools/build_language_pack.py\` with the license profile and exact upstream provenance.

The downloader is a build-time development utility. The runtime Core never downloads data or accesses the network.

## 4. License and provenance boundary

The Small lexicon and UniDic connection matrix remain separate license components. Their source lock checksums are pins, not proof that an archive was obtained. A successful download must match the pin. The final Pack must preserve complete upstream license and legal notice texts, component mapping, source version and source hashes. This specification does not declare a production Sudachi Pack complete.

Core code remains MIT. Third-party dictionary terms are inherited and never relicensed by this repository.

## 5. Validation and limitations

- C and Python ctypes tests validate the public N-best ABI.
- Sudachi pipeline tests validate URL allowlisting and matrix/context-ID compatibility using synthetic fixtures.
- Real upstream archives, a complete production Pack, legal notice audit, held-out accuracy, target CPU latency and peak RSS remain release gates.
- N-best scores are raw decoder scores and must not be exposed as probabilities.
