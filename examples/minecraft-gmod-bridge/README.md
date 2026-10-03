# Minecraft x Garry's Mod: a two-way bridge

Real Minecraft Java 26.3 (with a small Fabric mod) runs next to real Garry's Mod, and the two simulations are
joined:

- GMod weapons hurt Minecraft mobs. SMG, pistol, crowbar, shotgun and thrown props all count, and fire sets them
  alight.
- A GMod RPG rocket (or an exploding barrel or grenade) makes a real Minecraft explosion that breaks blocks. Minecraft
  TNT and creepers explode in GMod too: blast damage, the effect and screen shake.
- Minecraft mobs hunt the GMod player and GMod NPCs. Zombies, skeleton arrows and creeper blasts take GMod health, and
  hostile mobs and Combine/rebels fight each other.
- Water works the same on both sides.
  - GMod's water becomes Minecraft water, so mobs swim in it and it flows.
  - Minecraft's water is swimmable in GMod: buoyancy, drag and no fall damage. It floats props, puts out fires, and
    tints your view underwater.
- The physgun (and gravity gun) picks up Minecraft blocks and mobs.
  - A grabbed block leaves Minecraft's world. You can throw it, and it goes back into Minecraft where it lands.
  - A grabbed mob is held in GMod. When you let go, Minecraft takes it back with your throw's velocity, so fall
    damage applies.

> **Status: built, not yet run.** This example was written without either game available: the Fabric mod has not
> been compiled (Fabric's Maven was unreachable) and the addon has not been loaded in GMod. The Lua was
> syntax-checked and the Minecraft half has a fake GMod host (`host/fakegmod.py`) to test it alone. Expect API name
> fixes on first build. The design and the uncertain spots are listed below and in the knowledge note
> (`knowledge/games/garry-s-mod/minecraft-bridge.md`). Please update both when you get it running.

Pattern: **passthrough** (see `skills/mashup-mods/SKILL.md`), but drawn with GMod-native entities instead of a frame
compositor. GMod's Lua can't read another process's frames, but it can draw meshes and spawn physics objects, which
gives GMod's lighting, physics, bullets and physgun for free.

## How it works

```
 Garry's Mod (server realm, Lua)                         Minecraft 26.3 + gmodbridge (Fabric)
 ───────────────────────────────                         ────────────────────────────────────
 every tick: HTTP POST 127.0.0.1:25600/tick  ─────────►  queue → applied at the end of the server tick
   player pos/aim, peds (player, NPCs)                     spectator camera follows the GMod player
   voxelised GMod world near the player (solid/water)      → barrier blocks / water source blocks
   dmg / hold / release / explode / put / take / break     → hurts mobs, physgun, explosions, blocks
                                              ◄─────────  reply: {ready, events, mobs}
   mc_mob entities follow "mobs"                           mobs near the player: id, kind, pos, size, hp
   mc_block entities + water from "blocks"                 block/water changes near the player
   util.BlastDamage on "explosion"                         every explosion (TNT, creeper, fireball)
   DamageInfo on the real player/NPC on "mobhit"           a mob hit a ped's invisible villager proxy
```

**Authority.** Each game owns its own things. Minecraft owns its mobs, blocks and water: GMod mirrors them, and sends
back what GMod did to them. GMod owns its world, players, NPCs and props: Minecraft sees the world as invisible
barrier blocks (and water), and each player/NPC as an invisible, AI-less villager "proxy" that hostile mobs target.

**Coordinates.** 1 block = `mcbridge_scale` Hammer units (default 40: player height 72 u ≈ 1.8 blocks). GMod
(x, y, z) → Minecraft (x/S, z/S + yOff, −y/S); yaw_MC = −yaw_GMod − 90 (its own inverse); pitch is the same. `yOff`
puts the GMod player's start height at y = 64 and is kept for the whole map (`mcbridge_reset` re-levels).

**No loops, no double damage.**
- Explosions GMod asked for aren't reported back.
- A Minecraft explosion hurts mobs in Minecraft and people in GMod (BlastDamage), never both: proxies ignore
  explosion damage, and `mc_mob` ignores `DMG_BLAST`.
- Barrier/water cells the host placed, and blocks the physgun took, are "quiet": they aren't reported back.
- Drowning in Minecraft water isn't reported when GMod already drowns that person.

## Files

| Path | What |
|---|---|
| `mc/` | Fabric mod `gmodbridge` (Loom, JDK 25, Minecraft 26.3, unobfuscated: no mappings) |
| `mc/.../HttpLink.java` | HTTP server on 127.0.0.1 (`-Dgmodbridge.port`, default 25600). Needs header `X-MCBridge: 1`, rejects `Origin` |
| `mc/.../Bridge.java` | The protocol (documented at the top), world ops, block/water change reports, explosions, camera follow |
| `mc/.../Mobs.java` | Ped proxies, mob hits → host, host damage → mobs, physgun hold/release, mob list |
| `mc/.../mixin/` | Block-change hook, explosion reporting, proxies immune and targeted by hostile mobs |
| `mc/src/client/.../GmodBridgeClient.java` | Opens/creates the void world `gmodbridge`, sets gamerules, keeps running unfocused |
| `gmod/mcbridge/` | The GMod addon (copy to `garrysmod/addons/mcbridge`) |
| `.../lua/mcbridge/sh_core.lua` | Convars, coordinate mapping, mirrored water and swimming |
| `.../lua/mcbridge/sv_link.lua` | The per-tick HTTP poll |
| `.../lua/mcbridge/sv_world.lua` | Voxelising the GMod world, Minecraft blocks as entities, explosions both ways, buoyancy |
| `.../lua/mcbridge/sv_mobs.lua` | Mobs as entities, mob hits on people, NPC relationships, physgun/gravity gun hooks, commands |
| `.../lua/mcbridge/cl_render.lua` | Batched block meshes, water, Minecraft-style box models with their skins |
| `.../lua/entities/mc_mob.lua`, `mc_block.lua` | The GMod stand-ins |
| `.../lua/weapons/weapon_mc_blocks.lua` | "Minecraft blocks" tool: place/break blocks, light TNT, spawn mobs |
| `host/fakegmod.py` | A fake GMod host, to test the Minecraft half alone |
| `tools/extract_textures.py` | Copies block/mob textures out of **your** Minecraft jar into the addon (git-ignored) |

## Requirements

- Garry's Mod (Steam), single-player or a local listen server.
- Minecraft Java 26.3 with Fabric Loader 0.19.5 and Fabric API 0.161.0+26.3.
- JDK 25 to build the mod, and Python 3.10+ for the tools.

## Build, install, run

1. **Build the mod.** In `mc/`, run `./gradlew build` and take `build/libs/gmodbridge-0.1.0.jar`. Give it its own
   Fabric profile and game directory: the mod creates a world called `gmodbridge` and fills it with barrier blocks.
   Put the jar and Fabric API in that profile's `mods/`.
2. **Install the addon.** Copy `gmod/mcbridge` to `garrysmod/addons/mcbridge`. Then run
   `python tools/extract_textures.py [your 26.3.jar] [garrysmod/addons/mcbridge]` to get textures; without them,
   everything is drawn in flat colours.
3. **Start Minecraft.** It opens the `gmodbridge` world by itself and keeps running when unfocused. You can minimise
   it.
4. **Start GMod with `-allowlocalhttp`** (Steam → Properties → Launch options). Without it, GMod's Lua `HTTP()`
   refuses 127.0.0.1. Start a single-player map, e.g. `gm_construct` or `gm_flatgrass`. The console prints
   `[mcbridge] connected to Minecraft` and Minecraft's spectator camera follows you.
5. Give yourself the **Minecraft blocks** tool (Weapons → Minecraft), or use `mcbridge_spawn zombie 5`.

### Commands and convars

| | |
|---|---|
| `mcbridge_spawn <kind> [n]` | Spawn Minecraft mobs where you look (autocompletes) |
| `mcbridge_clearmobs` | Remove all Minecraft mobs near you |
| `mcbridge_reset` | Forget and re-send the world, re-levelled so Minecraft's y = 64 is where you stand |
| `mcbridge_enabled 0/1` | Pause/resume the link |
| `mcbridge_port` | Port of the mod's server (default 25600, match `-Dgmodbridge.port`) |
| `mcbridge_scale` | Hammer units per block (default 40; change before connecting) |
| `mcbridge_hp_scale` | GMod health per Minecraft health point (default 5: 100 GMod HP = 20 MC HP) |

In GMod: **physgun** blocks and mobs, and **E** on TNT lights it (so does the tool's flint and steel, or fire
damage to the block). Bullets break glass and leaves. The **tool**: left click places or spawns, right click breaks, reload picks the
next item.

## Tests without GMod

```
python host/fakegmod.py 20
```

It talks to the running Minecraft exactly like the addon. It builds a stone floor with a 3×3 water pool and stands
a player in the middle. Then it spawns zombies and a creeper, shoots the first zombie every second with SMG damage,
physguns it up and throws it, and fires an "RPG" at the other zombie. It prints:
- mob health changes;
- mob hits on the player (in GMod health);
- explosions;
- block/water reports.

What you should see:
- the zombies walk to the player and hit it;
- the shot zombie's health drops by 2.4 a second;
- the thrown zombie takes fall damage;
- the RPG reports no `explosion` event back (host-requested) but kills or hurts the other zombie;
- a creeper that reaches the player reports an `explosion` (`src` creeper) and a `mobhit`.

Checks in GMod, in order (a vertical slice first, then the rest):
1. `[mcbridge] connected to Minecraft`, a zombie from `mcbridge_spawn` stands on the map's floor (the voxelised world), and
   walks to you.
2. Shoot it with the SMG or pistol: it flashes red, and dies in Minecraft once 20 Minecraft HP (100 GMod damage at
   the default `mcbridge_hp_scale`) is dealt.
3. It hits you: your health drops in GMod.
4. Place TNT with the tool, press E: 4 s later GMod shakes and nearby props and NPCs are hurt.
5. Fire the RPG at a block pile: Minecraft's crater appears.
6. Physgun a block out of a wall, throw it, and watch it come back as a placed block. Then physgun a zombie and
   drop it from height.
7. Walk into Minecraft water placed with the tool: you swim. A zombie in GMod's water (e.g. `gm_construct`'s pool)
   swims too.

## Safety

- **Single-player or your own listen server only.** GMod is VAC-protected: this addon is plain Lua and needs nothing
  disabled, but don't run it, or `-allowlocalhttp`, on public servers, and never point it at anyone else's server.
- The mod's HTTP server listens on loopback only. It needs a custom header that browsers can't send cross-origin
  without a preflight it never answers, and it refuses any request with an `Origin`. So a web page can't drive it.
- Use a separate Minecraft game directory: the mod writes barrier blocks around the player in the `gmodbridge` world.
- Nothing from either game ships here. Textures come from your own jar, via `tools/extract_textures.py`, and their
  output folder is git-ignored. Don't publish it.

## Limitations and open questions

- **Unverified API names** in the Fabric mod (it compiles against 26.3 using names carried over from the GTA example
  where possible):
  - `ServerLevel.explode(..., Level.ExplosionInteraction.TNT)`;
  - `LivingEntity.resetFallDistance`, `setRemainingFireTicks`, `Mob.isNoAi`, and the `damageCooldownTime` field;
  - `DamageTypeTags.IS_FIRE` / `IS_DROWNING`, `FluidTags.WATER`;
  - the `fuse` NBT key of primed TNT.
- Only the world near the player is mirrored: blocks within 24 cells become entities, and up to 2500 of them.
  Minecraft's simulation distance limits the rest.
- GMod's dynamic props aren't voxelised: Minecraft mobs walk through them. World brushes and static props are.
- Non-cube blocks (slabs, stairs, fences) are drawn and collide as full cubes. Mobs without a box model (spiders,
  cows...) are drawn as coloured boxes of their size.
- Minecraft's own arrows and fireballs aren't drawn in GMod yet. Their hits are, as `mobhit`.
- Single host player: in multiplayer, only the first player drives Minecraft's camera and voxelisation.
- Re-levelling (`mcbridge_reset`) doesn't move blocks you've built.
- A frame compositor (Minecraft's real renderer drawn into GMod) would look closer to Minecraft. It would need a
  binary module, and is left for later.
