# Third-Party Dictionary Component Register

This register distinguishes a researched candidate from an approved, imported, redistributable Pack.

| Component | Upstream license information | Commercial use | Redistribution | Decision |
|---|---|---:|---:|---|
| SudachiDict software | Apache-2.0 | Yes | Yes, subject to Apache notices | Candidate |
| UniDic portion in SudachiDict Small | Apache-2.0 distribution terms plus BSD-3-Clause UniDic conditions in upstream LEGAL | Yes | Yes, preserving both license texts and notices | Preferred source, not yet imported |
| SudachiDict Core/Full NEologd-derived entries | Apache-2.0 package notice plus multiple upstream data sources | Requires component review | Not cleared as a whole | Excluded |
| Contemporary Written UniDic v2.x+ | GPLv2 / LGPLv2.1 / modified BSD triple license | Yes | Yes, subject to selected license | Candidate; prefer BSD option |
| Contemporary Spoken UniDic v2.2.x+ | GPLv2 / LGPLv2.1 / modified BSD triple license | Yes | Yes, subject to selected license | Candidate; prefer BSD option |
| UniDicMA / other editions | Edition-specific terms | Not assumed | Not assumed | Excluded pending written confirmation |
| IPADIC-compatible distributions | Version/source-specific | Not assumed | Not assumed | Audit individually |

## SudachiDict source notice

The release-specific upstream notice for tag v20260723 is preserved at [licenses/UPSTREAM_SUDACHIDICT_LEGAL.txt](../licenses/UPSTREAM_SUDACHIDICT_LEGAL.txt). The exact Apache-2.0 and BSD-3-Clause texts and four-component license profile are under [licenses/sudachi-small-20260723](../licenses/sudachi-small-20260723/). The authoritative source is [WorksApplications/SudachiDict LEGAL](https://github.com/WorksApplications/SudachiDict/blob/v20260723/LEGAL).

The upstream README identifies Small as containing UniDic vocabulary, while Core and Full add broader vocabulary. Its LEGAL file specifically identifies `small_lex.csv` and `matrix.def.zip` as UniDic-derived, and `core_lex.csv` / `notcore_lex.csv` as NEologd-derived.

The PyPI release page currently lists SudachiDict-small 20261015 with a 42.5 MB wheel. This is a package artifact size, not the final kana-decoder Pack size, but it is already above the project's 30 MiB lightweight-Pack target; the normalized Pack must be measured and may require a compact index, selective vocabulary, or an explicitly larger extension tier. See [the exact PyPI release](https://pypi.org/project/sudachidict-small/20261015/).\n\nThe repository pins a reproducible source candidate in `licenses/sudachidict-small-20260723-source-lock.json` (Small V1 lexicon and UniDic 2.1.2 matrix ZIP SHA-256 values). The archives are not included in the repository. `tools/fetch_sudachi_source.py` obtains them, `tools/verify_sudachi_source.py` verifies locally supplied bytes before extraction, and `tools/build_sudachi_pack.py` runs the complete validation/import/build/manifest-verification pipeline. The hashes are published by the Lindera raw-source mirror and still require confirmation against the downloaded official archive bytes before production release. A candidate release label is not a substitute for pinning the exact source artifact. Before importing, independently confirm the immutable release/tag, raw source archive URL, archive SHA-256, matrix checksum, complete license texts, and import mapping. Sudachi V1 source CSV is a header-based 18-column format and must not be parsed with the legacy V0 importer.

## Official references

- [SudachiDict repository and license notice](https://github.com/WorksApplications/SudachiDict)
- [SudachiDict LEGAL](https://github.com/WorksApplications/SudachiDict/blob/develop/LEGAL)
- [UniDic commercial-use statement (Japanese)](https://clrd.ninjal.ac.jp/unidic/commerce_use.html)
- [UniDic commercial-use statement (English)](https://clrd.ninjal.ac.jp/unidic/en/commerce_use_en.html)
- [UniDic FAQ and license distinctions](https://clrd.ninjal.ac.jp/unidic/faq.html)
