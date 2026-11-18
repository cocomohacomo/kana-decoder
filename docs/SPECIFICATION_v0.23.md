# kana-decoder 仕様書 v0.23

- Version: 0.23.0
- Core license: MIT
- Runtime: CPU-only / offline
- Development priority: correctness and functional completion before model-size optimization.

## 1. Engine module decomposition

The implementation is split by responsibility while preserving the public C++ API and C ABI:

| Module | Responsibility |
|---|---|
| `engine.cpp` | UTF-8 primitives, public decode facade and dependency graph |
| `morphology_decoder.cpp` | bounded N-best lattice decoding |
| `lexicon_trie.cpp` | lexicon insertion/loading and lattice generation |
| `scoring.cpp` | connection matrix, n-gram scorer and ambiguity resolution |
| `correction.cpp` | correction rule parsing, candidate generation and correction selection |
| `streaming_session.cpp` | append, provisional snapshots, prefix commit, best commit and reset |
| `pack_loader.cpp` | manifest-gated transactional Pack loading |

## 2. Compatibility and transactional guarantees

- Existing public types and method signatures remain in `include/kana_decoder/engine.hpp`.
- Existing callers continue to use `load_language_pack`, `decode_reading`, `decode_with_corrections` and `StreamingSession` without source changes.
- Pack loading continues to parse into temporary objects and publishes them to caller-owned objects only after all required files and optional correction rules have passed validation.
- Both static and shared libraries compile the same engine module set. The C ABI and Python ctypes wrapper continue to link against the shared library.
- The module split is organizational only; scoring weights, fallback policy, correction acceptance policy, streaming commit policy and decoder bounds are unchanged.

## 3. Build and test gates

- CMake registers all modules in both `kana_decoder_core` and `kana_decoder_shared`.
- Existing engine, C API, Python API/ctypes, streaming snapshot, correction, Pack verification, and evaluator trace tests remain required.
- GitHub Actions run `44379310687` for final code commit `f8ccb004b5f4fc9e8554dfc2eaa3a206f2867030`: completed / success. Both static and shared libraries built; CTest 14/14 PASS; bounded decode benchmark PASS; reference Pack verification PASS; Python Pack/license suite 45 discovered, 41 passed, 4 skipped. The final cleanup also removed helpers made obsolete by the extraction.
