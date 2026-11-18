# Third-Party Dictionary Component Register

This register distinguishes a researched candidate from an approved, imported, redistributable Pack.

| Component | Upstream license information | Commercial use | Redistribution | Decision |
|---|---|---:|---:|---|
| SudachiDict software | Apache-2.0 | Yes | Yes, subject to Apache notices | Candidate |
| UniDic portion in SudachiDict Small | BSD-3-Clause notice in upstream LEGAL | Yes | Yes, with copyright/conditions/disclaimer | Preferred candidate |
| SudachiDict Core/Full NEologd-derived entries | Apache-2.0 package notice plus multiple upstream data sources | Requires component review | Not cleared as a whole | Excluded |
| Contemporary Written UniDic v2.x+ | GPLv2 / LGPLv2.1 / modified BSD triple license | Yes | Yes, subject to selected license | Candidate; prefer BSD option |
| Contemporary Spoken UniDic v2.2.x+ | GPLv2 / LGPLv2.1 / modified BSD triple license | Yes | Yes, subject to selected license | Candidate; prefer BSD option |
| UniDicMA / other editions | Edition-specific terms | Not assumed | Not assumed | Excluded pending written confirmation |
| IPADIC-compatible distributions | Version/source-specific | Not assumed | Not assumed | Audit individually |

## SudachiDict source notice

The upstream notice is preserved at [licenses/UPSTREAM_SUDACHIDICT_LEGAL.txt](../licenses/UPSTREAM_SUDACHIDICT_LEGAL.txt). The authoritative source is [WorksApplications/SudachiDict LEGAL](https://github.com/WorksApplications/SudachiDict/blob/develop/LEGAL).

The upstream README identifies Small as containing UniDic vocabulary, while Core and Full add broader vocabulary. Its LEGAL file specifically identifies `small_lex.csv` and `matrix.def.zip` as UniDic-derived, and `core_lex.csv` / `notcore_lex.csv` as NEologd-derived.

The repository does not currently redistribute the Sudachi dictionary archive or generated lexicon. A candidate release label is not a substitute for pinning the exact source artifact. Before importing, record the immutable release/tag, raw source archive URL, archive SHA-256, matrix checksum, complete license texts, and import mapping.

## Official references

- [SudachiDict repository and license notice](https://github.com/WorksApplications/SudachiDict)
- [SudachiDict LEGAL](https://github.com/WorksApplications/SudachiDict/blob/develop/LEGAL)
- [UniDic commercial-use statement (Japanese)](https://clrd.ninjal.ac.jp/unidic/commerce_use.html)
- [UniDic commercial-use statement (English)](https://clrd.ninjal.ac.jp/unidic/en/commerce_use_en.html)
- [UniDic FAQ and license distinctions](https://clrd.ninjal.ac.jp/unidic/faq.html)
