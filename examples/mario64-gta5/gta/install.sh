#!/bin/bash
# Install Mario into GTA V Legacy (story mode): ScriptHookV + its ASI loader, Mario64GTA.asi and
# Mario64GTA\sm64.dll. Only adds files; `install.sh --remove` deletes exactly those (your ROM, Mario64GTA.ini and
# Mario64GTA.log stay; delete the Mario64GTA folder yourself if you want them gone).
# ScriptHookV only runs with BattlEye off (Rockstar launcher setting, or -nobattleye), i.e. story mode only.
#   GTA_DIR   the folder with GTA5.exe (default: GTA V Legacy, Steam app 271590, found in your Steam libraries)
#   RUNTIME   ScriptHookV.dll, dinput8.dll, sm64.dll from fetch_deps.sh (default third_party/runtime)
#   BUILD     where build.sh put Mario64GTA.asi (default <MARIO_WIN_DIR>\gta\build, C:\dev\mario64gta\...)
#   ROM       optional: your own Super Mario 64 (USA) dump, copied to Mario64GTA\sm64.us.z64 (any byte order;
#             the script checks it). It is only ever copied into your game folder.
#   FORCE=1   replace a dinput8.dll or args.txt that some other mod (or you) put there
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WIN=${MARIO_WIN_DIR:-'C:\dev\mario64gta'}
RUNTIME=${RUNTIME:-$HERE/third_party/runtime}
BUILD=${BUILD:-$(wslpath -u "$WIN\\gta\\build")}
ARGS='-nobattleye -noBE'
FILES=(ScriptHookV.dll dinput8.dll args.txt Mario64GTA.asi Mario64GTA/sm64.dll)

# the Steam libraries: the default ones and every other one listed in their libraryfolders.vdf
steam_libraries() {
	for steam in "/mnt/c/Program Files (x86)/Steam" "/mnt/c/Program Files/Steam"; do
		[ -d "$steam/steamapps" ] || continue
		echo "$steam"
		sed -n 's/^[[:space:]]*"path"[[:space:]]*"\(.*\)"/\1/p' "$steam/steamapps/libraryfolders.vdf" 2>/dev/null |
			sed 's/\\\\/\\/g' | while read -r p; do wslpath -u "$p"; done
	done
}
if [ -z "$GTA_DIR" ]; then
	while read -r lib; do
		acf="$lib/steamapps/appmanifest_271590.acf"
		[ -f "$acf" ] && GTA_DIR="$lib/steamapps/common/$(sed -n 's/^[[:space:]]*"installdir"[[:space:]]*"\(.*\)"/\1/p' "$acf")" && break
	done < <(steam_libraries)
fi
GTA=${GTA_DIR:?GTA V Legacy not found in your Steam libraries: set GTA_DIR to the folder with GTA5.exe}
case $GTA in [A-Za-z]:*) GTA=$(wslpath -u "$GTA") ;; esac

[ -f "$GTA/GTA5.exe" ] || { echo "GTA5.exe not found in: $GTA"; exit 1; }
if [ "$1" = "--remove" ]; then
	for f in "${FILES[@]}"; do rm -fv "$GTA/$f"; done
	exit 0
fi
[ -f "$RUNTIME/ScriptHookV.dll" ] && [ -f "$RUNTIME/sm64.dll" ] || { echo "fetch the runtime first: gta/fetch_deps.sh"; exit 1; }
[ -f "$BUILD/Mario64GTA.asi" ] || { echo "build it first: gta/build.sh"; exit 1; }
# another mod's ASI loader, or your own launch arguments, stay unless FORCE=1 (--remove would delete them)
clash=
[ ! -f "$GTA/dinput8.dll" ] || cmp -s "$RUNTIME/dinput8.dll" "$GTA/dinput8.dll" || clash+=" dinput8.dll"
[ ! -f "$GTA/args.txt" ] || [ "$(cat "$GTA/args.txt")" = "$ARGS" ] || clash+=" args.txt"
[ -z "$clash" ] || [ -n "$FORCE" ] || { echo "not replacing what is already in $GTA:$clash (FORCE=1 to replace)"; exit 1; }
cp -v "$RUNTIME/ScriptHookV.dll" "$RUNTIME/dinput8.dll" "$BUILD/Mario64GTA.asi" "$GTA/"
# ScriptHookV's own args.txt: story mode without BattlEye (no GTA Online while it is there)
printf -- '%s' "$ARGS" > "$GTA/args.txt"
mkdir -p "$GTA/Mario64GTA"
cp -v "$RUNTIME/sm64.dll" "$GTA/Mario64GTA/"
if [ -n "$ROM" ]; then
	cp -v "$ROM" "$GTA/Mario64GTA/sm64.us.z64"
elif [ ! -f "$GTA/Mario64GTA/sm64.us.z64" ]; then
	echo "now put your own Super Mario 64 (USA) ROM at $GTA/Mario64GTA/sm64.us.z64 (or rerun with ROM=<file>)"
fi
echo "installed into $GTA: start story mode, press F6"
