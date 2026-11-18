#!/usr/bin/env python3
"""Validate a Language Pack's explicit third-party license profile.

The validator is deliberately conservative. It checks declared permissions,
pinned versions, SPDX identifiers, and the presence of bundled notice files.
It cannot determine whether a declaration is legally correct.
"""
import argparse
import json
import re
from pathlib import Path, PurePosixPath

ALLOWED_SPDX = {
    "MIT", "BSD-2-Clause", "BSD-3-Clause", "ISC", "0BSD",
    "Zlib", "Apache-2.0", "CC0-1.0",
}
FLOATING = {"latest", "main", "master", "develop", "unknown", "unpinned", "head"}

def safe_relative(value):
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    path = PurePosixPath(value)
    return not path.is_absolute() and all(part not in ("", ".", "..") for part in path.parts)

def validate_profile(pack_root: Path, profile_name: str = "license-profile.json") -> dict:
    root = pack_root.resolve()
    profile_path = root / profile_name
    if profile_path.is_symlink() or not profile_path.is_file() or root not in profile_path.resolve().parents:
        raise ValueError("license profile missing or outside Pack")
    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    if not isinstance(profile, dict) or profile.get("schema_version") != 1:
        raise ValueError("unsupported license profile schema_version")
    components = profile.get("components")
    if not isinstance(components, list) or not components:
        raise ValueError("license profile must contain at least one component")
    ids, notices = set(), set()
    for index, component in enumerate(components):
        if not isinstance(component, dict):
            raise ValueError(f"component {index} must be an object")
        ident = component.get("id")
        if not isinstance(ident, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]{1,63}", ident) or ident in ids:
            raise ValueError(f"component {index} has invalid or duplicate id")
        ids.add(ident)
        version = component.get("version")
        if not isinstance(version, str) or not version.strip() or version.strip().lower() in FLOATING:
            raise ValueError(f"{ident}: exact immutable version is required")
        spdx = component.get("spdx")
        if spdx not in ALLOWED_SPDX:
            raise ValueError(f"{ident}: SPDX license is not on the reviewed permissive allowlist")
        for field in ("commercial_use", "redistribution", "modified_redistribution"):
            if component.get(field) is not True:
                raise ValueError(f"{ident}: {field} must be explicitly true")
        url = component.get("upstream_url")
        if not isinstance(url, str) or not url.startswith("https://"):
            raise ValueError(f"{ident}: HTTPS upstream_url is required")
        notice = component.get("notice_file")
        if not safe_relative(notice) or notice in notices:
            raise ValueError(f"{ident}: invalid or duplicate notice_file")
        notices.add(notice)
        notice_path = root / notice
        if notice_path.is_symlink() or not notice_path.is_file() or root not in notice_path.resolve().parents:
            raise ValueError(f"{ident}: notice file missing or unsafe: {notice}")
    return {
        "schema_version": 1,
        "components": len(components),
        "component_ids": sorted(ids),
        "notices_verified": sorted(notices),
        "release_profile_valid": True,
        "legal_accuracy_independently_verified": False,
    }

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pack_dir", type=Path)
    parser.add_argument("--profile", default="license-profile.json")
    args = parser.parse_args()
    print(json.dumps(validate_profile(args.pack_dir, args.profile), ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
