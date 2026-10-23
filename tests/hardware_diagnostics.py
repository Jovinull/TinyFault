"""Exercise ESP32Diagnostics on an authorized board without uploading firmware."""

import argparse
import re
import statistics
import time

import serial


def read_marker(link: serial.Serial, marker: str, timeout: float) -> list[str]:
    lines: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = link.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("TF_"):
            print(line)
            lines.append(line)
            if line.startswith(marker):
                return lines
    raise TimeoutError(f"No {marker} within {timeout}s; observed: {lines}")


def check_boot(lines: list[str], required_sequences: set[int]) -> tuple[int, int]:
    output = "\n".join(lines)
    boot = re.search(r"TF_BOOT native=(\d+) reason=(\w+) chip=([^ ]+) flash=(\d+)", output)
    recovery = re.search(r"TF_RECOVERY status=(\d+) count=(\d+) us=(\d+)", output)
    ram = re.search(r"TF_RAM_RECORD iterations=10000 us=(\d+)", output)
    events = re.findall(r"TF_EVENT seq=(\d+) id=(\d+) value=(\d+) ms=(\d+)", output)
    if boot is None or recovery is None or ram is None or "TF_READY" not in output:
        raise AssertionError(f"Incomplete diagnostics boot:\n{output}")
    if boot.group(1) != "3" or boot.group(2) != "software" or not boot.group(3).startswith("ESP32"):
        raise AssertionError(f"Unexpected chip or software reset classification:\n{output}")
    if int(boot.group(4)) != 4194304 or recovery.group(1) != "0":
        raise AssertionError(f"Unexpected flash size or recovery status:\n{output}")
    sequences = [int(event[0]) for event in events]
    if len(sequences) != int(recovery.group(2)) or not sequences:
        raise AssertionError(f"Recovered count does not match events:\n{output}")
    if sequences != list(range(sequences[0], sequences[-1] + 1)):
        raise AssertionError(f"Recovered event sequence is not contiguous:\n{output}")
    if not required_sequences.issubset(sequences):
        raise AssertionError(f"Recent written events were not recovered:\n{output}")
    for sequence, event_id, value, _ in events:
        if int(event_id) == 0xFF01 and int(value) != int(sequence):
            raise AssertionError(f"Manual event value differs from sequence:\n{output}")
    if "TF_WRITE" in output:
        raise AssertionError(f"Firmware wrote NVS on repeat boot:\n{output}")
    return int(recovery.group(3)), int(ram.group(1))


def summarize(label: str, values: list[int]) -> None:
    print(
        f"{label}: n={len(values)} min={min(values)} median={statistics.median(values)} "
        f"mean={statistics.mean(values):.1f} max={max(values)} us"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Confirmed ESP32 serial port, e.g. COM6")
    parser.add_argument("--writes", type=int, default=3, help="Controlled NVS samples (1..4)")
    parser.add_argument("--resets", type=int, default=5, help="Software reset samples (1..10)")
    args = parser.parse_args()
    if not 1 <= args.writes <= 4 or not 1 <= args.resets <= 10:
        parser.error("writes must be 1..4 and resets must be 1..10")

    write_us: list[int] = []
    recovery_us: list[int] = []
    ram_us: list[int] = []
    written_sequences: set[int] = set()
    with serial.Serial(args.port, 115200, timeout=1, rtscts=False, dsrdtr=False) as link:
        link.dtr = False
        link.rts = False
        time.sleep(2)
        for _ in range(args.writes):
            link.reset_input_buffer()
            link.write(b"w")
            link.flush()
            lines = read_marker(link, "TF_WRITE kind=manual", 15)
            match = re.search(r"TF_WRITE kind=manual status=(\d+) seq=(\d+) us=(\d+)", lines[-1])
            if match is None or match.group(1) != "0":
                raise AssertionError(f"NVS write failed: {lines}")
            written_sequences.add(int(match.group(2)))
            write_us.append(int(match.group(3)))

        for index in range(args.resets):
            link.reset_input_buffer()
            link.write(b"r")
            link.flush()
            read_marker(link, "TF_COMMAND software_reset", 15)
            lines = read_marker(link, "TF_READY", 20)
            recovery, ram = check_boot(lines, written_sequences)
            recovery_us.append(recovery)
            ram_us.append(ram)
            print(f"Physical reset {index + 1}/{args.resets} passed")

    summarize("NVS write", write_us)
    summarize("NVS recovery", recovery_us)
    summarize("10000 RAM records", ram_us)


if __name__ == "__main__":
    main()
