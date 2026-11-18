# kana-decoder 仕様書 v0.8

- Status: Design specification
- Version: 0.8.0
- Core license: MIT
- Runtime: CPU only / fully offline
- Data licensing: per-component inheritance; no repository-wide relicensing of third-party data

## 1. v0.8 scope

v0.8 strengthens the production Language Pack supply chain. It does not claim that a production Japanese dictionary has already been imported. The existing `ja-lite` Pack remains an original test fixture.

## 2. License architecture

The repository's original Core, original tools, and original test fixtures are MIT-licensed. Third-party dictionary entries, connection matrices, corpora, and model assets retain the licenses and notices of their actual upstream components.

A Language Pack may contain multiple independently licensed components. Its combined license is expressed as an SPDX conjunction, for example `Apache-2.0 AND BSD-3-Clause`. Every component must record:
- stable component identifier;
- exact immutable version;
- SPDX license identifier selected from the license actually offered by upstream;
- upstream HTTPS URL;
- explicit commercial-use permission;
- explicit redistribution permission;
- explicit permission to redistribute modifications/derived data;
- bundled license/NOTICE file path.

A Pack is not release-ready if a version floats on a branch, the notice is absent, or any required permission is false or unknown.

## 3. Source selection

### 3.1 SudachiDict Small

SudachiDict is the preferred first production dictionary candidate. Its upstream LEGAL notice identifies the Small vocabulary file and matrix as UniDic-derived, while the Core and Full vocabulary additions include NEologd-derived entries. The Small candidate therefore requires preserving both the SudachiDict Apache-2.0 terms and UniDic BSD-3-Clause notice.

The exact release archive, source checksum, matrix checksum, and complete notices must be pinned before conversion. No third-party dictionary data is currently bundled.

### 3.2 Core and Full exclusion

SudachiDict Core and Full are excluded from the initial Pack until all NEologd source components and their redistribution terms have been audited individually. A top-level package license is not sufficient to erase component-level conditions.

### 3.3 UniDic

For eligible Contemporary Written UniDic v2.x+ and Contemporary Spoken UniDic v2.2.x+ releases, upstream documents a GPLv2/LGPLv2.1/modified-BSD triple license. Where the exact release grants a choice, the Pack builder should select the BSD option and preserve its notices. Other UniDic editions are not presumed to share those terms.

## 4. License profile validation

`tools/validate_license_profile.py` validates a Pack's `license-profile.json`:
- schema version and unique component IDs;
- exact pinned versions rather than branch names or floating labels;
- reviewed permissive SPDX allowlist;
- explicit true values for commercial use, redistribution, and modified redistribution;
- HTTPS source URL;
- safe, present, non-symlink notice files.

This is a mechanical release gate, not a legal opinion or source-license verifier. Human review must confirm the profile accurately represents the exact upstream artifact. Pack builder and native Manifest checksums must include the profile and notice files.

## 5. Acceptance criteria

- Root repository MIT license exists.
- Third-party data remains separately licensed.
- SudachiDict Small is documented as a candidate, not as an already imported or approved production Pack.
- SudachiDict Core/Full and unverified UniDic editions are excluded.
- License-profile validator rejects missing notices, floating versions, unsupported licenses, and absent commercial/redistribution rights.
- No production dictionary is bundled until exact source artifacts and all component notices are pinned and reviewed.
