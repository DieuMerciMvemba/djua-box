"""Compile and run the real portable V2 drivers against simulated buses (Windows/MSVC)."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tests/sms"))
from run_tests import compiler_environment

environment = compiler_environment()
compiler = shutil.which("cl.exe", path=environment.get("PATH"))
sources = [HERE / "drivers_test.cpp", ROOT / "src/drivers/ina228.cpp",
           ROOT / "src/drivers/ina3221.cpp", ROOT / "src/drivers/pzem004t.cpp"]
with tempfile.TemporaryDirectory(prefix="iot-v2-host-") as directory:
    binary = Path(directory) / "drivers_test.exe"
    command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/utf-8",
               *map(str, sources), f"/Fe:{binary}"]
    result = subprocess.run(command, cwd=directory, env=environment,
                            capture_output=True, text=True, errors="replace")
    print(result.stdout, end="")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    subprocess.run([str(binary)], check=True)
