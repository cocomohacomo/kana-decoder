# Licensing Policy

## 1. Repository license

The kana-decoder implementation, documentation authored for this repository, and original test fixtures are released under the MIT License in the repository-root `LICENSE`.

MIT is the project's permissive default for original code. This does not relicense third-party dictionaries, corpora, model weights, or generated artifacts derived from them.

## 2. Separate code and data licensing

Every Language Pack is an independently licensed distribution unit. Its license is the conjunction of the terms that apply to all included components and derived data. A permissive repository license never overrides an upstream license.

A redistributed Pack must:
- identify every upstream component, exact immutable version, and SHA-256 of the exact source artifact;
- preserve all copyright, license, attribution, and NOTICE texts required by each component;
- identify which Pack files derive from each component;
- state whether commercial use, redistribution, and modification/redistribution are permitted;
- include the original license texts and a machine-readable `license-profile.json`;
- include SHA-256 hashes for the profile and all notice files in the Pack manifest;
- pass `tools/validate_license_profile.py` before release.

The profile is compliance metadata, not a legal opinion. Unknown, conflicting, non-commercial, no-redistribution, or unpinned components are rejected from the release workflow.

## 3. Preferred license selection

For original project code use MIT.

For upstream material that offers a genuine choice of licenses, select the least restrictive option that the upstream explicitly grants for the exact version and artifact. Do not infer a license choice from a project homepage or a package-level badge.

For a multi-component Pack, record each component's selected license separately and use an SPDX expression such as `Apache-2.0 AND BSD-3-Clause` for the combined Pack. Do not replace that expression with MIT.

The allowlist used by the current validator is intentionally narrow: MIT, BSD-2-Clause, BSD-3-Clause, ISC, 0BSD, Zlib, Apache-2.0, and CC0-1.0. Adding another license requires an explicit policy review and test update.

## 4. Japanese dictionary source decisions

### SudachiDict Small: candidate, not yet imported

The release-specific SudachiDict LEGAL notice for v20260723 states that:
- the distribution is under Apache-2.0;
- `small_lex.csv` contains part of UniDic;
- `matrix.def.zip` is part of UniDic;
- `core_lex.csv` and `notcore_lex.csv` contain part of NEologd.

The Small lexicon and matrix therefore each retain both the Apache-2.0 distribution terms and the UniDic BSD-3-Clause conditions. The exact release license texts and four-component profile are preserved under `licenses/sudachi-small-20260723/`. The profile does not replace the need to verify the downloaded archives and final Pack.

The Small dictionary is therefore the preferred Sudachi starting point because its documented composition is limited to the UniDic vocabulary subset rather than the additional NEologd-derived Core/Full entries. The candidate Pack license expression is `Apache-2.0 AND BSD-3-Clause`, retaining the UniDic BSD notice. The exact release, raw CSV archive, matrix, source hashes, and all release-specific notices are pinned in the repository. The source archives have not yet been downloaded and verified here, and no production Sudachi dictionary Pack is bundled.

### SudachiDict Core and Full: excluded from initial import

Core and Full include NEologd-derived entries. The upstream notice identifies multiple source datasets and conditions, including Hatena Keywords, Japan Post postal-code data, station-name data, personal-name entries, and web-crawled text. The package-level Apache-2.0 statement alone is not sufficient evidence that every included entry can be redistributed under Apache-2.0. These variants remain excluded unless each source component and its redistribution rights are independently cleared.

### UniDic

The National Institute for Japanese Language and Linguistics states that Contemporary Written UniDic v2.x onward and Contemporary Spoken UniDic v2.2.x onward are available under a GPLv2/LGPLv2.1/modified-BSD triple license and may be used commercially. Where the exact release offers that choice, select the BSD option and preserve the required notices. Do not apply this conclusion to UniDicMA or other editions with different license terms.

### IPADIC and other dictionaries

Do not infer commercial redistribution permission from compatibility with MeCab or an importer format. Each exact distribution and version must pass the same component-level audit. Dictionaries with NC, ND, unclear provenance, or unverified redistribution permission are excluded from distributable Packs.

## 5. Release gates

A production Pack is not releasable until:
1. its exact upstream artifact and version are pinned;
2. its complete upstream legal/NOTICE files are preserved;
3. all source components have explicit commercial-use and redistribution grants;
4. modifications and derived-data redistribution are allowed;
5. the normalized dictionary and connection matrix are version-matched;
6. the machine-readable license profile passes validation;
7. the Pack checksum manifest passes both Python and native verification;
8. a provenance report records source URLs, retrieval date, archive SHA-256, and importer version.

The current repository's `ja-lite` Pack is an original test fixture. It is not a production dictionary.
