"""Decode an explicit TinyFault TFX1 serial export locally."""

import argparse
import json
import re
import sys
import time
import zlib

BEGIN = re.compile(r"TFX1 BEGIN count=(0|[1-9][0-9]*)\Z")
EVENT = re.compile(
    r"TFX1 EVENT seq=(0|[1-9][0-9]*) id=(0|[1-9][0-9]*) "
    r"value=(0|[1-9][0-9]*) ms=(0|[1-9][0-9]*)\Z"
)
END = re.compile(r"TFX1 END count=(0|[1-9][0-9]*) crc32=([0-9A-F]{8})\Z")
MAX_EVENTS = 100000
MAX_LINE = 96


class ExportError(ValueError):
    """Invalid or incomplete TFX1 transfer."""


def _number(raw: str, maximum: int, field: str) -> int:
    if len(raw) > len(str(maximum)):
        raise ExportError(f"{field} exceeds {maximum}")
    value = int(raw)
    if value > maximum:
        raise ExportError(f"{field} exceeds {maximum}")
    return value


def parse(lines) -> list[dict[str, int]]:
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
    expected = _number(match.group(1), MAX_EVENTS, "count")
    events: list[dict[str, int]] = []
    crc = 0
    for raw in iterator:
        if len(raw) > MAX_LINE:
            raise ExportError("export line is too long")
        line = raw.rstrip("\r\n")
        end = END.fullmatch(line)
        if end is not None:
            count = _number(end.group(1), MAX_EVENTS, "end count")
            if count != expected or len(events) != expected:
                raise ExportError("event count does not match BEGIN and END")
            if int(end.group(2), 16) != crc:
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


def bounded_lines(source):
    while True:
        raw = source.readline(MAX_LINE + 1)
        if not raw:
            return
        if len(raw) > MAX_LINE:
            raise ExportError("export line is too long")
        yield raw


def capture(port: str, timeout: float) -> list[str]:
    try:
        import serial  # Optional; file decoding uses only the standard library.
    except ImportError as exc:
        raise ExportError("PySerial is required for --port") from exc
    lines: list[str] = []
    with serial.Serial(port, 115200, timeout=1, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        time.sleep(2)
        link.reset_input_buffer()
        link.write(b"x")
        link.flush()
        deadline = time.monotonic() + timeout
        started = False
        resent_after_ready = False
        while time.monotonic() < deadline:
            raw = link.read_until(b"\n", MAX_LINE + 1)
            if len(raw) > MAX_LINE:
                raise ExportError("serial export line is too long")
            line = raw.decode("ascii", errors="replace").rstrip("\r\n")
            if line.startswith("TF_ERROR"):
                raise ExportError(f"device reported: {line}")
            if not started:
                if line.startswith("TF_READY") and not resent_after_ready:
                    link.write(b"x")
                    link.flush()
                    resent_after_ready = True
                if line.startswith("TFX"):
                    started = True
                else:
                    continue
            lines.append(line)
            if len(lines) > MAX_EVENTS + 2:
                raise ExportError("serial export exceeds event limit")
            if line.startswith("TFX1 END"):
                return lines
    raise ExportError("serial export timed out or ended without TFX1 END")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", default="-", help="export text file, or - for stdin")
    parser.add_argument("--port", help="request export from an ESP32 serial port (needs PySerial)")
    parser.add_argument("--timeout", type=float, default=20.0, help="serial timeout in seconds")
    parser.add_argument("--json", action="store_true", help="print JSON instead of a text table")
    args = parser.parse_args()
    if args.port and args.input != "-":
        parser.error("use either an input file or --port")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        if args.port:
            events = parse(capture(args.port, args.timeout))
        elif args.input == "-":
            events = parse(bounded_lines(sys.stdin))
        else:
            with open(args.input, encoding="ascii") as source:
                events = parse(bounded_lines(source))
    except (ExportError, OSError, UnicodeError) as exc:
        print(f"TinyFault export error: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps({"format": "TFX1", "count": len(events), "events": events}))
    else:
        print(f"TFX1: {len(events)} event(s)")
        for event in events:
            print(
                f"seq={event['sequence']} id={event['id']} value={event['value']} "
                f"ms={event['timestamp_ms']}"
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
