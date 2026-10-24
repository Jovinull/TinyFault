"""Validate an already authorized AVREepromRecorder sketch on a serial Nano.

This script does not upload firmware. It writes at most five EEPROM events.
"""

import argparse
from pathlib import Path
import re
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tinyfault_decode import parse

EVENT = re.compile(r"TF_EVENT seq=(\d+) id=(\d+) value=(\d+) ms=(\d+)\Z")
RECOVERY = re.compile(r"TF_RECOVERY status=(\d+) count=(\d+) us=(\d+)\Z")
WRITE = re.compile(r"TF_WRITE status=(\d+) us=(\d+)\Z")


def read_until(link, marker, timeout):
    lines = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = link.read_until(b"\n", 121)
        if len(raw) > 120:
            raise AssertionError("serial line exceeded 120 bytes")
        line = raw.decode("ascii", errors="replace").strip()
        if line.startswith("TF_") or line.startswith("TFX1 "):
            lines.append(line)
            if line.startswith("TF_ERROR"):
                raise AssertionError(f"firmware error: {line}")
            if line.startswith(marker):
                return lines
    raise TimeoutError(f"no {marker} within {timeout}s; observed: {lines}")


def boot(link):
    lines = read_until(link, "TF_READY", 15)
    recovery = next((RECOVERY.fullmatch(line) for line in lines if line.startswith("TF_RECOVERY")), None)
    if recovery is None or recovery.group(1) != "0":
        raise AssertionError(f"recovery failed: {lines}")
    events = []
    for line in lines:
        match = EVENT.fullmatch(line)
        if match:
            events.append(tuple(map(int, match.groups())))
    if len(events) != int(recovery.group(2)):
        raise AssertionError(f"recovery count differs from events: {lines}")
    if any(events[i][0] + 1 != events[i + 1][0] for i in range(len(events) - 1)):
        raise AssertionError(f"recovered sequence is discontinuous: {events}")
    if any(line.startswith("TF_WRITE") for line in lines):
        raise AssertionError("firmware wrote EEPROM during boot")
    print(f"boot: {len(events)} event(s), recovery {recovery.group(3)} us")
    return events


def write_event(link):
    link.reset_input_buffer()
    link.write(b"w")
    link.flush()
    lines = read_until(link, "TF_EVENT", 15)
    write = next((WRITE.fullmatch(line) for line in lines if line.startswith("TF_WRITE")), None)
    event = EVENT.fullmatch(lines[-1])
    if write is None or write.group(1) != "0" or event is None:
        raise AssertionError(f"write was not confirmed: {lines}")
    result = tuple(map(int, event.groups()))
    print(f"write: seq={result[0]} {write.group(2)} us")
    return result


def reset_by_dtr(link):
    link.reset_input_buffer()
    link.dtr = True
    time.sleep(0.1)
    link.dtr = False
    return boot(link)


def export(link, expected):
    link.reset_input_buffer()
    link.write(b"x")
    link.flush()
    lines = read_until(link, "TFX1 END", 15)
    frame = [line for line in lines if line.startswith("TFX1 ")]
    decoded = parse(frame)
    actual = [(item["sequence"], item["id"], item["value"], item["timestamp_ms"])
              for item in decoded]
    if actual != expected:
        raise AssertionError(f"export differs from boot events: {actual} != {expected}")
    print(f"TFX1 export: {len(actual)} exact event(s)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="confirmed, authorized Nano serial port")
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.3, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        original = reset_by_dtr(link)
        written = [write_event(link) for _ in range(4)]
        recovered = reset_by_dtr(link)
        if recovered != (original + written)[-4:]:
            raise AssertionError(f"first reset lost or changed events: {recovered}")
        fifth = write_event(link)
        recovered = reset_by_dtr(link)
        if recovered != (written + [fifth])[-4:]:
            raise AssertionError(f"wrap/reset lost or changed events: {recovered}")
        export(link, recovered)
        print("AVR physical recorder and export passed")


if __name__ == "__main__":
    main()
