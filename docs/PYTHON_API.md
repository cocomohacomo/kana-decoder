# Python API

## Build and install

Build the shared native library first:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Install the Python wrapper:

    python3 -m pip install .

The wrapper uses the same native C ABI as C/C++ clients. It does not contain a separate tokenizer or conversion implementation.

## Basic conversion

If the shared library is not installed in a standard system library path, point the wrapper at the exact CMake output:

    export KANA_DECODER_LIBRARY="$PWD/build/libkana_decoder.so"

Then:

    from kana_decoder import KanaDecoder

    with KanaDecoder("packs/ja-lite") as decoder:
        print(decoder.decode("あめ"))
        for path in decoder.decode_nbest("あめ", max_paths=8):
            print(path.score, path.text)

Windows and macOS builds use their platform shared-library names. Alternatively, pass library_path directly to KanaDecoder.

## Streaming

    with KanaDecoder("packs/ja-lite") as decoder:
        with decoder.streaming(max_reading_codepoints=256) as session:
            print(session.append("きょうは"))
            print(session.append("はれる"))
            committed = session.commit_common_prefix()

Append returns a provisional best-path rendering. It may change as more reading arrives. commit_common_prefix commits only the exact common token prefix across retained alternatives. commit_best explicitly commits the current best path and may discard ambiguity. reset clears pending input.

A streaming session owns a native snapshot and remains usable if the original KanaDecoder is closed.

## Structured values and errors

decode_nbest returns immutable DecodedPath values containing a raw ranking score and immutable DecodedToken values. The first path's `text` is the SURGE-normalized output; its token surfaces remain the raw recognition hypotheses for diagnostics. Token spans are Unicode code-point offsets in the original reading. Scores are not calibrated probabilities.

The wrapper raises:
- KanaDecoderError for native API failures;
- FileNotFoundError when the shared library cannot be located;
- BufferError when an output exceeds max_buffer_bytes;
- ValueError or TypeError for invalid Python arguments.

Set KANA_DECODER_LIBRARY or pass library_path when the library is not discoverable. Pack loading is local and offline.


## SURGE journal persistence

The default engine creates an isolated temporary SURGE journal and removes it when
the engine closes. To retain context and decision history across engine restarts,
pass a caller-managed path:

```python
with KanaDecoder(
    "packs/ja-lite",
    surge_journal_path="state/normalization.surge-journal",
) as decoder:
    print(decoder.decode("きょうと"))
```

The journal is local and offline. The application owns its backup, retention and
deletion policy. Do not share one writable journal concurrently across independent
engine instances; EventJournal enforces a single writer.
