#!/usr/bin/env python3
"""Guard the public generic/Japanese Language Set header boundary."""
from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_public_header_boundary.py <repository-root>")
    root = Path(sys.argv[1])
    generic = (root / "include/kana_decoder/engine.hpp").read_text(encoding="utf-8")
    japanese = (root / "include/kana_decoder/japanese_decoder.hpp").read_text(encoding="utf-8")
    pack = (root / "include/kana_decoder/language_pack.hpp").read_text(encoding="utf-8")
    adapter = (root / "include/kana_decoder/surge_language.hpp").read_text(encoding="utf-8")

    if "struct MorphologyDescriptor" not in generic:
        raise SystemExit("Generic Candidate is missing provider-neutral MorphologyDescriptor")
    candidate_block = generic.split("struct Candidate", 1)[1].split("};", 1)[0]
    for legacy_field in ("pos;", "inflection_type;", "inflection_form;"):
        if legacy_field in candidate_block:
            raise SystemExit(f"Japanese-specific Candidate field leaked into engine.hpp: {legacy_field}")

    japanese_symbols = (
        "MaskedNgramScorer", "CorrectionOptions", "LatticeCandidate",
        "ConnectionModel", "LatticeEdge", "SurgeClusterModel",
        "DecoderOptions", "LexiconTrie", "MorphologyDecoder",
        "LatticePositionTrace",
    )
    for symbol in japanese_symbols:
        if symbol in generic:
            raise SystemExit(f"Japanese symbol leaked into generic engine.hpp: {symbol}")
        if symbol not in japanese:
            raise SystemExit(f"Japanese API header is missing declaration: {symbol}")

    for header_name, content in (("language_pack.hpp", pack), ("surge_language.hpp", adapter)):
        if "japanese_decoder.hpp" in content or "japanese_language.hpp" in content:
            raise SystemExit(f"Generic public header depends on Japanese API: {header_name}")

    print("public header boundary tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
