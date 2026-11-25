#!/usr/bin/env python3
"""Validate a metal-probe JSON report (schema version 1).

Default mode checks the schema and the constraints of a live probe: a
non-empty device name and architecture, kernel_count >= 1, and a
non-empty archive. Pass --schema-only to check types and key order
while allowing a null architecture and zero counts.

An optional expected archive path must match archive.path exactly.
"""

from __future__ import annotations

import json
import os
import sys
from typing import Any


TOP_KEYS = ["schema_version", "device", "library", "archive"]
DEVICE_KEYS = ["name", "architecture"]
LIBRARY_KEYS = ["kernel_count"]
ARCHIVE_KEYS = ["path", "bytes"]

SAMPLE_JSON = """\
{
  "schema_version": 1,
  "device": {
    "name": "Apple M2",
    "architecture": "applegpu_g14g"
  },
  "library": {
    "kernel_count": 1
  },
  "archive": {
    "path": "out.metallib",
    "bytes": 4096
  }
}
"""


class ReportError(Exception):
    pass


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _keys(value: Any, expected: list[str], label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ReportError(f"{label} must be an object")
    found = list(value.keys())
    if found != expected:
        raise ReportError(f"{label} keys must be {expected} in that order, got {found}")
    return value


def validate(document: Any, *, schema_only: bool = False, expected_path: str | None = None) -> None:
    doc = _keys(document, TOP_KEYS, "report")
    if not _is_int(doc["schema_version"]) or doc["schema_version"] != 1:
        raise ReportError("schema_version must be the integer 1")

    device = _keys(doc["device"], DEVICE_KEYS, "device")
    name = device["name"]
    if not isinstance(name, str):
        raise ReportError("device.name must be a string")
    if not schema_only and name == "":
        raise ReportError("device.name must be non-empty")

    architecture = device["architecture"]
    if architecture is None:
        if not schema_only:
            raise ReportError("device.architecture must be a non-empty string for a live probe")
    elif not isinstance(architecture, str) or architecture == "":
        raise ReportError("device.architecture must be a non-empty string or null")

    library = _keys(doc["library"], LIBRARY_KEYS, "library")
    kernel_count = library["kernel_count"]
    if not _is_int(kernel_count) or kernel_count < 0:
        raise ReportError("library.kernel_count must be an integer >= 0")
    if not schema_only and kernel_count < 1:
        raise ReportError("library.kernel_count must be >= 1 for a live probe")

    archive = _keys(doc["archive"], ARCHIVE_KEYS, "archive")
    path = archive["path"]
    if not isinstance(path, str) or path == "":
        raise ReportError("archive.path must be a non-empty string")
    if expected_path is not None and path != expected_path:
        raise ReportError(f"archive.path must be {expected_path!r}, got {path!r}")

    size = archive["bytes"]
    if not _is_int(size) or size < 0:
        raise ReportError("archive.bytes must be an integer >= 0")
    if not schema_only and size < 1:
        raise ReportError("archive.bytes must be >= 1 for a live probe")


def validate_text(raw: str, *, schema_only: bool = False, expected_path: str | None = None) -> Any:
    if not raw.endswith("\n"):
        raise ReportError("report must end with a newline")
    try:
        document = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ReportError(f"not JSON: {exc}") from exc
    validate(document, schema_only=schema_only, expected_path=expected_path)
    return document


def _expect_fail(raw: str, label: str, **kwargs: Any) -> None:
    try:
        validate_text(raw, **kwargs)
    except ReportError:
        return
    raise SystemExit(f"self-test: {label} unexpectedly passed")


def self_test() -> None:
    readme_path = os.path.join(os.path.dirname(__file__), "..", "README.md")
    with open(readme_path, encoding="utf-8") as handle:
        readme = handle.read()
    if SAMPLE_JSON not in readme:
        raise SystemExit("self-test: SAMPLE_JSON is not the README sample")

    sample = validate_text(SAMPLE_JSON, expected_path="out.metallib")
    if sample["device"]["name"] != "Apple M2":
        raise SystemExit("self-test: sample name mismatch")
    validate_text(SAMPLE_JSON, schema_only=True)

    null_arch = SAMPLE_JSON.replace('"applegpu_g14g"', "null")
    _expect_fail(null_arch, "null architecture live")
    validate_text(null_arch, schema_only=True)

    _expect_fail(SAMPLE_JSON.replace("4096", "0"), "zero bytes live")
    validate_text(SAMPLE_JSON.replace("4096", "0"), schema_only=True)

    _expect_fail(SAMPLE_JSON.replace("1,\n  \"device\"", "true,\n  \"device\""), "bool schema")
    _expect_fail('{"schema_version": 1}\n', "missing keys")
    _expect_fail(SAMPLE_JSON.replace('"bytes": 4096', '"bytes": "4096"'), "string size")
    extra = SAMPLE_JSON.replace(
        '"schema_version": 1,',
        '"schema_version": 1,\n  "extra": true,',
    )
    _expect_fail(extra, "extra key")
    _expect_fail(SAMPLE_JSON, "wrong path", expected_path="other.metallib")
    _expect_fail(SAMPLE_JSON.rstrip("\n"), "missing newline")
    _expect_fail(SAMPLE_JSON + "{}\n", "trailing value")


def main(argv: list[str]) -> int:
    args = argv[1:]
    if args == ["--self-test"]:
        self_test()
        print("ok self-test")
        return 0

    schema_only = False
    if args and args[0] == "--schema-only":
        schema_only = True
        args = args[1:]
    if len(args) not in (1, 2):
        print(
            f"usage: {argv[0]} [--schema-only] report.json [expected-archive-path]",
            file=sys.stderr,
        )
        print(f"       {argv[0]} --self-test", file=sys.stderr)
        return 2

    path = args[0]
    expected = args[1] if len(args) == 2 else None
    try:
        with open(path, "r", encoding="utf-8") as handle:
            raw = handle.read()
        document = validate_text(raw, schema_only=schema_only, expected_path=expected)
    except (OSError, UnicodeError, ReportError) as exc:
        print(f"invalid report: {exc}", file=sys.stderr)
        return 1

    device = document["device"]
    arch = device["architecture"] if device["architecture"] is not None else "null"
    print(
        "ok: {name} ({arch}), {kernels} kernel(s), {size} bytes".format(
            name=device["name"],
            arch=arch,
            kernels=document["library"]["kernel_count"],
            size=document["archive"]["bytes"],
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
