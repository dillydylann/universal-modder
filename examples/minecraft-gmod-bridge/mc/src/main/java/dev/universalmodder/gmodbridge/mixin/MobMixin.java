package dev.universalmodder.gmodbridge.mixin;

import dev.universalmodder.gmodbridge.Bridge;
import net.minecraft.world.entity.Mob;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** The bridge world is always noon (the host has its own lighting): undead don't burn in it while a host is attached. */
@Mixin(Mob.class)
abstract class MobMixin {
	@Inject(method = "burnUndead()V", at = @At("HEAD"), cancellable = true)
	private void gmodbridge$noSunburn(final CallbackInfo ci) {
		if (Bridge.active()) {
			ci.cancel();
		}
	}
}
