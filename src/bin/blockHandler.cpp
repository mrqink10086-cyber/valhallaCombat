#include "include/blockHandler.h"
#include "include/OwnedEffects.h"
#include "include/CombatTrace.h"
#include "include/data.h"
#include "include/settings.h"
#include "include/Utils.h"
#include "include/stunHandler.h"
#include "include/balanceHandler.h"
#include "include/reactionHandler.h"
#include "include/AI.h"
#include "include/lib/BlockSpark.h"
#include "include/offsets.h"
#include "include/staminaHandler.h"
#include "ValhallaCombat.hpp"


using HITFLAG = RE::HitData::Flag;
void blockHandler::init()
{
	this->pcPerfectBlockCountdown = settings::uPerfectBlockAccum;
}
void blockHandler::reset()
{
	pendingProjectiles.clear();
	++blockGeneration;
	isBlockButtonPressed = isPcTimedBlocking = isPcBlockingCoolDown = isPcTimedBlockSuccess = false;
	isPcTackling = isPcTackleCooldown = isPcHot = false;
	pcTimedBlockTimer = pcTimedBlockCooldownTimer = pcTackleTimer = pcTackleCooldownTimer = 0;
	pcHotTimer = 3;
	pcBlockWindowPenalty = none;
	init();
	ValhallaCombat::GetSingleton()->deactivateUpdate(ValhallaCombat::HANDLER::blockHandler);
}
/*Called every frame.
Decrement the timer for actors either perfect blocking or cooling down.*/
void blockHandler::update() {
	const float delta = std::max(0.0f, *RE::Offset::g_deltaTime);
	float blockCooldownDelta = delta;
	float tackleCooldownDelta = delta;
	if (isPcTimedBlocking) {
		pcTimedBlockTimer -= delta;
		if (pcTimedBlockTimer <= 0) {
			blockCooldownDelta = -pcTimedBlockTimer;
			onPcTimedBlockEnd();
		}
	}
	if (isPcBlockingCoolDown) {
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (pc && !pc->IsBlocking()) {
			pcTimedBlockCooldownTimer -= blockCooldownDelta;
			while (pcTimedBlockCooldownTimer <= 0 && pcBlockWindowPenalty != none) {
				pcBlockWindowPenalty_update(pcBlockWindowPenalty, false);
				pcTimedBlockCooldownTimer += settings::fTimedBlockCooldownTime;
			}
			if (pcBlockWindowPenalty == none) isPcBlockingCoolDown = false;
		}
	}
	if (isPcHot) {
		pcHotTimer -= delta;
		if (pcHotTimer <= 0) {
			isPcHot = false;
			pcHotTimer = 3.f;
			pcPerfectBlockCountdown = settings::uPerfectBlockAccum;
		}
	}


	if (isPcTackling) {
		pcTackleTimer -= delta;
		if (pcTackleTimer <= 0) {
			tackleCooldownDelta = -pcTackleTimer;
			isPcTackling = false;
			isPcTackleCooldown = true;
			pcTackleCooldownTimer = settings::fTackleCooldownTime;
		}
	}

	if (isPcTackleCooldown) {
		pcTackleCooldownTimer -= tackleCooldownDelta;
		if (pcTackleCooldownTimer <= 0) {
			isPcTackleCooldown = false;
		}
	}
	if (!isPcTimedBlocking && !isPcBlockingCoolDown && !isPcHot && !isPcTackling && !isPcTackleCooldown)
		ValhallaCombat::GetSingleton()->deactivateUpdate(ValhallaCombat::HANDLER::blockHandler);
}
void blockHandler::onPcTimedBlockEnd() {
	if (!isPcTimedBlockSuccess) {//apply penalty only if the current block is not a successful timed block.
		isPcBlockingCoolDown = true; //not a successful timed block, start penalty cool down.
		pcTimedBlockCooldownTimer = settings::fTimedBlockCooldownTime;
		//increment penalty.
		pcBlockWindowPenalty_update(pcBlockWindowPenalty, true);
		ValhallaCombat::GetSingleton()->activateUpdate(ValhallaCombat::HANDLER::blockHandler);

	}
	else {//previous timed block is successful
		isPcTimedBlockSuccess = false; //reset success state.
	}
	isPcTimedBlocking = false;
}

inline void blockHandler::pcBlockWindowPenalty_update(blockWindowPenaltyLevel& a_penaltyLevel, bool increase)
{
	if (increase) {
		switch (a_penaltyLevel) {
		case blockWindowPenaltyLevel::none:
			a_penaltyLevel = light;
			break;
		case blockWindowPenaltyLevel::light:
			a_penaltyLevel = medium;
			break;
		case blockWindowPenaltyLevel::medium:
			a_penaltyLevel = heavy;
			break;
		}
	} else {
		switch (a_penaltyLevel) {
		case blockWindowPenaltyLevel::light:
			a_penaltyLevel = none;
			break;
		case blockWindowPenaltyLevel::medium:
			a_penaltyLevel = light;
			break;
		case blockWindowPenaltyLevel::heavy:
			a_penaltyLevel = medium;
			break;
		}
	}
}

inline bool blockHandler::isTimedBlockElapsedTimeLessThan(float a_in)
{
	return settings::fTimedBlockWindow - pcTimedBlockTimer <= a_in;
}

void blockHandler::onBlockKeyDown() {
		if (!isBlockButtonPressed) ++blockGeneration;
		isBlockButtonPressed = true;
	if (!settings::bTimedBlockToggle && !settings::bTimedBlockProjectileToggle) {
		return;
	}
	RE::PlayerCharacter* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !Utils::Actor::canBlock(pc)) {
		return;
	}
	if (isPcTimedBlocking) {
		// onBlockKeyDown is reachable from two independent paths: the AttackBlockHandler::
		// ProcessButton hook (automatic block detection) and the Alt Block Key input handler.
		// When both fire for the same block, the second call used to run onPcTimedBlockEnd()
		// below and cancel the timed-block window that had just been opened - which is why
		// binding the Alt Block Key "broke" perfect block for keyboard/mouse users. If a window
		// is still open, treat the second call as a duplicate and keep the existing window.
		if (pcTimedBlockTimer > 0.f) {
			return;
		}
		onPcTimedBlockEnd();
	}

	isPcTimedBlocking = true;
	isBlockButtonPressed = true;
	isPcTimedBlockSuccess = false; //reset success state
	float blockWindow = settings::fTimedBlockWindow;
	
	switch (pcBlockWindowPenalty) {
	case blockWindowPenaltyLevel::none: break;
	case blockWindowPenaltyLevel::light: blockWindow *= 0.8f; break;
	case blockWindowPenaltyLevel::medium: blockWindow *= 0.5; break;
	case blockWindowPenaltyLevel::heavy: blockWindow *= 0.3f; break;
	}
	pcTimedBlockTimer = blockWindow;
	ValhallaCombat::GetSingleton()->activateUpdate(ValhallaCombat::HANDLER::blockHandler);
}

void blockHandler::onTackleKeyDown() {
	if (!settings::bTackleToggle) {
		return;
	}
	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !Utils::Actor::isPowerAttacking(pc)) {
		return;
	}
	if (isPcTackling || isPcTackleCooldown) {
		return;
	}
	isPcTackling = true;
	pcTackleTimer = settings::fTackleWindow;
	ValhallaUtils::playSound(pc, data::soundTackleStart);
	ValhallaCombat::GetSingleton()->activateUpdate(ValhallaCombat::HANDLER::blockHandler);
}

void blockHandler::onBlockKeyUp()
{
	isBlockButtonPressed = false;
}

bool blockHandler::isBlockKeyHeld()
{
	return isBlockButtonPressed;
}


void blockHandler::onBlockStop() {
	onPcTimedBlockEnd();
}

void blockHandler::OnPcSuccessfulTimedBlock() {
	isPcTimedBlockSuccess = true;
	pcBlockWindowPenalty = blockWindowPenaltyLevel::none;
}

bool blockHandler::isInBlockAngle(RE::Actor* blocker, RE::TESObjectREFR* a_obj) 
{
	auto angle = blocker->GetHeadingAngle(a_obj->GetPosition(), false);
	return (angle <= data::fCombatHitConeAngle && angle >= -data::fCombatHitConeAngle);
}

bool blockHandler::tryParryProjectile(RE::Actor* a_blocker, RE::Projectile* a_projectile, RE::hkpCollidable*, float a_cost, bool a_forced = false)
{
	if (a_forced || (a_blocker->IsPlayerRef() && getIsPcTimedBlocking())) {

		if (a_blocker->IsPlayerRef()) {
			OnPcSuccessfulTimedBlock();
		}

		
		const bool deflect = Utils::Actor::tryDamageAv(a_blocker, RE::ActorValue::kMagicka, a_cost);
		return queueProjectileBlock(a_blocker, a_projectile, deflect, settings::fTimedBlockProjectileExp);
	}
	return false;
}


bool blockHandler::tryBlockProjectile(RE::Actor* a_blocker, RE::Projectile* a_projectile, float a_cost) 
{
	if (Utils::Actor::tryDamageAv(a_blocker, RE::ActorValue::kMagicka, a_cost)) {
		return queueProjectileBlock(a_blocker, a_projectile, false, settings::fBlockProjectileExp);
	}
	return false;
}


bool blockHandler::processProjectileBlock(RE::Actor* a_blocker, RE::Projectile* a_projectile, RE::hkpCollidable* a_projectile_collidable) 
{
	if (!a_blocker || !a_projectile || !CombatRuntime::get().ready()) return false;
	const auto projectileHandle=a_projectile->GetHandle();
	if (pendingProjectiles.contains(projectileHandle)) return true;
	auto accepted=[this,projectileHandle] {
		pendingProjectiles.insert(projectileHandle);
		return true;
	};
	bool forced = getIsForcedTimedBlocking(a_blocker) || getIsForcedPerfectBlocking(a_blocker);

	if (!a_blocker->IsPlayerRef() && !forced) {
		return false;
	}
	if ((isInBlockAngle(a_blocker, a_projectile) && a_blocker->IsBlocking())
		|| forced
		) {
		// evaluate cost
			float cost = 0;
		if (forced) { // forced projectile block, no cost
			cost = 0;
		} else if (a_projectile->GetProjectileRuntimeData().spell) {  // parry spell
			cost = a_projectile->GetProjectileRuntimeData().spell->CalculateMagickaCost(a_blocker);
		} else { // parry arrow
			auto launcher = a_projectile->GetProjectileRuntimeData().weaponSource;
			auto ammo = a_projectile->GetProjectileRuntimeData().ammoSource;
			if (launcher) {
				cost += launcher->GetAttackDamage();
			}
			if (ammo) {
				cost += ammo->data.damage;
			}
		}
		
		if (!std::isfinite(cost) || cost<0) return false;
		//try timed block
		if (settings::bTimedBlockProjectileToggle) {
			if (tryParryProjectile(a_blocker, a_projectile, a_projectile_collidable, cost, forced)) {
				return accepted();
			}
		}  
			
		//try regular block(that depletes magicka)
		if (settings::bBlockProjectileToggle) {
			if (tryBlockProjectile(a_blocker, a_projectile, cost)) return accepted();
		}
	}
		
	return false;
}

bool blockHandler::queueProjectileBlock(RE::Actor* actor, RE::Projectile* projectile, bool deflect, float experience)
{
	// Havok still owns the collision collector on this stack; mutate projectiles after returning to the owner update.
	const auto projectileHandle = projectile->GetHandle();
	return CombatRuntime::get().schedule(0,[this,projectileHandle,actorHandle=actor->GetHandle(),deflect,experience,generation=blockGeneration] {
#if VC_COMBAT_TRACE
		VC_TRACE_SCOPE(CombatTrace::EventKind::ProjectileApply, 0, CombatRuntime::get().session());
#endif
		if (generation != blockGeneration) return;
		struct Cleanup {
			blockHandler& owner; RE::ObjectRefHandle handle; std::uint64_t generation;
			~Cleanup() { if (generation == owner.blockGeneration) owner.pendingProjectiles.erase(handle); }
		} cleanup{*this,projectileHandle,generation};
		auto projectileRef = projectileHandle.get();
		auto actorRef = actorHandle.get();
		auto* target = projectileRef ? projectileRef->AsProjectile() : nullptr;
		if (target) {
			const auto& rt = target->GetProjectileRuntimeData();
			if (!rt.flags.any(RE::Projectile::Flags::kDestroyed) && !rt.flags.any(RE::Projectile::Flags::kFading)) {
				if (!deflect) RE::Offset::destroyProjectile(target);
				else if (actorRef && target->Is3DLoaded()) {
					auto shooter = rt.shooter.get();
					ValhallaUtils::resetProjectileOwner(target,actorRef.get());
					if (shooter && shooter->Is3DLoaded()) ValhallaUtils::RetargetProjectile(target,shooter.get());
					else ValhallaUtils::ReflectProjectile(target);
				}
			}
		}
		if (generation != blockGeneration || !actorRef || !actorRef->Is3DLoaded() || Utils::Actor::isDead(actorRef.get())) return;
		if (actorRef->IsPlayerRef()) if (auto player=RE::PlayerCharacter::GetSingleton()) player->AddSkillExperience(RE::ActorValue::kBlock,experience);
		if (actorRef->IsBlocking()) actorRef->NotifyAnimationGraph("BlockHitStart");
		playBlockEffects(actorRef.get(),nullptr,blockType::timed);
	}) != 0;
}

#pragma region Process Block
void blockHandler::processPhysicalBlock(RE::Actor* blocker, RE::Actor* aggressor, REX::EnumSet<RE::HitData::Flag, std::uint32_t> a_hitFlag, RE::HitData& hitData)
{
	if (settings::bBlockStaminaToggle) {
		processStaminaBlock(blocker, aggressor, a_hitFlag, hitData);
	}
}


/// <returns>The stamina cost multiplier for a stamina block, based on the plugin settings.</returns>
inline float getBlockStaminaCostMult(RE::Actor* blocker, RE::Actor* aggressor, REX::EnumSet<RE::HitData::Flag, std::uint32_t> a_hitFlag)
{

	if (a_hitFlag.any(HITFLAG::kBlockWithWeapon)) {
		//DEBUG("hit blocked with weapon");
		if (blocker->IsPlayerRef()) {
			return settings::fBckWpnStaminaMult_PC_Block_NPC;
		}
		else {
			if (aggressor->IsPlayerRef()) {
				return settings::fBckWpnStaminaMult_NPC_Block_PC;
			}
			else {
				return settings::fBckWpnStaminaMult_NPC_Block_NPC;
			}
		}
	}
	else {
		//DEBUG("hit blocked with shield");
		if (blocker->IsPlayerRef()) {
			return settings::fBckShdStaminaMult_PC_Block_NPC;
		}
		else {
			if (aggressor->IsPlayerRef()) {
				return settings::fBckShdStaminaMult_NPC_Block_PC;
			}
			else {
				return settings::fBckShdStaminaMult_NPC_Block_NPC;
			}
		}
	}
}

void blockHandler::processStaminaBlock(RE::Actor* a_blocker, RE::Actor* a_aggressor, REX::EnumSet<RE::HitData::Flag, std::uint32_t> a_hitFlag, RE::HitData& a_hitData)
{
	using HITFLAG = RE::HitData::Flag;	
	float staminaDamage = a_hitData.totalDamage;

	inlineUtils::offsetRealDamage(staminaDamage, a_aggressor, a_blocker);
	float staminaDamageMult = getBlockStaminaCostMult(a_blocker, a_aggressor, a_hitFlag);
	staminaDamage *= staminaDamageMult;
	float targetStamina = a_blocker->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina);

	//check whether there's enough stamina to block incoming attack
	if (targetStamina < staminaDamage) {
		if (settings::bGuardBreak) {
			if (a_hitFlag.any(HITFLAG::kPowerAttack)) {
				reactionHandler::triggerStagger(a_aggressor, a_blocker, reactionHandler::kLarge);
			}
			else {
				reactionHandler::triggerStagger(a_aggressor, a_blocker, reactionHandler::kMedium);
			}
		}
			a_hitData.totalDamage *= staminaDamage > 0 ? 1.0f - std::clamp(targetStamina / staminaDamage, 0.0f, 1.0f) : 0.0f;
		
		Utils::Actor::damageav(a_blocker, RE::ActorValue::kStamina,targetStamina);
		debuffHandler::GetSingleton()->initStaminaDebuff(a_blocker); //initialize debuff for the failed blocking attempt
	}
	else {
		a_hitData.totalDamage = 0;
		Utils::Actor::damageav(a_blocker, RE::ActorValue::kStamina,
			staminaDamage);
	}
		staminaHandler::checkStamina(a_blocker);
}
bool blockHandler::getIsPcTimedBlocking() {
	return isPcTimedBlocking;
}

bool blockHandler::getIsPcPerfectBlocking() {
	//bool ret = isTimedBlockElapsedTimeLessThan(settings::uPerfectBlockAccum);
	//return ret;
	return isPcHot && pcPerfectBlockCountdown == 0;
}

void blockHandler::resetPcPerfectBlockCountdown() {
	pcPerfectBlockCountdown = settings::uPerfectBlockAccum;
}

bool blockHandler::getIsForcedTimedBlocking(RE::Actor* a_actor)
{
	bool ret = false;
	return (Utils::Actor::getGraphVariable(ret, a_actor, gv_bool_force_timed_blocking) && ret);
}

bool blockHandler::getIsForcedPerfectBlocking(RE::Actor* a_actor)
{
	bool ret = false;
	return (Utils::Actor::getGraphVariable(ret, a_actor, gv_bool_force_perfect_blocking) && ret);
}


bool blockHandler::processMeleeTimedBlock(RE::Actor* a_blocker, RE::Actor* a_attacker, bool a_forced, bool a_forcedPerfect) {
#if VC_COMBAT_TRACE
	VC_TRACE_SCOPE(CombatTrace::EventKind::TimedBlock, 0, CombatRuntime::get().session());
#endif
	if (!isInBlockAngle(a_blocker, a_attacker)) return false;

	if (!a_forced && !a_forcedPerfect) { // perform checks if it's not forced
		if (!a_blocker->IsPlayerRef()) return false;
		if (!getIsPcTimedBlocking()) return false;
		if (!Utils::Actor::canBlock(a_blocker)) return false;
	}

	if (ValorCompatibility::get_perilous_state(a_attacker) > ValorCompatibility::PERILOUS_TYPE::yellow) {
		return false;
	}

	bool isPerfectblock = a_forcedPerfect || (a_blocker->IsPlayerRef() && this->getIsPcPerfectBlocking());

	if (a_blocker->IsPlayerRef()) {
		if (isPerfectblock) {
			resetPcPerfectBlockCountdown();
		}
		else {
			if (pcPerfectBlockCountdown > 0) --pcPerfectBlockCountdown;
			isPcHot = true;
			pcHotTimer = 3.f;
		}
		OnPcSuccessfulTimedBlock();
		auto pc = RE::PlayerCharacter::GetSingleton();
		if (pc) {
			pc->AddSkillExperience(RE::ActorValue::kBlock, settings::fTimedBlockMeleeExp);
		}
	}
	
	float reflectedDamage = 0;
	auto blockerWeapon = Utils::Actor::getWieldingWeapon(a_blocker);
	if (blockerWeapon) {
		reflectedDamage = blockerWeapon->GetAttackDamage();//get attack damage of blocker's weapon
	} else {
		reflectedDamage = a_blocker->CalcUnarmedDamage();
	}
	inlineUtils::offsetRealDamage(reflectedDamage, a_blocker, a_attacker);
	float stunDmg = reflectedDamage;
	stunHandler::GetSingleton()->processStunDamage(stunHandler::STUNSOURCE::timedBlock, nullptr, a_blocker, a_attacker, stunDmg);


	if (isPerfectblock) {
		playBlockEffects(a_blocker, a_attacker, blockType::perfect);
	} else {
		playBlockEffects(a_blocker, a_attacker, blockType::timed);
	}

	if (isPerfectblock) {//stagger opponent immediately on perfect block.
		//reactionHandler::triggerRecoil(a_attacker, reactionHandler::reactionType::kLarge);
		reactionHandler::triggerStagger(a_blocker, a_attacker, reactionHandler::reactionType::kMedium);
		debuffHandler::GetSingleton()->stopDebuff(a_blocker);
		Utils::Actor::refillActorValue(a_blocker, RE::ActorValue::kStamina); //perfect blocking completely restores actor value.
	}
	else {
		RE::HitData hitData;
		RE::InventoryEntryData* attackerWeapon = a_attacker->GetAttackingWeapon();
		
		hitData.Populate(a_attacker, a_blocker, attackerWeapon);
		Utils::Actor::damageav(a_blocker, RE::ActorValue::kStamina,
			hitData.totalDamage * getBlockStaminaCostMult(a_blocker, a_attacker, hitData.flags) * settings::fTimedBlockStaminaCostMult);
			staminaHandler::checkStamina(a_blocker);
		
	}

	if (EldenCounterCompatibility::on) {
		EldenCounterCompatibility::triggerCounter(a_blocker);
	}
	
	a_blocker->NotifyAnimationGraph("BlockHitStart");

	return true;
}
bool blockHandler::processMeleeTackle(RE::Actor* a_tackler, RE::Actor* a_attacker) {
#if VC_COMBAT_TRACE
	VC_TRACE_SCOPE(CombatTrace::EventKind::Tackle, 0, CombatRuntime::get().session());
#endif
	if (!a_tackler->IsPlayerRef()) {
		return false;
	}

	if (!isPcTackling) {
		return false;
	}
	
	if (!Utils::Actor::isPowerAttacking(a_tackler)) { //tackle only happens in power attacks.
		return false;
	}

	a_attacker->NotifyAnimationGraph("recoillargestart");
	playBlockEffects(a_tackler, a_attacker, blockHandler::tackle);
	
	return true;
}
PRECISION_API::PreHitCallbackReturn blockHandler::precisionPrehitCallbackFunc(const PRECISION_API::PrecisionHitData& a_precisionHitData) {
#if VC_COMBAT_TRACE
	VC_TRACE_SCOPE(CombatTrace::EventKind::PrecisionPreHit, 0, CombatRuntime::get().session());
#endif
	PRECISION_API::PreHitCallbackReturn ret;
		if (!CombatRuntime::get().ready() || (!settings::bTimedBlockToggle && !settings::bTackleToggle)) {
		return ret;
	}
	auto attacker = a_precisionHitData.attacker;
	if (!attacker) {
		return ret;
	}
	auto target = a_precisionHitData.target;
	if (!target) {
		return ret;
	}
	if (target->GetFormType() != RE::FormType::ActorCharacter) {
		return ret;
	}

	RE::Actor* targetActor = target->As<RE::Actor>();

	if (settings::bTimedBlockToggle 
		&& blockHandler::GetSingleton()
		->processMeleeTimedBlock(targetActor, attacker, 
			blockHandler::GetSingleton()->getIsForcedTimedBlocking(targetActor), 
			blockHandler::GetSingleton()->getIsForcedPerfectBlocking(targetActor))) {
		ret.bIgnoreHit = true;
	}
	if (settings::bTackleToggle 
		&& targetActor->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone) {
		if (blockHandler::GetSingleton()->processMeleeTackle(targetActor, attacker)) {
			ret.bIgnoreHit = true;
		}
	} 

	return ret;
}
#pragma endregion

void blockHandler::playBlockSFX(RE::Actor* blocker, blockType blockType, bool blockedWithWeapon) {
	if (blockedWithWeapon) {
		switch (blockType) {
		case blockType::guardBreaking:
			ValhallaUtils::playSound(blocker, data::soundParryWeapon_gb, settings::fTimedBlockSFXVolume);
			break;
		case blockType::perfect:
			ValhallaUtils::playSound(blocker, data::soundParryWeapon_perfect, settings::fTimedBlockSFXVolume);
			break;
		case blockType::timed:
			ValhallaUtils::playSound(blocker, data::soundParryWeapon, settings::fTimedBlockSFXVolume);
			break;
		case blockType::tackle:
			ValhallaUtils::playSound(blocker, data::soundParryWeapon, settings::fTimedBlockSFXVolume);
			break;
		}
	}
	else {
		switch (blockType) {
		case blockType::guardBreaking:
			ValhallaUtils::playSound(blocker, data::soundParryShield_gb, settings::fTimedBlockSFXVolume);
			break;
		case blockType::perfect:
			ValhallaUtils::playSound(blocker, data::soundParryShield_perfect, settings::fTimedBlockSFXVolume);
			break;
		case blockType::timed:
			ValhallaUtils::playSound(blocker, data::soundParryShield, settings::fTimedBlockSFXVolume);
			break;
		case blockType::tackle:
			ValhallaUtils::playSound(blocker, data::soundParryWeapon, settings::fTimedBlockSFXVolume);
			break;
		}
	}
}
void blockHandler::playBlockVFX(RE::Actor* blocker, blockType blockType, bool blockedWithWeapon){
		if (!blocker || !blocker->Is3DLoaded() || !data::BlockFX) return;
	//
	auto blockFXNode = RE::Offset::PlaceAtMe(blocker, data::BlockFX, 1, false, false);
		if (!blockFXNode) return;
	std::string node;
	if (blockedWithWeapon) {
		node = "WEAPON";
	} else {
		node = "SHIELD";
	}
	blockFXNode->MoveToNode(blocker, node);
	RE::Offset::PlaceAtMe(blockFXNode, data::BlockSpark, 1, false, false);
	RE::Offset::PlaceAtMe(blockFXNode, data::BlockSparkFlare, 1, false, false);
	switch (blockType) {
	case blockType::guardBreaking:
	case blockType::perfect:
		RE::Offset::PlaceAtMe(blockFXNode, data::BlockSparkRing, 1, false, false);
	}
	blockFXNode->SetDelete(true);
}
void blockHandler::playBlockScreenShake(RE::Actor* blocker, blockType blockType) {
	switch (blockType) {
	case blockType::guardBreaking:RE::Offset::shakeCamera(1.7, RE::PlayerCharacter::GetSingleton()->GetPosition(), 0.8f); break;
	case blockType::timed: RE::Offset::shakeCamera(1.5, RE::PlayerCharacter::GetSingleton()->GetPosition(), 0.3f); break;
	case blockType::perfect: RE::Offset::shakeCamera(1.5, RE::PlayerCharacter::GetSingleton()->GetPosition(), 0.4f); break;
	}
}

void blockHandler::playBlockSlowTime(blockType blockType) {
	float slowDuration = 0.0f;
	switch (blockType) {
	case blockType::timed:
	case blockType::tackle:
		slowDuration = settings::fTimedBlockSlowTime_Timed;
		break;
	case blockType::guardBreaking:
	case blockType::perfect:
		slowDuration = settings::fTimedBlockSlowTime_Perfect;
		break;
	}
	if (slowDuration == 0) {
		return;
	}
	inlineUtils::slowTime(slowDuration, 0.1f);
}

void blockHandler::playBlockEffects(RE::Actor* blocker, RE::Actor* attacker, blockType blockType) {
	//DEBUG("playing effects");
	bool shieldEquipped = RE::Offset::getEquippedShield(blocker);
	if (settings::bTimeBlockSFX) {
		playBlockSFX(blocker, blockType, !shieldEquipped);
	}
	if (settings::bTimedBlockScreenShake
		&&(blocker->IsPlayerRef() || (attacker && attacker->IsPlayerRef()))) {
		playBlockScreenShake(blocker, blockType);
	}

	if (settings::bTimedBlockSlowTime
		&&((attacker && attacker->IsPlayerRef()) || blocker->IsPlayerRef())) {
		playBlockSlowTime(blockType);
	}
	switch (settings::uTimedBlockSparkType) {
	case 0:
		if (blockType == blockType::tackle) {
			playBlockVFX(attacker, blockType, true);
		} else {
			playBlockVFX(blocker, blockType, !shieldEquipped);
		}
		break;
	case 1:
		if (blockType == blockType::tackle) {
			MaxsuBlockSpark::blockSpark::playPerfectBlockSpark(attacker);
		} else {
			MaxsuBlockSpark::blockSpark::playPerfectBlockSpark(blocker);
		}
		break;
	}
}

void blockHandler::EldenCounterCompatibility::attemptInit()
{
	logger::info("Initializing Elden Counter compatibility");
	ec_triggerSpell = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(0x801, "EldenCounter.esp");
	if (!ec_triggerSpell) {
		logger::info("EldenCounter.esp not found");
		on = false;
	} else {
		logger::info("found Elden Counter plugin; compatibility enabled.");
		EldenCounterCompatibility::readSettings();
		on = true;
	}
}

inline void blockHandler::EldenCounterCompatibility::triggerCounter(RE::Actor* a_actor)
{
	if (!a_actor || !ec_triggerSpell || !CombatRuntime::get().ready() || a_actor->HasSpell(ec_triggerSpell)) {
		return;
	}
	if (!a_actor->AddSpell(ec_triggerSpell)) return;
	OwnedEffects::add(a_actor,OwnedEffects::counter);
	ownedActors[a_actor->GetHandle()]=++generation;
	async_removeECTriggerSpell(a_actor);
}

void blockHandler::EldenCounterCompatibility::async_removeECTriggerSpell(RE::Actor* a_actor)
{
	if (!a_actor) return;
	const auto handle=a_actor->GetHandle();
	const auto instance=ownedActors.at(handle);
	CombatRuntime::get().schedule(ec_Time, [handle,instance] {
		const auto it=ownedActors.find(handle);
		if (it==ownedActors.end() || it->second!=instance) return;
		ownedActors.erase(it);
		auto actor = handle.get();
		if (actor && ec_triggerSpell) { actor->RemoveSpell(ec_triggerSpell); OwnedEffects::remove(actor.get(),OwnedEffects::counter); }
	});
	
}

void blockHandler::EldenCounterCompatibility::reset()
{
	const auto pending=std::exchange(ownedActors,{});
	for (const auto& [handle,instance] : pending) { auto actor = handle.get(); if (actor && ec_triggerSpell) { actor->RemoveSpell(ec_triggerSpell); OwnedEffects::remove(actor.get(),OwnedEffects::counter); } }
}

void blockHandler::EldenCounterCompatibility::readSettings()
{
	logger::info("EldenCounter compatibility: reading settings...");
	CSimpleIniA ini;
#define SETTINGFILE_PATH "Data\\SKSE\\Plugins\\EldenCounter.ini"
	ini.LoadFile(SETTINGFILE_PATH);
		const auto seconds = ini.GetDoubleValue("General", "Time", 0.5);
		ec_Time = std::isfinite(seconds) ? static_cast<float>(std::clamp(seconds, 0.0, 60.0)) : 0.5f;
	logger::info("EldenCounter compatibilty: settings loaded.");
}

/// <summary>
/// Get the Valor perilous state of A_ACTOR. This function may be called even when Valor isn't enabled, which will
/// result in a return value of 0.
/// </summary>
blockHandler::ValorCompatibility::PERILOUS_TYPE blockHandler::ValorCompatibility::get_perilous_state(RE::Actor* a_actor)
{
	int gv;
	if (a_actor->GetGraphVariableInt(gv_int_perilous_attack_type, gv)) {
		return static_cast<PERILOUS_TYPE>(gv);
	}
	return PERILOUS_TYPE::none;
}
