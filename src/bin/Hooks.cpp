#include "include/Hooks.h"
#include "include/Utils.h"
#include "include/blockHandler.h"
#include "ValhallaCombat.hpp"
#include "include/stunHandler.h"
#include "include/hitProcessor.h"
#include "include/AI.h"
#include "include/settings.h"
#include "include/attackHandler.h"
namespace Hooks
{
#pragma region GetHeavyStaminaCost
	float Hook_OnGetAttackStaminaCost::getAttackStaminaCost(RE::ActorValueOwner* avOwner, RE::BGSAttackData* atkData)
	{
		RE::Actor* a_actor = Utils::AvOwner::asActor(avOwner);
			if (!CombatRuntime::get().ready() || !a_actor) return _getAttackStaminaCost(avOwner,atkData);
		if (!settings::bNonCombatStaminaCost && !a_actor->IsInCombat()) {
			return 0;
		}
		return _getAttackStaminaCost(avOwner, atkData);
	}
#pragma endregion

#pragma region StaminaRegen
	/*function generating conditions for stamina regen. Iff returned value is true, no regen.
used to block stamina regen in certain situations.*/
	bool Hook_OnCheckStaminaRegenCondition::HasFlags1(RE::ActorState* a_this, uint16_t a_flags)
	{
		//if bResult is true, prevents regen.
		bool bResult = _HasFlags1(a_this, a_flags);  // is sprinting?

		if (!bResult) {
			RE::Actor* actor = SKSE::stl::adjust_pointer<RE::Actor>(a_this, -0xB8); //apparently the offset is different for the newest AE build; TODO:find the offset. currently using restoreactorvalue hook instead to block regen.
			auto attackState = actor->AsActorState()->GetAttackState();
			if (actor != attackHandler::GetSingleton()->actorToRegenStamina) {
				//if melee hit regen is needed, no need to disable regen.
				bResult = (attackState > RE::ATTACK_STATE_ENUM::kNone && attackState <= RE::ATTACK_STATE_ENUM::kBowFollowThrough);  //don't regen stamina if attacking
			}
		}
		return bResult;
	}

	void Hook_OnRestoreActorValue::RestoreActorValue(RE::Actor* a_actor, RE::ActorValue a_actorValue, float a_val)
	{
			if (!CombatRuntime::get().ready() || !a_actor) { _RestoreActorValue(a_actor,a_actorValue,a_val); return; }
		switch (a_actorValue) {
		case RE::ActorValue::kStamina:
			if (a_actor->IsBlocking()) {
				a_val *= settings::fBlockingStaminaRegenMult;
			} else if (a_actor != attackHandler::GetSingleton()->actorToRegenStamina) {
				RE::ATTACK_STATE_ENUM atkState = a_actor->AsActorState()->GetAttackState();
				if (atkState > RE::ATTACK_STATE_ENUM::kNone && atkState <= RE::ATTACK_STATE_ENUM::kBowFollowThrough) {
					return;  //don't regen stamina if attacking
				}
			}
			break;
		case RE::ActorValue::kHealth:
			stunHandler::GetSingleton()->modStun(a_actor, a_val);//stun regen on health regen
		}

		_RestoreActorValue(a_actor, a_actorValue, a_val);
	}
#pragma endregion

//#pragma region getStaggerMagnitude_Weapon
//	float Hook_OnGetStaggerMagnitude::getStaggerMagnitude_Weapon(RE::ActorValueOwner* a1, RE::ActorValueOwner* a2, RE::TESObjectWEAP* a3, float a4)
//	{
//		if (settings::bBalanceToggle) {
//			return 0;
//		}
//		return _getStaggerMagnitude_Weapon(a1, a2, a3, a4);
//	}
//
//	float Hook_OnGetStaggerMagnitude::getStaggerManitude_Bash(uintptr_t a1, uintptr_t a2)
//	{
//		if (settings::bBalanceToggle) {
//			return 0;
//		}
//		return _getStaggerManitude_Bash(a1, a2);
//
//	}

#pragma endregion
#pragma region MeleeHit
	void Hook_OnMeleeHit::processHit(RE::Actor* victim, RE::HitData& hitData)
	{
		using HITFLAG = RE::HitData::Flag;
			auto aggressor = hitData.aggressor.get();
			if (!CombatRuntime::get().ready() || !victim || !aggressor || Utils::Actor::isDead(victim)) {
			_ProcessHit(victim, hitData);
			return;
		}
			hitProcessor::GetSingleton()->processHit(aggressor.get(), victim, hitData);
		_ProcessHit(victim, hitData);
	}

#pragma endregion

//#pragma region MainUpdate
//	void Hook_MainUpdate::Update(RE::Main* a_this, float a2)
//	{
//		ValhallaCombat::GetSingleton()->update();
//		_Update(a_this, a2);
//	}
//#pragma endregion

#pragma region projectileHit
	/*Decide whether the collision is a actor-projectile collision. If it is, initialize a deflection attempt by the actor.
@param a_projectile: the projectile to be deflected.
@param a_AllCdPointCollector: pointer to the container storing all collision points.
@return whether a successful deflection is performed by the actor.*/
	inline bool IsProjectileSafeToInspect(RE::Projectile* a_projectile)
	{
		if (!a_projectile) {
			return false;
		}
		const auto& rt = a_projectile->GetProjectileRuntimeData();
		if (rt.flags.any(RE::Projectile::Flags::kDestroyed) || rt.flags.any(RE::Projectile::Flags::kFading)) {
			return false;
		}
		if (!a_projectile->Is3DLoaded()) {
			return false;
		}
		return true;
	}
	inline bool shouldIgnoreHit(RE::Projectile* a_projectile, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
	{
			if (!CombatRuntime::get().ready() || !a_projectile || !a_AllCdPointCollector) {
			return false;
		}
		if (!IsProjectileSafeToInspect(a_projectile)) {
			return false;
		}
		for (auto& hit : a_AllCdPointCollector->hits) {
			if (!hit.rootCollidableA || !hit.rootCollidableB) {
				continue;
			}
			auto refrA = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableA);
			auto refrB = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableB);
			if (refrA && refrA->formType == RE::FormType::ActorCharacter) {
				if (auto* actorA = refrA->As<RE::Actor>(); actorA && actorA->Is3DLoaded() && !Utils::Actor::isDead(actorA)) {
						if (blockHandler::GetSingleton()->processProjectileBlock(actorA, a_projectile, const_cast<RE::hkpCollidable*>(hit.rootCollidableB))) return true;
				}
			}
			if (refrB && refrB->formType == RE::FormType::ActorCharacter) {
				if (auto* actorB = refrB->As<RE::Actor>(); actorB && actorB->Is3DLoaded() && !Utils::Actor::isDead(actorB)) {
						if (blockHandler::GetSingleton()->processProjectileBlock(actorB, a_projectile, const_cast<RE::hkpCollidable*>(hit.rootCollidableA))) return true;
				}
			}
		}
		return false;
	}
		bool Hook_OnProjectileCollision::OnArrowCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
	{
		//DEBUG("hooked arrow collission vfunc");
		if (!a_this || !a_AllCdPointCollector) {
				return false;
		}
		if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
				return true;
		};
			return _arrowCollission(a_this, a_AllCdPointCollector);
	};

		bool Hook_OnProjectileCollision::OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
	{
		//DEBUG("hooked missile collission vfunc");
		if (!a_this || !a_AllCdPointCollector) {
				return false;
		}
		if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
				return true;
		};
			return _missileCollission(a_this, a_AllCdPointCollector);
	}
#pragma endregion

	void Hook_OnMeleeCollision::processHit(RE::Actor* a_aggressor, RE::Actor* a_victim, std::int64_t a_int1, bool a_bool, void* a_unkptr)
	{
			if (!CombatRuntime::get().ready() || !a_aggressor || !a_victim) { _ProcessHit(a_aggressor,a_victim,a_int1,a_bool,a_unkptr); return; }
		if (settings::bTimedBlockToggle && blockHandler::GetSingleton()->processMeleeTimedBlock(a_victim, a_aggressor, blockHandler::GetSingleton()->getIsForcedTimedBlocking(a_victim), blockHandler::GetSingleton()->getIsForcedPerfectBlocking(a_victim))) {
			return;
		}
		if (settings::bTackleToggle && blockHandler::GetSingleton()->processMeleeTackle(a_victim, a_aggressor)) {
			return;
		}
		_ProcessHit(a_aggressor, a_victim, a_int1, a_bool, a_unkptr);
	}

	/*Check if the attack action should be performed depending on the actor's debuff state.*/
	bool Hook_OnAttackAction::PerformAttackAction(RE::TESActionData* a_actionData)
	{
			if (!CombatRuntime::get().ready() || !a_actionData || !settings::bStaminaDebuffToggle) {
			return _PerformAttackAction(a_actionData);
		}
		auto ref = a_actionData->source.get();
		if (!ref) {
			return _PerformAttackAction(a_actionData);
		}
		RE::Actor* actor = ref->As<RE::Actor>();
		if (!actor) {
			return _PerformAttackAction(a_actionData);
		}
		if (debuffHandler::GetSingleton()->isInDebuff(actor)) {
			return false;
		}

		return _PerformAttackAction(a_actionData);
	}


	void Hook_AttackBlockHandler_OnProcessButton::ProcessButton(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
	{
		if (!CombatRuntime::get().ready() || !a_event) { _ProcessButton(a_this, a_event, a_data); return; }
		if (a_event->QUserEvent() == "Left Attack/Block") {
			blockHandler* blockHandler = blockHandler::GetSingleton();
			if (a_event->IsDown()) {
				blockHandler->onBlockKeyDown();
				if (settings::bTackleToggle) {
					blockHandler->onTackleKeyDown();
				}
			} else if (a_event->IsUp()) {
				blockHandler->onBlockKeyUp();
				if (settings::bBlockCommitmentToggle) {/* Block commitment; Immediately releasing the block key after pressing it will not cause the actor to immediately unblock.*/
					float delay_time = settings::fBlockCommitmentTime - a_event->HeldDuration();
					//blockCommitment::GetSingleton()->queueUnblock(delay_time);
					auto pc = RE::PlayerCharacter::GetSingleton();
					if (pc && pc->IsBlocking() && a_event->HeldDuration() < settings::fBlockCommitmentTime) { //do not process this request until later
						const auto generation = blockHandler->keyGeneration();
						const auto device = a_event->GetDevice();
						const auto code = a_event->GetIDCode();
						const auto duration = a_event->HeldDuration();
						CombatRuntime::get().schedule(delay_time, [generation, device, code, duration] {
							auto handler = blockHandler::GetSingleton();
							auto player = RE::PlayerCharacter::GetSingleton();
							auto controls = RE::PlayerControls::GetSingleton();
							if (!player || !controls || handler->isBlockKeyHeld() || handler->keyGeneration() != generation) return;
							auto event = RE::ButtonEvent::Create(device, "forceRelease", code, 0, duration);
							if (event) {
								_ProcessButton(controls->attackBlockHandler, event, &controls->data);
								delete event;
							}
							player->NotifyAnimationGraph("blockStop");
						});
						return;
					}
				}
			}
		}
		_ProcessButton(a_this, a_event, a_data);
	}

	std::int32_t& Hook_GetWantBlock::GetWantBlock(void* unk_ptr, const RE::BSFixedString& a_channelName, std::uint8_t unk_int, RE::Actor* a_actor, std::int32_t& a_result)
	{
		// // O: insert return statement here
		_GetWantBlock(unk_ptr, a_channelName, unk_int, a_actor, a_result);

		if (!a_actor->IsPlayerRef()) {
			if (debuffHandler::GetSingleton()->isInDebuff(a_actor)) {
				a_result = 1;
				if (a_actor->IsBlocking()) {
					a_actor->NotifyAnimationGraph("blockStop");
				}
			}
		}
	
		return a_result;
	}

}
