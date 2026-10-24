"""Offline checks for the physical incident harness; no serial hardware is used."""

import sys
from pathlib import Path
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hardware_incident import boot, checkpoint, export


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
    prefix = "TFI1 META req=A1B2C3D4 platform=esp32 reset=software native=3 trust=observed"
    event = "TFX1 EVENT seq=1 id=4097 value=1 ms=1000"
    return [f"{prefix} crc32={zlib.crc32(prefix.encode()):08X}",
            "TFX1 BEGIN req=A1B2C3D4 count=1", event,
            f"TFX1 END req=A1B2C3D4 count=1 crc32={zlib.crc32((event + chr(10)).encode()):08X}",
            "TF_EXPORT status=0 us=500"]


class HarnessTests(unittest.TestCase):
    def test_boot_requires_no_unsolicited_write(self):
        lines = ["TF_BOOT native=3 reason=software",
                 "TF_RECOVERY status=0 count=1 us=100 ram_count=0",
                 "TF_EVENT seq=1 id=4097 value=1 ms=1000", "TF_READY"]
        self.assertEqual(boot(Link(lines), [(1, 4097, 1, 1000)])[0:3], (3, "software", 100))
        with self.assertRaises(AssertionError):
            boot(Link(lines[:-1] + ["TF_WRITE status=0", "TF_READY"]))

    def test_checkpoint_and_correlated_export(self):
        link = Link(["TF_WRITE status=0 id=4097 seq=1 value=1 ms=1000 us=700 count=1 nvs=0"])
        self.assertEqual(checkpoint(link, b"a", 4097), ((1, 4097, 1, 1000), 700))
        self.assertEqual(link.written, b"a")
        with mock.patch("secrets.token_hex", return_value="a1b2c3d4"):
            link = Link(frame())
            self.assertEqual(export(link, [(1, 4097, 1, 1000)], 3, "software"),
                             ("A1B2C3D4", 500))
            self.assertEqual(link.written, b"XA1B2C3D4\n")
            broken = frame()
            broken[0] = broken[0].replace("native=3", "native=4")
            with self.assertRaises(ValueError):
                export(Link(broken), [(1, 4097, 1, 1000)], 3, "software")


if __name__ == "__main__":
    unittest.main()
