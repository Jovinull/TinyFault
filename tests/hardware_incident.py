"""Validate explicit M5 checkpoints and incident export on an authorized ESP32."""

import argparse
import re
import secrets
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tinyfault_decode import parse_incident

BOOT = re.compile(r"TF_BOOT native=(\d+) reason=([a-z_]+)\Z")
RECOVERY = re.compile(r"TF_RECOVERY status=(\d+) count=(\d+) us=(\d+) ram_count=(\d+)\Z")
EVENT = re.compile(r"TF_EVENT seq=(\d+) id=(\d+) value=(\d+) ms=(\d+)\Z")
WRITE = re.compile(r"TF_WRITE status=(\d+) id=(\d+) seq=(\d+) value=(\d+) ms=(\d+) us=(\d+) count=(\d+) nvs=(-?\d+)\Z")
RAM = re.compile(r"TF_RAM status=(\d+) count=(\d+) value=(\d+) ms=(\d+) us=(\d+)\Z")
BATCH = re.compile(r"TF_RAM_BATCH status=(\d+) iterations=10000 us=(\d+)\Z")
EXPORT = re.compile(r"TF_EXPORT status=(\d+) us=(\d+)\Z")


def read_until(link, marker: str, seconds: float) -> list[str]:
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
                raise AssertionError(f"device error: {line}")
            if line.startswith(marker):
                return lines
    raise TimeoutError(f"no {marker} in {seconds}s: {lines}")


def send(link, command: bytes, marker: str, seconds: float = 15) -> list[str]:
    link.reset_input_buffer()
    if link.write(command) != len(command):
        raise AssertionError("serial command write incomplete")
    link.flush()
    return read_until(link, marker, seconds)


def boot(link, expected=None):
    lines = read_until(link, "TF_READY", 20)
    boot_line = next((BOOT.fullmatch(x) for x in lines if x.startswith("TF_BOOT")), None)
    recovery = next((RECOVERY.fullmatch(x) for x in lines if x.startswith("TF_RECOVERY")), None)
    events = [tuple(map(int, match.groups())) for x in lines if (match := EVENT.fullmatch(x))]
    if boot_line is None or recovery is None or recovery.group(1) != "0":
        raise AssertionError(f"incomplete boot: {lines}")
    if int(recovery.group(2)) != len(events) or recovery.group(4) != "0":
        raise AssertionError(f"invalid recovery/RAM count: {lines}")
    if any(x.startswith("TF_WRITE") for x in lines):
        raise AssertionError("unsolicited persistent write during boot")
    if expected is not None and events != expected:
        raise AssertionError(f"recovered events changed: {events} != {expected}")
    return (int(boot_line.group(1)), boot_line.group(2), int(recovery.group(3)), events)


def checkpoint(link, command: bytes, event_id: int):
    lines = send(link, command, "TF_WRITE")
    match = WRITE.fullmatch(lines[-1])
    if match is None or match.group(1) != "0" or match.group(8) != "0" or int(match.group(2)) != event_id:
        raise AssertionError(f"checkpoint failed: {lines}")
    seq, value, timestamp, elapsed = map(int, match.group(3, 4, 5, 6))
    return (seq, event_id, value, timestamp), elapsed


def ram_event(link):
    match = RAM.fullmatch(send(link, b"r", "TF_RAM")[-1])
    if match is None or match.group(1) != "0":
        raise AssertionError("RAM breadcrumb failed")
    return int(match.group(5))


def export(link, expected, native: int, reason: str):
    request = secrets.token_hex(4).upper()
    lines = send(link, b"X" + request.encode("ascii") + b"\n", "TFX1 END")
    frame = [line for line in lines if line.startswith(("TFI1 ", "TFX1 "))]
    report = parse_incident(frame, expected_request=request)
    if report["reset"] != {"platform": "esp32", "reason": reason,
                            "native": native, "evidence": "observed"}:
        raise AssertionError(f"reset metadata changed: {report}")
    actual = [(item["sequence"], item["id"], item["value"], item["timestamp_ms"])
              for item in report["events"]]
    if actual != expected or report["count"] != len(expected):
        raise AssertionError(f"export differs from persisted events: {actual} != {expected}")
    meta_prefix, meta_crc = frame[0].rsplit(" crc32=", 1)
    if zlib.crc32(meta_prefix.encode("ascii")) != int(meta_crc, 16):
        raise AssertionError("independent TFI1 CRC mismatch")
    event_lines = [line for line in frame if line.startswith("TFX1 EVENT")]
    crc = zlib.crc32("".join(line + "\n" for line in event_lines).encode("ascii"))
    if int(frame[-1].rsplit("crc32=", 1)[1], 16) != crc:
        raise AssertionError("independent TFX1 CRC mismatch")
    timing = EXPORT.fullmatch(read_until(link, "TF_EXPORT", 10)[-1])
    if timing is None or timing.group(1) != "0":
        raise AssertionError("export did not complete")
    return request, int(timing.group(2))


def main():
    import serial  # PySerial is needed only for the physical run.

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.4, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        time.sleep(0.2)  # Let serial control lines settle after opening.
        link.dtr = True
        time.sleep(0.2)
        link.dtr = False
        native, reason, initial_us, initial = boot(link)
        if initial:
            raise AssertionError("M5 namespace is not empty; stop before writing")
        first, first_us = checkpoint(link, b"a", 0x1001)
        ram1_us = ram_event(link)
        second, second_us = checkpoint(link, b"c", 0x1002)
        ram2_us = ram_event(link)
        expected = [first, second]
        send(link, b"s", "TF_COMMAND software_reset")
        native, reason, recovery_us, recovered = boot(link, expected)
        if (native, reason) != (3, "software"):
            raise AssertionError(f"unexpected reset classification: {native}/{reason}")
        reports = [export(link, expected, native, reason) for _ in range(2)]
        if reports[0][0] == reports[1][0]:
            raise AssertionError("consecutive exports reused a request identifier")
        batches = []
        for _ in range(5):
            match = BATCH.fullmatch(send(link, b"b", "TF_RAM_BATCH")[-1])
            if match is None or match.group(1) != "0":
                raise AssertionError("RAM benchmark failed")
            batches.append(int(match.group(2)))
        recoveries = [recovery_us]
        for _ in range(2):
            send(link, b"s", "TF_COMMAND software_reset")
            native, reason, elapsed, _ = boot(link, expected)
            if (native, reason) != (3, "software"):
                raise AssertionError("software reset changed classification")
            recoveries.append(elapsed)
        print(f"initial reset={initial_us} us, checkpoints={[first_us, second_us]} us")
        print(f"RAM singles={[ram1_us, ram2_us]} us, 10000-record batches={batches} us")
        print(f"recovery n={len(recoveries)} range={min(recoveries)}..{max(recoveries)} us")
        print(f"serial exports={[duration for _, duration in reports]} us")
        print(f"recovered events={expected}, reset={native}/{reason}, consecutive requests distinct")
        print("M5 physical incident passed")


if __name__ == "__main__":
    main()
