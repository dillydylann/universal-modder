"""A stand-in for Garry's Mod: talks to the gmodbridge Minecraft mod exactly like the GMod addon does, so the
Minecraft half can be tested without GMod.

It gives Minecraft a stone floor at y = 63 with a water pool, stands a "player" at the centre, spawns zombies, shoots
the first one with "SMG" damage every second, picks it up with a "physgun" and throws it, fires an "RPG" at the
others, and prints everything Minecraft reports back: mobs, mob hits on the player, explosions, block changes.

Start Minecraft with the gmodbridge mod first (it opens the "gmodbridge" world), then:
    python fakegmod.py [seconds] [port]
Python 3.10+, standard library only.
"""
import json
import sys
import time
import urllib.request

PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 25600
URL = f"http://127.0.0.1:{PORT}/tick"
SECONDS = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
PLAYER = [0.5, 64.0, 0.5]
PLAYER_PED = 1          # the "entity index" of the fake player
FLOOR = range(-12, 13)
POOL = [(x, 63, z) for x in range(4, 7) for z in range(-6, -3)]  # a 3x3 pool, one deep


def post(body):
    req = urllib.request.Request(URL, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "X-MCBridge": "1"})
    with urllib.request.urlopen(req, timeout=2) as r:
        return json.loads(r.read())


def flat(cells):
    return [c for cell in cells for c in cell]


def main():
    try:
        print("hello:", urllib.request.urlopen(f"http://127.0.0.1:{PORT}/hello", timeout=2).read().decode())
    except OSError as e:
        sys.exit(f"Minecraft isn't listening on 127.0.0.1:{PORT} ({e}). Start it with the gmodbridge mod first.")

    floor = [(x, 63, z) for x in FLOOR for z in FLOOR if (x, 63, z) not in POOL]
    first = {"v": 1, "clear": True, "player": PLAYER + [0, 0], "solid": flat(floor), "water": flat(POOL), "sync": 16,
             "peds": [[PLAYER_PED] + PLAYER]}
    reply = post(first)
    print("connected, ready =", reply.get("ready"))

    start = time.time()
    tick = 0
    spawned = held = released = exploded = False
    target = None
    seen_kinds = set()
    counts = {}
    last_hp = {}
    player_hp = 100.0

    while time.time() - start < SECONDS:
        t = time.time() - start
        body = {"v": 1, "player": PLAYER + [0, 0], "peds": [[PLAYER_PED] + PLAYER]}

        if not spawned and t > 1:
            body["spawn"] = [["zombie", 6.5, 64, 6.5], ["zombie", -6.5, 64, 6.5], ["creeper", 0.5, 64, 9.5]]
            spawned = True

        if target is not None:
            if tick % 20 == 0 and not held:
                # one SMG bullet: 12 GMod damage / mcbridge_hp_scale 5 = 2.4 Minecraft health
                body["dmg"] = [[target, 2.4, PLAYER_PED, "bullet"]]
            if 8 < t < 10:
                body["hold"] = [[target, 0.5, 66.0 + (t - 8), 3.5]]
                held = True
            elif held and not released:
                body["release"] = [[target, 0.0, 0.4, 0.8]]  # thrown forward and up
                released = True

        if not exploded and t > 12:
            body["explode"] = [[-6.5, 64.5, 6.5, 2.0]]  # an RPG rocket
            exploded = True

        try:
            reply = post(body)
        except OSError as e:
            print(f"{t:5.1f}s  link error: {e}")
            time.sleep(0.5)
            continue

        for m in reply.get("mobs", []):
            mid, kind, hp = m[0], m[1], m[8]
            if kind not in seen_kinds:
                seen_kinds.add(kind)
                print(f"{t:5.1f}s  mob {mid} {kind} at ({m[2]:.1f}, {m[3]:.1f}, {m[4]:.1f}) hp {hp}/{m[9]}"
                      f" hostile={m[11] if len(m) > 11 else '?'}")
            if target is None and kind == "zombie":
                target = mid
            if last_hp.get(mid, hp) != hp:
                print(f"{t:5.1f}s  mob {mid} {kind} hp {last_hp[mid]} -> {hp}{'  (held)' if m[10] else ''}")
            last_hp[mid] = hp

        for e in reply.get("events", []):
            kind = e.get("t")
            counts[kind] = counts.get(kind, 0) + 1
            if kind == "mobhit" and e.get("h") == PLAYER_PED:
                player_hp -= e.get("d", 0) * 5
                print(f"{t:5.1f}s  player hit by mob {e.get('id')} ({e.get('k')}) for {e.get('d')} -> {player_hp:.0f} GMod hp")
            elif kind == "explosion":
                print(f"{t:5.1f}s  explosion at {e.get('pos')} power {e.get('r')} ({e.get('src')})")
            elif kind == "blocks":
                print(f"{t:5.1f}s  blocks: {len(e.get('set', []))} set, {len(e.get('clear', [])) // 3} cleared,"
                      f" {len(e.get('water', []))} water")
            elif kind != "mobhit":
                print(f"{t:5.1f}s  {e}")

        tick += 1
        time.sleep(0.05)

    post({"v": 1, "clearmobs": True})
    print("events:", counts)
    print("mob kinds seen:", sorted(seen_kinds))


if __name__ == "__main__":
    main()
