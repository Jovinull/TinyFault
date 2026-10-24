"""Decode an explicit TinyFault TFX1 serial export locally."""

import argparse
import itertools
import json
import re
import secrets
import sys
import time
import zlib

BEGIN = re.compile(r"TFX1 BEGIN(?: req=([0-9A-F]{8}))? count=(0|[1-9][0-9]*)\Z")
EVENT = re.compile(
    r"TFX1 EVENT seq=(0|[1-9][0-9]*) id=(0|[1-9][0-9]*) "
    r"value=(0|[1-9][0-9]*) ms=(0|[1-9][0-9]*)\Z"
)
END = re.compile(r"TFX1 END(?: req=([0-9A-F]{8}))? count=(0|[1-9][0-9]*) crc32=([0-9A-F]{8})\Z")
META = re.compile(
    r"(TFI1 META(?: req=([0-9A-F]{8}))? platform=(esp32|avr) "
    r"reset=([a-z_]{1,24}) native=(-?(?:0|[1-9][0-9]*)) "
    r"trust=(observed|unverified)) crc32=([0-9A-F]{8})\Z"
)
MAX_EVENTS = 100000
MAX_LINE = 128


class ExportError(ValueError):
    """Invalid or incomplete TFX1 transfer."""


def _number(raw: str, maximum: int, field: str) -> int:
    if len(raw) > len(str(maximum)):
        raise ExportError(f"{field} exceeds {maximum}")
    value = int(raw)
    if value > maximum:
        raise ExportError(f"{field} exceeds {maximum}")
    return value


def parse(lines, expected_request: str | None = None) -> list[dict[str, int]]:
    iterator = iter(lines)
    try:
        first = next(iterator)
    except StopIteration as exc:
        raise ExportError("missing TFX1 BEGIN") from exc
    if len(first) > MAX_LINE:
        raise ExportError("export line is too long")
    match = BEGIN.fullmatch(first.rstrip("\r\n"))
    if match is None:
        raise ExportError("invalid or unsupported export header")
    request = match.group(1)
    if expected_request is not None and request != expected_request:
        raise ExportError("export belongs to another request")
    expected = _number(match.group(2), MAX_EVENTS, "count")
    events: list[dict[str, int]] = []
    crc = 0
    for raw in iterator:
        if len(raw) > MAX_LINE:
            raise ExportError("export line is too long")
        line = raw.rstrip("\r\n")
        end = END.fullmatch(line)
        if end is not None:
            if end.group(1) != request:
                raise ExportError("request identifier differs between BEGIN and END")
            count = _number(end.group(2), MAX_EVENTS, "end count")
            if count != expected or len(events) != expected:
                raise ExportError("event count does not match BEGIN and END")
            if int(end.group(3), 16) != crc:
                raise ExportError("transfer CRC-32 mismatch")
            if next(iterator, None) is not None:
                raise ExportError("unexpected data after TFX1 END")
            return events
        match = EVENT.fullmatch(line)
        if match is None:
            raise ExportError("invalid TFX1 event or missing END")
        if len(events) >= expected:
            raise ExportError("more events than BEGIN declared")
        sequence = _number(match.group(1), 0xFFFFFFFF, "sequence")
        event_id = _number(match.group(2), 0xFFFF, "ID")
        value = _number(match.group(3), 0xFFFFFFFF, "value")
        timestamp = _number(match.group(4), 0xFFFFFFFF, "timestamp")
        if sequence == 0 or (events and sequence <= events[-1]["sequence"]):
            raise ExportError("sequences must be positive and increasing")
        events.append({"sequence": sequence, "id": event_id, "value": value, "timestamp_ms": timestamp})
        crc = zlib.crc32((line + "\n").encode("ascii"), crc)
    raise ExportError("missing TFX1 END")


def parse_incident(lines, expected_request: str | None = None) -> dict:
    iterator = iter(lines)
    first = next(iterator, None)
    if first is None:
        raise ExportError("missing export header")
    if not first.startswith("TFI"):
        events = parse(itertools.chain((first,), iterator), expected_request=expected_request)
        return {"format": "TFX1", "count": len(events), "events": events}
    if len(first) > MAX_LINE:
        raise ExportError("incident metadata line is too long")
    match = META.fullmatch(first.rstrip("\r\n"))
    if match is None:
        raise ExportError("invalid or unsupported incident metadata")
    if expected_request is not None and match.group(2) != expected_request:
        raise ExportError("incident belongs to another request")
    if int(match.group(7), 16) != zlib.crc32(match.group(1).encode("ascii")):
        raise ExportError("incident metadata CRC-32 mismatch")
    native = int(match.group(5))
    if native < -0x80000000 or native > 0x7fffffff:
        raise ExportError("native reset code exceeds signed 32-bit range")
    if match.group(6) == "unverified" and match.group(4) != "unknown":
        raise ExportError("unverified reset cannot be classified")
    events = parse(iterator, expected_request=match.group(2))
    return {"format": "TFI1", "reset": {
        "platform": match.group(3), "reason": match.group(4),
        "native": native, "evidence": match.group(6)},
        "count": len(events), "events": events}


def bounded_lines(source):
    while True:
        raw = source.readline(MAX_LINE + 1)
        if not raw:
            return
        if len(raw) > MAX_LINE:
            raise ExportError("export line is too long")
        yield raw


def capture(port: str, timeout: float) -> tuple[list[str], str]:
    try:
        import serial  # Optional; file decoding uses only the standard library.
    except ImportError as exc:
        raise ExportError("PySerial is required for --port") from exc
    lines: list[str] = []
    request = secrets.token_hex(4).upper()
    with serial.Serial(port, 115200, timeout=0.2, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        startup_deadline = time.monotonic() + min(timeout, 5.0)
        while time.monotonic() < startup_deadline:
            raw = link.read_until(b"\n", MAX_LINE + 1)
            if len(raw) > MAX_LINE:
                raise ExportError("serial startup line is too long")
            if raw.startswith(b"TF_READY"):
                break
        link.reset_input_buffer()
        command = b"X" + request.encode("ascii") + b"\n"
        if link.write(command) != len(command):
            raise ExportError("serial command write was incomplete")
        link.flush()
        deadline = time.monotonic() + timeout
        started = False
        seen_meta = False
        while time.monotonic() < deadline:
            raw = link.read_until(b"\n", MAX_LINE + 1)
            if len(raw) > MAX_LINE:
                raise ExportError("serial export line is too long")
            line = raw.decode("ascii", errors="replace").rstrip("\r\n")
            if line.startswith("TF_ERROR"):
                raise ExportError(f"device reported: {line}")
            if not started:
                if line.startswith("TF_READY"):
                    raise ExportError("device became ready after request; retry capture")
                if line.startswith("TFI"):
                    header = META.fullmatch(line)
                    if seen_meta or header is None or header.group(2) != request:
                        raise ExportError("stale or invalid incident response")
                    seen_meta = True
                    lines.append(line)
                    continue
                if line.startswith("TFX"):
                    header = BEGIN.fullmatch(line)
                    if header is None or header.group(1) != request:
                        raise ExportError("stale or invalid export response")
                    started = True
                else:
                    continue
            lines.append(line)
            if len(lines) > MAX_EVENTS + 3:
                raise ExportError("serial export exceeds event limit")
            if line.startswith("TFX1 END"):
                return lines, request
    raise ExportError("serial export timed out or ended without TFX1 END")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", default="-", help="export text file, or - for stdin")
    parser.add_argument("--port", help="request export from a supported serial board (needs PySerial)")
    parser.add_argument("--timeout", type=float, default=20.0, help="serial timeout in seconds")
    parser.add_argument("--json", action="store_true", help="print JSON instead of a text table")
    args = parser.parse_args()
    if args.port and args.input != "-":
        parser.error("use either an input file or --port")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        if args.port:
            lines, request = capture(args.port, args.timeout)
            report = parse_incident(lines, expected_request=request)
        elif args.input == "-":
            report = parse_incident(bounded_lines(sys.stdin))
        else:
            with open(args.input, encoding="ascii") as source:
                report = parse_incident(bounded_lines(source))
    except (ExportError, OSError, UnicodeError) as exc:
        print(f"TinyFault export error: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report))
    else:
        if report["format"] == "TFI1":
            reset = report["reset"]
            print(f"TFI1: platform={reset['platform']} reset={reset['reason']} "
                  f"native={reset['native']} evidence={reset['evidence']}")
        else:
            print("TFX1: reset information unavailable")
        print(f"Observed persisted events: {report['count']}")
        for event in report["events"]:
            print(
                f"seq={event['sequence']} id={event['id']} value={event['value']} "
                f"ms={event['timestamp_ms']}"
            )
        print("Cause and completion of unrecorded operations: unknown")
        print("Timestamps are relative to the boot that recorded each event")
    return 0


if __name__ == "__main__":
    sys.exit(main())
