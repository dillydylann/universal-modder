---
kind: game
title: "Super Mario 64's Mario inside GTA V (libsm64 in a ScriptHookV script)"
game: "Grand Theft Auto V"
games_also: ["Super Mario 64"]
game_version: "Targets GTA V Legacy (Steam, story mode) + ScriptHookV; libsm64 at commit fd11813; Super Mario 64 (USA) ROM supplied by the user. Not yet run in the game."
platform: windows
engine: rage
route: decomp-recomp
tools: ["libsm64 (CC0), cross-built with MinGW-w64", "ScriptHookV + ASI loader", "MSVC", "g++ (offline tests)"]
anti_cheat: "BattlEye guards GTA Online only: story mode, launched with BattlEye off (-nobattleye), never online"
status: in-progress
agents: ["GitHub Copilot"]
humans: ["@dillydylann"]
date: 2026-10-03
links: ["https://github.com/libsm64/libsm64"]
tags: [mashup, libsm64, decomp-as-library, scripthookv, collision-probing, camera, combat, untested-in-game]
---

# Super Mario 64's Mario inside GTA V (libsm64 in a ScriptHookV script)

> Mario from the SM64 decomp (libsm64, loaded as `sm64.dll` by a ScriptHookV ASI) runs on GTA V's map.
> - **Collision:** probed from GTA's world, with cars as moving platforms.
> - **Combat both ways:** GTA's AI shoots a hidden stand-in ped, which becomes damage to Mario, and Mario's
>   punches, stomps and ground pounds hurt or kill peds.
> - **Camera:** a scripted GTA camera runs an SM64-style Mario cam.
>
> Code: `examples/mario64-gta5`. **Status: built and tested offline only.** The core passes unit tests and checks
> inside the real libsm64, and the ASI compiles. Nobody has run it in GTA yet, so treat everything game-side
> here as a plan until someone does.

## Setup
- **libsm64** at commit `fd11813208272b4271d92bd92feb8f3fdbe61be5`.
  `make OS=Windows_NT CC=x86_64-w64-mingw32-gcc lib` on Linux/WSL gives `dist/sm64.dll`, which only imports
  `KERNEL32` and `msvcrt`. The build runs libsm64's `import-mario-geo.py`, which downloads Mario's geometry
  tables from the sm64-port decomp on GitHub, so it needs network access.
- **GTA side:** ScriptHookV SDK, MSVC `cl /LD` into `Mario64GTA.asi`. The ASI loads `sm64.dll` with
  LoadLibrary/GetProcAddress from `<GTA>\Mario64GTA\`, so there's no import lib to match between MinGW and MSVC.
- **ROM:** the user's own Super Mario 64 (USA), 8 MiB. Header name `SUPER MARIO 64` at 0x20 and country `E`
  at 0x3E once in .z64 order. `.v64` is byte-swapped and `.n64` is word-swapped.

## Route and why
- **Taken: embed the decomp as a library** (mashup Pattern 3). libsm64 already exposes exactly what's needed:
  static collision, moving surface objects, input → state + mesh, take-damage and attack calls.
- **Rejected: a passthrough** (running real SM64 or an emulator next to GTA). Mario's physics have to run on
  GTA's geometry, and passing whole frames and collision between two processes is far heavier than one DLL
  call per tick.
- **Rejected: reimplementing Mario** in GTA script, which loses the exact movement that makes it Mario.

## How the game works (what we had to learn)
**libsm64 facts, checked by building it and calling it:**
- **Vertex range:** surface vertices are `int32` in this libsm64, not SM64's `int16`, so the old ±32767 limit
  is gone. A floating origin still keeps numbers small.
- **Limits:**
  - Static collision is one linear list with no spatial grid, so keep it to a few thousand triangles.
  - Level-boundary checks are commented out.
  - `FLOOR_LOWER_LIMIT` is −110000 and means "no floor".
- **Surface normal convention:** the normal is `(v2−v1) × (v3−v2)`. Wind every triangle so this points out
  of the solid side, or floors become ceilings.
- **Swapping static collision:** `sm64_static_surfaces_load` frees the old list, so Mario's cached floor
  pointer dangles until the next tick. Swap surfaces right before a tick.
- **Object yaw:** for `sm64_surface_object_*`, `eulerRotation[1]` in degrees is **minus** the GTA heading
  under the mapping below. A GTA heading is a right-handed turn about SM64 +Y, and libsm64's `CONVERT_ANGLE`
  negates whatever it's given. This was checked with a box that only extends forward, at heading 45 (see
  gotcha 5).
- **Copying:** surface objects copy the surface array you pass in.
- **`sm64_mario_attack(x, y, z, hitboxHeight)`:**
  - It only checks angles (punch/kick within ±60° of facing, ground pound while falling, and so on), never
    distance, so the caller must test reach.
  - It makes Mario bounce when it connects.
- **`sm64_mario_take_damage(damage)`:**
  - 1 is one wedge (it adds 4 × damage to hurtCounter).
  - It is ×1.5 without the cap and 0 with the metal cap.
  - It ignores `invincTimer`, so check that yourself if you want SM64's invincibility frames.
- **Input:** `stickX` is positive right and `stickY` positive *down/back*, the same as GTA's
  `INPUT_MOVE_LR` / `INPUT_MOVE_UD` normals. `camLookX/Z` = Mario − camera in SM64 x/z.
- **Ticking:** tick at 30 Hz and interpolate the mesh between ticks. That's 1024 triangles max, with
  per-vertex colour and UVs into a 704 × 64 RGBA atlas that `sm64_global_init` fills from the ROM.

**Mapping GTA to SM64:**
- **Positions:** with GTA in metres and Z up, `sm = (x, z, −y) × 100` around a floating origin. That is a
  proper rotation, so winding survives. At 100 units per metre Mario is about 1.6 m tall.
- **Heading:** GTA heading = faceAngle + π.

**GTA:**
- **No collision mesh:** no native hands out GTA's collision mesh. `START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE`
  (0x377906D8A31E5586) followed by `GET_SHAPE_TEST_RESULT` gives point + normal hits right away, and the
  collision is built from those.
- **Native hashes:** all of them were checked against alloc8or's native DB.

## Build steps
`examples/mario64-gta5/README.md`: `fetch_deps.sh`, then `build.sh`, then `install.sh ROM=...`, then F6 in
story mode.

## Verification
- **Offline oracle:** `tests/core_test.cpp` builds a fake world (ground, a building, a curb, a bridge) probed
  with exact ray-box tests. It checks floors, roofs, overhangs, steps, walls, the camera's behaviours, combat
  (reach, cooldown, ground-pound kill odds, shockwave falloff), ROM byte orders and the fixed step.
- **Real libsm64, no ROM:** with `-DWITH_LIBSM64` the same collision is loaded into the real libsm64, and
  `sm64_surface_find_floor_height` and the wall-collision calls agree with the fake world. A rotated, moving car
  box is checked as well. The ROM is only needed for Mario himself, not for collision queries.
- **The ASI:** `script.cpp` passes a MinGW `-Wall -Wextra` syntax check against the ScriptHookV API.
- **Not verified:** everything that needs the game. See the open questions below.

## Gotchas
1. **Roofs came out missing in the first collision tests.** **Cause:** a downward probe from high above lands on
   the roof, but a probe from Mario's height up into a building hits nothing, because probes don't see shapes
   from the inside. **Fix:** for any floor hit well above Mario, run a low probe from just above Mario's feet,
   then an *upward* probe from the low hit to the high one. If the upward probe hits, the high surface is an
   overhang (bridge, awning) and Mario uses the low floor. If not, he is beside a solid building and the roof
   is the floor. This relies on GTA's probes behaving the same way, which hasn't been checked in game yet.
2. **Cells at different heights leave gaps Mario can slip into.** **Fix:** add a vertical wall between
   neighbouring floor cells more than 0.45 m apart, facing the lower cell. Curbs and ledges then work too.
3. **Mario's punches "hit" people across the street.** **Cause:** `sm64_mario_attack` checks angles only.
   **Fix:** a reach test first (1.4 m horizontal, −1.6 … +1.9 m vertical), plus a per-ped cooldown.
4. **Mouse and stick look can't share one scale.** GTA's mouse look normal is a per-frame delta, but the stick
   is a rate. **Fix:** the camera takes radians per frame, and the script scales the mouse (a constant) and the
   stick (× frame time) separately, switching on `IS_USING_KEYBOARD_AND_MOUSE`.
5. **The vehicle-box yaw test passed with the wrong sign.** **Cause:** it used a box centred on its origin at
   heading 90, and +90° and −90° give the same footprint for that box. **Fix:** test a box that only extends
   forward, at a heading that isn't a multiple of 90°. It showed that libsm64 needs −heading.
6. **Moving the floating origin while Mario is in the air.** libsm64 keeps `peakHeight` for fall damage and has
   no call to shift it, so moving Mario mid-fall loses or invents fall damage. **Fix:** only move the origin
   while he's on the ground.
7. **Respawning after a fall.** The collision window follows Mario down, so by respawn time his last safe spot
   isn't in it, and `sm64_mario_create` returns −1 when there's no floor under it. **Fix:** rebuild the window at
   the respawn point synchronously before creating him. "Fell out of the world" is detected as 2.5 s with no
   floor under Mario, not as a height drop, so jumping off a tower stays a normal SM64 fall.

## Assets
None made. Mario's model, textures and animations come from the user's ROM at run time, through libsm64.

## Open questions
- **Probes from inside shapes:** do GTA's LOS probes really return no hit when they start inside a building?
  Gotcha 1 depends on it.
- **DRAW_POLY:** which winding it culls, and whether about 2,000 triangles per frame (both sides) is fast
  enough. If not, draw Mario in a ReShade add-on, as the Minecraft example draws its frame.
- **The stand-in ped:** does GTA's AI target a hidden, frozen player ped? Do cops shoot an "armed" one (it
  carries an invisible pistol) rather than arrest? Do big explosions kill it before the per-frame health
  refill?
- **Look scaling:** `MouseLook` and `PadLook` need tuning.
- **Missing pieces:** no audio (libsm64's audio isn't wired up), and peds aren't solid to Mario.
