# metal-probe

A small macOS command-line tool that reports the default Metal device and
GPU architecture, then writes a one-kernel `.metallib` binary archive.
Useful for checking what a GitHub Actions macOS runner (or any Mac) exposes
to Metal.

The archive is built with `MTLBinaryArchive`, so it only caches pipelines for
the GPU family of the machine it runs on.

## Build

`make probe` (or `make`) compiles `probe` with clang, ARC, Metal, and
Foundation. That target requires macOS:

```sh
make probe
```

The same build by hand:

```sh
clang -fobjc-arc -Wall -Wextra -c probe.m -o probe.o
clang -std=c11 -Wall -Wextra -Wpedantic -Werror -c probe_format.c -o probe_format.o
clang -o probe probe.o probe_format.o -framework Metal -framework Foundation
```

Formatting does not link Metal. On any host with clang and Python 3:

```sh
make test
```

`make test` runs the C unit tests for the formatter and the schema
validator's self-test.

## Run

```sh
./probe out.metallib
./probe --json out.metallib
./probe --help
```

The output path is required. `--json` may appear before or after it. `--`
ends flag parsing, so a path that starts with `-` can be passed after it.

Text is the default. `--json` writes only the JSON document to stdout.
Diagnostics always go to stderr. The process exits `0` after a complete
report (and after `--help`). It exits `1` for usage errors, a missing Metal
device, or a compile, archive, or serialize failure.

## Output schema

Schema version is **1**. A successful report always has the same keys, in
the order below. `device.architecture` is `null` when the device does not
report `MTLArchitecture.name` (an empty name is treated the same). Every
other string is present. Counts and sizes are non-negative integers.

| Field | Type | Meaning |
| --- | --- | --- |
| `schema_version` | integer | Always `1` for this document. |
| `device.name` | string | `MTLDevice.name` of the system default device. |
| `device.architecture` | string or null | `MTLDevice.architecture.name`, or null when unavailable. |
| `library.kernel_count` | integer >= 0 | Functions in the compiled Metal library. This tool compiles one kernel. |
| `archive.path` | string | Output path as it was passed on the command line. |
| `archive.bytes` | integer >= 0 | Size in bytes of the serialized binary archive. |

### Text

Six lines, each `key: value`, separated by LF, with a trailing LF. The keys
are fixed:

```text
schema_version
device_name
architecture
kernel_count
archive_path
archive_bytes
```

`architecture: null` means the architecture is absent. Every other value is
a string or a decimal integer. Strings are literal UTF-8. `\`, backspace,
form feed, newline, carriage return, and tab are escaped as `\\`, `\b`,
`\f`, `\n`, `\r`, and `\t`. Other bytes below `0x20` use `\u00XX`. Quotes
are not escaped in text.

### JSON

One JSON object, 2-space indent, LF newlines, trailing LF. Key order is part
of the schema:

```text
schema_version
device.name
device.architecture
library.kernel_count
archive.path
archive.bytes
```

Strings use standard JSON escaping. `/` is not escaped. A live Apple GPU
report has a non-empty `device.architecture`, `library.kernel_count >= 1`,
and `archive.bytes >= 1`.

### Sample output

The numbers below are an example of the formatting, not a capture from a
particular machine.

Text:

```text
schema_version: 1
device_name: Apple M2
architecture: applegpu_g14g
kernel_count: 1
archive_path: out.metallib
archive_bytes: 4096
```

JSON (`./probe --json out.metallib`):

```json
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
```

When the architecture is unavailable the same document uses
`"architecture": null` and the text line `architecture: null`.

## Checking a report

`scripts/validate_report.py` accepts a schema-version-1 document. By default
it also enforces the live-probe constraints (non-empty architecture, at least
one kernel, and a non-empty archive). `--schema-only` checks types and key
order only. Pass the archive path to require `archive.path` to match it.

```sh
./probe --json out.metallib | tee report.json
python3 scripts/validate_report.py report.json out.metallib
python3 scripts/validate_report.py --self-test
```

## Continuous integration

`.github/workflows/probe.yml` runs on the current GitHub-hosted arm64
images `macos-15` and `macos-26`. The `macos-14` label was retired on 2
November 2026, so the workflow no longer requests it. Each job runs
`make test`, builds `./probe`, runs `./probe --json out.metallib`, validates
that JSON against the live schema (including the archive path), and prints
the first 64 bytes of the metallib.

## Layout

| File | Role |
| --- | --- |
| `probe.m` | Metal device lookup, kernel compile, binary archive. |
| `probe_format.c` | Text and JSON formatting. No Metal calls. |
| `test_format.c` | Unit tests for the formatter, including the README samples. |
| `scripts/validate_report.py` | Schema check used by CI and `make test`. |
