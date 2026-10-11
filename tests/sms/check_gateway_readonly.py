"""Feed C++ generated fixtures to the real gateway parser, without any gateway writes.

Pure parser/security/normalizer only: no service, SQLite, serial or MQTT.
Python bytecode writes are disabled BEFORE importing the gateway.
"""
import argparse
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from run_tests import D2FirmwareTests

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--gateway-root", type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.gateway_root / "src"))
from djua_sms_gateway.protocol.d2 import parse_d2, verify_d2_security, normalize_d2_to_backend
from djua_sms_gateway.protocol.d2t2 import parse_d2t2, verify_d2t2_security, normalize_d2t2_to_backend

D2FirmwareTests.setUpClass()
try:
    test = D2FirmwareTests("test_cpp_failure_and_recovery_scenarios")
    total = 0
    for filename in ("d2_conformance.json", "d2t2_conformance.json"):
        gateway = json.loads((args.gateway_root / "tests/vectors" / filename).read_text(encoding="utf-8"))
        local = json.loads((Path(__file__).parent / filename).read_text(encoding="utf-8"))
        assert gateway["vectors"] == local["vectors"], "Local vectors differ from the read-only source"
        for vector in gateway["vectors"]:
            d = vector.get("expected_decoded")
            if not d or d["protocol"] == "D2T":
                continue
            packed = d["protocol"] == "D2T2"
            groups = [d.get(group, {}) for group in ("battery", "solar", "ac_load", "dc_load")]
            battery, solar, ac, dc = groups
            values = [d.get("latitude"), d.get("longitude"),
                battery.get("voltage_v"), battery.get("current_a"), battery.get("power_w"),
                solar.get("voltage_v"), solar.get("current_a"), solar.get("power_w"), solar.get("energy_interval_wh"),
                ac.get("voltage_v"), ac.get("current_a"), ac.get("active_power_w"),
                ac.get("apparent_power_va"), ac.get("energy_interval_wh"), d.get("distance_m")]
            if packed:
                values += [dc.get("voltage_v"), dc.get("current_a"), dc.get("power_w"), dc.get("energy_interval_wh")]
            command = ["encode", d["protocol"], d["device_id"], d["sequence"],
                d["rtc_epoch_s"] if d["rtc_epoch_s"] is not None else "-", d["uptime_ms"],
                d.get("interval_seconds", d.get("event_code")), d["flags"],
                *["-" if v is None else repr(v) for v in values], vector["mode"]]
            actual = test.run_cpp(*command)
            assert actual == vector["sms"]
            parse, verify, normalize = (parse_d2t2, verify_d2t2_security, normalize_d2t2_to_backend) if packed else (
                parse_d2, verify_d2_security, normalize_d2_to_backend)
            message = parse(actual)
            auth = verify(message, mode=vector["mode"], key=bytes.fromhex(vector.get("test_key_hex", gateway["test_key_hex"])),
                          sender=vector["sender"], allowed_senders=[vector["sender"]])
            assert message.sequence == d["sequence"]
            if "expected_backend_payload" in vector:
                actual_json = normalize(message, gateway_received_at=vector["gateway_received_at"], auth_status=auth)
                assert actual_json == vector["expected_backend_payload"]
            total += 1
            print(f"PASS actual gateway parser/security/backend JSON: {vector['name']}")
    print(f"{total} C++ generated D2T2/D2E fixtures accepted by gateway code (read only, no real SMS).")
finally:
    D2FirmwareTests.tearDownClass()
