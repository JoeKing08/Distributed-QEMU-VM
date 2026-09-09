#!/usr/bin/env python3
"""Redact credentials from JSON Lines session exports.

The tool preserves the JSON structure, handles plain JSONL and gzip JSONL,
creates a stable input snapshot, and replaces files atomically when
--in-place is requested. A post-redaction scan fails closed if a recognized
credential pattern remains.
"""

from __future__ import annotations

import argparse
import gzip
import json
import os
import re
import shutil
import stat
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterator


REDACTED = "[REDACTED]"

# These are deliberately broad. Losing one command/output string is safer
# than publishing a partial private-key block or an authentication header.
PRIVATE_KEY_RE = re.compile(r"(?i)PRIVATE[ _-]*KEY")
PUBLIC_KEY_RE = re.compile(
    r"(?im)(?<![A-Za-z0-9_-])"
    r"(?:ssh-(?:rsa|ed25519|ecdsa(?:-[A-Za-z0-9-]+)*)|ecdsa-[A-Za-z0-9-]+)"
    r"[ \t]+[A-Za-z0-9+/=]{20,}(?:[ \t]+[^\r\n]*)?"
)

TOKEN_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("github token", re.compile(r"(?i)(?:github_pat_|gh[pousr]_|gh_)\w+")),
    ("gitlab token", re.compile(r"(?i)glpat-[A-Za-z0-9_-]+")),
    ("circleci token", re.compile(r"(?i)CCIPAT_[A-Za-z0-9_-]+")),
    ("tailscale token", re.compile(r"(?i)tskey-(?:api|auth)-[A-Za-z0-9_-]+")),
    ("openai-style api key", re.compile(r"(?i)sk-[A-Za-z0-9_-]{20,}")),
    ("aws access key", re.compile(r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    ("slack token", re.compile(r"\bxox[baprs]-[A-Za-z0-9-]+\b")),
    (
        "authorization header",
        re.compile(
            r"(?i)\b(?:authorization|proxy-authorization):[ \t]*"
            r"(?:bearer|basic)[ \t]+\S+"
        ),
    ),
)

SENSITIVE_FIELD_NAMES = {
    "password",
    "passwd",
    "passphrase",
    "secret",
    "token",
    "api_key",
    "apikey",
    "access_key",
    "accesskey",
    "private_key",
    "privatekey",
    "credential",
    "authorization",
    "bearer",
    "ssh_key",
    "sshkey",
}


def is_sensitive_field(name: str) -> bool:
    normalized = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    normalized = re.sub(r"[^A-Za-z0-9]+", "_", normalized).strip("_").lower()
    return normalized in SENSITIVE_FIELD_NAMES or any(
        normalized.endswith(f"_{suffix}") for suffix in SENSITIVE_FIELD_NAMES
    )


def redact_string(value: str, literals: tuple[str, ...]) -> tuple[str, int]:
    # A private key may be truncated and contain only one boundary. Replace
    # the complete scalar instead of trying to reconstruct the key block.
    if PRIVATE_KEY_RE.search(value):
        return REDACTED, 1

    result = value
    changes = 0
    result, count = PUBLIC_KEY_RE.subn(REDACTED, result)
    changes += count
    for _label, pattern in TOKEN_PATTERNS:
        result, count = pattern.subn(REDACTED, result)
        changes += count
    for literal in literals:
        if literal and literal in result:
            result = result.replace(literal, REDACTED)
            changes += 1
    return result, changes


def redact_value(
    value: Any, literals: tuple[str, ...], key_context: bool = False
) -> tuple[Any, int]:
    if isinstance(value, str):
        if key_context and value not in ("", REDACTED):
            return REDACTED, 1
        return redact_string(value, literals)
    if isinstance(value, list):
        result_list: list[Any] = []
        count = 0
        for item in value:
            redacted, item_count = redact_value(item, literals, key_context)
            result_list.append(redacted)
            count += item_count
        return result_list, count
    if isinstance(value, dict):
        result_dict: dict[str, Any] = {}
        count = 0
        for key, item in value.items():
            key_text = str(key)
            redacted_key = key_text
            key_count = 0
            redacted_item, item_count = redact_value(
                item, literals, key_context or is_sensitive_field(key_text)
            )
            result_dict[redacted_key] = redacted_item
            count += key_count + item_count
        return result_dict, count
    if key_context and value is not None:
        return REDACTED, 1
    return value, 0


def iter_records(path: Path) -> Iterator[tuple[int, Any]]:
    opener = gzip.open if path.name.endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8", newline="") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                raise ValueError(f"line {line_number}: blank JSONL record")
            try:
                yield line_number, json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"line {line_number}: invalid JSON: {exc.msg}") from exc


def write_jsonl(path: Path, records: Iterator[tuple[int, Any]], literals: tuple[str, ...]) -> int:
    opener = gzip.open if path.name.endswith(".gz") else open
    changes = 0
    with opener(path, "wt", encoding="utf-8", newline="") as target:
        for _line_number, record in records:
            redacted, count = redact_value(record, literals)
            target.write(json.dumps(redacted, ensure_ascii=False, separators=(",", ":")))
            target.write("\n")
            changes += count
        target.flush()
        os.fsync(target.fileno())
    return changes


def scan_output(path: Path) -> list[tuple[int, str]]:
    findings: list[tuple[int, str]] = []
    for line_number, record in iter_records(path):
        for value in scalar_values(record):
            if PRIVATE_KEY_RE.search(value):
                findings.append((line_number, "private-key marker"))
            if PUBLIC_KEY_RE.search(value):
                findings.append((line_number, "SSH public key"))
            for label, pattern in TOKEN_PATTERNS:
                if pattern.search(value):
                    findings.append((line_number, label))
    return findings


def scalar_values(value: Any) -> Iterator[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from scalar_values(item)
    elif isinstance(value, dict):
        for item in value.values():
            yield from scalar_values(item)


def process(
    input_path: Path,
    output_path: Path,
    literals: tuple[str, ...],
    snapshot_dir: Path,
) -> tuple[int, list[tuple[int, str]]]:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    source_mode = stat.S_IMODE(input_path.stat().st_mode)
    snapshot_dir.mkdir(parents=True, exist_ok=True)
    snapshot_fd, snapshot_name = tempfile.mkstemp(
        prefix=f".{input_path.name}.", suffix=input_path.suffix, dir=snapshot_dir
    )
    os.close(snapshot_fd)
    snapshot_path = Path(snapshot_name)
    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{output_path.name}.", suffix=output_path.suffix, dir=output_path.parent
    )
    os.close(fd)
    temporary_path = Path(temporary_name)
    try:
        # Copy first so an actively growing rollout produces one stable backup.
        shutil.copyfile(input_path, snapshot_path)
        changes = write_jsonl(temporary_path, iter_records(snapshot_path), literals)
        findings = scan_output(temporary_path)
        if findings:
            preview = ", ".join(f"line {line}: {kind}" for line, kind in findings[:8])
            raise ValueError(f"post-redaction scan failed ({preview})")
        os.chmod(temporary_path, source_mode)
        os.replace(temporary_path, output_path)
        return changes, []
    except Exception:
        temporary_path.unlink(missing_ok=True)
        raise
    finally:
        snapshot_path.unlink(missing_ok=True)


def load_literals(path: Path | None) -> tuple[str, ...]:
    if path is None:
        return ()
    values = tuple(line.rstrip("\r\n") for line in path.read_text(encoding="utf-8").splitlines())
    return tuple(value for value in values if value)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path, help="JSONL or JSONL.GZ files")
    parser.add_argument(
        "--in-place",
        action="store_true",
        help="replace each input atomically; otherwise use --output-dir",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="write redacted files here, preserving each input filename",
    )
    parser.add_argument(
        "--secrets-file",
        type=Path,
        help="optional local file with one additional secret literal per line",
    )
    parser.add_argument(
        "--snapshot-dir",
        type=Path,
        help="directory for transient input snapshots (default: output directory)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="exact output path; only valid with one input",
    )
    args = parser.parse_args()
    if args.in_place and args.output_dir:
        parser.error("--in-place and --output-dir are mutually exclusive")
    if args.in_place and args.output:
        parser.error("--in-place and --output are mutually exclusive")
    if args.output and len(args.inputs) != 1:
        parser.error("--output requires exactly one input")
    if not args.in_place and not args.output_dir:
        if not args.output:
            parser.error("choose --in-place, --output, or --output-dir")
    return args


def main() -> int:
    args = parse_args()
    literals = load_literals(args.secrets_file)
    total = 0
    for input_path in args.inputs:
        if not input_path.is_file():
            print(f"error: file not found: {input_path}", file=sys.stderr)
            return 2
        if args.in_place:
            output_path = input_path
        elif args.output:
            output_path = args.output
        else:
            output_path = args.output_dir / input_path.name
        snapshot_dir = args.snapshot_dir or output_path.parent
        try:
            count, _ = process(input_path, output_path, literals, snapshot_dir)
        except (OSError, ValueError) as exc:
            print(f"error: {input_path}: {exc}", file=sys.stderr)
            return 1
        total += count
        print(f"{input_path}: redacted {count} JSON value(s) -> {output_path}")
    print(f"total redacted JSON values: {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
