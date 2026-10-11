"""Compile and run the real portable V2 drivers against simulated buses (Windows/MSVC)."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import os

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/sms"))
from run_tests import compiler_environment

environment = compiler_environment()
compiler = shutil.which("cl.exe", path=environment.get("PATH"))
sources = [HERE / "drivers_test.cpp", ROOT / "src/drivers/ina228.cpp",
           ROOT / "src/drivers/ina3221.cpp", ROOT / "src/drivers/pzem004t.cpp",
           ROOT / "src/communication/gsm.cpp", ROOT / "src/communication/d2_codec.cpp",
           ROOT / "src/communication/d2t2_codec.cpp", ROOT / "src/communication/d3_codec.cpp",
           ROOT / "src/communication/d2_store.cpp", ROOT / "src/geofencing/geofence.cpp"]
with tempfile.TemporaryDirectory(prefix="iot-v2-host-") as directory:
    binary = Path(directory) / "drivers_test.exe"
    command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS", f"/I{ROOT / 'tests/dc_load'}",
               *map(str, sources), f"/Fe:{binary}"]
    result = subprocess.run(command, cwd=directory, env=environment,
                            capture_output=True, text=True, errors="replace")
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    subprocess.run([str(binary)], check=True)
    # Independent Python reference: do not use the C++ decoder to verify its encoder.
    import base64
    import hashlib
    import hmac
    vector_sms = subprocess.check_output([str(binary), "vector"], text=True).splitlines()
    def field(value, width, scale=1):
        scaled = value * scale
        raw = int(scaled + .5) if scaled >= 0 else -int(-scaled + .5)
        return format(raw & ((1 << width) - 1), f"0{width}b")
    def slot(voltage=0, current=0, power=0, energy=0, valid=False, complete=False, fault=1, age=0):
        return "".join(field(v, w, s) for v, w, s in (
            (valid, 1, 1), (complete, 1, 1), (fault, 4, 1), (age, 16, 1),
            (voltage, 17, 1000), (current, 17, 1000), (power, 20, 10), (energy, 27, 100)))
    for part, sms in enumerate(vector_sms):
        protocol, device, payload, auth = sms.split(",")
        assert protocol == "D3T" and len(sms) == 149 and len(payload) == 100
        bits = "".join(field(v, w) for v, w in ((1 + part, 32), (1, 32), (0, 32),
            (123456, 32), (1800, 17), (part, 1), (0, 1)))
        if part:
            dc = slot(12, 2, 24, 12, True, True, 0, 5)
            bits += dc + dc + slot(fault=4) + dc
        else:
            bits += "".join(field(v, w, s) for v, w, s in ((1, 2, 1), (1, 1, 1), (0, 1, 1),
                (1000, 16, 1), (0, 16, 1), (-4.33086, 25, 100000), (15.30678, 26, 100000)))
            bits += slot(12.543, -1.234, -15.5, -4.56, True, True, 0, 2)
            bits += slot(18.2, 2, 36.4, 18.2, True, True, 0, 3)
            bits += "".join(field(v, w, s) for v, w, s in ((1, 1, 1), (1, 1, 1), (1, 1, 1),
                (0, 1, 1), (0, 4, 1), (0, 4, 1), (4, 16, 1), (230, 12, 10), (1.2, 14, 1000),
                (230, 15, 10), (276, 15, 10), (115, 27, 100), (1000, 24, 1), (50, 10, 10), (.83, 7, 100)))
        assert len(bits) == (559 if part else 592)
        expected = int(bits.ljust(600, "0"), 2).to_bytes(75, "big")
        assert base64.urlsafe_b64decode(payload) == expected
        digest = hmac.new(bytes(range(1, 33)), sms.rsplit(",", 1)[0].encode("ascii"), hashlib.sha256).digest()
        assert auth == base64.urlsafe_b64encode(digest[:8]).decode().rstrip("=")
    print("PASS independent Python reference: both 75-byte payloads, Base64URL, signed part, HMAC, GSM-7")
    json_library = Path(os.environ.get("ARDUINOJSON_INCLUDE", str(Path.home() / "Documents/Arduino/libraries/ArduinoJson/src")))
    if not (json_library / "ArduinoJson.h").exists():
        raise RuntimeError("JSON test requires installed ArduinoJson 7; set ARDUINOJSON_INCLUDE to its src directory.")
    json_binary = Path(directory) / "json_test.exe"
    command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8",
               f"/I{json_library}", f"/I{ROOT / 'tests/dc_load'}", str(HERE / "json_test.cpp"), f"/Fe:{json_binary}"]
    result = subprocess.run(command, cwd=directory, env=environment, capture_output=True, text=True, errors="replace")
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    subprocess.run([str(json_binary)], check=True)
