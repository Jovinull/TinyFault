"""Check real ESP-IDF reset classification and NVS recovery in ESP32 QEMU."""

import argparse
from pathlib import Path
import re
import shutil
import tempfile

from qemu_smoke import boot


def require(pattern: str, output: str) -> None:
    if re.search(pattern, output, re.MULTILINE) is None:
        raise AssertionError(f"Missing {pattern!r}:\n{output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="tinyfault-diag-", dir=args.image.resolve().parent) as directory:
        flash = Path(directory) / "flash.bin"
        shutil.copyfile(args.image, flash)
        first = boot(args.qemu.resolve(), flash, b"TF_READY")
        second = boot(args.qemu.resolve(), flash, b"TF_READY")
        restarted = boot(args.qemu.resolve(), flash, b"TF_READY", software_reset=True)
    require(r"TF_BOOT native=1 reason=power_on chip=ESP32[^ ]* flash=\d+", first)
    require(r"TF_RECOVERY status=0 count=0 us=\d+", first)
    require(r"TF_WRITE kind=boot status=0 us=\d+", first)
    require(r"TF_RAM_RECORD iterations=10000 us=\d+", first)
    require(r"TF_BOOT native=1 reason=power_on chip=ESP32[^ ]* flash=\d+", second)
    require(r"TF_RECOVERY status=0 count=1 us=\d+", second)
    require(r"TF_EVENT seq=1 id=65280 value=1 ms=\d+", second)
    require(r"TF_RAM_RECORD iterations=10000 us=\d+", second)
    if "TF_WRITE" in second:
        raise AssertionError("Second boot unexpectedly wrote NVS")
    require(r"TF_COMMAND software_reset", restarted)
    require(r"TF_BOOT native=3 reason=software chip=ESP32[^ ]* flash=\d+", restarted)
    if restarted.count("TF_READY") != 2:
        raise AssertionError(f"Expected two completed boots:\n{restarted}")
    first_recovery = re.search(r"TF_RECOVERY status=0 count=0 us=(\d+)", first)
    first_write = re.search(r"TF_WRITE kind=boot status=0 us=(\d+)", first)
    second_recovery = re.search(r"TF_RECOVERY status=0 count=1 us=(\d+)", second)
    ram = re.search(r"TF_RAM_RECORD iterations=10000 us=(\d+)", second)
    if first_recovery is None or first_write is None or second_recovery is None or ram is None:
        raise AssertionError("Missing measured operation time")
    print(
        "QEMU ESP32 diagnostics passed: power-on, NVS recovery and software reset; "
        f"emulated recovery empty={first_recovery.group(1)} us, "
        f"write={first_write.group(1)} us, "
        f"recovery with one event={second_recovery.group(1)} us, "
        f"10000 RAM records={ram.group(1)} us"
    )


if __name__ == "__main__":
    main()
