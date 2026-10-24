"""One-shot M5 Nano bench test after confirming an empty, authorized EEPROM region.

This script never uploads firmware. It requests exactly four EEPROM checkpoints.
"""

import argparse
import re
import secrets
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tinyfault_decode import parse_incident

BOOT = re.compile(r"TF_BOOT reset=unknown post_bootloader_mcusr=(\d+)\Z")
RECOVERY = re.compile(r"TF_RECOVERY count=(\d+) us=(\d+) ram_count=(\d+)\Z")
EVENT = re.compile(r"TF_EVENT seq=(\d+) id=(\d+) value=(\d+) ms=(\d+)\Z")
WRITE = re.compile(r"TF_WRITE status=(\d+) id=(\d+) value=(\d+) ms=(\d+) us=(\d+) count=(\d+)\Z")
RAM = re.compile(r"TF_RAM status=(\d+) count=(\d+) value=(\d+) ms=(\d+) us=(\d+)\Z")
BATCH = re.compile(r"TF_RAM_BATCH status=(\d+) iterations=10000 us=(\d+)\Z")
EXPORT = re.compile(r"TF_EXPORT status=(\d+) us=(\d+)\Z")


def read_until(link, marker, seconds=20):
    lines = []
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        raw = link.read_until(b"\n", 257)
        if len(raw) > 256:
            raise AssertionError("serial line too long")
        line = raw.decode("ascii", errors="replace").strip()
        if line.startswith(("TF_", "TFI1 ", "TFX1 ")):
            lines.append(line)
            if line.startswith("TF_ERROR"):
                raise AssertionError(f"firmware error: {line}")
            if line.startswith(marker):
                return lines
    raise TimeoutError(f"no {marker} in {seconds}s: {lines}")


def send(link, command, marker, seconds=20):
    link.reset_input_buffer()
    if link.write(command) != len(command):
        raise AssertionError("serial command write incomplete")
    link.flush()
    return read_until(link, marker, seconds)


def boot(link, expected=None):
    lines = read_until(link, "TF_READY")
    boot_match = next((BOOT.fullmatch(x) for x in lines if x.startswith("TF_BOOT")), None)
    recovery = next((RECOVERY.fullmatch(x) for x in lines if x.startswith("TF_RECOVERY")), None)
    events = [tuple(map(int, match.groups())) for x in lines if (match := EVENT.fullmatch(x))]
    if boot_match is None or recovery is None:
        raise AssertionError(f"incomplete boot: {lines}")
    if int(recovery.group(1)) != len(events) or recovery.group(3) != "0":
        raise AssertionError(f"invalid recovery/RAM count: {lines}")
    if expected is not None and events != expected:
        raise AssertionError(f"recovered events changed: {events} != {expected}")
    if any(x.startswith("TF_WRITE") for x in lines):
        raise AssertionError("unsolicited EEPROM write during boot")
    return int(boot_match.group(1)), int(recovery.group(2)), events


def reset(link, expected):
    link.reset_input_buffer()
    link.dtr = True
    time.sleep(0.1)
    link.dtr = False
    return boot(link, expected)


def checkpoint(link, command, event_id, expected_count):
    match = WRITE.fullmatch(send(link, command, "TF_WRITE")[-1])
    if (match is None or match.group(1) != "0" or
            int(match.group(2)) != event_id or int(match.group(6)) != expected_count):
        raise AssertionError("checkpoint telemetry invalid")
    value, timestamp, elapsed = map(int, match.group(3, 4, 5))
    return (expected_count, event_id, value, timestamp), elapsed


def report(link, expected, native):
    request = secrets.token_hex(4).upper()
    lines = send(link, b"X" + request.encode("ascii") + b"\n", "TFX1 END")
    frame = [line for line in lines if line.startswith(("TFI1 ", "TFX1 "))]
    decoded = parse_incident(frame, expected_request=request)
    if decoded["reset"] != {"platform": "avr", "reason": "unknown",
                            "native": native, "evidence": "unverified"}:
        raise AssertionError(f"AVR reset provenance misreported: {decoded}")
    actual = [(e["sequence"], e["id"], e["value"], e["timestamp_ms"])
              for e in decoded["events"]]
    if actual != expected or decoded["count"] != len(expected):
        raise AssertionError(f"export differs from recovered records: {actual} != {expected}")
    prefix, crc = frame[0].rsplit(" crc32=", 1)
    if zlib.crc32(prefix.encode("ascii")) != int(crc, 16):
        raise AssertionError("independent TFI1 CRC mismatch")
    event_lines = [line for line in frame if line.startswith("TFX1 EVENT")]
    event_crc = zlib.crc32("".join(x + "\n" for x in event_lines).encode("ascii"))
    if int(frame[-1].rsplit("crc32=", 1)[1], 16) != event_crc:
        raise AssertionError("independent TFX1 CRC mismatch")
    timing = EXPORT.fullmatch(read_until(link, "TF_EXPORT")[-1])
    if timing is None or timing.group(1) != "0":
        raise AssertionError("export did not complete")
    return request, int(timing.group(2)), decoded


def main():
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.4, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        time.sleep(0.2)
        native, initial_us, initial = reset(link, [])
        if initial:
            raise AssertionError("authorized EEPROM region is not empty")
        ram = RAM.fullmatch(send(link, b"r", "TF_RAM")[-1])
        if ram is None or ram.group(1) != "0" or ram.group(2) != "1":
            raise AssertionError("RAM breadcrumb failed")
        first, first_us = checkpoint(link, b"a", 0x1101, 1)
        second, second_us = checkpoint(link, b"d", 0x1103, 2)
        native, recovery_us, recovered = reset(link, [first, second])
        third, third_us = checkpoint(link, b"a", 0x1101, 3)
        fourth, fourth_us = checkpoint(link, b"d", 0x1103, 4)
        expected = [first, second, third, fourth]
        native, final_recovery_us, recovered = reset(link, expected)
        reports = [report(link, expected, native) for _ in range(2)]
        if reports[0][0] == reports[1][0]:
            raise AssertionError("consecutive reports reused a request ID")
        batches = []
        for _ in range(3):
            match = BATCH.fullmatch(send(link, b"b", "TF_RAM_BATCH")[-1])
            if match is None or match.group(1) != "0":
                raise AssertionError("RAM batch failed")
            batches.append(int(match.group(2)))
        print(f"post-bootloader MCUSR={native} (unverified); initial recovery={initial_us} us")
        print(f"checkpoints={[first_us, second_us, third_us, fourth_us]} us")
        print(f"recoveries={[recovery_us, final_recovery_us]} us; RAM single={ram.group(5)} us")
        print(f"RAM 10000-record batches={batches} us")
        print(f"exports={[item[1] for item in reports]} us; distinct requests")
        print(f"recovered={expected}; report={reports[-1][2]}")
        print("M5 AVR physical incident passed")


if __name__ == "__main__":
    main()
