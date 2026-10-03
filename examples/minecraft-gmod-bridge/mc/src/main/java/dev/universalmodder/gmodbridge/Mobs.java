package dev.universalmodder.gmodbridge;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import dev.universalmodder.gmodbridge.mixin.MobAccessor;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import net.minecraft.core.BlockPos;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.tags.DamageTypeTags;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.effect.MobEffects;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntitySpawnReason;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.EntityTypes;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.attributes.AttributeInstance;
import net.minecraft.world.entity.ai.attributes.Attributes;
import net.minecraft.world.entity.ai.goal.GoalSelector;
import net.minecraft.world.entity.monster.Enemy;
import net.minecraft.world.entity.npc.villager.Villager;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/**
 * Minecraft's mobs vs Garry's Mod's player and NPCs.
 *
 * <ul>
 * <li>Every GMod person the host lists ("peds") gets an invisible, AI-less villager "proxy" here that follows them
 *     and that hostile mobs hunt. A hit on a proxy goes back to the host ({"t":"mobhit"}), which takes it off the real
 *     person's health. The proxy itself never takes damage.</li>
 * <li>Every mob (and lit TNT) near the player is listed in each reply's "mobs"; the host keeps a solid stand-in for
 *     each that its bullets hit and its physgun grabs, and sends the damage ("dmg") and grabs ("hold"/"release")
 *     back.</li>
 * </ul>
 */
public final class Mobs {
	public static final String PROXY_TAG = "gmod_proxy";
	/** Mobs hunt proxies this far away (blocks). */
	private static final double FOLLOW_RANGE = 48.0;
	/** Mobs further than this from the player aren't reported (blocks). */
	private static final double REPORT_RANGE = 64.0;
	/** A grab with no "hold" for this long is let go (the host stopped sending). */
	private static final long HOLD_TIMEOUT_NANOS = 1_000_000_000L;
	/** Brain-driven hostiles pick targets from brain memories, not goals: they'd never hunt proxies. */
	private static final Set<String> BRAIN_MOBS = Set.of("piglin", "piglin_brute", "hoglin", "zoglin", "breeze", "creaking", "warden");

	// server thread only
	private static final Map<Integer, Villager> proxies = new HashMap<>();
	private static final Map<Integer, Integer> handleOf = new HashMap<>();
	private static final Map<Integer, Integer> missingPolls = new HashMap<>();
	/** Entity id -> [nanos of the last "hold", 1 if it had no AI before the grab]. */
	private static final Map<Integer, long[]> held = new HashMap<>();

	private Mobs() {
	}

	/** A host person's stand-in (the client only sees an invisible villager: the hidden effect keeps the flag set). */
	public static boolean isProxy(final Entity e) {
		return e instanceof Villager && (e.isInvisible() || e.entityTags().contains(PROXY_TAG));
	}

	/** Mobs that hunt the host's people: hostile, goal-driven ones. */
	private static boolean fighter(final Entity e) {
		return e instanceof Mob && e instanceof Enemy && !isProxy(e) && !BRAIN_MOBS.contains(kind(e));
	}

	/** Reported to the host: every mob but the proxies, and lit TNT (so the physgun can carry it). */
	private static boolean reported(final Entity e) {
		return e.isAlive() && ((e instanceof Mob && !isProxy(e)) || "tnt".equals(kind(e)));
	}

	static String kind(final Entity e) {
		return BuiltInRegistries.ENTITY_TYPE.getKey(e.getType()).getPath();
	}

	/** New mobs (and mobs loaded again) hunt proxies; stale proxies from an earlier session are removed. */
	static void onEntityLoad(final Entity e, final ServerLevel level) {
		if (e instanceof Villager v && v.entityTags().contains(PROXY_TAG) && !proxies.containsValue(v)) {
			v.discard();
			return;
		}

		if (!fighter(e)) {
			return;
		}

		Mob mob = (Mob) e;
		GoalSelector targets = ((MobAccessor) mob).gmodbridge$targetSelector();
		if (targets.getAvailableGoals().stream().noneMatch(w -> w.getGoal() instanceof ProxyTargetGoal)) {
			targets.addGoal(2, new ProxyTargetGoal(mob));
		}

		AttributeInstance range = mob.getAttribute(Attributes.FOLLOW_RANGE);
		if (range != null && range.getBaseValue() < FOLLOW_RANGE) {
			range.setBaseValue(FOLLOW_RANGE);
		}
	}

	/** The host's people, [[handle,x,y,z], ...] (feet, Minecraft coordinates): proxies follow them. */
	static void peds(final ServerLevel level, final JsonArray list) {
		Set<Integer> seen = new HashSet<>();
		for (JsonElement el : list) {
			JsonArray p = el.getAsJsonArray();
			int handle = p.get(0).getAsInt();
			double x = p.get(1).getAsDouble(), y = p.get(2).getAsDouble(), z = p.get(3).getAsDouble();
			seen.add(handle);
			Villager v = proxies.get(handle);
			if (v == null || v.isRemoved()) {
				v = EntityTypes.VILLAGER.create(level, EntitySpawnReason.COMMAND);
				if (v == null) {
					continue;
				}

				v.setInvisible(true);
				v.addEffect(new MobEffectInstance(MobEffects.INVISIBILITY, MobEffectInstance.INFINITE_DURATION, 0, false, false), null);
				v.setNoAi(true);
				v.setNoGravity(true);
				v.setSilent(true);
				v.addTag(PROXY_TAG);
				v.snapTo(x, y, z, 0.0F, 0.0F);
				proxies.put(handle, v); // before adding: the load event must not take it for a stale one
				if (!level.addFreshEntity(v)) {
					proxies.remove(handle);
					continue;
				}

				handleOf.put(v.getId(), handle);
			} else {
				v.setPos(x, y, z);
			}

			missingPolls.remove(handle);
		}

		// a person missing from a few lists in a row (dead, removed, out of range) loses their proxy
		for (Iterator<Map.Entry<Integer, Villager>> it = proxies.entrySet().iterator(); it.hasNext();) {
			Map.Entry<Integer, Villager> e = it.next();
			if (seen.contains(e.getKey())) {
				continue;
			}

			int missing = missingPolls.merge(e.getKey(), 1, Integer::sum);
			if (missing > 6 || e.getValue().isRemoved()) {
				handleOf.remove(e.getValue().getId());
				e.getValue().discard();
				missingPolls.remove(e.getKey());
				it.remove();
			}
		}
	}

	/**
	 * A proxy was hurt (from LivingEntity.hurtServer; the damage itself is always cancelled). Mob attacks, mob arrows,
	 * fire/lava and drowning go to the host; explosions don't (every Minecraft explosion is already mirrored there).
	 */
	public static void onProxyHit(final LivingEntity proxy, final DamageSource source, final float amount) {
		Integer handle = handleOf.get(proxy.getId());
		Entity attacker = source.getEntity();
		boolean projectile = source.getDirectEntity() instanceof Projectile;
		if (projectile && !(attacker instanceof Player)) {
			source.getDirectEntity().discard(); // a skeleton's arrow ends in the person it hit
		}

		if (handle == null || source.is(DamageTypeTags.IS_EXPLOSION) || attacker instanceof Player) {
			return;
		}

		String kind;
		if (attacker != null) {
			kind = kind(attacker);
		} else if (source.is(DamageTypeTags.IS_FIRE)) {
			kind = "fire";
		} else if (source.is(DamageTypeTags.IS_DROWNING)) {
			kind = "drown";
		} else {
			return;
		}

		Bridge.emit(String.format(Locale.ROOT, "{\"t\":\"mobhit\",\"h\":%d,\"d\":%.2f,\"id\":%d,\"k\":\"%s\",\"proj\":%b,\"from\":[%.3f,%.3f,%.3f]}",
			handle, amount, attacker == null ? -1 : attacker.getId(), kind, projectile,
			attacker == null ? proxy.getX() : attacker.getX(), attacker == null ? proxy.getY() : attacker.getY(), attacker == null ? proxy.getZ() : attacker.getZ()));
	}

	/**
	 * The host hurt a mob (`amount` in Minecraft health points). It comes from the attacker's proxy when there is one,
	 * so knockback pushes the mob away from them and the mob turns on them (or flees, for animals).
	 */
	static void damage(final ServerLevel level, final int id, final float amount, final int attackerHandle, final String kind) {
		if (!(level.getEntity(id) instanceof LivingEntity mob) || !mob.isAlive() || isProxy(mob) || amount <= 0.0F) {
			return;
		}

		Villager from = proxies.get(attackerHandle);
		DamageSource source = from != null && !from.isRemoved() ? level.damageSources().mobAttack(from) : level.damageSources().generic();
		if ("fire".equals(kind)) {
			mob.setRemainingFireTicks(100);
		}

		mob.damageCooldownTime = 0; // automatic fire lands several hits inside the usual 10-tick cooldown
		mob.hurtServer(level, source, Math.min(amount, 1000.0F));
	}

	/** The physgun has this entity: it hangs where the host holds it, with no gravity and no AI of its own. */
	static void hold(final ServerLevel level, final int id, final double x, final double y, final double z) {
		Entity e = level.getEntity(id);
		if (e == null || !e.isAlive() || isProxy(e)) {
			return;
		}

		long[] h = held.get(id);
		if (h == null) {
			h = new long[] {0L, e instanceof Mob mob && mob.isNoAi() ? 1L : 0L};
			held.put(id, h);
			if (e instanceof Mob mob) {
				mob.setNoAi(true);
			}

			e.setNoGravity(true);
		}

		h[0] = System.nanoTime();
		e.setPos(x, y, z);
		e.setDeltaMovement(Vec3.ZERO);
		e.resetFallDistance();
	}

	/** Let go (velocity in blocks/tick): it falls, flies and takes fall damage like any thrown Minecraft mob. */
	static void release(final ServerLevel level, final int id, final Vec3 velocity) {
		long[] h = held.remove(id);
		Entity e = level.getEntity(id);
		if (h == null || e == null) {
			return;
		}

		e.setNoGravity(false);
		if (e instanceof Mob mob && h[1] == 0L) {
			mob.setNoAi(false);
		}

		double max = 4.0; // blocks/tick: a hard throw, not a bullet
		e.setDeltaMovement(new Vec3(clamp(velocity.x, max), clamp(velocity.y, max), clamp(velocity.z, max)));
	}

	private static double clamp(final double v, final double max) {
		return Double.isFinite(v) ? Math.max(-max, Math.min(max, v)) : 0.0;
	}

	/** Spawn a mob at a spot the host picked (e.g. where its player is looking). */
	static void spawn(final ServerLevel level, final String kind, final double x, final double y, final double z) {
		if (!Bridge.validName(kind)) {
			return;
		}

		Optional<EntityType<?>> type = BuiltInRegistries.ENTITY_TYPE.getOptional(Identifier.withDefaultNamespace(kind));
		if (type.isEmpty()) {
			GmodBridge.LOG.warn("spawn: unknown mob {}", kind);
			return;
		}

		Entity e = type.get().spawn(level, BlockPos.containing(x, y, z), EntitySpawnReason.COMMAND);
		if (e instanceof Mob mob) {
			mob.setPersistenceRequired();
		}
	}

	/**
	 * Every tick: let go of stale grabs, and list the mobs near the player for the host:
	 * [[id,"kind",x,y,z,yaw,width,height,health,max health,held 0/1,hostile 0/1], ...].
	 */
	static void tick(final ServerLevel level) {
		long now = System.nanoTime();
		for (Iterator<Map.Entry<Integer, long[]>> it = held.entrySet().iterator(); it.hasNext();) {
			Map.Entry<Integer, long[]> h = it.next();
			if (now - h.getValue()[0] > HOLD_TIMEOUT_NANOS) {
				Entity e = level.getEntity(h.getKey());
				it.remove();
				if (e != null) {
					e.setNoGravity(false);
					if (e instanceof Mob mob && h.getValue()[1] == 0L) {
						mob.setNoAi(false);
					}
				}
			}
		}

		Vec3 c = Bridge.player();
		if (!Bridge.active() || c == null) {
			return;
		}

		double r = REPORT_RANGE;
		List<Entity> near = level.getEntities((Entity) null, new AABB(c.x - r, c.y - r, c.z - r, c.x + r, c.y + r, c.z + r), Mobs::reported);
		StringBuilder b = new StringBuilder(64 + near.size() * 96).append('[');
		int n = 0;
		for (Entity e : near) {
			float hp = e instanceof LivingEntity l ? l.getHealth() : 0.0F;
			float max = e instanceof LivingEntity l ? l.getMaxHealth() : 0.0F;
			b.append(n++ == 0 ? "" : ",").append(String.format(Locale.ROOT, "[%d,\"%s\",%.3f,%.3f,%.3f,%.1f,%.3f,%.3f,%.1f,%.1f,%d,%d]",
				e.getId(), kind(e), e.getX(), e.getY(), e.getZ(), e.getYRot(), e.getBbWidth(), e.getBbHeight(), hp, max,
				held.containsKey(e.getId()) ? 1 : 0, e instanceof Enemy ? 1 : 0));
		}

		Bridge.mobs(b.append(']').toString());
	}

	static void clearMobs(final ServerLevel level) {
		List<Entity> all = new ArrayList<>();
		level.getAllEntities().forEach(all::add);
		all.stream().filter(e -> e instanceof Mob && !isProxy(e)).forEach(Entity::discard);
		held.clear();
	}

	/** The host stopped polling: nobody left to hunt, nothing held. */
	static void hostGone(final ServerLevel level) {
		for (Integer id : new ArrayList<>(held.keySet())) {
			release(level, id, Vec3.ZERO);
		}

		clearProxies();
		Bridge.mobs("[]");
	}

	private static void clearProxies() {
		proxies.values().forEach(Entity::discard);
		proxies.clear();
		handleOf.clear();
		missingPolls.clear();
	}

	static void detach(final net.minecraft.server.MinecraftServer s) {
		hostGone(s.overworld());
		held.clear();
	}
}
