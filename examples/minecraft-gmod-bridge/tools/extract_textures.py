"""Copy the textures the GMod addon draws Minecraft with out of YOUR Minecraft client jar.

Nothing from Minecraft ships with this example: run this on your own install, and it writes PNGs into the addon's
materials/mcbridge/{block,entity}/ (git-ignored). Without them the addon draws flat colours.

    python extract_textures.py [path/to/26.3.jar] [path/to/garrysmod/addons/mcbridge]

The jar defaults to .minecraft/versions/26.3/26.3.jar; the addon folder defaults to ../gmod/mcbridge.
Python 3.10+, standard library only. Don't publish the output.
"""
import os
import sys
import zipfile
from pathlib import Path

VERSION = "26.3"

# Minecraft's entity skins -> the names cl_render.lua looks for
ENTITIES = {
    "zombie/zombie.png": "zombie", "zombie/husk.png": "husk", "zombie/drowned.png": "drowned",
    "skeleton/skeleton.png": "skeleton", "skeleton/stray.png": "stray", "skeleton/wither_skeleton.png": "wither_skeleton",
    "skeleton/bogged.png": "bogged", "creeper/creeper.png": "creeper",
}


def default_jar():
    if sys.platform == "win32":
        root = Path(os.environ.get("APPDATA", "")) / ".minecraft"
    elif sys.platform == "darwin":
        root = Path.home() / "Library" / "Application Support" / "minecraft"
    else:
        root = Path.home() / ".minecraft"
    return root / "versions" / VERSION / f"{VERSION}.jar"


def main():
    jar = Path(sys.argv[1]) if len(sys.argv) > 1 else default_jar()
    addon = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parent.parent / "gmod" / "mcbridge"
    if not jar.is_file():
        sys.exit(f"no Minecraft jar at {jar}: launch {VERSION} once in the official launcher, or pass the jar's path")

    blocks = addon / "materials" / "mcbridge" / "block"
    entities = addon / "materials" / "mcbridge" / "entity"
    blocks.mkdir(parents=True, exist_ok=True)
    entities.mkdir(parents=True, exist_ok=True)

    nb = ne = 0
    with zipfile.ZipFile(jar) as z:
        for name in z.namelist():
            if name.startswith("assets/minecraft/textures/block/") and name.endswith(".png"):
                rest = name[len("assets/minecraft/textures/block/"):]
                if "/" in rest:
                    continue
                (blocks / rest).write_bytes(z.read(name))
                nb += 1
            elif name.startswith("assets/minecraft/textures/entity/"):
                rest = name[len("assets/minecraft/textures/entity/"):]
                if rest in ENTITIES:
                    (entities / f"{ENTITIES[rest]}.png").write_bytes(z.read(name))
                    ne += 1

    print(f"{nb} block and {ne} mob textures -> {addon / 'materials' / 'mcbridge'}")
    if ne < len(ENTITIES):
        print("some mob skins weren't found (paths change between versions): those mobs are drawn as coloured boxes")


if __name__ == "__main__":
    main()
