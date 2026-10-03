package dev.universalmodder.gmodbridge;

import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerEntityEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Minecraft half of the Garry's Mod bridge. Garry's Mod (the host) polls {@link HttpLink} on 127.0.0.1 every tick;
 * {@link Bridge} applies what it sends on the server thread and queues what happened here for the next poll.
 */
public class GmodBridge implements ModInitializer {
	public static final String ID = "gmodbridge";
	public static final Logger LOG = LoggerFactory.getLogger(ID);

	@Override
	public void onInitialize() {
		HttpLink.launch();
		ServerLifecycleEvents.SERVER_STARTED.register(Bridge::attach);
		ServerLifecycleEvents.SERVER_STOPPING.register(server -> {
			// before the world is saved: the host's proxies and its ground/water aren't kept in it
			Mobs.detach(server);
			Bridge.detach(server);
		});
		ServerEntityEvents.ENTITY_LOAD.register(Mobs::onEntityLoad);
		ServerTickEvents.END_SERVER_TICK.register(Bridge::tick);
		LOG.info("gmodbridge loaded");
	}
}
