package dev.universalmodder.gmodbridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Pattern;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.Vec3;

/**
 * Server-side state of the bridge. Everything from the host is queued by the HTTP thread and applied on the server
 * thread at the end of each tick; everything for the host is queued here and drained by the next poll.
 *
 * <p>Host to Minecraft (one JSON object per poll, Minecraft coordinates; every key optional):
 * <ul>
 * <li>"clear": true — remove the host's ground and water placed so far (new map, or re-levelled)</li>
 * <li>"solid": [x,y,z, ...] — cells the host's map fills: barriers (only replaces air)</li>
 * <li>"water": [x,y,z, ...] — cells of the host's map water: water sources (only replaces air)</li>
 * <li>"player": [x,y,z,yaw,pitch] — the host player's feet: Minecraft's (spectator) player follows it, so chunks
 *     around it stay loaded and its mobs keep ticking</li>
 * <li>"peds": [[handle,x,y,z], ...] — the host's player and NPCs (see {@link Mobs})</li>
 * <li>"dmg": [[id,amount,attacker handle,kind], ...] — the host hurt a mob (Minecraft health points)</li>
 * <li>"hold": [[id,x,y,z], ...] / "release": [[id,vx,vy,vz], ...] — physgun on a mob (velocity in blocks/tick)</li>
 * <li>"explode": [[x,y,z,power], ...] — a host explosion (rocket, grenade, barrel): a Minecraft explosion there</li>
 * <li>"take": [x,y,z, ...] — the physgun lifted these blocks out of the world (removed quietly)</li>
 * <li>"put": [[x,y,z,"block"], ...] — set a block down there (or the first free cell just above)</li>
 * <li>"break": [x,y,z, ...], "ignite": [x,y,z, ...] (TNT), "spawn": [["zombie",x,y,z], ...], "clearmobs": true</li>
 * <li>"sync": radius — report every block and water cell within radius of the player</li>
 * </ul>
 * Minecraft to host, in the reply: {"v":1,"ready":bool,"events":[...],"mobs":[...]}; events are
 * {"t":"blocks","set":[[x,y,z,"block"],...],"clear":[x,y,z,...],"water":[[x,y,z,level],...]},
 * {"t":"explosion","pos":[x,y,z],"r":power,"src":"tnt"} and {"t":"mobhit",...} (see {@link Mobs}).
 */
public final class Bridge {
	private static final Pattern NAME = Pattern.compile("[a-z0-9_]{1,64}");
	/** The host counts as gone after this long without a poll. */
	private static final long HOST_TIMEOUT_NANOS = 2_000_000_000L;
	private static final int MAX_OUTBOX = 4096;

	private static volatile MinecraftServer server;
	private static final ConcurrentLinkedQueue<JsonObject> inbound = new ConcurrentLinkedQueue<>();
	private static final ConcurrentLinkedQueue<String> outbox = new ConcurrentLinkedQueue<>();
	private static final AtomicInteger outboxSize = new AtomicInteger();
	private static volatile long lastPollNanos;
	private static volatile boolean active;
	/** The newest mob/TNT snapshot, a JSON array (server thread writes, HTTP thread reads). */
	private static volatile String mobs = "[]";

	// server thread only
	/** Barriers and water we placed for the host (a reset only removes ours, never what was built). */
	private static final Set<BlockPos> hostCells = new HashSet<>();
	/** Block changes this tick: block name, or "" when no longer solid. */
	private static final Map<BlockPos, String> changes = new LinkedHashMap<>();
	/** Water changes this tick: 1-8 (8 = source), or 0 when no longer water. */
	private static final Map<BlockPos, Integer> waterChanges = new LinkedHashMap<>();
	/** While placing the host's own cells or lifting blocks for its physgun: not news to the host. */
	private static boolean quiet;
	/** While setting off an explosion the host asked for: the host already has its own. */
	private static boolean hostExplosion;
	private static Vec3 player;
	private static double[] lastTp;

	private Bridge() {
	}

	static void attach(final MinecraftServer s) {
		server = s;
	}

	static void detach(final MinecraftServer s) {
		clearHostCells(s.overworld());
		server = null;
		changes.clear();
		waterChanges.clear();
		player = null;
		lastTp = null;
	}

	public static boolean active() {
		return active;
	}

	static MinecraftServer server() {
		return server;
	}

	/** Where the host's player is (Minecraft coordinates), or null before the first poll. Server thread. */
	static Vec3 player() {
		return player;
	}

	// ---- HTTP thread ----

	static void fromHost(final JsonObject message) {
		lastPollNanos = System.nanoTime();
		if (server != null && inbound.size() < 64) {
			inbound.add(message);
		}
	}

	static String forHost() {
		StringBuilder b = new StringBuilder(256).append("{\"v\":1,\"ready\":").append(server != null).append(",\"events\":[");
		int n = 0;
		for (String e; (e = outbox.poll()) != null;) {
			outboxSize.decrementAndGet();
			b.append(n++ == 0 ? "" : ",").append(e);
		}

		return b.append("],\"mobs\":").append(mobs).append('}').toString();
	}

	static String hello() {
		return String.format(Locale.ROOT, "{\"v\":1,\"mod\":\"%s\",\"ready\":%b,\"pid\":%d}", GmodBridge.ID, server != null, ProcessHandle.current().pid());
	}

	/** Queue an event (one JSON object) for the host's next poll; dropped while no host is polling. */
	static void emit(final String json) {
		if (active && outboxSize.get() < MAX_OUTBOX) {
			outbox.add(json);
			outboxSize.incrementAndGet();
		}
	}

	static void mobs(final String json) {
		mobs = json;
	}

	// ---- server thread ----

	static void tick(final MinecraftServer s) {
		boolean was = active;
		active = System.nanoTime() - lastPollNanos < HOST_TIMEOUT_NANOS;
		ServerLevel level = s.overworld();
		if (was && !active) {
			GmodBridge.LOG.info("host gone");
			Mobs.hostGone(level);
			outbox.clear();
			outboxSize.set(0);
		}

		for (JsonObject m; (m = inbound.poll()) != null;) {
			try {
				apply(s, level, m);
			} catch (RuntimeException e) {
				GmodBridge.LOG.warn("bad host message: {}", e.toString());
			}
		}

		flush();
		Mobs.tick(level);
	}

	private static void apply(final MinecraftServer s, final ServerLevel level, final JsonObject m) {
		if (m.has("clear") && m.get("clear").getAsBoolean()) {
			clearHostCells(level);
		}

		if (m.has("solid")) {
			placeHostCells(level, ints(m.getAsJsonArray("solid")), Blocks.BARRIER.defaultBlockState());
		}

		if (m.has("water")) {
			placeHostCells(level, ints(m.getAsJsonArray("water")), Blocks.WATER.defaultBlockState());
		}

		if (m.has("player")) {
			follow(s, m.getAsJsonArray("player"));
		}

		if (m.has("peds")) {
			Mobs.peds(level, m.getAsJsonArray("peds"));
		}

		for (JsonElement e : list(m, "dmg")) {
			JsonArray d = e.getAsJsonArray();
			Mobs.damage(level, d.get(0).getAsInt(), d.get(1).getAsFloat(), d.get(2).getAsInt(), d.get(3).getAsString());
		}

		for (JsonElement e : list(m, "hold")) {
			JsonArray h = e.getAsJsonArray();
			Mobs.hold(level, h.get(0).getAsInt(), h.get(1).getAsDouble(), h.get(2).getAsDouble(), h.get(3).getAsDouble());
		}

		for (JsonElement e : list(m, "release")) {
			JsonArray r = e.getAsJsonArray();
			Mobs.release(level, r.get(0).getAsInt(), new Vec3(r.get(1).getAsDouble(), r.get(2).getAsDouble(), r.get(3).getAsDouble()));
		}

		for (JsonElement e : list(m, "explode")) {
			JsonArray x = e.getAsJsonArray();
			explode(level, x.get(0).getAsDouble(), x.get(1).getAsDouble(), x.get(2).getAsDouble(), x.get(3).getAsFloat());
		}

		if (m.has("take")) {
			int[] p = ints(m.getAsJsonArray("take"));
			for (int i = 0; i + 2 < p.length; i += 3) {
				take(level, new BlockPos(p[i], p[i + 1], p[i + 2]));
			}
		}

		for (JsonElement e : list(m, "put")) {
			JsonArray p = e.getAsJsonArray();
			put(level, new BlockPos(p.get(0).getAsInt(), p.get(1).getAsInt(), p.get(2).getAsInt()), p.get(3).getAsString());
		}

		if (m.has("break")) {
			int[] p = ints(m.getAsJsonArray("break"));
			for (int i = 0; i + 2 < p.length; i += 3) {
				BlockPos pos = new BlockPos(p[i], p[i + 1], p[i + 2]);
				if (!hostCells.contains(pos) && level.isLoaded(pos)) {
					level.destroyBlock(pos, false);
				}
			}
		}

		if (m.has("ignite")) {
			int[] p = ints(m.getAsJsonArray("ignite"));
			for (int i = 0; i + 2 < p.length; i += 3) {
				ignite(s, level, new BlockPos(p[i], p[i + 1], p[i + 2]));
			}
		}

		for (JsonElement e : list(m, "spawn")) {
			JsonArray p = e.getAsJsonArray();
			Mobs.spawn(level, p.get(0).getAsString(), p.get(1).getAsDouble(), p.get(2).getAsDouble(), p.get(3).getAsDouble());
		}

		if (m.has("clearmobs") && m.get("clearmobs").getAsBoolean()) {
			Mobs.clearMobs(level);
		}

		if (m.has("sync")) {
			sync(level, Math.max(1, Math.min(64, m.get("sync").getAsInt())));
		}
	}

	/** Minecraft's player is a spectator standing where the host's player stands (mobs ignore spectators). */
	private static void follow(final MinecraftServer s, final JsonArray p) {
		double x = p.get(0).getAsDouble(), y = p.get(1).getAsDouble(), z = p.get(2).getAsDouble();
		double yaw = p.get(3).getAsDouble(), pitch = p.get(4).getAsDouble();
		player = new Vec3(x, y, z);
		if (lastTp != null && Math.abs(lastTp[0] - x) + Math.abs(lastTp[1] - y) + Math.abs(lastTp[2] - z) < 0.1
			&& Math.abs(lastTp[3] - yaw) + Math.abs(lastTp[4] - pitch) < 2.0) {
			return;
		}

		lastTp = new double[] {x, y, z, yaw, pitch};
		command(s, String.format(Locale.ROOT, "tp @a %.3f %.3f %.3f %.1f %.1f", x, y, z, yaw, pitch));
	}

	public static void command(final MinecraftServer s, final String command) {
		s.getCommands().performPrefixedCommand(s.createCommandSourceStack(), command);
	}

	private static void placeHostCells(final ServerLevel level, final int[] cells, final BlockState state) {
		BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
		quiet = true;
		try {
			for (int i = 0; i + 2 < cells.length; i += 3) {
				pos.set(cells[i], cells[i + 1], cells[i + 2]);
				if (level.isInWorldBounds(pos) && level.getBlockState(pos).isAir()) {
					level.setBlock(pos, state, Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE);
					hostCells.add(pos.immutable());
				}
			}
		} finally {
			quiet = false;
		}
	}

	private static void clearHostCells(final ServerLevel level) {
		quiet = true;
		try {
			for (BlockPos pos : hostCells) {
				BlockState state = level.getBlockState(pos);
				if (state.is(Blocks.BARRIER) || state.is(Blocks.WATER)) {
					level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_CLIENTS | Block.UPDATE_KNOWN_SHAPE);
				}
			}
		} finally {
			quiet = false;
		}

		hostCells.clear();
	}

	/** The host set off an explosion (power like Minecraft's: TNT is 4): break blocks and hurt mobs here too. */
	private static void explode(final ServerLevel level, final double x, final double y, final double z, final float power) {
		hostExplosion = true;
		try {
			level.explode(null, x, y, z, Math.max(0.5F, Math.min(8.0F, power)), Level.ExplosionInteraction.TNT);
		} finally {
			hostExplosion = false;
		}
	}

	/** The physgun lifted a block: it's in the host's hands now, so it leaves the world quietly. */
	private static void take(final ServerLevel level, final BlockPos pos) {
		if (hostCells.contains(pos) || !level.isLoaded(pos) || level.getBlockState(pos).isAir()) {
			return;
		}

		quiet = true;
		try {
			level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
		} finally {
			quiet = false;
		}
	}

	/** A block set down (physgun drop or the block tool): at pos, or the first free cell up to 4 above it. */
	private static void put(final ServerLevel level, final BlockPos pos, final String name) {
		if (!NAME.matcher(name).matches()) {
			return;
		}

		Optional<Block> block = BuiltInRegistries.BLOCK.getOptional(Identifier.withDefaultNamespace(name));
		if (block.isEmpty() || block.get() == Blocks.BARRIER || block.get() == Blocks.AIR) {
			return;
		}

		for (int dy = 0; dy <= 4; dy++) {
			BlockPos p = pos.above(dy);
			BlockState there = level.getBlockState(p);
			if (level.isInWorldBounds(p) && level.isLoaded(p) && (there.isAir() || (there.is(Blocks.WATER) && !hostCells.contains(p)))) {
				level.setBlock(p, block.get().defaultBlockState(), Block.UPDATE_ALL);
				return;
			}
		}

		// nowhere to put it: tell the host it's gone, so it doesn't wait for a block that never comes
		emit(String.format(Locale.ROOT, "{\"t\":\"putfail\",\"pos\":[%d,%d,%d],\"b\":\"%s\"}", pos.getX(), pos.getY(), pos.getZ(), name));
	}

	/** Flint and steel from the host: a TNT block becomes primed TNT (a normal 4 s fuse). */
	private static void ignite(final MinecraftServer s, final ServerLevel level, final BlockPos pos) {
		if (!level.isLoaded(pos) || !level.getBlockState(pos).is(Blocks.TNT)) {
			return;
		}

		level.setBlock(pos, Blocks.AIR.defaultBlockState(), Block.UPDATE_ALL);
		command(s, String.format(Locale.ROOT, "summon minecraft:tnt %.2f %d %.2f {fuse:80}", pos.getX() + 0.5, pos.getY(), pos.getZ() + 0.5));
	}

	/** Server thread, from Level.setBlock: remember the change; flushed once per tick. */
	public static void onBlockChanged(final ServerLevel level, final BlockPos pos, final BlockState state) {
		if (!active || quiet || level != level.getServer().overworld()) {
			return;
		}

		BlockPos p = pos.immutable();
		hostCells.remove(p); // ours no longer: something replaced it
		changes.put(p, solidName(level, p, state));
		waterChanges.put(p, waterLevel(state));
	}

	private static String solidName(final ServerLevel level, final BlockPos pos, final BlockState state) {
		if (state.isAir() || state.is(Blocks.BARRIER) || state.getCollisionShape(level, pos).isEmpty()) {
			return "";
		}

		return BuiltInRegistries.BLOCK.getKey(state.getBlock()).getPath();
	}

	private static int waterLevel(final BlockState state) {
		FluidState fluid = state.getFluidState();
		return fluid.isEmpty() || !fluid.is(FluidTags.WATER) ? 0 : Math.max(1, Math.min(8, fluid.getAmount()));
	}

	/** Every block and water cell within `radius` of the player (not the host's own), as one "blocks" event. */
	private static void sync(final ServerLevel level, final int radius) {
		if (player == null) {
			return;
		}

		BlockPos c = BlockPos.containing(player);
		BlockPos.MutableBlockPos p = new BlockPos.MutableBlockPos();
		int found = 0;
		for (int x = -radius; x <= radius && found < 6000; x++) {
			for (int z = -radius; z <= radius && found < 6000; z++) {
				for (int y = -radius; y <= radius; y++) {
					p.set(c.getX() + x, c.getY() + y, c.getZ() + z);
					if (!level.isInWorldBounds(p) || !level.isLoaded(p) || hostCells.contains(p)) {
						continue;
					}

					BlockState state = level.getBlockState(p);
					String name = solidName(level, p, state);
					int water = waterLevel(state);
					if (!name.isEmpty() || water > 0) {
						BlockPos at = p.immutable();
						changes.put(at, name);
						waterChanges.put(at, water);
						found++;
					}
				}
			}
		}

		flush();
	}

	/** End of each tick: {"t":"blocks","set":[[x,y,z,"stone"],...],"clear":[x,y,z,...],"water":[[x,y,z,level],...]}. */
	private static void flush() {
		if (changes.isEmpty() && waterChanges.isEmpty()) {
			return;
		}

		StringBuilder set = new StringBuilder(), clear = new StringBuilder(), water = new StringBuilder();
		for (Map.Entry<BlockPos, String> e : changes.entrySet()) {
			BlockPos p = e.getKey();
			if (e.getValue().isEmpty()) {
				clear.append(clear.isEmpty() ? "" : ",").append(p.getX()).append(',').append(p.getY()).append(',').append(p.getZ());
			} else {
				set.append(set.isEmpty() ? "" : ",").append('[').append(p.getX()).append(',').append(p.getY()).append(',').append(p.getZ())
					.append(",\"").append(e.getValue()).append("\"]");
			}
		}

		for (Map.Entry<BlockPos, Integer> e : waterChanges.entrySet()) {
			BlockPos p = e.getKey();
			water.append(water.isEmpty() ? "" : ",").append('[').append(p.getX()).append(',').append(p.getY()).append(',').append(p.getZ())
				.append(',').append(e.getValue()).append(']');
		}

		changes.clear();
		waterChanges.clear();
		emit("{\"t\":\"blocks\",\"set\":[" + set + "],\"clear\":[" + clear + "],\"water\":[" + water + "]}");
	}

	/** From the explosion mixin: every Minecraft explosion (TNT, creepers, ...) except the ones the host set off. */
	public static void onExplosion(final Vec3 center, final float radius, final String source) {
		if (!hostExplosion) {
			emit(String.format(Locale.ROOT, "{\"t\":\"explosion\",\"pos\":[%.3f,%.3f,%.3f],\"r\":%.2f,\"src\":\"%s\"}",
				center.x, center.y, center.z, radius, source));
		}
	}

	private static Iterable<JsonElement> list(final JsonObject m, final String key) {
		return m.has(key) ? m.getAsJsonArray(key) : new JsonArray();
	}

	private static int[] ints(final JsonArray a) {
		int[] out = new int[a.size()];
		for (int i = 0; i < out.length; i++) {
			out[i] = a.get(i).getAsInt();
		}

		return out;
	}

	static boolean validName(final String name) {
		return NAME.matcher(name).matches();
	}
}
