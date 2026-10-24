import json
from pathlib import Path
import random
import subprocess
import sys
import types
import unittest
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.tinyfault_decode import ExportError, capture, parse

FIXTURE_BINARY = None


def frame(events, request=None):
    tag = f" req={request}" if request else ""
    lines = [f"TFX1 BEGIN{tag} count={len(events)}"]
    crc = 0
    for sequence, event_id, value, timestamp in events:
        line = f"TFX1 EVENT seq={sequence} id={event_id} value={value} ms={timestamp}"
        lines.append(line)
        crc = zlib.crc32((line + "\n").encode("ascii"), crc)
    lines.append(f"TFX1 END{tag} count={len(events)} crc32={crc:08X}")
    return "\n".join(lines) + "\n"


class DecodeTests(unittest.TestCase):
    def test_empty_one_full_and_gap(self):
        self.assertEqual(parse(frame([]).splitlines()), [])
        self.assertEqual(parse(frame([(1, 42, 7, 100)]).splitlines())[0]["value"], 7)
        events = parse(frame([(5, 1, 2, 3), (7, 4, 5, 6)]).splitlines())
        self.assertEqual([event["sequence"] for event in events], [5, 7])

    def test_rejects_partial_invalid_and_unknown(self):
        valid = frame([(1, 42, 7, 100)])
        invalid = [
            "", "TFX2 BEGIN count=0\n", "TFX1 BEGIN count=1\n",
            valid.replace("value=7", "value=8"), valid.replace("END count=1", "END count=2"),
            valid + "junk\n", valid.replace("seq=1", "seq=0"),
            valid.replace("id=42", "id=65536"), valid.replace("seq=1", "seq=0001"),
            frame([(2, 1, 0, 0), (2, 1, 0, 0)]),
            "TFX1 BEGIN count=" + "9" * 10000 + "\n",
        ]
        for text in invalid:
            with self.subTest(text=text[:40]), self.assertRaises(ExportError):
                parse(text.splitlines())

    def test_fuzzed_lines_never_create_events(self):
        rng = random.Random(23)
        alphabet = "TFX1 BEGIN EVENT END seq=idvaluems0123456789 -"
        for _ in range(300):
            text = "".join(rng.choice(alphabet) for _ in range(rng.randrange(80)))
            with self.assertRaises(ExportError):
                parse([text])

    def test_mutated_valid_frames_are_rejected(self):
        rng = random.Random(44)
        for _ in range(100):
            sequence = rng.randrange(1, 0xFFFFFFFF)
            value = rng.randrange(0, 1000000)
            original = frame([(sequence, 42, value, 123)])
            self.assertEqual(parse(original.splitlines())[0]["value"], value)
            mutated = original.replace(f"value={value} ", f"value={value + 1} ")
            with self.assertRaises(ExportError):
                parse(mutated.splitlines())

    def test_request_identifier_and_limits(self):
        tagged = frame([(0xFFFFFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF)], "A1B2C3D4")
        self.assertEqual(parse(tagged.splitlines(), "A1B2C3D4")[0]["value"], 0xFFFFFFFF)
        with self.assertRaises(ExportError):
            parse(tagged.splitlines(), "DEADBEEF")
        with self.assertRaises(ExportError):
            parse(tagged.replace("END req=A1B2C3D4", "END req=DEADBEEF").splitlines())
        with self.assertRaises(ExportError):
            parse(tagged.replace("ms=4294967295", "ms=4294967296").splitlines())
        with self.assertRaises(ExportError):
            parse((tagged + "TFX1 BEGIN count=0\n").splitlines())

    def test_cpp_export_and_cli(self):
        if FIXTURE_BINARY is None:
            self.skipTest("C++ fixture binary was not supplied")
        fixture = subprocess.check_output([FIXTURE_BINARY, "--fixture"], text=True)
        events = parse(fixture.splitlines())
        self.assertEqual([event["sequence"] for event in events], [1, 2])
        process = subprocess.run(
            [sys.executable, "tools/tinyfault_decode.py", "--json"],
            input=fixture, capture_output=True, text=True, check=True,
        )
        decoded = json.loads(process.stdout)
        self.assertEqual(decoded["events"], events)
        failed = subprocess.run(
            [sys.executable, "tools/tinyfault_decode.py"],
            input=fixture[:-22], capture_output=True, text=True,
        )
        self.assertEqual(failed.returncode, 2)

    def test_serial_capture_and_timeout(self):
        class FakeSerial:
            def __init__(self, lines):
                self.lines = iter(lines)
                self.written = b""
                self.dtr = False
                self.rts = False
                self.short_write = False

            def __enter__(self):
                return self

            def __exit__(self, *_):
                return False

            def reset_input_buffer(self):
                pass

            def write(self, data):
                self.written += data
                return len(data) - 1 if self.short_write else len(data)

            def flush(self):
                pass

            def read_until(self, _terminator, _maximum):
                return next(self.lines, b"")

        data = frame([(1, 2, 3, 4)], "A1B2C3D4")
        link = FakeSerial([b"ROM boot\n", b"TF_READY\n"] +
                          [line.encode() for line in data.splitlines(keepends=True)])
        fake_serial = types.SimpleNamespace(Serial=lambda *_args, **_kwargs: link)
        with mock.patch.dict(sys.modules, {"serial": fake_serial}), mock.patch(
            "secrets.token_hex", return_value="a1b2c3d4"
        ):
            lines, request = capture("COM-test", 1)
            self.assertEqual(parse(lines, request), parse(data.splitlines()))
            self.assertEqual(link.written, b"XA1B2C3D4\n")
        broken = FakeSerial([b"TF_READY\n", b"TFX1 BEGIN req=A1B2C3D4 count=1\n"])
        fake_serial.Serial = lambda *_args, **_kwargs: broken
        with mock.patch.dict(sys.modules, {"serial": fake_serial}), mock.patch(
            "secrets.token_hex", return_value="a1b2c3d4"
        ):
            with self.assertRaises(ExportError):
                capture("COM-test", 0.01)
        stale = FakeSerial([b"TF_READY\n"] +
                           [line.encode() for line in frame([], "DEADBEEF").splitlines(keepends=True)])
        fake_serial.Serial = lambda *_args, **_kwargs: stale
        with mock.patch.dict(sys.modules, {"serial": fake_serial}), mock.patch(
            "secrets.token_hex", return_value="a1b2c3d4"
        ):
            with self.assertRaisesRegex(ExportError, "stale"):
                capture("COM-test", 1)
        partial = FakeSerial([b"TF_READY\n"])
        partial.short_write = True
        fake_serial.Serial = lambda *_args, **_kwargs: partial
        with mock.patch.dict(sys.modules, {"serial": fake_serial}), mock.patch(
            "secrets.token_hex", return_value="a1b2c3d4"
        ):
            with self.assertRaisesRegex(ExportError, "incomplete"):
                capture("COM-test", 1)
        consecutive = [FakeSerial([b"TF_READY\n"] +
                                  [line.encode() for line in frame([], token).splitlines(keepends=True)])
                       for token in ("A1B2C3D4", "DEADBEEF")]
        fake_serial.Serial = lambda *_args, **_kwargs: consecutive.pop(0)
        with mock.patch.dict(sys.modules, {"serial": fake_serial}), mock.patch(
            "secrets.token_hex", side_effect=["a1b2c3d4", "deadbeef"]
        ):
            for _ in range(2):
                lines, request = capture("COM-test", 1)
                self.assertEqual(parse(lines, request), [])


if __name__ == "__main__":
    FIXTURE_BINARY = sys.argv[1] if len(sys.argv) > 1 else None
    sys.argv = sys.argv[:1]
    unittest.main()
