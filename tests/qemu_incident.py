"""Exercise explicit ESP32 checkpoint, NVS recovery and TFI1 reset context in QEMU."""

import argparse
from pathlib import Path
import re
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tinyfault_decode import parse_incident
from qemu_smoke import boot


def require(pattern: str, output: str) -> None:
    if re.search(pattern, output) is None:
        raise AssertionError(f"missing {pattern!r}:\n{output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="tinyfault-incident-", dir=args.image.resolve().parent) as directory:
        flash = Path(directory) / "flash.bin"
        shutil.copyfile(args.image, flash)
        first = boot(args.qemu.resolve(), flash, b"TF_READY", ready_command=b"a",
                     command_marker=b"TF_WRITE")
        require(r"TF_RECOVERY count=0", first)
        require(r"TF_WRITE status=0 id=4097 count=1", first)
        second = boot(args.qemu.resolve(), flash, b"TF_READY")
        require(r"TF_RECOVERY count=1", second)
        require(r"TF_EVENT seq=1 id=4097 value=1 ms=\d+", second)
        if "TF_WRITE" in second:
            raise AssertionError("recovery unexpectedly persisted an event")
        restarted = boot(args.qemu.resolve(), flash, b"TF_READY", software_reset=True,
                         reset_command=b"s", export=True)
        require(r"TF_COMMAND software_reset", restarted)
        require(r"TF_BOOT native=3 reason=software", restarted)
        frame = [line for line in restarted.splitlines()
                 if line.startswith(("TFI1 ", "TFX1 "))]
        report = parse_incident(frame, expected_request="A1B2C3D4")
        if (report["reset"] != {"platform": "esp32", "reason": "software",
                                "native": 3, "evidence": "observed"} or
                len(report["events"]) != 1 or report["events"][0]["id"] != 4097 or
                report["events"][0]["sequence"] != 1):
            raise AssertionError(f"unexpected incident report: {report}")
    print("QEMU incident passed: explicit checkpoint, recovery, software reset, TFI1/TFX1")


if __name__ == "__main__":
    main()
