#include "include/executionHandler.h"
#include "include/data.h"
#include "include/settings.h"
#include "include/stunHandler.h"
#include "include/Utils.h"
#include "include/CombatTrace.h"
#define DATA data::GetSingleton()
using namespace inlineUtils;
void executionHandler::tryPcExecution() {
	if (!CombatRuntime::get().ready()) return;
	report("No eligible execution target in range");

	auto pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) {
		return;
	}
		std::vector<std::pair<float, RE::ActorHandle>> candidates;

	for (auto& handle : stunHandler::GetSingleton()->getStunBrokenActors()) {
		if (!handle) {
			continue;
		}
			auto actorRef = handle.get();
			auto actor = actorRef.get();
		
		if (!actor || !actor->Is3DLoaded() || Utils::Actor::isDead(actor) || actor->IsInKillMove() ||
			!actor->GetActorRuntimeData().currentProcess || !actor->GetActorRuntimeData().currentProcess->InHighProcess()) {
				continue;
		}
		float dist = pc->GetPosition().GetDistance(actor->GetPosition());
		if (dist < 250) {
				candidates.emplace_back(dist, handle);
		}
	}
	
		std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (const auto& candidate : candidates) {
			auto actor = candidate.second.get();
			if (actor && attemptExecute(pc, actor.get())) break;
		}

}
bool executionHandler::attemptExecute(RE::Actor* a_executor, RE::Actor* a_victim) {
#if VC_COMBAT_TRACE
	VC_TRACE_SCOPE(CombatTrace::EventKind::Execution, 0, CombatRuntime::get().session());
#endif
	//check if victim can be executed
		if (!CombatRuntime::get().ready() || !a_executor || !a_victim || !settings::bStunToggle
		|| !Utils::Actor::isHumanoid(a_executor) || Utils::Actor::isDead(a_executor) || Utils::Actor::isDead(a_victim)
		|| !a_executor->Is3DLoaded() || !a_victim->Is3DLoaded()
		|| a_executor->IsInKillMove() || a_victim->IsInKillMove()
		|| a_executor->IsOnMount() || a_victim->IsOnMount()
		|| a_victim->IsPlayerRef() || a_victim->IsPlayerTeammate()
		|| a_victim->IsEssential() || a_victim->IsInKillMove() || a_victim->AsMagicTarget()->HasEffectWithArchetype(RE::MagicTarget::Archetype::kParalysis)) {
		//logger::info("Execution preconditions not met, terminating execution.");
		return false;
	}

		if (!stunHandler::GetSingleton()->getIsStunBroken(a_victim) || pending.contains(a_executor->GetHandle()) || pending.contains(a_victim->GetHandle())) return false;
		auto victimRace = a_victim->GetRace();
	if (!victimRace) {
		return false;
	}
	
	auto it2 = data::raceMapping.find(victimRace);
	if (it2 == data::raceMapping.end()) {
		report("Rejected: target race has no execution mapping");
		return false;
	}
	auto victimRaceType = it2->second;

	RE::WEAPON_TYPE weaponType;
	auto weapon = Utils::Actor::getWieldingWeapon(a_executor);
	if (!weapon) {
		//logger::info("Executor weapon not found, using unarmed as weapon type.");
		weaponType = RE::WEAPON_TYPE::kHandToHandMelee;
	}
	else {
		weaponType = weapon->GetWeaponType();
	}

	if (weaponType == RE::WEAPON_TYPE::kBow || weaponType == RE::WEAPON_TYPE::kCrossbow) {
		//logger::info("long range weapon");
		return false;
	}

	//logger::info("weapon type is {}", weaponType);
#define RACE data::raceCatagory
		const auto before = queued;
		switch (victimRaceType) {
	case RACE::Humanoid: executeHumanoid(a_executor, a_victim, weaponType); break;
	case RACE::Undead: executeDraugr(a_executor, a_victim, weaponType); break;
	case RACE::Falmer: executeFalmer(a_executor, a_victim, weaponType); break;
	case RACE::Spider: executeSpider(a_executor, a_victim, weaponType); break;
	case RACE::Gargoyle: executeGargoyle(a_executor, a_victim, weaponType); break;
	case RACE::Giant: executeGiant(a_executor, a_victim, weaponType); break;
	case RACE::Bear: executeBear(a_executor, a_victim, weaponType); break;
	case RACE::SabreCat: executeSabreCat(a_executor, a_victim, weaponType); break;
	case RACE::Wolf: executeWolf(a_executor, a_victim, weaponType); break;
	case RACE::Troll: executeTroll(a_executor, a_victim, weaponType); break;
	case RACE::Boar: executeBoar(a_executor, a_victim, weaponType); break;
	case RACE::Hagraven: executeHagraven(a_executor, a_victim, weaponType); break;
	case RACE::Spriggan: executeSpriggan(a_executor, a_victim, weaponType); break;
	case RACE::Riekling: executeRiekling(a_executor, a_victim, weaponType); break;
	case RACE::AshHopper: executeAshHopper(a_executor, a_victim, weaponType); break;
	case RACE::SteamCenturion: executeSteamCenturion(a_executor, a_victim, weaponType); break;
	case RACE::DwarvenBallista: executeDwarvenBallista(a_executor, a_victim, weaponType); break;
	case RACE::ChaurusFlyer: executeChaurusFlyer(a_executor, a_victim, weaponType); break;
	case RACE::Lurker: executeLurker(a_executor, a_victim, weaponType); break;
	case RACE::Dragon: executeDragon(a_executor, a_victim, weaponType); break;
	default: return false; //iff no body part match, no need to set ghost.
	}
		if (queued==before) report("Rejected: no supported idle for this race and weapon");
		return queued != before;
};

void executionHandler::queueExecutionIdle(RE::Actor* executor, RE::Actor* victim, RE::TESIdleForm* idle, int = 1)
{
	if (!CombatRuntime::get().ready() || !executor || !victim || !idle) return;
	const auto a = executor->GetHandle(), b = victim->GetHandle();
	if (pending.contains(a) || pending.contains(b)) return;
	const auto race = victim->GetRace();
	const auto weapon = Utils::Actor::getWieldingWeapon(executor);
	const auto weaponID = weapon ? weapon->GetFormID() : 0;
	pending.insert(a); pending.insert(b);
	const auto ticket = CombatRuntime::get().schedule(0, [this, a, b, idle, race, weaponID] {
		auto attacker = a.get(); auto target = b.get();
		bool keepPending=false;
		const auto release=std::unique_ptr<executionHandler,std::function<void(executionHandler*)>>(this,[&keepPending,a,b](executionHandler* self) {
			if (!keepPending) { self->pending.erase(a); self->pending.erase(b); }
		});
		report("Cancelled: execution preconditions changed before playback");
		if (!attacker || !target || Utils::Actor::isDead(attacker.get()) || Utils::Actor::isDead(target.get()) ||
			!settings::bStunToggle || !stunHandler::GetSingleton()->getIsStunBroken(target.get()) ||
			!attacker->Is3DLoaded() || !target->Is3DLoaded() || attacker->IsInKillMove() || target->IsInKillMove() ||
			attacker->IsOnMount() || target->IsOnMount() || target->IsEssential() || target->IsPlayerTeammate() || target->IsPlayerRef() ||
			target->AsMagicTarget()->HasEffectWithArchetype(RE::MagicTarget::Archetype::kParalysis) || target->GetRace() != race ||
			attacker->GetPosition().GetDistance(target->GetPosition()) >= 250) return;
		auto process = attacker->GetActorRuntimeData().currentProcess;
		auto targetProcess = target->GetActorRuntimeData().currentProcess;
		auto currentWeapon = Utils::Actor::getWieldingWeapon(attacker.get());
		if (!process || !process->InHighProcess() || !targetProcess || !targetProcess->InHighProcess() ||
			(currentWeapon ? currentWeapon->GetFormID() : 0) != weaponID) return;
		const bool accepted = RE::Offset::playPairedIdle(process, attacker.get(), RE::DEFAULT_OBJECT::kActionIdle, idle, true, false, target.get());
		logger::debug("Execution {} -> {} engine accepted={}", attacker->GetFormID(), target->GetFormID(), accepted);
		report(accepted?"Execution accepted by engine":"Execution rejected by engine");
		if (accepted) {
			keepPending=CombatRuntime::get().schedule(0.5f, [this, a, b] { pending.erase(a); pending.erase(b); })!=0;
		}
	});
	if (ticket) { ++queued; report("Execution queued; playback not yet confirmed"); } else { pending.erase(a); pending.erase(b); report("Execution request cancelled with session"); }
}
void executionHandler::queueExecutionIdle(RE::Actor* executor, RE::Actor* victim, std::vector<RE::TESIdleForm*> idles, int i = 1)
{
	std::erase(idles, nullptr);
	if (idles.empty()) { logger::warn("No execution idle for this race/weapon combination"); return; }
	queueExecutionIdle(executor, victim, *inlineUtils::select_randomly(idles.begin(), idles.end()), i);
}


void executionHandler::executeHumanoid(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE a_weaponType) {
	//logger::info("executing humanoid!");
	if (Utils::Actor::isDualWielding(a_executor)) {
		//logger::info("dual wielding!");
		queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_DW);
	}
	else if (Utils::Actor::isBackFacing(a_victim, a_executor)) {
		//logger::info("backstab!");
		switch (a_weaponType) {
		case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_2hw_Back); break;
		case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_2hm_Back); break;
		case RE::WEAPON_TYPE::kHandToHandMelee: queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_H2H_Back); break;
		default: queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_1hm_Back); break;
		}
	}
	else {
		switch (a_weaponType) {
		case RE::WEAPON_TYPE::kOneHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_Sword); break;
		case RE::WEAPON_TYPE::kOneHandDagger: queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_Dagger); break;
		case RE::WEAPON_TYPE::kOneHandAxe:  queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_Axe); break;
		case RE::WEAPON_TYPE::kOneHandMace:  queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_Mace); break;
		case RE::WEAPON_TYPE::kTwoHandAxe:  queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_2hw); break;
		case RE::WEAPON_TYPE::kTwoHandSword:  queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_GreatSword); break;
		case RE::WEAPON_TYPE::kHandToHandMelee:  queueExecutionIdle(a_executor, a_victim, data::KM_Humanoid_H2H); break;
		}
	}
};
void executionHandler::executeDraugr(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Undead_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Undead_2hm); break;
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Undead_1hm); break;
	}
};
void executionHandler::executeFalmer(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Falmer_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Falmer_2hm); break;
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Falmer_1hm);
	}
};
void executionHandler::executeSpider(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Spider_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Spider_2hm); break;
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Spider_1hm);
	}
}
void executionHandler::executeGargoyle(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Gargoyle_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Gargoyle_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Gargoyle_1hm); break;
	}
}
void executionHandler::executeGiant(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Giant_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Giant_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Giant_1hm); break;
	}
}
void executionHandler::executeBear(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Bear_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Bear_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Bear_1hm); break;
	}
}
void executionHandler::executeSabreCat(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_SabreCat_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_SabreCat_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_SabreCat_1hm); break;
	}
}
void executionHandler::executeWolf(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Wolf_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Wolf_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Wolf_1hm); break;
	}
}
void executionHandler::executeTroll(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Troll_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Troll_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Troll_1hm); break;
	}
}
void executionHandler::executeHagraven(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Hagraven_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Hagraven_2hm);  break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Hagraven_1hm); break;
	}
}
void executionHandler::executeSpriggan(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Spriggan_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Spriggan_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Spriggan_1hm); break;
	}
}
void executionHandler::executeBoar(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Boar_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Boar_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Boar_1hm); break;
	}
}
void executionHandler::executeRiekling(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Riekling_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Riekling_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Riekling_1hm); break;
	}
}
void executionHandler::executeAshHopper(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_AshHopper_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_AshHopper_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_AshHopper_1hm); break;
	}
}
void executionHandler::executeSteamCenturion(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Centurion_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Centurion_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Centurion_1hm); break;
	}
}
void executionHandler::executeDwarvenBallista(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Ballista_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Ballista_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Ballista_1hm); break;
	}
}
void executionHandler::executeChaurusFlyer(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_ChaurusFlyer_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_ChaurusFlyer_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_ChaurusFlyer_1hm); break;
	}
}
void executionHandler::executeLurker(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Lurker_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Lurker_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Lurker_1hm); break;
	}
}
void executionHandler::executeDragon(RE::Actor* a_executor, RE::Actor* a_victim, RE::WEAPON_TYPE weaponType) {
	switch (weaponType) {
	case RE::WEAPON_TYPE::kHandToHandMelee: break;
	case RE::WEAPON_TYPE::kTwoHandAxe: queueExecutionIdle(a_executor, a_victim, data::KM_Dragon_2hw); break;
	case RE::WEAPON_TYPE::kTwoHandSword: queueExecutionIdle(a_executor, a_victim, data::KM_Dragon_2hm); break;
	default: queueExecutionIdle(a_executor, a_victim, data::KM_Dragon_1hm); break;
	}
}

