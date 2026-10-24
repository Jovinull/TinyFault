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


def boot(
    qemu: Path,
    flash: Path,
    ready: bytes = b"Recorder ready.",
    software_reset: bool = False,
    export: bool = False,
) -> str:
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
    reset_sent = False
    export_sent = False
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None and lines.empty():
                break
            try:
                line = lines.get(timeout=0.5)
            except queue.Empty:
                continue
            captured.extend(line)
            if software_reset and not reset_sent and ready in captured:
                assert process.stdin is not None
                process.stdin.write(b"r")
                process.stdin.flush()
                reset_sent = True
            ready_count = captured.count(ready)
            if export and not export_sent and ready_count >= (2 if software_reset else 1):
                assert process.stdin is not None
                process.stdin.write(b"XA1B2C3D4\n")
                process.stdin.flush()
                export_sent = True
            if (b"TFX1 END" in captured if export else ready_count >= (2 if software_reset else 1)):
                break
        else:
            raise RuntimeError(
                "QEMU boot timed out:\n" + captured.decode("utf-8", errors="replace")[-3000:]
            )
        output = captured.decode("utf-8", errors="replace")
        if output.count(ready.decode("ascii")) < (2 if software_reset else 1) or (
            export and "TFX1 END" not in output
        ):
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
