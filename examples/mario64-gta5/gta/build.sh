#!/bin/bash
# Build from WSL: mirror gta/ to <MARIO_WIN_DIR>\gta (default C:\dev\mario64gta\gta) and run MSVC there.
#   ./build.sh          build\Mario64GTA.asi (build.bat); run fetch_deps.sh first
# The portable logic (src/core) is tested on any OS without the game: see tests/core_test.cpp, or run
# `uv run --with pytest pytest -q tests/test_mario64_core.py` from the repo root.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WIN=${MARIO_WIN_DIR:-'C:\dev\mario64gta'}
DST=$(wslpath -u "$WIN\\gta")
mkdir -p "$DST"
rsync -a --delete --exclude build --exclude 'third_party/libsm64/build' "$HERE/" "$DST/"
# cmd.exe starts in the mirror; .\ because cmd may not look in the current folder (NoDefaultCurrentDirectoryInExePath)
cd "$DST"
cmd.exe /c '.\build.bat' 2>&1 | grep -v "UNC paths\|CMD.EXE was started\|Defaulting to Windows"
exit ${PIPESTATUS[0]}
