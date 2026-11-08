#include "include/stunHandler.h"
#include "ValhallaCombat.hpp"
#include "include/reactionHandler.h"
#include "include/Utils.h"
#include "include/OwnedEffects.h"
#include "include/CombatTrace.h"
#include "include/EnemyClassifier.h"

void stunHandler::publish(const RE::ActorHandle& handle, const State& state)
{
    std::lock_guard lock(snapshotMutex);
    if (state.cleanup) snapshots.erase(handle);
    else snapshots.insert_or_assign(handle, Snapshot{state.maximum, state.current, state.broken});
}
void stunHandler::publishBatch()
{
    if (snapshotRemovals.empty() && snapshotUpdates.empty()) return;
    {
        std::lock_guard lock(snapshotMutex);
        for (const auto& handle : snapshotRemovals) snapshots.erase(handle);
        for (const auto& [handle, snapshot] : snapshotUpdates) snapshots.insert_or_assign(handle, snapshot);
    }
    snapshotRemovals.clear();
    snapshotUpdates.clear();
}
void stunHandler::clearSnapshots()
{
    std::lock_guard lock(snapshotMutex);
    snapshots.clear();
}
float stunHandler::getCurrentStun(RE::Actor* actor)
{
    if (!actor) return 1;
    const auto handle = actor->GetHandle();
    std::lock_guard lock(snapshotMutex);
    const auto it = snapshots.find(handle);
    return it == snapshots.end() ? 1 : it->second.current;
}
float stunHandler::getMaxStun(RE::Actor* actor)
{
    if (!actor) return 1;
    const auto handle = actor->GetHandle();
    std::lock_guard lock(snapshotMutex);
    const auto it = snapshots.find(handle);
    return it == snapshots.end() ? 1 : it->second.maximum;
}
bool stunHandler::getIsStunBroken(RE::Actor* actor)
{
    if (!actor) return false;
    const auto handle = actor->GetHandle();
    std::lock_guard lock(snapshotMutex);
    const auto it = snapshots.find(handle);
    return it != snapshots.end() && it->second.broken;
}
std::unordered_set<RE::ActorHandle> stunHandler::getStunBrokenActors()
{
    std::unordered_set<RE::ActorHandle> result;
    std::lock_guard lock(snapshotMutex);
    for (const auto& [handle, value] : snapshots) if (value.broken) result.insert(handle);
    return result;
}
bool stunHandler::recover(RE::Actor* actor, State& state)
{
    if (state.downed) {
        if (!actor->Is3DLoaded() || actor->IsInKillMove() || actor->AsActorState()->IsSwimming()) return false;
        if (!reactionHandler::recoverDownedState(actor)) return false;
        OwnedEffects::remove(actor,OwnedEffects::downed);
        state.downed = false;
    }
    if (std::exchange(state.meterOwned,false)) TrueHUDUtils::revertSpecialMeter(actor);
    return true;
}
void stunHandler::reset()
{
    if (mutating) { resetPending=true; return; }
    Mutation transaction(*this);
    for (auto& [handle, state] : states) {
        auto actor = handle.get();
        if (actor) recover(actor.get(), state);
    }
    states.clear();
    clearSnapshots();
}
bool stunHandler::pendingCleanup() const
{
    for (const auto& [handle,state] : states) if (state.cleanup || (state.downed && !settings::bDownedStateToggle)) return true;
    return false;
}
void stunHandler::refreshMeter(bool enabled)
{
    if (mutating) return;
    Mutation transaction(*this);
    for (auto& [handle, state] : states) {
        auto actor=handle.get(); if (!actor) continue;
        if (!enabled) {
            if (std::exchange(state.meterOwned,false)) TrueHUDUtils::revertSpecialMeter(actor.get());
        } else if (state.broken && !state.cleanup && !state.meterOwned) {
            state.meterOwned=TrueHUDUtils::greyOutSpecialMeter(actor.get());
        }
    }
}
void stunHandler::update(float delta)
{
#if VC_COMBAT_TRACE
    VC_TRACE_SCOPE(CombatTrace::EventKind::StunUpdate, 0, CombatRuntime::get().session());
#endif
    if (mutating) return;
    Mutation transaction(*this);
    if (delta < 0) delta = std::max(0.0f, *RE::Offset::g_deltaTime);
    flashCountdown -= delta==0?0:CombatRuntime::get().activeDelta();
    const bool flash = flashCountdown <= 0;
    if (flash) flashCountdown = 0.5f;
    snapshotUpdates.clear();
    snapshotRemovals.clear();
    for (auto it = states.begin(); it != states.end();) {
        const auto handle = it->first;
        auto& state = it->second;
        auto actor = handle.get();
        if (!actor) {
            snapshotRemovals.push_back(handle);
            it = states.erase(it);
            continue;
        }
        const bool wasCleanup = state.cleanup;
        const float previousCurrent = state.current;
        if (!settings::bStunToggle || Utils::Actor::isDead(actor.get())) state.cleanup = true;
        if (state.downed && (!settings::bDownedStateToggle || !state.broken)) {
            if (actor->Is3DLoaded() && !actor->IsInKillMove() && !actor->AsActorState()->IsSwimming()) {
                if (reactionHandler::recoverDownedState(actor.get())) {
                    OwnedEffects::remove(actor.get(),OwnedEffects::downed);
                    state.downed = false;
                }
            }
        }
        if (!state.cleanup && state.broken) {
            const float elapsed=std::min(std::max(0.0f,state.countdown),delta);
            state.countdown-=elapsed;
            if (state.countdown<=0) {
                state.broken=false;
                state.current=state.maximum;
                state.resistance=state.protectedTier ? settings::fBossStunResistance : 0;
                state.resistance=std::max(0.0f,state.resistance-(delta-elapsed));
                const bool recovered=recover(actor.get(),state);
                if (recovered && state.resistance<=0) state.cleanup=true;
            }
        } else if (!state.cleanup) {
            state.resistance=std::max(0.0f,state.resistance-delta);
            const float waiting = std::min(std::max(0.0f, state.countdown), delta);
            state.countdown -= waiting;
            const float recoveryDelta = delta - waiting;
            if (recoveryDelta > 0 && !actor->IsInCombat()) {
                state.current = std::min(state.maximum, state.current + recoveryDelta / 7 * state.maximum);
            }
            if (state.current>=state.maximum && state.resistance<=0 && !state.downed) state.cleanup=true;
        }
        if (state.cleanup) {
            if (recover(actor.get(), state)) {
                snapshotRemovals.push_back(handle);
                it = states.erase(it);
                continue;
            }
            if (!wasCleanup) snapshotRemovals.push_back(handle);
        } else if (flash && state.broken && settings::bUIAlert) {
            TrueHUDUtils::flashActorValue(actor.get(), RE::ActorValue::kHealth);
        }
        if (!state.cleanup && state.current != previousCurrent)
            snapshotUpdates.emplace_back(handle, Snapshot{state.maximum, state.current, state.broken});
        ++it;
    }
    publishBatch();
    if (states.empty()) ValhallaCombat::GetSingleton()->deactivateUpdate(ValhallaCombat::stunHandler);
}
void stunHandler::damageStun(RE::Actor*, RE::Actor* victim, float amount)
{
#if VC_COMBAT_TRACE
    VC_TRACE_SCOPE(CombatTrace::EventKind::StunDamage, 0, CombatRuntime::get().session());
#endif
    if (!victim || !std::isfinite(amount) || amount <= 0) return;
    const auto handle = victim->GetHandle();
    auto [it, inserted] = states.try_emplace(handle);
    auto& state = it->second;
    if (!inserted && (state.broken || state.resistance>0 || state.cleanup)) return;
    if (inserted) {
        state.maximum = std::max(1.0f, (victim->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth) +
            victim->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kStamina)) / 2);
        state.current = state.maximum;
    }
    state.cleanup = false;
    state.countdown = 3;
    state.current = std::max(0.0f, state.current - amount);
    if (!state.broken && state.current <= 0) {
        state.broken = true;
        state.countdown=settings::fStunBrokenWindow;
        state.protectedTier=encounters::classify(victim).tier!=encounters::Tier::normal;
        ValhallaUtils::playSound(victim, data::soundStunBreak);
        if (settings::bStunMeterToggle) state.meterOwned=TrueHUDUtils::greyOutSpecialMeter(victim);
    }
    publish(handle, state);
    if (state.broken && settings::bDownedStateToggle && !state.downed && victim->Is3DLoaded() &&
        !victim->IsInKillMove() && !victim->AsActorState()->IsSwimming() &&
        (data::isRaceType(victim, data::Humanoid) || data::isRaceType(victim, data::Undead))) {
        bool alreadyDown = false;
        victim->GetGraphVariableBool("IsBleedingOut", alreadyDown);
        if ((alreadyDown && OwnedEffects::contains(victim,OwnedEffects::downed)) ||
            (!alreadyDown && reactionHandler::triggerDownedState(victim))) { state.downed = true; OwnedEffects::add(victim,OwnedEffects::downed); }
    }
    ValhallaCombat::GetSingleton()->activateUpdate(ValhallaCombat::stunHandler);
}
void stunHandler::modStun(RE::Actor* actor, float amount)
{
    if (!actor || !CombatRuntime::get().ready() || !std::isfinite(amount)) return;
    if (mutating) {
        CombatRuntime::get().schedule(0,[handle=actor->GetHandle(),amount] { auto value=handle.get(); if (value) GetSingleton()->modStun(value.get(),amount); });
        return;
    }
    Mutation transaction(*this);
    const auto handle = actor->GetHandle();
    const auto it = states.find(handle);
    if (it == states.end()) return;
    if (it->second.broken || it->second.resistance>0 || it->second.cleanup) return;
    const float current = std::clamp(it->second.current + amount, 0.0f, it->second.maximum);
    if (current == it->second.current) return;
    it->second.current = current;
    publish(handle, it->second);
}
void stunHandler::processStunDamage(STUNSOURCE source, RE::TESObjectWEAP* weapon, RE::Actor* aggressor, RE::Actor* victim, float base)
{
    if (!CombatRuntime::get().ready() || !settings::bStunToggle || !victim || !aggressor ||
        !std::isfinite(base) || victim->IsPlayerRef() || (victim->IsPlayerTeammate() && !settings::bFriendlyStunToggle)) return;
    if (mutating) {
        const auto form=weapon?weapon->GetFormID():0;
        CombatRuntime::get().schedule(0,[source,form,a=aggressor->GetHandle(),v=victim->GetHandle(),base] {
            auto attacker=a.get(),target=v.get();
            if (attacker && target) GetSingleton()->processStunDamage(source,form?RE::TESForm::LookupByID<RE::TESObjectWEAP>(form):nullptr,attacker.get(),target.get(),base);
        });
        return;
    }
    Mutation transaction(*this);
    float damage = 0;
    switch (source) {
    case timedBlock: damage = base * settings::fStunTimedBlockMult; break;
    case bash: damage = aggressor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kBlock) * settings::fStunBashMult; break;
    case powerBash: damage = aggressor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kBlock) * settings::fStunPowerBashMult; break;
    case parry: damage = aggressor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kBlock) * settings::fStunParryMult; break;
    case counterAttack: damage = base * settings::fStunCounterAttackMult; break;
    case lightAttack:
    case powerAttack:
        damage = base * (source == lightAttack ? settings::fStunNormalAttackMult : settings::fStunPowerAttackMult);
        switch (weapon ? weapon->GetWeaponType() : RE::WEAPON_TYPE::kHandToHandMelee) {
        case RE::WEAPON_TYPE::kHandToHandMelee: damage *= settings::fStunUnarmedMult; break;
        case RE::WEAPON_TYPE::kOneHandDagger: damage *= settings::fStunDaggerMult; break;
        case RE::WEAPON_TYPE::kOneHandSword: damage *= settings::fStunSwordMult; break;
        case RE::WEAPON_TYPE::kOneHandAxe: damage *= settings::fStunWarAxeMult; break;
        case RE::WEAPON_TYPE::kOneHandMace: damage *= settings::fStunMaceMult; break;
        case RE::WEAPON_TYPE::kTwoHandAxe: damage *= settings::fStun2HBluntMult; break;
        case RE::WEAPON_TYPE::kTwoHandSword: damage *= settings::fStunGreatSwordMult; break;
        default: break;
        }
        break;
    }
    if (source == bash || source == powerBash || source == parry) inlineUtils::offsetRealDamage(damage, aggressor, victim);
    damageStun(aggressor, victim, damage);
}
