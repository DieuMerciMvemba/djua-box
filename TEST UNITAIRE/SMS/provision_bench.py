"""Import the existing local test key to the ESP32 over USB, without printing it.

No SMS is sent. No configuration file, clipboard, or repository is written.
Requires pyserial from DJUA_SMS's existing virtual environment.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import time

DEVICE = "DJUA-TEST-20261006-A"
MARKER = "[BENCH] FW=D2T2_ESP32_HMAC_V2 "
PUBLIC_KEY = bytes(range(32))


def read_config(path: Path) -> dict[str, str]:
    result = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"configuration: invalid line {number}")
        name, value = (part.strip() for part in line.split("=", 1))
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) is None:
            raise ValueError(f"configuration: invalid variable name on line {number}")
        if name in result:
            raise ValueError(f"configuration: duplicate variable {name}")
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        result[name] = value
    return result


def key_command(config: dict[str, str]) -> bytearray:
    try:
        keys = json.loads(config.get("D2_HMAC_KEYS_JSON", "{}"))
    except json.JSONDecodeError:
        raise ValueError("D2 key configuration is not valid JSON") from None
    if not isinstance(keys, dict):
        raise ValueError("D2 key configuration must be an object")
    value = keys.get(DEVICE)
    if not isinstance(value, str) or re.fullmatch(r"[0-9A-Fa-f]{64}", value) is None:
        raise ValueError("local test key is missing or is not 64 hexadecimal characters")
    if bytes.fromhex(value) == PUBLIC_KEY:
        raise ValueError("public conformance key must not be provisioned")
    return bytearray(f"BENCH KEY {value}\n", "ascii")


def read_line(port) -> str:
    # No received line is printed: a misconfigured terminal must not leak the key.
    raw = port.read_until(b"\n", 256)
    return raw.decode("ascii", errors="replace").strip()


def import_key(port, command: bytearray, timeout: float = 20) -> None:
    deadline = time.monotonic() + timeout
    next_status = 0.0
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_status:
            port.write(b"BENCH STATUS\n")
            next_status = now + 1
        line = read_line(port)
        if not line.startswith(MARKER):
            continue
        tokens = dict(token.split("=", 1) for token in line[len("[BENCH] "):].split() if "=" in token)
        if tokens.get("CRYPTO") != "OK" or tokens.get("ID") != DEVICE:
            raise RuntimeError("ESP32 identity or crypto self-test is not valid")
        if tokens.get("STATE") not in {"D2_UNPROVISIONED", "D2_READY"}:
            raise RuntimeError("ESP32 storage fault: no key overwrite/reset attempted")
        break
    else:
        raise RuntimeError("bench sketch not detected; NO KEY SENT. Check the ESP32 port and upload")

    if port.write(command) != len(command):
        raise RuntimeError("USB write incomplete; check BENCH STATUS before retrying")
    port.flush()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        line = read_line(port)
        if line == "[BENCH] KEY_OK":
            return
        if line == "[BENCH] KEY_REFUSED":
            raise RuntimeError("key refused: existing identity/key differs or NVS write failed; no erase")
    raise RuntimeError("no key confirmation; retry this importer to confirm, never erase/reset the counter")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 USB port for local provisioning")
    parser.add_argument("--receiver-config", type=Path,
                        default=Path(r"C:\Users\Dell\Desktop\DJUA_SMS\config\gateway.env"))
    args = parser.parse_args(argv)
    command = bytearray()
    try:
        config = read_config(args.receiver_config)
        if args.port.casefold() == config.get("SERIAL_PORT", "").casefold():
            raise ValueError("choose the ESP32 USB port, not the receiver modem port")
        command = key_command(config)
        try:
            import serial
        except ImportError:
            raise RuntimeError("use DJUA_SMS's .venv Python, which already contains pyserial") from None
        # Opening USB can reset a DevKit; handshake waits for the exact bench firmware.
        with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=2) as port:
            # Release common DevKit automatic reset/boot lines; never keep EN low.
            port.dtr = False
            port.rts = False
            import_key(port, command)
        print("KEY_OK: test key provisioned/confirmed on ESP32; no SMS sent.")
        return 0
    except (OSError, ValueError, RuntimeError) as exc:
        # Never display raw USB input, configuration JSON or secret command.
        print(f"BENCH_IMPORT_ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        command[:] = b"\x00" * len(command)


if __name__ == "__main__":
    raise SystemExit(main())
