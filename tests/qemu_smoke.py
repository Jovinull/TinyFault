"""Verify that the ESP32 example recovers an NVS event after a QEMU restart."""

import argparse
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time


def boot(qemu: Path, flash: Path) -> str:
    command = [
        str(qemu),
        "-nographic",
        "-machine",
        "esp32",
        "-global",
        "driver=timer.esp32.timg,property=wdt_disable,value=true",
        "-drive",
        f"file={flash},if=mtd,format=raw",
    ]
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        creationflags=flags,
    )
    lines: queue.Queue[bytes] = queue.Queue()

    def read_output() -> None:
        assert process.stdout is not None
        for line in process.stdout:
            lines.put(line)

    threading.Thread(target=read_output, daemon=True).start()
    captured = bytearray()
    deadline = time.monotonic() + 30.0
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None and lines.empty():
                break
            try:
                line = lines.get(timeout=0.5)
            except queue.Empty:
                continue
            captured.extend(line)
            if b"Boot event stored. Reset to see it recovered." in captured:
                break
        else:
            raise RuntimeError(
                "QEMU boot timed out:\n" + captured.decode("utf-8", errors="replace")[-3000:]
            )
        output = captured.decode("utf-8", errors="replace")
        if "Boot event stored. Reset to see it recovered." not in output:
            raise RuntimeError(f"Sketch did not finish setup:\n{output}")
        return output
    finally:
        if process.poll() is None:
            assert process.stdin is not None
            process.stdin.write(b"\x01x")  # QEMU -nographic quit sequence.
            process.stdin.flush()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if process.stdin:
            process.stdin.close()
        if process.stdout:
            process.stdout.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(
        prefix="tinyfault-qemu-", dir=args.image.resolve().parent
    ) as directory:
        flash = Path(directory) / "flash.bin"
        shutil.copyfile(args.image, flash)
        first = boot(args.qemu.resolve(), flash)
        second = boot(args.qemu.resolve(), flash)
    if "seq=" in first:
        raise AssertionError(f"Fresh flash unexpectedly contained events:\n{first}")
    if "seq=1 id=1 value=0" not in second:
        raise AssertionError(f"Second boot did not recover the first event:\n{second}")
    print("QEMU ESP32 recovery passed: second boot recovered seq=1 id=1")


if __name__ == "__main__":
    main()
