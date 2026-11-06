#include "include/staminaHandler.h"
#include "include/attackHandler.h"

void attackHandler::cacheAttack(RE::Actor* a_actor) {
	if (!enabledFor(a_actor)) {
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
		const auto handle = a_actor->GetHandle();
		std::lock_guard lock(mtx);
		cachedAttackers.insert(handle);

	}
}

void attackHandler::OnLightHit(RE::Actor* a_actor) {
	if (!enabledFor(a_actor)) {
		return;
	}
	
	const auto handle = a_actor->GetHandle();
	{
		std::lock_guard lock(mtx);
		cachedAttackers.erase(handle);
	}
	staminaHandler::staminaLightHit(a_actor);
}

void attackHandler::checkout(RE::Actor* a_actor) {
	if (!a_actor) return;
	const auto handle = a_actor->GetHandle();
	{
		std::lock_guard lock(mtx);
		if (cachedAttackers.erase(handle) == 0) return;
	}
	if (enabledFor(a_actor)) {
		staminaHandler::staminaLightMiss(a_actor);
	}
}
