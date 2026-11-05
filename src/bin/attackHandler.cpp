#include "include/staminaHandler.h"
#include "include/attackHandler.h"

void attackHandler::cacheAttack(RE::Actor* a_actor) {
	if (!a_actor->IsPlayerRef() && !settings::bAttackStaminaToggle_NPC) {
		return;
	}
	checkout(a_actor);
	//TODO:fix bash implementation
	//DEBUG("registering attack for {}", actor->GetName());
	if (a_actor->GetActorRuntimeData().currentProcess && a_actor->GetActorRuntimeData().currentProcess->high) {
		auto attckData = a_actor->GetActorRuntimeData().currentProcess->high->attackData;
		if (!attckData) {
			return;
		}
		if (attckData->data.flags.any(RE::AttackData::AttackFlag::kBashAttack)) {
			return;
		}
		if (attckData->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack)) {
			return;
		}
		else {
			//DEBUG("registered light attack");
			mtx.lock();
				cachedAttackers.insert(a_actor->GetHandle());
			mtx.unlock();
			//DEBUG("attack heap size: {}", attackerHeap.size());
		}

	}
}

void attackHandler::OnLightHit(RE::Actor* a_actor) {
	if (!a_actor->IsPlayerRef() && !settings::bAttackStaminaToggle_NPC) {
		return;
	}
	
	staminaHandler::staminaLightHit(a_actor);

	mtx.lock();
	cachedAttackers.erase(a_actor->GetHandle());
	mtx.unlock();
}


void attackHandler::checkout(RE::Actor* a_actor) {
	if (!a_actor) return;
	{ std::lock_guard lock(mtx); if (!cachedAttackers.erase(a_actor->GetHandle())) return; }
	staminaHandler::staminaLightMiss(a_actor);
}
