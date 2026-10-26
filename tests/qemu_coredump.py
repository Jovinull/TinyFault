"""Exercise official ESP-IDF flash core dump in an isolated QEMU flash copy."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import sys

from qemu_smoke import boot
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from tinyfault_correlate import correlate


def crash(qemu: Path, flash: Path) -> str:
    command = [str(qemu), "-nographic", "-machine", "esp32", "-global",
               "driver=timer.esp32.timg,property=wdt_disable,value=true",
               "-drive", f"file={flash},if=mtd,format=raw"]
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, bufsize=0)
    assert process.stdout is not None and process.stdin is not None
    import queue
    import threading
    lines: queue.Queue[bytes] = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line) for line in process.stdout],
                     daemon=True).start()
    output = bytearray()
    stage = 0
    ready_since = None
    native_since = None
    deadline = time.monotonic() + 90
    try:
        while time.monotonic() < deadline:
            if stage == 2 and time.monotonic() - ready_since >= 0.3:
                process.stdin.write(b"q")
                process.stdin.flush()
                stage = 3
            if stage == 4 and time.monotonic() - native_since >= 0.2:
                process.stdin.write(b"XA1B2C3D4\n")
                process.stdin.flush()
                stage = 5
            try:
                line = lines.get(timeout=0.5)
            except queue.Empty:
                continue
            output.extend(line)
            if b"TF_READY" in output and stage == 0:
                process.stdin.write(b"!")
                process.stdin.flush()
                stage = 1
            if stage == 1 and b"TF_TEST_CRASH" in output and output.count(b"TF_READY") >= 2:
                stage = 2
                ready_since = time.monotonic()
            if stage == 3 and line.startswith(b"TFN1"):
                stage = 4
                native_since = time.monotonic()
            if stage == 5 and line.startswith(b"TFX1 END req=A1B2C3D4"):
                return output.decode("utf-8", errors="replace")
        raise AssertionError("QEMU did not complete post-crash report: " +
                             output[-1600:].decode("utf-8", errors="replace"))
    finally:
        if process.poll() is None:
            process.stdin.write(b"\x01x")
            process.stdin.flush()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        process.stdin.close()
        process.stdout.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--analyzer", type=Path, help="optional official esp-coredump CLI")
    parser.add_argument("--gdb", type=Path, help="matching Espressif GDB")
    parser.add_argument("--elf", type=Path, help="exact ELF used to build the QEMU image")
    parser.add_argument("--analysis-output", type=Path, help="private path for synthetic analysis")
    parser.add_argument("--context-output", type=Path, help="private path for synthetic combined JSON")
    args = parser.parse_args()
    if any((args.analyzer, args.gdb, args.elf)) and not all((args.analyzer, args.gdb, args.elf)):
        parser.error("--analyzer, --gdb and --elf must be supplied together")
    if args.analysis_output and not args.analyzer:
        parser.error("--analysis-output requires --analyzer")
    if args.context_output and not args.analyzer:
        parser.error("--context-output requires --analyzer")
    with tempfile.TemporaryDirectory(prefix="tinyfault-coredump-",
                                     dir=args.image.resolve().parent) as directory:
        flash = Path(directory) / "flash.bin"
        shutil.copyfile(args.image, flash)
        empty = boot(args.qemu.resolve(), flash, b"TF_READY", ready_command=b"q",
                     command_marker=b"TFN1", command_delay_s=0.3)
        if "TFN1 state=absent" not in empty:
            raise AssertionError("fresh QEMU flash did not report absent core dump")
        recorded = boot(args.qemu.resolve(), flash, b"TF_READY", ready_command=b"a",
                        command_marker=b"TF_WRITE", command_delay_s=0.3)
        if not re.search(r"TF_WRITE status=0 id=4097 seq=1", recorded):
            raise AssertionError("checkpoint was not committed before emulated crash")
        panic_boot = crash(args.qemu.resolve(), flash)
        if "TF_BOOT native=4 reason=panic" not in panic_boot:
            raise AssertionError("controlled crash did not report panic reset")
        incident = "\n".join(line for line in panic_boot.splitlines()
                             if line.startswith(("TFI1 ", "TFX1 "))) + "\n"
        native_text = "\n".join(line for line in panic_boot.splitlines()
                                if line.startswith("TFN1")) + "\n"
        context = correlate(incident, native_text)
        if (context["count"] != 1 or context["events"][0]["id"] != 4097 or
                context["reset"] != {"platform": "esp32", "reason": "panic",
                                     "native": 4, "evidence": "observed"}):
            raise AssertionError("post-panic TFI1/TFX1 did not match the checkpoint")
        if context["native_dump"]["state"] != "valid":
            raise AssertionError("post-panic native dump was not valid")
        if not re.search(r"TF_RECOVERY status=0 count=1\b", panic_boot):
            raise AssertionError("checkpoint was not recovered after emulated crash")
        if not re.search(r"TFN1 state=valid error=0 size=[1-9][0-9]* summary=1", panic_boot):
            native = next((line for line in panic_boot.splitlines() if line.startswith("TFN1")), "missing")
            raise AssertionError("official API did not validate saved core dump: " + native)
        if args.analyzer:
            partition = flash.read_bytes()[0x3F0000:0x400000]
            length = int.from_bytes(partition[:4], "little")
            if not 4 < length <= len(partition):
                raise AssertionError("emulated dump length is outside the reserved partition")
            core = Path(directory) / "synthetic-core.bin"
            core.write_bytes(partition[:length])
            command = [str(args.analyzer.resolve()), "--chip", "esp32", "info_corefile",
                       "--gdb", str(args.gdb.resolve()), "--core", str(core),
                       "--core-format", "raw", str(args.elf.resolve())]
            analyzed = subprocess.run(command, capture_output=True, text=True, timeout=90)
            if analyzed.returncode != 0:
                raise AssertionError("official analyzer failed on synthetic QEMU dump: stderr=" +
                                     analyzed.stderr[-1200:] + " stdout=" + analyzed.stdout[-500:])
            if "Panic reason:" not in analyzed.stdout or "CURRENT THREAD STACK" not in analyzed.stdout:
                raise AssertionError("official analyzer did not produce a panic reason and stack")
            if args.analysis_output:
                args.analysis_output.write_text(analyzed.stdout, encoding="utf-8")
            combined = correlate(incident, native_text, analyzed.stdout,
                                 args.elf.resolve().read_bytes())
            if combined["native_dump"]["official_analysis"]["status"] != "parsed":
                raise AssertionError("official result was not linked to TinyFault context")
            if args.context_output:
                args.context_output.write_text(json.dumps(combined, indent=2) + "\n", encoding="utf-8")
            print("Official esp-coredump analyzed synthetic QEMU dump with matching ELF")
    print("QEMU core dump passed: checkpoint recovered and ESP-IDF validated native dump")


if __name__ == "__main__":
    main()
