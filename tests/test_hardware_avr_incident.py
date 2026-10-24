"""Offline checks for the Nano M5 bench parser; no serial hardware is used."""

import sys
from pathlib import Path
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hardware_avr_incident import boot, checkpoint, report


class Link:
    def __init__(self, lines):
        self.lines = iter((line + "\n").encode("ascii") for line in lines)
        self.written = b""

    def read_until(self, _end, _max):
        return next(self.lines, b"")

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.written += data
        return len(data)

    def flush(self):
        pass


def frame():
    prefix = "TFI1 META req=A1B2C3D4 platform=avr reset=unknown native=2 trust=unverified"
    event = "TFX1 EVENT seq=1 id=4353 value=1 ms=1000"
    return [f"{prefix} crc32={zlib.crc32(prefix.encode()):08X}",
            "TFX1 BEGIN req=A1B2C3D4 count=1", event,
            f"TFX1 END req=A1B2C3D4 count=1 crc32={zlib.crc32((event + chr(10)).encode()):08X}",
            "TF_EXPORT status=0 us=500"]


class HarnessTests(unittest.TestCase):
    def test_boot_checks_counts_and_ram_reset(self):
        lines = ["TF_BOOT reset=unknown post_bootloader_mcusr=2",
                 "TF_RECOVERY count=1 us=120 ram_count=0",
                 "TF_EVENT seq=1 id=4353 value=1 ms=1000", "TF_READY"]
        self.assertEqual(boot(Link(lines), [(1, 4353, 1, 1000)]),
                         (2, 120, [(1, 4353, 1, 1000)]))
        with self.assertRaises(AssertionError):
            boot(Link(lines[:1] + ["TF_RECOVERY count=1 us=120 ram_count=1"] + lines[2:]))

    def test_checkpoint_and_unverified_report(self):
        link = Link(["TF_WRITE status=0 id=4353 value=1 ms=1000 us=80000 count=1"])
        self.assertEqual(checkpoint(link, b"a", 4353, 1), ((1, 4353, 1, 1000), 80000))
        self.assertEqual(link.written, b"a")
        with mock.patch("secrets.token_hex", return_value="a1b2c3d4"):
            link = Link(frame())
            request, elapsed, decoded = report(link, [(1, 4353, 1, 1000)], 2)
            self.assertEqual((request, elapsed), ("A1B2C3D4", 500))
            self.assertEqual(decoded["reset"]["evidence"], "unverified")
            self.assertEqual(link.written, b"XA1B2C3D4\n")
            corrupt = frame()
            corrupt[0] = corrupt[0].replace("native=2", "native=3")
            with self.assertRaises(ValueError):
                report(Link(corrupt), [(1, 4353, 1, 1000)], 2)


if __name__ == "__main__":
    unittest.main()
