#!/bin/bash
# Fetch and build what the GTA side needs into third_party/ (none of it is redistributed in this repo):
#   shv/       ScriptHookV SDK: main.h, nativeCaller.h, types.h, ScriptHookV.lib (dev-c.com; needs browser headers)
#   libsm64/   libsm64 (CC0) at a pinned commit; its build downloads Mario's geometry tables from the
#              sm64-port decomp (that is libsm64's own import-mario-geo.py, not game data)
#   runtime/   what install.sh puts in the game folder: ScriptHookV.dll, its ASI loader dinput8.dll, and sm64.dll
#              (libsm64 cross-built with MinGW-w64). RUNTIME=<folder> puts them elsewhere.
# Needs: curl, unzip, git, make, python3 and x86_64-w64-mingw32-gcc (Debian/Ubuntu/WSL: apt install mingw-w64).
# Nothing here touches a ROM: Mario's model, textures and animations are read from your own ROM at run time.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
RUNTIME=${RUNTIME:-$HERE/third_party/runtime}
LIBSM64_REPO=https://github.com/libsm64/libsm64
LIBSM64_COMMIT=fd11813208272b4271d92bd92feb8f3fdbe61be5
MINGW=${MINGW:-x86_64-w64-mingw32}
UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36"
command -v "$MINGW-gcc" >/dev/null || { echo "$MINGW-gcc not found: apt install mingw-w64 (or set MINGW)"; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$HERE/third_party/shv" "$RUNTIME"

# ScriptHookV: SDK and runtime
page=$(curl -fsSL -A "$UA" -H "Accept: text/html" -H "Accept-Language: en-US" https://www.dev-c.com/gtav/scripthookv/)
for f in $(echo "$page" | grep -o '/files/ScriptHookV_[^"]*\.zip' | sort -u); do
	curl -fsSL -A "$UA" -H "Referer: https://www.dev-c.com/gtav/scripthookv/" "https://www.dev-c.com$f" -o "$TMP/$(basename "$f")"
done
ls "$TMP"/ScriptHookV_SDK_*.zip >/dev/null 2>&1 ||
	{ echo "no ScriptHookV downloads found: get the SDK and ScriptHookV from https://www.dev-c.com/gtav/scripthookv/"; exit 1; }
unzip -qo "$TMP"/ScriptHookV_SDK_*.zip -d "$TMP/sdk"
cp "$TMP"/sdk/inc/{main.h,nativeCaller.h,types.h} "$TMP"/sdk/lib/ScriptHookV.lib "$HERE/third_party/shv/"
unzip -qo "$(ls "$TMP"/ScriptHookV_*.zip | grep -v SDK)" -d "$TMP/rt"
cp "$TMP"/rt/bin/{ScriptHookV.dll,dinput8.dll} "$RUNTIME/"

# libsm64 -> sm64.dll
SM=$HERE/third_party/libsm64
if [ ! -d "$SM/.git" ]; then
	git clone -q "$LIBSM64_REPO" "$SM"
fi
git -C "$SM" fetch -q origin "$LIBSM64_COMMIT" 2>/dev/null || true
git -C "$SM" checkout -q "$LIBSM64_COMMIT"
make -C "$SM" clean >/dev/null 2>&1 || true
# OS=Windows_NT makes libsm64's Makefile name the output dist/sm64.dll
make -C "$SM" -j"$(nproc 2>/dev/null || echo 4)" OS=Windows_NT CC="$MINGW-gcc" CXX="$MINGW-g++" lib
cp "$SM/dist/sm64.dll" "$RUNTIME/"
ls "$HERE/third_party" "$RUNTIME"
