"""Check that the physical verifier rejects incomplete or inconsistent output."""

import unittest
from unittest import mock
import zlib

from hardware_avr import benchmark_ram, boot, export, write_event


class FakeLink:
    def __init__(self, lines):
        self.lines = [line.encode("ascii") + b"\n" for line in lines]
        self.written = bytearray()

    def read_until(self, _terminator, _maximum):
        return self.lines.pop(0) if self.lines else b""

    def reset_input_buffer(self):
        pass

    def write(self, data):
        self.written.extend(data)
        return len(data)

    def flush(self):
        pass


class HardwareAvrTests(unittest.TestCase):
    def test_boot_and_write(self):
        link = FakeLink(["TF_RECOVERY status=0 count=1 us=123",
                         "TF_EVENT seq=1 id=64001 value=7 ms=7", "TF_READY"])
        self.assertEqual(boot(link), [(1, 64001, 7, 7)])
        link.lines = [b"TF_WRITE status=0 us=5000\n",
                      b"TF_EVENT seq=2 id=64001 value=9 ms=9\n"]
        self.assertEqual(write_event(link), (2, 64001, 9, 9))
        self.assertEqual(link.written, b"w")

    def test_boot_rejects_count_and_unsolicited_write(self):
        with self.assertRaises(AssertionError):
            boot(FakeLink(["TF_RECOVERY status=0 count=1 us=1", "TF_READY"]))
        with self.assertRaises(AssertionError):
            boot(FakeLink(["TF_RECOVERY status=0 count=0 us=1",
                           "TF_WRITE status=0 us=9", "TF_READY"]))

    def test_tagged_export_and_corruption(self):
        event = "TFX1 EVENT seq=2 id=64001 value=9 ms=9"
        crc = zlib.crc32((event + "\n").encode("ascii"))
        lines = ["TFX1 BEGIN req=A1B2C3D4 count=1", event,
                 f"TFX1 END req=A1B2C3D4 count=1 crc32={crc:08X}"]
        link = FakeLink(lines)
        with mock.patch("hardware_avr.secrets.token_hex", return_value="a1b2c3d4"):
            export(link, [(2, 64001, 9, 9)])
        self.assertEqual(link.written, b"XA1B2C3D4\n")
        corrupted = FakeLink(lines[:-1] + ["TFX1 END req=A1B2C3D4 count=1 crc32=00000000"])
        with mock.patch("hardware_avr.secrets.token_hex", return_value="a1b2c3d4"):
            with self.assertRaises(ValueError):
                export(corrupted, [(2, 64001, 9, 9)])
        wrong = FakeLink(lines)
        with mock.patch("hardware_avr.secrets.token_hex", return_value="deadbeef"):
            with self.assertRaises(ValueError):
                export(wrong, [(2, 64001, 9, 9)])

    def test_ram_samples(self):
        link = FakeLink(["TF_RAM_RECORD iterations=10000 us=100",
                         "TF_RAM_RECORD iterations=10000 us=101"])
        self.assertEqual(benchmark_ram(link, 2), [100, 101])
        self.assertEqual(link.written, b"bb")


if __name__ == "__main__":
    unittest.main()
