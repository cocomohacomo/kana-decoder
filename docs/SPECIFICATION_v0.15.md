# kana-decoder 仕様書 v0.15

- Version: 0.15.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: functional conversion and API usability. The 30 MiB figure remains a provisional optimization target, not a build-time cap.

## 1. Public Python package

The project provides a pip-installable Python package under python/kana_decoder. The Python package is a thin ctypes binding over the native C ABI; it must not implement an independent tokenizer, dictionary, scoring engine or conversion path.

The package:
- loads the shared library from an explicit library_path, KANA_DECODER_LIBRARY, a package/build-local path, or the system library search path;
- checks kd_c_api_version() against KD_C_API_VERSION before creating an engine;
- loads a local Language Pack through kd_engine_load_pack;
- exposes decode() for best-path text;
- exposes decode_nbest() as immutable DecodedPath and DecodedToken values;
- exposes streaming() with append(), commit_common_prefix(), commit_best() and reset();
- translates native error codes into KanaDecoderError and handles output-buffer growth without committing native state on a buffer-too-small result;
- owns and destroys native engine/result/session handles deterministically and supports context managers.

Token spans are Unicode code-point offsets in the original reading. Path and token scores are ranking features, not calibrated probabilities.

The Python wrapper does not bundle a platform-specific native shared library. The native CMake build and Python package installation are separate steps. Users either install the shared library in a standard search path or pass library_path / KANA_DECODER_LIBRARY.

## 2. Python streaming lifetime

A StreamingSession is a native snapshot independent of its originating engine. Closing the KanaDecoder does not invalidate an already-created streaming session. Each session has explicit close/context-manager semantics.

A buffer-too-small return must not consume appended input, commit tokens or advance the native revision. The wrapper retries with a larger buffer up to max_buffer_bytes and surfaces BufferError if that bound is exceeded.

## 3. Matrix and Sudachi Pack pipeline

The v0.13 matrix representation and source build pipeline remain normative:
- sorted sparse packed-coordinate entries in the C++ connection model;
- explicit matrix dimensions in normalized connections.tsv;
- duplicate and out-of-range validation;
- complete matrix and lexicon compatibility scan;
- one-command Pack build with source archive hash verification, safe extraction, importer/tool provenance, full upstream LEGAL and component-level Apache-2.0 / BSD-3-Clause notices.

The repository Core remains MIT. The Sudachi Small Pack data expression remains Apache-2.0 AND BSD-3-Clause.

## 4. Validation

CTest exercises C++, C ABI, ctypes, the public Python package, pip installation, matrix validation, source import, synthetic end-to-end Pack generation, license profile and Pack verification. The generic Python unittest discovery path must remain usable without CTest-specific command-line arguments.

## 5. Release limitations

The production Sudachi source archives have not yet been downloaded and verified in this repository. The end-to-end Pack builder is tested with synthetic fixtures, not a real Sudachi corpus. A production Pack, held-out accuracy, target CPU latency, peak RSS, unknown-word coverage and long-running streaming stability remain release gates.


## 6. Native Pack verification resource consistency

- C++ runtime reads \`resource_limits.max_expanded_bytes\` from the same nested Manifest object used by the Python verifier. If the object exists but the field is missing, loading fails closed rather than silently falling back to the default.
- The runtime rejects a Pack whose declared payload plus manifest size exceeds the Pack-declared limit. The default remains 8 GiB when the optional resource_limits object is absent for legacy compatibility.
- SHA-256 verification processes files in bounded 64 KiB input chunks and a 64-byte compression block. It no longer materializes an entire dictionary file in RAM solely to hash it.
- The native verifier accepts payloads above the provisional 30 MiB optimization target, subject to the Pack's declared byte budget and available machine resources.
- Native payload entry ceiling raised from 64 to 256 files. Manifest size remains capped at 1 MiB.
- Conformance tests cover acceptance of a declared budget above 30 MiB, rejection when the declared budget is smaller than the manifest and payload, and SHA-256 verification of a 70,000-byte payload spanning multiple input chunks.
