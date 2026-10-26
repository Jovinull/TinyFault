"""Deterministic, synthetic tests; no real device dump is retained in Git."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))
from tinyfault_correlate import correlate, parse_native
from test_decode import incident_frame


class CorrelateTests(unittest.TestCase):
    def setUp(self):
        self.incident = incident_frame([(1, 4097, 3, 1200)])
        self.elf = b"synthetic application ELF bytes"
        self.prefix = hashlib.sha256(self.elf).hexdigest()[:9]
        self.native = ("TFN1 state=valid error=0 size=1200 summary=1 summary_error=0 "
                       f"elf_prefix=1 sha={self.prefix} pc=400D1234\n")
        self.official = ("Panic reason: abort() was called on core 0\n"
                         "================ CURRENT THREAD STACK ================\n"
                         "#0  0x400d1234 in app_main () at main.cpp:10\n")

    def test_combined_observations(self):
        report = correlate(self.incident, self.native, self.official, self.elf)
        self.assertEqual(report["events"][0]["sequence"], 1)
        self.assertEqual(report["native_dump"]["official_analysis"]["top_frame_symbol"], "app_main")
        self.assertIn("unproven", report["relationship"])

    def test_absence_and_missing_analysis(self):
        native = "TFN1 state=absent error=0 size=0 summary=0 summary_error=0 elf_prefix=0 sha=unknown pc=00000000\n"
        report = correlate(self.incident, native)
        self.assertEqual(report["native_dump"]["state"], "absent")
        self.assertEqual(report["native_dump"]["official_analysis"]["status"], "not_performed")
        for state in ("not_configured", "partition_missing", "invalid", "read_error"):
            report = correlate(self.incident, native.replace("absent", state))
            self.assertEqual(report["native_dump"]["state"], state)
            with self.assertRaisesRegex(ValueError, "valid inspected dump"):
                correlate(self.incident, native.replace("absent", state), self.official, self.elf)

    def test_rejects_wrong_elf_and_incomplete_report(self):
        with self.assertRaisesRegex(ValueError, "ELF SHA-256"):
            correlate(self.incident, self.native, self.official, b"wrong ELF")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            correlate(self.incident, self.native, "Panic reason: abort\n", self.elf)
        misleading = ("#0 fake_function\nPanic reason: abort\n"
                      "================ CURRENT THREAD STACK ================\n"
                      "================ THREADS INFO ================\n")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            correlate(self.incident, self.native, misleading, self.elf)

    def test_rejects_invalid_inputs(self):
        with self.assertRaises(ValueError):
            parse_native(self.native + self.native)
        with self.assertRaises(ValueError):
            parse_native("TFN1 state=valid garbage\n")
        with self.assertRaises(ValueError):
            parse_native(self.native.replace("TFN1", "TFN2"))
        with self.assertRaises(ValueError):
            parse_native(self.native.replace("size=1200", "size=0"))
        with self.assertRaises(ValueError):
            parse_native(self.native.replace("summary=1", "summary=0"))
        with self.assertRaises(ValueError):
            correlate(self.incident[:-18], self.native)
        with self.assertRaises(ValueError):
            correlate(self.incident, self.native, self.official)

    def test_cli_json_and_exit_code(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            (folder / "incident.txt").write_text(self.incident, encoding="ascii")
            (folder / "native.txt").write_text(self.native, encoding="ascii")
            (folder / "official.txt").write_text(self.official, encoding="utf-8")
            (folder / "app.elf").write_bytes(self.elf)
            command = [sys.executable, str(ROOT / "tools" / "tinyfault_correlate.py"),
                       str(folder / "incident.txt"), str(folder / "native.txt"),
                       "--official-info", str(folder / "official.txt"),
                       "--elf", str(folder / "app.elf")]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["count"], 1)
            (folder / "app.elf").write_bytes(b"wrong")
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)
            (folder / "incident.txt").write_bytes(b"X" * 2_000_001)
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)


if __name__ == "__main__":
    unittest.main()
