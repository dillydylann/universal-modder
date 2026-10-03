# Super Mario 64 x GTA V: Mario in Los Santos

Super Mario 64's Mario, with his real moves and physics, running around GTA V story mode. Press F6 and the
player turns into Mario:

- **He runs, jumps, wall-kicks, long-jumps, swims and ground-pounds on GTA's map.** The streets, curbs,
  stairs, walls, roofs, bridges and water around him are turned into SM64 collision every few frames. Cars
  are solid moving platforms that he can stand on and ride.
- **GTA's people fight him.** Cops and gangs shoot at him and thugs punch him. Every hit takes wedges off his
  power meter, and he gets knocked back the way SM64 enemies knock him back. At zero he dies, and he comes
  back where he last stood.
- **He fights back.** Punches, kicks, dives, slide kicks and stomps hurt people and knock them over. A ground
  pound onto someone does the most damage and often kills outright. Its landing sends out a shockwave that
  hurts, and sometimes kills, everyone close by. People he hits may pull a gun, and the police come.
- **The camera is SM64's Mario cam.** It hangs behind him at one of three distances and swings round as he
  runs. It doesn't bob with every jump. You can orbit it by hand, and it eases back to following him, or
  snaps behind him at the press of a button.

> **Status: not run in the game yet.** This example was written and tested without GTA V, a GPU or a
> Super Mario 64 ROM:
>
> - the portable core (collision, camera, combat, ROM checks) passes its unit tests;
> - the collision it builds passes checks run inside the real libsm64;
> - `sm64.dll` cross-builds with MinGW;
> - `script.cpp` compiles for Windows against the ScriptHookV API.
>
> The things only the real game can settle are listed in [Not verified in game](#not-verified-in-game). Expect
> to tune them on the first run, and update this section and the field note
> ([knowledge/games/gta-v/mario64-libsm64.md](../../knowledge/games/gta-v/mario64-libsm64.md)) when you do.

## How it works

It's the mashup-mods skill's Pattern 3, *embed a decomp as a library*, done as G64 did it in Garry's Mod.
[libsm64](https://github.com/libsm64/libsm64) packages the Super Mario 64 decomp as a library: you feed it
collision triangles and controller input, and each tick it returns Mario's state and his animated mesh. It reads
Mario's model, textures and animations from **your own ROM** when it starts. The ASI loads it as `sm64.dll`.

```
GTA world ──probes──▶ collision window ──▶ libsm64 ◀── pad/keyboard (GTA controls)
GTA vehicles ─boxes─▶ surface objects ──▶   │
GTA water height ──────────────────────▶    │ 30 Hz
                                            ▼
              Mario's mesh ── DRAW_POLY ──▶ GTA frame ◀── scripted cam (Mario cam)
              Mario's position ──▶ hidden player ped ◀── GTA's bullets and fists ──▶ wedges off Mario
              Mario's attacks ──▶ damage / ragdoll / kill on GTA peds
```

- **Coordinates** (`src/core/geom.h`): GTA is metres with Z up. SM64 is units with Y up, at 100 units per metre,
  which makes Mario about 1.6 m tall. The mapping is `sm = (x, z, −y) × 100` around a floating origin. That is a
  rotation, not a mirror, so triangle winding survives the trip. The origin follows Mario in jumps of about
  60 m so libsm64's numbers stay small in an 8 km city. Heading is `h = faceAngle + π`.
- **Collision** (`src/core/collision.cpp`): no native hands out GTA's collision mesh, so the script probes the
  world with line-of-sight shape tests on a 24 × 24 m window round Mario, at a few hundred probes per frame.
  - **Floors:** one downward probe per 1 m cell becomes a quad on the hit's plane.
  - **Overhangs:** if that hit is a roof well above Mario, a second probe from just above him finds what's
    underneath. An upward probe then tells a bridge (he walks under it) from a building he's next to (the roof
    is the floor and the cell's sides are walls).
  - **Steps:** cells at different heights get a wall between them, so curbs and ledges are real.
  - **Walls:** fans of horizontal probes at three heights turn walls into wall quads.
  - **Swapping in:** when Mario walks 4 m from the window's centre, a new window is probed and swapped in.
  - **Vehicles:** every vehicle within 25 m is a box from its model dimensions, moved every tick as a libsm64
    surface object, so Mario rides along on it.
- **Mario** (`src/script.cpp`): ticked at SM64's 30 Hz and drawn every frame, interpolated between ticks, as
  `DRAW_POLY` triangles. Each triangle is coloured from libsm64's vertex colours and its texture atlas
  (`src/core/mario.cpp`), then lit. He gets a blob shadow and SM64's 8-wedge power meter.
- **Getting hurt** (`src/core/combat.h`): the player ped stays in the game, hidden and frozen, and is moved onto
  Mario every frame. GTA's AI therefore aims, shoots and punches at Mario without any changes. The ped's health
  is refilled every frame, and what it lost becomes wedges: 20 HP per wedge, at most 3 per hit, through
  `sm64_mario_take_damage`. The knockback comes from the nearest ped that is shooting or brawling. While
  libsm64's invincibility frames run after a hit, damage is ignored, as in SM64. The ped carries an invisible
  pistol so cops treat Mario as armed and shoot rather than try to cuff him.
- **Hurting people** (`src/core/combat.cpp`): every tick, each ped within reach (1.4 m) is offered to
  `sm64_mario_attack`. That uses SM64's own rules for whether the current move connects: punches and kicks
  within 60° of where he faces, dives, slide kicks, stomps from above, a falling ground pound. Damage by move:

  | move | damage (GTA HP) | extra |
  |---|---|---|
  | punch | 25 | |
  | kick | 35 | |
  | dive | 30 | |
  | slide kick | 40 | |
  | stomp | 35 | |
  | **ground pound** onto someone | **100** | 50% instant kill |
  | ground-pound **shockwave** (3.5 m) | up to 60, less further out | up to 30% instant kill at the centre |

  Hits ragdoll the ped and knock them away from Mario. One ped can't be hit twice within 8 ticks. A ped who
  survives may fight back (50%), and hitting anyone raises the wanted level to 2.
- **Camera** (`src/core/camera.cpp`): the SM64 "Mario cam" on a GTA scripted camera.
  - **Follow:** it trails at 4, 6.5 or 9.5 m and swings round behind Mario in proportion to his speed. It
    doesn't swing when he runs at it (it backs off, as Lakitu does).
  - **Height:** it looks at the height of the ground he last stood on, so it ignores ordinary jumps. It only
    follows him once he is 2.5 m above that ground or falling below it.
  - **Walls:** it pulls in against walls rather than clipping.
  - **Steering:** the stick steers Mario relative to the camera, as in SM64.

## Files

```
gta/
  src/core/geom.h            GTA <-> SM64 frame, angles
  src/core/collision.*       probe-built collision window, vehicle boxes
  src/core/camera.*          Mario cam
  src/core/combat.*          Mario vs peds, peds vs Mario
  src/core/mario.*           ROM checks, 30 Hz fixed step, triangle colours
  src/natives.h              the GTA natives used (hashes checked against alloc8or's native DB)
  src/sm64.h                 loads sm64.dll at run time
  src/script.cpp             the ScriptHookV script
  tests/core_test.cpp        tests for src/core; with -DWITH_LIBSM64 also inside the real libsm64
  fetch_deps.sh              ScriptHookV SDK + runtime; libsm64 cross-built to sm64.dll
  build.sh / build.bat       MSVC build of Mario64GTA.asi (from WSL or Windows)
  install.sh                 install into GTA V / --remove
```

## Requirements

- **GTA V Legacy (Steam) in story mode**, with BattlEye off. ScriptHookV doesn't run with BattlEye on. GTA V
  Enhanced needs its own ScriptHookV build, and that combination hasn't been tried.
- **Your own Super Mario 64 (USA) ROM.** Dump it from a cartridge you own. It must be the 8 MiB US version;
  `.z64`, `.v64` and `.n64` byte orders all work, and the script checks it. The ROM is never in this repo,
  never in a release, and never copied anywhere except your game folder.
- **Windows** with Visual Studio's C++ x64 tools, for the ASI.
- **WSL or another Linux** with `mingw-w64`, `git`, `make`, `python3`, `curl` and `unzip`, for `fetch_deps.sh`.
  libsm64 is built from source.

## Build, install, run

```bash
cd examples/mario64-gta5/gta
./fetch_deps.sh                         # ScriptHookV into third_party/shv + runtime, libsm64 -> third_party/runtime/sm64.dll
./build.sh                              # (WSL) mirrors to C:\dev\mario64gta\gta and runs build.bat -> build\Mario64GTA.asi
ROM=~/roms/sm64.us.z64 ./install.sh     # ScriptHookV, dinput8.dll, Mario64GTA.asi, Mario64GTA\sm64.dll (+ your ROM)
./install.sh --remove                   # takes those files out again (your ROM and .ini stay)
```

Start GTA V and choose **Story Mode yourself**. In free roam, press **F6**. If anything is missing (the DLL,
the ROM, or a ROM that isn't the US version), a notification says so. `Mario64GTA\Mario64GTA.log` records what
happened, including libsm64's own messages.

### Settings (`<GTA V>\Mario64GTA\Mario64GTA.ini`, all optional)

```ini
[Mario]
ToggleKey=117        ; F6 (virtual-key code)
GangKey=120          ; F9: spawn three armed gang members to fight
Rom=sm64.us.z64
Scale=100            ; SM64 units per metre (bigger = smaller Mario)
ProbesPerFrame=250   ; collision probes per frame
DrawBothSides=1      ; draw every triangle both ways round (DRAW_POLY culls one side)
HpPerWedge=20
MaxWedgesPerHit=3
WantedOnHit=2        ; 0 = hitting people never raises the wanted level
FightBackPercent=50
MouseLook=6.0
PadLook=3.2          ; radians per second at full right stick
RespawnSeconds=3
```

## Controls

GTA's own control bindings are used (jump, attack, melee, duck, aim, look, look behind), so a pad and
keyboard + mouse both work, with whatever you've rebound them to. The pad column is GTA's default layout.

| SM64 | does | keyboard + mouse | pad |
|---|---|---|---|
| stick | run (relative to the camera) | WASD | left stick |
| A | jump (double, triple, wall kick, long jump) | Space | X / ◻ (GTA's jump) |
| B | punch, kick, dive | left mouse / R | RT / R2, B / ◯ |
| Z | crouch, slide; **in the air: ground pound** | Ctrl / right mouse | left stick click / LT |
| C buttons | orbit the camera | mouse | right stick |
| C-up / C-down | camera nearer / further | mouse wheel | |
| R | camera behind Mario | C | right stick click (look behind) |
| | Mario on / off | F6 | |
| | spawn a gang to fight (testing) | F9 | |

To ground-pound, jump and then press Z in the air.

## Tests without GTA

```bash
uv run --with pytest pytest -q tests/test_mario64_core.py         # from the repo root: builds and runs core_test
cd examples/mario64-gta5/gta
g++ -std=c++17 -O1 -Isrc tests/core_test.cpp src/core/*.cpp -o core_test && ./core_test
```

The tests build a small fake GTA world: open ground, a 4 m building, a curb and a bridge you can walk under,
probed with exact ray-box tests. On it they check:

- the frame mapping;
- floors, roofs, overhangs, steps and walls, including the winding SM64 expects;
- the camera: follows, doesn't swing toward Mario, recenters, zooms, pulls in at walls, ignores small jumps;
- combat: reach, cooldown, ground-pound kills and shockwave falloff;
- the ROM checks and the fixed step.

With a built libsm64 (`git clone https://github.com/libsm64/libsm64 && make -C libsm64 lib`), set
`LIBSM64_DIR=<that checkout>` and the pytest also loads the collision into the real libsm64. It checks floor
heights and wall pushes there, plus a rotated, moving car box. No ROM is needed for those checks.

## Not verified in game

These were written from documentation and reading code, not watched in GTA. They are the first things to check
on a real run:

- **Probes from inside a closed shape.** The roof/overhang logic assumes GTA's shape tests don't report a hit
  for a ray that starts inside a building. If they do, roofs next to Mario become overhangs, and he can walk
  "under" buildings into their walls.
- **DRAW_POLY.** The winding it culls, and how it looks under GTA's lighting. `DrawBothSides=1` sidesteps the
  culling at twice the draw calls (about 2,000 per frame). If it's too slow or looks wrong, drawing Mario in a
  ReShade add-on or as a real mesh is the next step.
- **Whether GTA's AI attacks a hidden, frozen player ped**, and whether cops shoot it rather than trying to
  arrest it.
- **Health refills against big hits.** An explosion or a car at speed can kill the hidden ped outright before it
  is refilled. The script then switches Mario off.
- **Look scaling.** `MouseLook` and `PadLook` are guesses.
- **Vehicles are boxes with heading only.** They have no pitch or roll, and their cabins aren't hollow.
- **No sound.** libsm64's audio isn't wired up.
- **Peds aren't solid to Mario.** He walks through people; only attacks connect.

## Safety

- **Story mode only; never GTA Online.** BattlEye protects GTA Online. This runs with BattlEye off, which also
  keeps Online from starting, and ScriptHookV closes the game if it goes online anyway.
- **Never automate clicks on GTA's landing page while someone is at the keyboard.** See the Minecraft example's
  note on how close that came to sending a modded game to Online. Pick Story Mode by hand.
- **The game folder.** `install.sh` lists what it adds and won't replace another mod's ASI loader or your
  `args.txt` without `FORCE=1`. `--remove` takes out exactly what it added.
- **Saves.** The script doesn't touch saves, but back them up before modded play anyway (`um backup`).
- **Redistribution.**
  - **Never commit or publish a ROM**; `.gitignore` here blocks `*.z64`, `*.v64` and `*.n64`.
  - **No game data in `sm64.dll`:** it is built from libsm64's source. Mario's look comes from your ROM at run
    time.
  - **Not redistributed here:** ScriptHookV is fetched from its own site.

## Credits

- [libsm64](https://github.com/libsm64/libsm64) (CC0) by jaburns and contributors, built on the
  [Super Mario 64 decompilation](https://github.com/n64decomp/sm64). G64 (libsm64 in Garry's Mod) showed the way.
- [ScriptHookV](https://www.dev-c.com/gtav/scripthookv/) and its ASI loader by Alexander Blade.
- GTA V native names and hashes from alloc8or's [native DB](https://github.com/alloc8or/gta5-nativedb-data)
  and the NativeDB authors.
- Written by an AI coding agent (GitHub Copilot).
- Super Mario 64 belongs to Nintendo, and GTA V to Rockstar Games and Take-Two. This is a fan project.
