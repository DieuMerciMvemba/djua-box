"""Compile the actual DC driver against a register-level I2C fake; no hardware."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "sms"))
from run_tests import compiler_environment

HERE = Path(__file__).resolve().parent
environment = compiler_environment()
compiler = shutil.which("cl.exe", path=environment.get("PATH"))
with tempfile.TemporaryDirectory(prefix="djua-dc-host-") as directory:
    for name, enabled, configured, polarity in (
        ("configured", 1, 1, 1), ("reversed", 1, 1, -1),
        ("unconfigured", 1, 0, 1), ("disabled", 0, 1, 1)):
        executable = Path(directory) / f"{name}.exe"
        command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/utf-8", "/DIOT_HARDWARE_V2=0",
            f"/I{HERE}", f"/DDC_LOAD_MEASUREMENT_ENABLED={enabled}",
            f"/DDC_LOAD_SHUNT_CONFIGURED={configured}", f"/DDC_LOAD_CURRENT_POLARITY={polarity}",
            str(HERE / "dc_load_test.cpp"), f"/Fe:{executable}"]
        result = subprocess.run(command, cwd=directory, env=environment, capture_output=True, text=True)
        print(result.stdout, end="")
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"{name}: {result.stdout}{result.stderr}")
        print(f"{name}: {result.stdout}", end="")
