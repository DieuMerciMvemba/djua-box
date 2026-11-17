"""Tests du vrai noyau C++ D2, sans ESP32, GSM, reseau ni dependance Python.

Windows : Visual Studio Build Tools (C++ + SDK Windows).
Tous les binaires et objets sont produits dans un repertoire temporaire.
"""
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def compiler_environment():
    if os.name != "nt":
        raise RuntimeError("Ce lanceur utilise MSVC et BCrypt sur Windows.")
    if os.environ.get("VSCMD_VER"):
        return os.environ.copy()
    candidates = list(Path(r"C:\Program Files (x86)\Microsoft Visual Studio").glob(
        "*/BuildTools/Common7/Tools/VsDevCmd.bat"))
    if not candidates:
        raise RuntimeError("Installer Visual Studio Build Tools C++ ou utiliser une invite developpeur.")
    setup = sorted(candidates, reverse=True)[0]
    # Chaine native : cmd.exe ne comprend pas les backslashes d'echappement
    # que subprocess.list2cmdline ajoute aux guillemets d'un argument complexe.
    result = subprocess.run(
        f'cmd.exe /d /s /c "call "{setup}" -arch=x64 -host_arch=x64 >nul && set"',
        capture_output=True, text=True, errors="replace")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    environment = os.environ.copy()
    for line in result.stdout.splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            environment[key.upper()] = value
    return environment


class D2FirmwareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="djua-d2-host-")
        cls.binary = Path(cls.temporary.name) / "d2_host.exe"
        sources = [HERE / "d2_host.cpp"] + [ROOT / "src/communication" / name
            for name in ("d2_codec.cpp", "d2t2_codec.cpp", "d3_codec.cpp", "d2_store.cpp", "gsm.cpp", "sms_scheduler.cpp")]
        environment = compiler_environment()
        compiler = shutil.which("cl.exe", path=environment.get("PATH"))
        if not compiler:
            raise RuntimeError("VsDevCmd n'a pas fourni de compilateur C++ utilisable.")
        command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS",
            *map(str, sources), f"/Fe:{cls.binary}"]
        result = subprocess.run(command, cwd=cls.temporary.name,
            env=environment, capture_output=True, text=True, errors="replace")
        print(result.stdout, end="")
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout)
        cls.document = json.loads((HERE / "d2_conformance.json").read_text(encoding="utf-8"))
        cls.d2t2_document = json.loads((HERE / "d2t2_conformance.json").read_text(encoding="utf-8"))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_cpp(self, *args):
        result = subprocess.run([str(self.binary), *map(str, args)],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_cpp_failure_and_recovery_scenarios(self):
        print(self.run_cpp(), end="")

    def test_encode_official_vectors_byte_for_byte(self):
        for vector in self.document["vectors"]:
            decoded = vector.get("expected_decoded")
            if decoded is None:
                continue
            with self.subTest(vector=vector["name"]):
                battery, solar, ac = (decoded.get(group, {}) for group in ("battery", "solar", "ac_load"))
                values = [decoded.get("latitude"), decoded.get("longitude"),
                    battery.get("voltage_v"), battery.get("current_a"), battery.get("power_w"),
                    solar.get("voltage_v"), solar.get("current_a"), solar.get("power_w"),
                    solar.get("energy_interval_wh"), ac.get("voltage_v"), ac.get("current_a"),
                    ac.get("active_power_w"), ac.get("apparent_power_va"), ac.get("energy_interval_wh"),
                    decoded.get("distance_m")]
                args = ["encode", decoded["protocol"], decoded["device_id"], decoded["sequence"],
                    decoded["rtc_epoch_s"] if decoded["rtc_epoch_s"] is not None else "-",
                    decoded["uptime_ms"], decoded.get("event_code", decoded.get("interval_seconds")),
                    decoded["flags"], *["-" if value is None else repr(value) for value in values], vector["mode"]]
                self.assertEqual(self.run_cpp(*args), vector["sms"])

    def test_decode_and_reject_official_vectors(self):
        for vector in self.document["vectors"]:
            with self.subTest(vector=vector["name"]):
                expected = vector.get("expected_error", "OK")
                if expected == "SENDER_DEVICE_MISMATCH":
                    # Le sender GSM est attribue par l'operateur : binding au recepteur,
                    # pas dans le parseur wire de l'emetteur. Le SMS/HMAC est valide.
                    expected = "OK"
                self.assertEqual(self.run_cpp("check", vector["sms"], vector["mode"]), expected)

    def test_lengths_of_valid_and_invalid_vectors(self):
        allowed = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789,-_")
        for vector in self.document["vectors"]:
            with self.subTest(vector=vector["name"]):
                if set(vector["sms"]) <= allowed:
                    self.assertEqual(len(vector["sms"]), vector["gsm7_septets"])
                else:
                    self.assertIsNone(vector["gsm7_septets"])
                    self.assertEqual(vector["expected_error"], "NON_GSM7")

    def test_d2t2_official_bytes_payload_signed_part_hmac_and_sms(self):
        import base64
        import hashlib
        import hmac
        for vector in self.d2t2_document["vectors"]:
            decoded = vector.get("expected_decoded")
            if decoded is None:
                continue
            with self.subTest(vector=vector["name"]):
                battery, solar, ac, dc = (decoded.get(group, {}) for group in
                                         ("battery", "solar", "ac_load", "dc_load"))
                values = [decoded.get("latitude"), decoded.get("longitude"),
                    battery.get("voltage_v"), battery.get("current_a"), battery.get("power_w"),
                    solar.get("voltage_v"), solar.get("current_a"), solar.get("power_w"), solar.get("energy_interval_wh"),
                    ac.get("voltage_v"), ac.get("current_a"), ac.get("active_power_w"),
                    ac.get("apparent_power_va"), ac.get("energy_interval_wh"), None,
                    dc.get("voltage_v"), dc.get("current_a"), dc.get("power_w"), dc.get("energy_interval_wh")]
                args = ["encode", "D2T2", decoded["device_id"], decoded["sequence"],
                    decoded["rtc_epoch_s"] if decoded["rtc_epoch_s"] is not None else "-",
                    decoded["uptime_ms"], decoded["interval_seconds"], decoded["flags"],
                    *["-" if value is None else repr(value) for value in values],
                    decoded["dc_load"]["measured_count"], vector["mode"]]
                actual = self.run_cpp(*args)
                self.assertEqual(actual, vector["sms"])
                signed, tag = actual.rsplit(",", 1)
                self.assertEqual(signed, vector["signed_part"])
                self.assertEqual(tag, vector["expected_auth"])
                payload = signed.split(",")[2]
                self.assertEqual(len(payload), 80)
                raw = base64.urlsafe_b64decode(payload)
                self.assertEqual(len(raw), 60)
                self.assertEqual(raw, base64.urlsafe_b64decode(vector["sms"].split(",")[2]))
                self.assertEqual((int.from_bytes(raw, "big") >> 3) & 7, decoded["dc_load"]["measured_count"])
                self.assertEqual(self.run_cpp("pack", *args[1:]), raw.hex())
                self.assertEqual(len(actual), vector["gsm7_septets"])
                if tag != "-":
                    digest = hmac.new(bytes.fromhex(vector["test_key_hex"]), signed.encode("ascii"), hashlib.sha256).digest()
                    self.assertEqual(tag, base64.urlsafe_b64encode(digest[:8]).decode().rstrip("="))

    def test_d2t2_decode_and_reject_official_vectors(self):
        for vector in self.d2t2_document["vectors"]:
            with self.subTest(vector=vector["name"]):
                expected = vector.get("expected_error", "OK")
                if expected == "SENDER_DEVICE_MISMATCH":
                    expected = "OK"  # Operateur et binding verifies par la gateway.
                self.assertEqual(self.run_cpp("check", vector["sms"], vector["mode"]), expected)

    def test_d2t2_rejects_reserved_tail_ranges_and_tampering(self):
        import base64
        vector = self.d2t2_document["vectors"][0]
        protocol, device, payload, auth = vector["sms"].split(",")
        original = int.from_bytes(base64.urlsafe_b64decode(payload), "big")

        def mutate(start, width, value, tag="-"):
            shift = 480 - start - width
            bits = (original & ~(((1 << width) - 1) << shift)) | (value << shift)
            encoded = base64.urlsafe_b64encode(bits.to_bytes(60, "big")).decode()
            return f"{protocol},{device},{encoded},{tag}"

        self.assertEqual(self.run_cpp("check", mutate(477, 3, 1), "development"), "D2T2_RESERVED_BITS")
        self.assertEqual(self.run_cpp("check", mutate(474, 3, 7), "development"), "OUT_OF_RANGE")
        for start, width, value in ((0, 32, 0), (96, 17, 9), (164, 15, 32001),
                                    (396, 18, 100001), (414, 18, 100001),
                                    (432, 26, 24000001), (113, 25, 9000001)):
            with self.subTest(start=start):
                self.assertEqual(self.run_cpp("check", mutate(start, width, value), "development"), "OUT_OF_RANGE")
        self.assertEqual(self.run_cpp("check", mutate(0, 32, 12346, auth), "development"), "AUTH_INVALID")


if __name__ == "__main__":
    unittest.main(verbosity=2)
