"""Read-only M7 ESP32 bench check; commands never request a persistent write."""

import argparse
import json
from pathlib import Path
import re
import secrets
import sys
import time
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from tinyfault_correlate import correlate, parse_native


BOOT = re.compile(r"TF_BOOT native=(\d+) reason=([a-z_]+)")
RECOVERY = re.compile(r"TF_RECOVERY status=(\d+) count=(\d+) us=(\d+) ram_count=(\d+)")
EVENT = re.compile(r"TF_EVENT seq=(\d+) id=(\d+) value=(\d+) ms=(\d+)")
EXPECTED_M5 = [(1, 4097, 1, 1039), (2, 4098, 3, 1055)]


def receive(link, end_prefix: str, timeout: float = 20.0) -> list[str]:
    lines = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = link.read_until(b"\n", 512)
        if len(raw) >= 512:
            raise AssertionError("serial line exceeds limit")
        line = raw.decode("ascii", errors="replace").strip()
        if line.startswith(("TF_", "TFI1 ", "TFX1 ", "TFN1 ")):
            if "\ufffd" in line:
                raise AssertionError("corrupted diagnostic line")
            lines.append(line)
            if line.startswith("TF_ERROR"):
                raise AssertionError(f"device error: {line}")
            if line.startswith(end_prefix):
                return lines
    raise TimeoutError(f"no {end_prefix} within {timeout}s; received {lines}")


def send(link, command: bytes, end_prefix: str) -> tuple[list[str], float]:
    link.reset_input_buffer()
    started = time.perf_counter_ns()
    if link.write(command) != len(command):
        raise AssertionError("incomplete command transmission")
    link.flush()
    lines = receive(link, end_prefix)
    return lines, (time.perf_counter_ns() - started) / 1_000_000.0


def parse_boot(lines: list[str]) -> tuple[int, str, int, list[tuple[int, int, int, int]]]:
    boot = next((BOOT.fullmatch(x) for x in lines if x.startswith("TF_BOOT")), None)
    recovery = next((RECOVERY.fullmatch(x) for x in lines if x.startswith("TF_RECOVERY")), None)
    events = [tuple(map(int, match.groups())) for line in lines
              if (match := EVENT.fullmatch(line))]
    if boot is None or recovery is None or recovery.group(1) != "0":
        raise AssertionError(f"boot or recovery failed: {lines}")
    if int(recovery.group(2)) != len(events) or recovery.group(4) != "0":
        raise AssertionError("recovery count or RAM state disagrees with output")
    if events != EXPECTED_M5 or any(line.startswith("TF_WRITE") for line in lines):
        raise AssertionError("M5 records changed or boot wrote storage")
    return int(boot.group(1)), boot.group(2), int(recovery.group(3)), events


def inspect(link) -> tuple[str, float]:
    lines, elapsed = send(link, b"q", "TFN1 ")
    native_line = lines[-1]
    native = parse_native(native_line + "\n")
    if native["state"] != "absent" or native["native_error"] != 0:
        raise AssertionError(f"unexpected native dump state: {native}")
    return native_line + "\n", elapsed


def export(link, native_text: str, reset_native: int, reset_reason: str) -> tuple[dict, float]:
    request = secrets.token_hex(4).upper()
    lines, elapsed = send(link, b"X" + request.encode("ascii") + b"\n", "TFX1 END")
    frame = [line for line in lines if line.startswith(("TFI1 ", "TFX1 "))]
    incident = "\n".join(frame) + "\n"
    report = correlate(incident, native_text)
    if report["reset"] != {"platform": "esp32", "reason": reset_reason,
                           "native": reset_native, "evidence": "observed"}:
        raise AssertionError("reset metadata differs from observed boot")
    events = [(event["sequence"], event["id"], event["value"], event["timestamp_ms"])
              for event in report["events"]]
    if events != EXPECTED_M5 or report["count"] != len(EXPECTED_M5):
        raise AssertionError("export differs from recovered M5 records")
    if not all(f"req={request}" in line for line in (frame[0], frame[1], frame[-1])):
        raise AssertionError("request identifier mismatch")
    prefix, crc = frame[0].rsplit(" crc32=", 1)
    if zlib.crc32(prefix.encode("ascii")) != int(crc, 16):
        raise AssertionError("independent TFI1 CRC mismatch")
    event_lines = [line for line in frame if line.startswith("TFX1 EVENT")]
    event_crc = zlib.crc32("".join(line + "\n" for line in event_lines).encode("ascii"))
    if event_crc != int(frame[-1].rsplit("crc32=", 1)[1], 16):
        raise AssertionError("independent TFX1 CRC mismatch")
    if report["native_dump"]["state"] != "absent" or "unproven" not in report["relationship"]:
        raise AssertionError("incident correlation overstated native evidence")
    completion = receive(link, "TF_EXPORT")[-1]
    if not re.fullmatch(r"TF_EXPORT status=0 us=\d+", completion):
        raise AssertionError("device did not complete export successfully")
    return report, elapsed


def main() -> None:
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--json-output", type=Path, required=True,
                        help="private, ignored path for the interpreted report")
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.4, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        time.sleep(0.2)
        link.dtr = True
        time.sleep(0.2)
        link.dtr = False
        native_code, reason, first_recovery, _ = parse_boot(receive(link, "TF_READY"))
        native_text, first_query = inspect(link)
        first_report, first_export = export(link, native_text, native_code, reason)
        ram_lines, _ = send(link, b"r", "TF_RAM")
        if not re.fullmatch(r"TF_RAM status=0 count=1 value=\d+ ms=\d+ us=\d+", ram_lines[-1]):
            raise AssertionError("RAM-only breadcrumb failed")
        send(link, b"s", "TF_COMMAND software_reset")
        native_code, reason, second_recovery, _ = parse_boot(receive(link, "TF_READY"))
        if (native_code, reason) != (3, "software"):
            raise AssertionError("software reset classification changed")
        native_text, second_query = inspect(link)
        second_report, second_export = export(link, native_text, native_code, reason)
        if first_report["events"] != second_report["events"]:
            raise AssertionError("persisted events changed across software reset")
        args.json_output.write_text(json.dumps(second_report, indent=2) + "\n", encoding="utf-8")
        print(f"M7 phase 1 passed: {len(EXPECTED_M5)} M5 events; native dump absent; reset=3/software")
        print(f"recovery_us={[first_recovery, second_recovery]}")
        print(f"serial_query_round_trip_ms={[round(first_query, 3), round(second_query, 3)]}")
        print(f"serial_export_round_trip_ms={[round(first_export, 3), round(second_export, 3)]}")


if __name__ == "__main__":
    main()
