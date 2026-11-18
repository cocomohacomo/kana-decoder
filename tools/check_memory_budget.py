#!/usr/bin/env python3
"""Fail CI when GNU time reports a runtime peak RSS above the declared budget."""
import argparse
import re
from pathlib import Path


def peak_rss_kib(log_text):
    match = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", log_text)
    if not match:
        raise ValueError("GNU time peak RSS record is missing")
    return int(match.group(1))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--max-rss-mib", required=True, type=int)
    args = parser.parse_args()
    used = peak_rss_kib(args.log.read_text(encoding="utf-8", errors="replace"))
    limit = args.max_rss_mib * 1024
    print(f"peak_rss_mib={used / 1024:.1f} limit_mib={args.max_rss_mib}")
    if used > limit:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
