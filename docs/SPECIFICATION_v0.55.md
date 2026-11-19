# kana-decoder: conformance test reliability and provider-neutral streaming v0.55

Version: 0.55.0
Parent: SPECIFICATION_v0.54.md

## 1. Test execution correctness

- Release builds define NDEBUG, which previously compiled out C/C++ assert expressions. CTest targets now explicitly undefine NDEBUG so assertions and the function calls inside them execute in Release CI.
- The generic StreamingSession test now uses provider-decoded paths via append_decoded. The Japanese decoder is invoked explicitly by the test provider helper; the generic session no longer exposes the removed Japanese-specific append overload.
- Language Pack startup tests report loader diagnostics and return failure instead of dereferencing a null Pack when assertions are disabled.
- Sudachi V1 TSV test uses an actual tab delimiter when parsing the normalized output.

## 2. Pack loader diagnostics

The Japanese Language Pack loader now propagates the first failing stage through an optional error output:
- manifest and payload verification error from verify_pack_manifest;
- lexicon parse failure;
- N-gram parse failure;
- connection matrix parse failure;
- correction-rule parse failure.

This makes a C API Pack-load failure diagnosable from the native conformance test without changing the C ABI.

## 3. Verification

CI run #998 failed during Build because tests/test_engine.cpp still called the removed StreamingSession::append API. The test has been migrated to append_decoded. The earlier test run also exposed Pack-load failures and a malformed test TSV split; the new diagnostics, active assertions and delimiter fix are intended to isolate and resolve those failures. A fresh final-head CI run is required.

## 4. Release gates

- All 29 CTest/Python test cases must pass with assertions active.
- Pack loading must pass both native and C/Python API paths.
- Static and shared builds must pass.
- Performance and held-out accuracy must be measured independently; passing tests alone does not establish practical accuracy or edge-PC performance.