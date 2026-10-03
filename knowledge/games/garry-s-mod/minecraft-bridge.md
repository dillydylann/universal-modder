---
kind: game
title: "Minecraft mobs, blocks and water inside Garry's Mod (two-way bridge)"
game: "Garry's Mod"
games_also: ["Minecraft Java Edition"]
game_version: "Not yet run. Written for Garry's Mod (Steam, current branch) + Minecraft Java 26.3 with Fabric Loader 0.19.5 / Fabric API 0.161.0+26.3"
platform: windows
engine: source
route: passthrough
tools: ["Fabric Loader + Fabric API (Loom, JDK 25)", "GMod Lua addon (HTTP + entities + IMesh)", "Python (fake host, texture converter)"]
anti_cheat: "VAC on GMod servers: plain Lua addon, single-player / own listen server only, nothing disabled; Minecraft offline world"
status: in-progress
agents: ["GitHub Copilot coding agent"]
humans: []
date: 2026-10-03
links: ["examples/minecraft-gmod-bridge"]
tags: [mashup, passthrough, http, fabric, mixin, gmod-lua, physgun, explosions, water, voxelisation, proxies]
---

# Minecraft mobs, blocks and water inside Garry's Mod (two-way bridge)

> Real Minecraft 26.3 (Fabric mod `gmodbridge`) runs next to Garry's Mod and owns the mobs, blocks and water. GMod
> polls it over HTTP on 127.0.0.1 every tick. GMod mirrors them as native entities (bullets, physgun, lighting) and
> sends back damage, grabs, explosions and its own voxelised world. **Unverified:** written without either game.
> The mod hasn't been compiled (Fabric's Maven was unreachable from the sandbox) and the addon hasn't been loaded;
> only Lua syntax and the Python tools were checked. Code: `examples/minecraft-gmod-bridge`.

## Setup
- Garry's Mod launched with **`-allowlocalhttp`**: without it, Lua `HTTP()` refuses localhost and private
  addresses.
- Minecraft Java 26.3 in its own Fabric profile and game directory, with the mod and Fabric API. The mod opens a
  void world "gmodbridge" by itself.
- Textures: `tools/extract_textures.py` copies them out of the user's own client jar into the addon (git-ignored).

## Route and why
- **Passthrough, but host-native drawing.** GMod Lua has no shared memory or GPU interop without a binary module, so
  a GTA-style frame compositor (`knowledge/games/gta-v/minecraft-passthrough.md`) would need a C++ module. Drawing
  Minecraft's state with GMod entities and IMeshes is pure Lua, and it gets GMod's own bullets, physgun, gravity
  gun, NPC AI and lighting for free. That's what the requested interactions need.
- **Transport: HTTP polling.** GMod Lua has `HTTP()` but no sockets. The host POSTs its state once per tick, with
  one request in flight, and the reply carries Minecraft's events since the last poll. The latency is one to two
  ticks, which is fine for mobs and blocks.
- **Considered: porting content** (pattern 1, GMod NPCs that imitate zombies). It was rejected because the request
  is real Minecraft: its AI, explosions, water flow and block world.

## How the game works (what we had to learn)
- **Scale.** 1 block = 40 Hammer units by default (GMod player 72 u ≈ Steve's 1.8 blocks). GMod (x, y, z) → MC
  (x/S, z/S + yOff, −y/S), and yaw_MC = −yaw_GMod − 90. GMod velocity (u/s) → blocks/tick: divide by S·20.
- **GMod → Minecraft world.** Hull traces on a grid of cells around the player become barrier blocks.
  `util.PointContents` with `CONTENTS_WATER` becomes water source blocks, but only after a leak check, so the
  water doesn't flow out of open sides. The scan stays under a trace budget per tick.
- **People.** Each GMod player/NPC has an invisible, no-AI villager **proxy** in Minecraft. A mixin makes hostile
  mobs target proxies, and proxies cancel all damage and report it back as `mobhit` (amount, attacker, kind). GMod
  applies it to the real entity as a `DamageInfo` (melee, arrow, explosion, fire), scaled by `mcbridge_hp_scale`.
- **NPCs vs mobs.** Each hostile `mc_mob` carries a parented, non-solid `npc_bullseye`. Every NPC is given `D_HT`
  towards it, so Combine and rebels shoot zombies. Their bullets hit the `mc_mob` and become `dmg`.
- **Explosions.**
  - Minecraft → GMod: an explosion mixin reports position, power and source, and GMod does `util.BlastDamage`
    (radius 2 × power blocks) plus the effect and screen shake.
  - GMod → Minecraft: an `EntityRemoved` of `rpg_missile` / `npc_grenade_frag` and friends, or a `PropBreak` of
    explosive barrels, calls `level.explode` with TNT interaction.
  - Host-requested explosions are flagged so they aren't echoed back.
- **Physgun.** `PhysgunPickup` returning true allows it.
  - Blocks: `OnPhysgunPickup` takes the block out of Minecraft quietly and makes the entity a loose physics cube.
    When it rests (or is frozen), it's "put" back at the cell it's in.
  - Mobs: the mob is held in Minecraft (`hold` follows the physgun, with no AI, no gravity and no fall damage). On release,
    Minecraft gets the throw velocity.

## Build steps
See `examples/minecraft-gmod-bridge/README.md`:
1. `./gradlew build` in `mc/` and install the jar in a separate Fabric profile.
2. Copy `gmod/mcbridge` to `garrysmod/addons/`, then run `tools/extract_textures.py`.
3. Start Minecraft, then GMod with `-allowlocalhttp`, on a single-player map.

## Verification
- Done:
  - `luajit -bl` parses every Lua file;
  - `javac` finds no syntax errors in the Java (symbols unresolved without Minecraft);
  - the texture converter was run on a synthetic jar.
- Not done: everything in the real games. `host/fakegmod.py` is the oracle for the Minecraft half (it prints mob
  HP, mob hits, explosions and block reports). The README lists the in-game checks, in order.

## Gotchas
1. **Lua `HTTP()` to 127.0.0.1 fails silently.** **Cause:** GMod blocks local/private addresses by default.
   **Fix:** the `-allowlocalhttp` launch option.
2. **The body posted as `[]` when there was nothing to send.** **Cause:** `util.TableToJSON({})` gives an array,
   and the mod expects an object. **Fix:** always include `"v": 1`.
3. **Double damage from explosions.** **Cause:** a Minecraft explosion hurts mobs there, and GMod's `BlastDamage`
   hit the `mc_mob` stand-ins too. **Fix:** `mc_mob` ignores `DMG_BLAST`; proxies ignore explosion damage in
   Minecraft.
4. **Feedback loops.** **Cause:** blocks the host placed (barriers, water, physgun takes) were reported back as
   changes. **Fix:** a "quiet" flag while applying host ops, and a set of host-owned cells that never get
   reported.
5. **Web pages could drive a localhost server.** **Cause:** any page can POST to 127.0.0.1. **Fix:** a required
   custom header, which forces a CORS preflight that's never answered, plus a refusal of any `Origin`.
6. **The GMod world never reached Minecraft when GMod was up first** (found in review, before running). **Cause:**
   the mod's HTTP server starts before its world loads, and messages are dropped until then. The addon counted
   any reply as "connected" and never re-sent the cells it had scanned. **Fix:** only `ready: true` counts as
   connected, and going not-ready (a world closing) resets the scan.

## Assets
None committed. Block and mob textures come from the user's own Minecraft jar, via `tools/extract_textures.py`.
Mobs are drawn as Minecraft-style box models using the vanilla skin layout. Mobs without a model are drawn as
coloured boxes.

## Open questions
- First compile against 26.3: confirm `ServerLevel.explode`, `resetFallDistance`, `setRemainingFireTicks`,
  `isNoAi`, the `damageCooldownTime` field, the `DamageTypeTags` / `FluidTags` names and the primed-TNT `fuse` key.
- Do HTTP polls every tick keep up at 66 tick/s with a big reply? If not, poll every other tick.
- Voxelising dynamic props, non-cube block shapes, drawing Minecraft projectiles, and multiplayer.
