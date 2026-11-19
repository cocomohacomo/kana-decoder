"""Python ctypes wrapper for the kana-decoder C ABI."""
from __future__ import annotations
import ctypes
import ctypes.util
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple

__version__ = "0.14.0"
KD_C_API_VERSION = 1
KD_ERR_INVALID_ARGUMENT = -1
KD_ERR_BUFFER_TOO_SMALL = -2
KD_ERR_INTERNAL = -3

class KanaDecoderError(RuntimeError):
    def __init__(self, operation: str, code: int):
        super().__init__(f"{operation} failed with native error {code}")
        self.operation = operation
        self.code = code

@dataclass(frozen=True)
class DecodedToken:
    start_codepoint: int
    end_codepoint: int
    candidate_id: str
    surface: str
    pos: str
    inflection_type: str
    inflection_form: str
    morphology_score: float
    context_score: float

@dataclass(frozen=True)
class DecodedPath:
    score: float
    tokens: Tuple[DecodedToken, ...]

    @property
    def text(self) -> str:
        return "".join(token.surface for token in self.tokens)

class _TokenView(ctypes.Structure):
    _fields_ = [
        ("start_codepoint", ctypes.c_size_t),
        ("end_codepoint", ctypes.c_size_t),
        ("candidate_id", ctypes.c_char_p),
        ("surface", ctypes.c_char_p),
        ("pos", ctypes.c_char_p),
        ("inflection_type", ctypes.c_char_p),
        ("inflection_form", ctypes.c_char_p),
        ("morphology_score", ctypes.c_double),
        ("context_score", ctypes.c_double),
    ]

def _decode(value: Optional[bytes]) -> str:
    return value.decode("utf-8") if value else ""

def _encode(value: str, label: str) -> bytes:
    if not isinstance(value, str):
        raise TypeError(f"{label} must be str")
    if not value or "\x00" in value:
        raise ValueError(f"{label} must be non-empty and cannot contain NUL")
    return value.encode("utf-8")

def _find_library(explicit: Optional[str]) -> str:
    if explicit:
        path = Path(explicit).expanduser()
        if not path.is_file():
            raise FileNotFoundError(path)
        return str(path.resolve())
    env = os.environ.get("KANA_DECODER_LIBRARY")
    if env:
        path = Path(env).expanduser()
        if not path.is_file():
            raise FileNotFoundError(path)
        return str(path.resolve())
    suffixes = ("kana_decoder.dll", "libkana_decoder.dylib", "libkana_decoder.so")
    roots = [Path(__file__).resolve().parent, Path(__file__).resolve().parents[2] / "build"]
    for root in roots:
        for name in suffixes:
            candidate = root / name
            if candidate.is_file():
                return str(candidate)
    located = ctypes.util.find_library("kana_decoder")
    if located:
        return located
    raise FileNotFoundError(
        "kana-decoder shared library not found; pass library_path or set KANA_DECODER_LIBRARY"
    )

class KanaDecoder:
    """Owns a native engine loaded from a local Language Pack."""

    def __init__(self, pack_path: str, library_path: Optional[str] = None,
                 max_buffer_bytes: int = 16 * 1024 * 1024,
                 surge_journal_path: Optional[str] = None):
        if max_buffer_bytes < 256:
            raise ValueError("max_buffer_bytes must be at least 256")
        self._lib = ctypes.CDLL(_find_library(library_path))
        self._max_buffer_bytes = max_buffer_bytes
        self._engine = None
        self._bind()
        version = self._lib.kd_c_api_version()
        if version != KD_C_API_VERSION:
            raise RuntimeError(
                f"unsupported kana-decoder C ABI version {version}; expected {KD_C_API_VERSION}"
            )
        engine = self._lib.kd_engine_create()
        if not engine:
            raise MemoryError("could not allocate kana-decoder engine")
        self._engine = engine
        try:
            if surge_journal_path is not None:
                journal = str(Path(surge_journal_path).expanduser().resolve()).encode("utf-8")
                rc = self._lib.kd_engine_set_surge_journal(self._engine, journal)
                if rc != 0:
                    raise KanaDecoderError("kd_engine_set_surge_journal", rc)
            pack = str(Path(pack_path).expanduser().resolve()).encode("utf-8")
            rc = self._lib.kd_engine_load_pack(self._engine, pack)
            if rc != 0:
                raise KanaDecoderError("kd_engine_load_pack", rc)
        except Exception:
            self.close()
            raise

    def _bind(self):
        lib = self._lib
        lib.kd_c_api_version.argtypes = []
        lib.kd_c_api_version.restype = ctypes.c_uint32
        lib.kd_engine_create.argtypes = []
        lib.kd_engine_create.restype = ctypes.c_void_p
        lib.kd_engine_destroy.argtypes = [ctypes.c_void_p]
        lib.kd_engine_destroy.restype = None
        lib.kd_engine_set_surge_journal.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.kd_engine_set_surge_journal.restype = ctypes.c_int
        lib.kd_engine_load_pack.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.kd_engine_load_pack.restype = ctypes.c_int
        lib.kd_engine_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                         ctypes.POINTER(ctypes.c_char), ctypes.c_size_t]
        lib.kd_engine_decode.restype = ctypes.c_int
        lib.kd_engine_decode_nbest.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        lib.kd_engine_decode_nbest.restype = ctypes.c_void_p
        lib.kd_decode_result_destroy.argtypes = [ctypes.c_void_p]
        lib.kd_decode_result_path_count.argtypes = [ctypes.c_void_p]
        lib.kd_decode_result_path_count.restype = ctypes.c_size_t
        lib.kd_decode_result_path_score.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                                    ctypes.POINTER(ctypes.c_double)]
        lib.kd_decode_result_path_score.restype = ctypes.c_int
        lib.kd_decode_result_token_count.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        lib.kd_decode_result_token_count.restype = ctypes.c_size_t
        lib.kd_decode_result_token.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                               ctypes.c_size_t, ctypes.POINTER(_TokenView)]
        lib.kd_decode_result_token.restype = ctypes.c_int
        lib.kd_streaming_session_create.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        lib.kd_streaming_session_create.restype = ctypes.c_void_p
        lib.kd_streaming_session_destroy.argtypes = [ctypes.c_void_p]
        lib.kd_streaming_session_destroy.restype = None
        lib.kd_streaming_append.argtypes = [ctypes.c_void_p, ctypes.c_char_p,
                                            ctypes.POINTER(ctypes.c_char), ctypes.c_size_t,
                                            ctypes.POINTER(ctypes.c_uint64)]
        lib.kd_streaming_append.restype = ctypes.c_int
        lib.kd_streaming_commit_common_prefix.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char),
                                                          ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint64)]
        lib.kd_streaming_commit_common_prefix.restype = ctypes.c_int
        lib.kd_streaming_commit_best.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char),
                                                 ctypes.c_size_t, ctypes.POINTER(ctypes.c_uint64)]
        lib.kd_streaming_commit_best.restype = ctypes.c_int
        lib.kd_streaming_reset.argtypes = [ctypes.c_void_p]
        lib.kd_streaming_reset.restype = ctypes.c_int

    def _require_open(self):
        if not self._engine:
            raise RuntimeError("KanaDecoder is closed")

    def _text_call(self, operation, callback):
        capacity = 256
        while capacity <= self._max_buffer_bytes:
            output = ctypes.create_string_buffer(capacity)
            rc = callback(output, capacity)
            if rc == 0:
                return output.value.decode("utf-8")
            if rc != KD_ERR_BUFFER_TOO_SMALL:
                raise KanaDecoderError(operation, rc)
            capacity *= 2
        raise BufferError(f"{operation} output exceeds max_buffer_bytes")

    def decode(self, reading: str) -> str:
        self._require_open()
        encoded = _encode(reading, "reading")
        return self._text_call(
            "kd_engine_decode",
            lambda output, capacity: self._lib.kd_engine_decode(
                self._engine, encoded, output, capacity
            ),
        )

    def decode_nbest(self, reading: str, max_paths: int = 8) -> Tuple[DecodedPath, ...]:
        self._require_open()
        encoded = _encode(reading, "reading")
        if not isinstance(max_paths, int) or not 0 <= max_paths <= 8:
            raise ValueError("max_paths must be an integer from 0 through 8")
        result = self._lib.kd_engine_decode_nbest(self._engine, encoded, max_paths)
        if not result:
            raise KanaDecoderError("kd_engine_decode_nbest", KD_ERR_INVALID_ARGUMENT)
        try:
            paths = []
            for path_index in range(self._lib.kd_decode_result_path_count(result)):
                score = ctypes.c_double()
                rc = self._lib.kd_decode_result_path_score(
                    result, path_index, ctypes.byref(score)
                )
                if rc != 0:
                    raise KanaDecoderError("kd_decode_result_path_score", rc)
                tokens = []
                for token_index in range(
                    self._lib.kd_decode_result_token_count(result, path_index)
                ):
                    view = _TokenView()
                    rc = self._lib.kd_decode_result_token(
                        result, path_index, token_index, ctypes.byref(view)
                    )
                    if rc != 0:
                        raise KanaDecoderError("kd_decode_result_token", rc)
                    tokens.append(DecodedToken(
                        view.start_codepoint, view.end_codepoint,
                        _decode(view.candidate_id), _decode(view.surface),
                        _decode(view.pos), _decode(view.inflection_type),
                        _decode(view.inflection_form), view.morphology_score,
                        view.context_score,
                    ))
                paths.append(DecodedPath(score.value, tuple(tokens)))
            return tuple(paths)
        finally:
            self._lib.kd_decode_result_destroy(result)

    def streaming(self, max_reading_codepoints: int = 256):
        self._require_open()
        if not isinstance(max_reading_codepoints, int) or max_reading_codepoints <= 0:
            raise ValueError("max_reading_codepoints must be positive")
        handle = self._lib.kd_streaming_session_create(
            self._engine, max_reading_codepoints
        )
        if not handle:
            raise MemoryError("could not allocate streaming session")
        return StreamingSession(self._lib, handle, self._max_buffer_bytes)

    def close(self):
        if self._engine:
            self._lib.kd_engine_destroy(self._engine)
            self._engine = None

    def __enter__(self):
        self._require_open()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

class StreamingSession:
    """Independent native streaming snapshot; survives engine close/reload."""

    def __init__(self, library, handle, max_buffer_bytes):
        self._lib = library
        self._handle = handle
        self._max_buffer_bytes = max_buffer_bytes
        self.revision = 0

    def _require_open(self):
        if not self._handle:
            raise RuntimeError("StreamingSession is closed")

    def _text_call(self, operation, callback):
        capacity = 256
        while capacity <= self._max_buffer_bytes:
            output = ctypes.create_string_buffer(capacity)
            revision = ctypes.c_uint64(self.revision)
            rc = callback(output, capacity, ctypes.byref(revision))
            if rc == 0:
                self.revision = revision.value
                return output.value.decode("utf-8")
            if rc != KD_ERR_BUFFER_TOO_SMALL:
                raise KanaDecoderError(operation, rc)
            capacity *= 2
        raise BufferError(f"{operation} output exceeds max_buffer_bytes")

    def append(self, chunk: str) -> str:
        self._require_open()
        encoded = _encode(chunk, "chunk")
        return self._text_call(
            "kd_streaming_append",
            lambda output, capacity, revision: self._lib.kd_streaming_append(
                self._handle, encoded, output, capacity, revision
            ),
        )

    def commit_common_prefix(self) -> str:
        self._require_open()
        return self._text_call(
            "kd_streaming_commit_common_prefix",
            lambda output, capacity, revision: self._lib.kd_streaming_commit_common_prefix(
                self._handle, output, capacity, revision
            ),
        )

    def commit_best(self) -> str:
        self._require_open()
        return self._text_call(
            "kd_streaming_commit_best",
            lambda output, capacity, revision: self._lib.kd_streaming_commit_best(
                self._handle, output, capacity, revision
            ),
        )

    def reset(self):
        self._require_open()
        rc = self._lib.kd_streaming_reset(self._handle)
        if rc != 0:
            raise KanaDecoderError("kd_streaming_reset", rc)
        self.revision += 1

    def close(self):
        if self._handle:
            self._lib.kd_streaming_session_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        self._require_open()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

__all__ = [
    "KanaDecoder", "StreamingSession", "KanaDecoderError",
    "DecodedToken", "DecodedPath", "__version__",
]
