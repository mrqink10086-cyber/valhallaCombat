#pragma once
#include "staminaHandler.h"
#include <mutex>
#include <unordered_set>
class attackHandler
{
private:
	std::mutex mtx;
public:
	static attackHandler* GetSingleton()
	{
		static attackHandler singleton;
		return  std::addressof(singleton);
	}
	static bool enabledFor(RE::Actor* actor) { return actor && (actor->IsPlayerRef() ? settings::bAttackStaminaToggle : settings::bAttackStaminaToggle_NPC); }

	RE::Actor* actorToRegenStamina;

	std::unordered_set<RE::ActorHandle> cachedAttackers;
	void reset() { std::lock_guard lock(mtx); cachedAttackers.clear(); actorToRegenStamina = nullptr; }
	// Settle the previous light miss before registering a non-bash, non-power swing.
	void cacheAttack(RE::Actor* actor);

	// Reward each light hit and cancel a cached light swing's miss cost.
	void OnLightHit(RE::Actor* actor);
	// Charge an outstanding miss on attackStop or before registering the next swing.
	void checkout(RE::Actor* actor);

};

