"""The Mario 64 in GTA V example's portable core (collision, camera, combat, ROM checks), built and run natively.

    uv run --with pytest pytest -q tests/test_mario64_core.py

Skipped when there's no C++ compiler. The libsm64 integration half of core_test.cpp needs a built libsm64 and
runs only when LIBSM64_DIR points at one (a libsm64 checkout after `make lib`).
"""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

GTA = Path(__file__).resolve().parents[1] / "examples" / "mario64-gta5" / "gta"
CXX = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")


def build_and_run(tmp_path: Path, extra: list) -> str:
    exe = tmp_path / "core_test"
    sources = [str(GTA / "tests" / "core_test.cpp")] + sorted(str(p) for p in (GTA / "src" / "core").glob("*.cpp"))
    cmd = [CXX, "-std=c++17", "-O1", "-Wall", "-Wextra", "-I", str(GTA / "src"), *sources, "-o", str(exe), *extra]
    subprocess.run(cmd, check=True, capture_output=True, text=True)
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
    assert run.returncode == 0, run.stdout + run.stderr
    assert " 0 failed" in run.stdout, run.stdout
    return run.stdout


@pytest.mark.skipif(not CXX, reason="no C++ compiler")
def test_core(tmp_path):
    build_and_run(tmp_path, [])


@pytest.mark.skipif(not CXX or not os.environ.get("LIBSM64_DIR"), reason="needs LIBSM64_DIR (a built libsm64)")
def test_core_with_libsm64(tmp_path):
    lib = Path(os.environ["LIBSM64_DIR"]).resolve() / "dist"
    out = build_and_run(tmp_path, ["-DWITH_LIBSM64", "-I", str(lib / "include"), "-L", str(lib), "-lsm64",
                                   f"-Wl,-rpath,{lib}"])
    assert "libsm64" in out
