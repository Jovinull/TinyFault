"""Combine an explicit TinyFault incident export with an explicit ESP-IDF analysis.

The core dump itself is never read or transmitted by this tool. Run Espressif's
esp-coredump info_corefile with the exact application ELF to create the optional
analysis text. Keep the dump and that text private.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from tinyfault_decode import ExportError, parse_incident

NATIVE = re.compile(
    r"TFN1 state=(not_configured|partition_missing|absent|valid|invalid|read_error) "
    r"error=(-?\d+) size=(\d+) summary=([01]) summary_error=(-?\d+) "
    r"elf_prefix=([012]) sha=(unknown|[0-9a-f]{1,64}) pc=([0-9A-F]{8})"
)
FRAME = re.compile(
    r"^\s*#0\s+(?:(0x[0-9a-fA-F]+)\s+in\s+)?([A-Za-z_][A-Za-z0-9_:$<>.~]*)",
    re.M,
)


def read_bounded(path: Path, maximum: int) -> str:
    if path.stat().st_size > maximum:
        raise ValueError(f"input too large: {path.name}")
    return path.read_text(encoding="utf-8")


def parse_native(text: str) -> dict:
    lines = [line.strip() for line in text.splitlines() if line.startswith("TFN1")]
    if len(lines) != 1:
        raise ValueError("expected exactly one explicit TFN1 response")
    match = NATIVE.fullmatch(lines[0])
    if not match:
        raise ValueError("malformed or unsupported TFN1 response")
    state, error, size, summary, summary_error, relation, sha, pc = match.groups()
    result = {
        "state": state, "native_error": int(error), "image_size": int(size),
        "summary_available": summary == "1", "summary_error": int(summary_error),
        "running_elf_prefix_relation": {"0": "unknown", "1": "match", "2": "mismatch"}[relation],
        "dump_elf_sha_prefix": None if sha == "unknown" else sha,
        "exception_pc": None if summary == "0" else "0x" + pc.lower(),
    }
    if state != "valid" and result["summary_available"]:
        raise ValueError("inconsistent native status metadata")
    if state == "valid" and result["image_size"] == 0:
        raise ValueError("valid dump has zero size")
    if state in {"not_configured", "partition_missing", "absent"} and result["image_size"] != 0:
        raise ValueError("absent or unavailable dump has nonzero size")
    if not result["summary_available"] and (sha != "unknown" or relation != "0" or pc != "00000000"):
        raise ValueError("unavailable summary contains crash metadata")
    if sha == "unknown" and relation != "0":
        raise ValueError("ELF relation has no dump hash prefix")
    return result


def correlate(incident: str, native_text: str, official_text: str | None = None,
              elf_bytes: bytes | None = None) -> dict:
    report = parse_incident(incident.splitlines())
    if report["format"] != "TFI1" or report["reset"]["platform"] != "esp32":
        raise ValueError("an ESP32 TFI1 incident is required")
    native = parse_native(native_text)
    if (official_text is None) != (elf_bytes is None):
        raise ValueError("official analysis and its ELF must be supplied together")
    if official_text is not None:
        if native["state"] != "valid" or not native["summary_available"]:
            raise ValueError("official analysis requires a valid inspected dump")
        prefix = native["dump_elf_sha_prefix"]
        if prefix is None or not hashlib.sha256(elf_bytes).hexdigest().startswith(prefix):
            raise ValueError("ELF SHA-256 does not match the dump's recorded prefix")
        if len(official_text) > 4_000_000:
            raise ValueError("official analysis is too large")
        reason = re.search(r"^Panic reason:\s*(.+)$", official_text, re.M)
        stack_marker = "CURRENT THREAD STACK"
        stack_start = official_text.find(stack_marker)
        stack_end = official_text.find("THREADS INFO", stack_start) if stack_start >= 0 else -1
        stack_text = official_text[stack_start:stack_end if stack_end >= 0 else None]
        frame = FRAME.search(stack_text)
        if not reason or not frame or stack_start < 0:
            raise ValueError("official analysis is incomplete or unsupported")
        native["official_analysis"] = {
            "status": "parsed", "panic_reason": reason.group(1)[:256],
            "top_frame_pc": frame.group(1).lower() if frame.group(1) else None,
            "top_frame_symbol": frame.group(2),
            "elf_sha256": hashlib.sha256(elf_bytes).hexdigest(),
        }
    else:
        native["official_analysis"] = {"status": "not_performed"}
    report["native_dump"] = native
    report["relationship"] = "unproven: event order and dump presence do not establish cause or boot identity"
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("incident", type=Path, help="complete TFI1/TFX1 export")
    parser.add_argument("native", type=Path, help="one explicit TFN1 response")
    parser.add_argument("--official-info", type=Path, help="private Espressif info_corefile output")
    parser.add_argument("--elf", type=Path, help="exact application ELF used by info_corefile")
    args = parser.parse_args()
    if bool(args.official_info) != bool(args.elf):
        parser.error("--official-info and --elf must be supplied together")
    try:
        if args.elf and args.elf.stat().st_size > 64_000_000:
            raise ValueError("ELF is too large")
        result = correlate(read_bounded(args.incident, 2_000_000),
                           read_bounded(args.native, 4_096),
                           read_bounded(args.official_info, 4_000_000) if args.official_info else None,
                           args.elf.read_bytes() if args.elf else None)
    except (OSError, UnicodeError, ValueError, ExportError) as exc:
        print(f"TinyFault correlation error: {exc}", file=sys.stderr)
        return 2
    json.dump(result, sys.stdout, indent=2)
    print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
