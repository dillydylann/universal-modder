package dev.universalmodder.gmodbridge.mixin;

import dev.universalmodder.gmodbridge.Mobs;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.LivingEntity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Host people's proxies: hits on them go to the host instead of doing damage; they neither push nor get pushed
 * (they follow their person every tick); and the Minecraft window's crosshair ignores them.
 */
@Mixin(LivingEntity.class)
abstract class ProxyMixin {
	@Inject(method = "hurtServer(Lnet/minecraft/server/level/ServerLevel;Lnet/minecraft/world/damagesource/DamageSource;F)Z", at = @At("HEAD"), cancellable = true)
	private void gmodbridge$proxyHurt(final ServerLevel level, final DamageSource source, final float amount, final CallbackInfoReturnable<Boolean> cir) {
		LivingEntity self = (LivingEntity) (Object) this;
		if (Mobs.isProxy(self)) {
			Mobs.onProxyHit(self, source, amount);
			cir.setReturnValue(false);
		}
	}

	@Inject(method = "isPushable()Z", at = @At("HEAD"), cancellable = true)
	private void gmodbridge$proxyNotPushable(final CallbackInfoReturnable<Boolean> cir) {
		if (Mobs.isProxy((LivingEntity) (Object) this)) {
			cir.setReturnValue(false);
		}
	}

	@Inject(method = "pushEntities()V", at = @At("HEAD"), cancellable = true)
	private void gmodbridge$proxyNoPush(final CallbackInfo ci) {
		if (Mobs.isProxy((LivingEntity) (Object) this)) {
			ci.cancel();
		}
	}

	@Inject(method = "isPickable()Z", at = @At("HEAD"), cancellable = true)
	private void gmodbridge$proxyNotPickable(final CallbackInfoReturnable<Boolean> cir) {
		LivingEntity self = (LivingEntity) (Object) this;
		// client only: on the server this is also what lets projectiles hit, and mobs' arrows should hit proxies
		if (self.level().isClientSide() && Mobs.isProxy(self)) {
			cir.setReturnValue(false);
		}
	}
}
