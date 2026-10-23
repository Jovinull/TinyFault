"""Check ESP32Diagnostics over a serial port after an authorized upload."""

import argparse
import re
import time

import serial


def check(lines: list[str]) -> None:
    output = "\n".join(lines)
    boot = re.search(r"TF_BOOT native=(\d+) reason=(\w+) chip=([^ ]+) flash=(\d+)", output)
    recovery = re.search(r"TF_RECOVERY status=(\d+) count=(\d+) us=(\d+)", output)
    events = re.findall(r"TF_EVENT seq=(\d+) id=(\d+) value=(\d+) ms=(\d+)", output)
    ram = re.search(r"TF_RAM_RECORD iterations=10000 us=(\d+)", output)
    if boot is None or recovery is None or ram is None or "TF_READY" not in output:
        raise AssertionError(f"Incomplete diagnostics boot:\n{output}")
    if boot.group(2) != "software" or recovery.group(1) != "0":
        raise AssertionError(f"Unexpected reset reason or recovery status:\n{output}")
    if int(recovery.group(2)) < 1 or not any(int(event[1]) == 0xFF00 for event in events):
        raise AssertionError(f"Boot event was not recovered:\n{output}")
    if "TF_WRITE" in output:
        raise AssertionError(f"Firmware wrote NVS on repeat boot:\n{output}")
    print(
        f"Hardware serial check passed: chip={boot.group(3)} flash={boot.group(4)} bytes, "
        f"software reset, {recovery.group(2)} recovered event(s), "
        f"recovery={recovery.group(3)} us, 10000 RAM records={ram.group(1)} us"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Confirmed ESP32 serial port, e.g. COM6")
    args = parser.parse_args()
    lines: list[str] = []
    with serial.Serial(args.port, 115200, timeout=1, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        time.sleep(2)
        link.reset_input_buffer()
        link.write(b"r")
        link.flush()
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            line = link.readline().decode("utf-8", errors="replace").strip()
            if line.startswith("TF_"):
                print(line)
                lines.append(line)
                if line.startswith("TF_READY"):
                    break
    check(lines)


if __name__ == "__main__":
    main()
