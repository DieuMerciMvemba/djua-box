"""Host tests for the key importer. No real key/serial/modem/network is accessed."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import provision_bench as bench

TEST_HEX = "ab" * 32  # Public fixture, not a provisioning secret.


class FakeSerial:
    def __init__(self, lines):
        self.lines = list(lines)
        self.writes = []

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def flush(self):
        pass

    def read_until(self, *args):
        return self.lines.pop(0) if self.lines else b""


def config(hex_key=TEST_HEX):
    return {"D2_HMAC_KEYS_JSON": json.dumps({bench.DEVICE: hex_key})}


def marker(state="D2_UNPROVISIONED", crypto="OK", device=bench.DEVICE):
    return f"{bench.MARKER}STATE={state} ID={device} PENDING=0 CRYPTO={crypto} MODEM=WAIT\n".encode()


class ImportTests(unittest.TestCase):
    def test_config_reader_quotes_comments_and_equals(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "gateway.env"
            path.write_text('# comment\nSERIAL_PORT=COM16\nVALUE="a=b"\n', encoding="utf-8")
            self.assertEqual(bench.read_config(path), {"SERIAL_PORT": "COM16", "VALUE": "a=b"})

    def test_duplicate_config_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "gateway.env"
            path.write_text("D2_AUTH_MODE=development\nD2_AUTH_MODE=production\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate"):
                bench.read_config(path)

    def test_invalid_config_name_refused_without_echo(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "gateway.env"
            path.write_text(f"{TEST_HEX}!=value\n", encoding="utf-8")
            with self.assertRaises(ValueError) as error:
                bench.read_config(path)
            self.assertNotIn(TEST_HEX, str(error.exception))

    def test_key_command_exact_no_send(self):
        self.assertEqual(bench.key_command(config()), f"BENCH KEY {TEST_HEX}\n".encode())

    def test_bad_keys_and_json_refused_without_secret_in_error(self):
        for value in ("", "ab" * 31, "z" * 64, 42, bench.PUBLIC_KEY.hex()):
            with self.subTest(value_type=type(value).__name__):
                with self.assertRaises(ValueError):
                    bench.key_command(config(value))
        for raw in ("[", "[]", "{}"):
            with self.assertRaises(ValueError):
                bench.key_command({"D2_HMAC_KEYS_JSON": raw})

    def test_handshake_then_key_no_console_leak(self):
        port = FakeSerial([b"boot chatter\n", marker(), f"BENCH KEY {TEST_HEX}\n".encode(), b"[BENCH] KEY_OK\n"])
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            bench.import_key(port, bench.key_command(config()))
        self.assertEqual(output.getvalue(), "")
        self.assertIn(f"BENCH KEY {TEST_HEX}\n".encode(), port.writes)
        self.assertNotIn(b"SEND\n", port.writes)

    def test_no_key_sent_to_old_or_wrong_sketch(self):
        port = FakeSerial([b"AT\r\n", b"OK\r\n"])
        with patch.object(bench.time, "monotonic", side_effect=[0, 0, 0, 1]):
            with self.assertRaisesRegex(RuntimeError, "NO KEY SENT"):
                bench.import_key(port, bench.key_command(config()), timeout=0.5)
        self.assertTrue(all(data == b"BENCH STATUS\n" for data in port.writes))

    def test_faults_and_wrong_identity_do_not_receive_key(self):
        for line in (marker(state="D2_STORAGE_FAULT"), marker(crypto="FAIL"), marker(device="OTHER")):
            port = FakeSerial([line])
            with self.assertRaises(RuntimeError):
                bench.import_key(port, bench.key_command(config()))
            self.assertTrue(all(data == b"BENCH STATUS\n" for data in port.writes))

    def test_existing_key_refusal_not_overwritten(self):
        port = FakeSerial([marker(state="D2_READY"), b"[BENCH] KEY_REFUSED\n"])
        with self.assertRaisesRegex(RuntimeError, "no erase"):
            bench.import_key(port, bench.key_command(config()))
        self.assertEqual(len([data for data in port.writes if data.startswith(b"BENCH KEY ")]), 1)

    def test_short_usb_key_write_refused(self):
        class ShortSerial(FakeSerial):
            def write(self, data):
                written = super().write(data)
                return written - 1 if data.startswith(b"BENCH KEY ") else written
        port = ShortSerial([marker()])
        with self.assertRaisesRegex(RuntimeError, "incomplete"):
            bench.import_key(port, bench.key_command(config()))

    def test_key_confirmation_timeout_does_not_reset_or_send(self):
        port = FakeSerial([marker()])
        with patch.object(bench.time, "monotonic", side_effect=[0, 0, 0, 0, 11]):
            with self.assertRaisesRegex(RuntimeError, "never erase/reset"):
                bench.import_key(port, bench.key_command(config()))
        self.assertNotIn(b"SEND\n", port.writes)

    def test_receiver_port_is_refused_before_usb_access(self):
        with patch.object(bench, "read_config", return_value={"SERIAL_PORT": "COM16"}), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(bench.main(["--port", "com16"]), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
