#include "include/executionHandler.h"
#include "include/data.h"
#include "include/settings.h"
#include "include/stunHandler.h"
#include "include/Utils.h"
#include "include/CombatTrace.h"
#include "include/EnemyClassifier.h"
#define DATA data::GetSingleton()
using namespace inlineUtils;
namespace {
void notifyExecution(const char* message)
{
    if (auto vm=RE::BSScript::Internal::VirtualMachine::GetSingleton()) {
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
        vm->DispatchStaticCall("Debug","Notification",RE::MakeFunctionArguments(RE::BSFixedString(message)),result);
    }
}
}
void executionHandler::tryPcExecution() {
	if (!CombatRuntime::get().ready()) return;
	auto player=RE::PlayerCharacter::GetSingleton();
	auto target=selectTarget(player);
	if (target) attemptExecute(player,target);
	else {
		report("No eligible target in front: check health/stun, range, protection and weapon support");
		notifyExecution(settings::bMenuChinese ? "前方没有可处决目标，请检查血量、破防、距离与动作支持" : "No eligible execution target in front");
	}
}
executionHandler::Eligibility executionHandler::evaluate(RE::Actor* executor,RE::Actor* victim,bool checkPending)
{
    if (!CombatRuntime::get().ready() || !settings::bStunToggle || !executor || !victim || executor==victim)
        return {false,"Execution disabled or no target"};
    if (!Utils::Actor::isHumanoid(executor) || Utils::Actor::isDead(executor) || Utils::Actor::isDead(victim) ||
        !executor->Is3DLoaded() || !victim->Is3DLoaded() || executor->IsInKillMove() || victim->IsInKillMove() ||
        executor->IsOnMount() || victim->IsOnMount()) return {false,"Actor state prevents execution"};
    if (victim->IsPlayerRef() || victim->IsPlayerTeammate() || victim->IsEssential() ||
        (victim->IsProtected() && !executor->IsPlayerRef()) || victim->IsChild()) return {false,"Protected target"};
    if (!executor->IsHostileToActor(victim)) return {false,"Target is not hostile"};
    if (checkPending && (pending.contains(executor->GetHandle()) || pending.contains(victim->GetHandle())))
        return {false,"Execution already pending"};
    if (victim->AsMagicTarget()->HasEffectWithArchetype(RE::MagicTarget::Archetype::kParalysis)) return {false,"Target is paralyzed"};
    auto a=executor->GetActorRuntimeData().currentProcess, b=victim->GetActorRuntimeData().currentProcess;
    if (!a || !b || !a->InHighProcess() || !b->InHighProcess()) return {false,"Actors not in high process"};
    if (executor->GetPosition().GetDistance(victim->GetPosition())>=250 ||
        executor->GetHeadingAngle(victim->GetPosition(),true)>70) return {false,"Target out of range or behind executor"};
    bool unknown=false;
    if (!executor->HasLineOfSight(victim,unknown)) return {false,"No line of sight"};
    const auto tier=encounters::classify(victim).tier;
    const float maximum=victim->GetActorValueMax(RE::ActorValue::kHealth);
    const float health=victim->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
    const float fraction=maximum>0 && std::isfinite(maximum) ? health/maximum : 2.0f;
    if (!encounters::canFinish(tier,stunHandler::GetSingleton()->getIsStunBroken(victim),fraction,settings::fExecutionHealthThreshold))
        return {false,tier==encounters::Tier::normal ? "Requires broken stun or low health" : "Elite/boss requires low health"};
    const auto idles=idlesFor(executor,victim);
    if (!idles || std::none_of(idles->begin(),idles->end(),[](auto idle) { return idle!=nullptr; }))
        return {false,"No supported idle for this race and weapon"};
    return {true,"Ready for execution"};
}
bool executionHandler::attemptExecute(RE::Actor* executor,RE::Actor* victim)
{
    const auto eligibility=evaluate(executor,victim);
    report(eligibility.reason);
    if (!eligibility.allowed) return false;
    const auto before=queued;
    queueExecutionIdle(executor,victim,*idlesFor(executor,victim),1);
    return queued!=before;
}
bool executionHandler::tryAutomatic(RE::Actor* executor,RE::Actor* victim)
{
    if (!executor || (executor->IsPlayerRef() ? !settings::bAutoExecution :
        executor->IsPlayerTeammate() && !settings::bFollowerAutoExecution)) return false;
    return attemptExecute(executor,victim);
}
float executionHandler::openingDamageMultiplier(RE::Actor* victim)
{
    return settings::bStunToggle && stunHandler::GetSingleton()->getIsStunBroken(victim) &&
        encounters::classify(victim).tier!=encounters::Tier::normal ? settings::fBossOpeningDamageMult : 1.0f;
}
RE::Actor* executionHandler::selectTarget(RE::Actor* player)
{
    if (!player) return nullptr;
    if (auto crosshair=RE::CrosshairPickData::GetSingleton()) {
        auto reference=crosshair->GetActiveTarget().get();
        auto actor=reference ? reference->As<RE::Actor>() : nullptr;
        if (evaluate(player,actor).allowed) return actor;
    }
    RE::Actor* selected=nullptr;
    float best=std::numeric_limits<float>::max();
    if (auto processes=RE::ProcessLists::GetSingleton()) processes->ForEachHighActor([&](RE::Actor* actor) {
        if (evaluate(player,actor).allowed) {
            const float score=std::abs(player->GetHeadingAngle(actor->GetPosition(),true))*4+
                player->GetPosition().GetDistance(actor->GetPosition());
            if (score<best) { best=score; selected=actor; }
        }
        return RE::BSContainer::ForEachResult::kContinue;
    });
    return selected;
}
void executionHandler::updatePrompt(float delta)
{
    promptCountdown-=delta;
    if (promptCountdown>0) return;
    promptCountdown=0.25f;
    auto player=RE::PlayerCharacter::GetSingleton();
    auto eligible=selectTarget(player);
    RE::Actor* diagnostic=eligible;
    if (!diagnostic) if (auto crosshair=RE::CrosshairPickData::GetSingleton()) {
        auto reference=crosshair->GetActiveTarget().get();
        diagnostic=reference ? reference->As<RE::Actor>() : nullptr;
    }
    std::string info="No target";
    if (diagnostic) info=fmt::format("{}: {} / {}",diagnostic->GetName(),encounters::describe(encounters::classify(diagnostic)),evaluate(player,diagnostic).reason);
    { std::lock_guard lock(statusMutex); targetInfo=std::move(info); }
    const auto handle=eligible ? eligible->GetHandle() : RE::ActorHandle{};
    if (eligible && handle!=promptTarget && settings::bExecutionPrompt) {
        const char* hint=settings::iExecutionKey<0 ?
            (settings::bMenuChinese ? "可处决：请在设置中绑定处决键" : "Execution available: bind the execution key in settings") :
            (settings::bMenuChinese ? "可处决：按已绑定的处决键" : "Execution available: press your execution key");
        notifyExecution(hint);
    }
    promptTarget=handle;
}

void executionHandler::queueExecutionIdle(RE::Actor* executor, RE::Actor* victim, RE::TESIdleForm* idle, int = 1)
{
	if (!idle || !evaluate(executor,victim).allowed) return;
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
		if (!attacker || !target || !evaluate(attacker.get(),target.get(),false).allowed || target->GetRace()!=race) return;
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

const std::vector<RE::TESIdleForm*>* executionHandler::idlesFor(RE::Actor* executor,RE::Actor* victim)
{
    const auto found=data::raceMapping.find(victim->GetRace());
    if (found==data::raceMapping.end()) return nullptr;
    auto weapon=Utils::Actor::getWieldingWeapon(executor);
    if (weapon && !weapon->IsMelee()) return nullptr;
    const auto type=weapon ? weapon->GetWeaponType() : RE::WEAPON_TYPE::kHandToHandMelee;
    if (type==RE::WEAPON_TYPE::kBow || type==RE::WEAPON_TYPE::kCrossbow) return nullptr;
    using W=RE::WEAPON_TYPE;
    if (found->second==data::Humanoid) {
        if (Utils::Actor::isDualWielding(executor)) return &data::KM_Humanoid_DW;
        if (Utils::Actor::isBackFacing(victim,executor)) {
            switch (type) {
            case W::kTwoHandAxe: return &data::KM_Humanoid_2hw_Back;
            case W::kTwoHandSword: return &data::KM_Humanoid_2hm_Back;
            case W::kHandToHandMelee: return &data::KM_Humanoid_H2H_Back;
            default: return &data::KM_Humanoid_1hm_Back;
            }
        }
        switch (type) {
        case W::kOneHandSword: return &data::KM_Humanoid_Sword;
        case W::kOneHandDagger: return &data::KM_Humanoid_Dagger;
        case W::kOneHandAxe: return &data::KM_Humanoid_Axe;
        case W::kOneHandMace: return &data::KM_Humanoid_Mace;
        case W::kTwoHandAxe: return &data::KM_Humanoid_2hw;
        case W::kTwoHandSword: return &data::KM_Humanoid_GreatSword;
        case W::kHandToHandMelee: return &data::KM_Humanoid_H2H;
        default: return nullptr;
        }
    }
    if (type==W::kHandToHandMelee) return nullptr;
#define VC_IDLE_POOL(race,prefix) case data::race: return type==W::kTwoHandAxe ? &data::prefix##_2hw : type==W::kTwoHandSword ? &data::prefix##_2hm : &data::prefix##_1hm;
    switch (found->second) {
    VC_IDLE_POOL(Undead,KM_Undead)
    VC_IDLE_POOL(Falmer,KM_Falmer)
    VC_IDLE_POOL(Spider,KM_Spider)
    VC_IDLE_POOL(Gargoyle,KM_Gargoyle)
    VC_IDLE_POOL(Giant,KM_Giant)
    VC_IDLE_POOL(Bear,KM_Bear)
    VC_IDLE_POOL(SabreCat,KM_SabreCat)
    VC_IDLE_POOL(Wolf,KM_Wolf)
    VC_IDLE_POOL(Troll,KM_Troll)
    VC_IDLE_POOL(Hagraven,KM_Hagraven)
    VC_IDLE_POOL(Spriggan,KM_Spriggan)
    VC_IDLE_POOL(Boar,KM_Boar)
    VC_IDLE_POOL(Riekling,KM_Riekling)
    VC_IDLE_POOL(AshHopper,KM_AshHopper)
    VC_IDLE_POOL(SteamCenturion,KM_Centurion)
    VC_IDLE_POOL(DwarvenBallista,KM_Ballista)
    VC_IDLE_POOL(ChaurusFlyer,KM_ChaurusFlyer)
    VC_IDLE_POOL(Lurker,KM_Lurker)
    VC_IDLE_POOL(Dragon,KM_Dragon)
    default: return nullptr;
    }
#undef VC_IDLE_POOL
}
