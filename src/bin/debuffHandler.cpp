#include "include/debuffHandler.h"
#include "include/data.h"
#include "include/settings.h"
#include "include/offsets.h"
#include "include/Utils.h"
#include "ValhallaCombat.hpp"
#include "include/OwnedEffects.h"

void debuffHandler::update(float delta)
{
    std::vector<RE::ActorHandle> handles;
    { std::shared_lock lock(mtx_actorInDebuff); handles.assign(actorInDebuff.begin(), actorInDebuff.end()); }
    if (delta < 0) delta = std::max(0.0f, *RE::Offset::g_deltaTime);
    flashCountdown -= delta==0?0:CombatRuntime::get().activeDelta();
    const bool flash = flashCountdown <= 0;
    if (flash) flashCountdown = 0.5f;
    for (const auto handle : handles) {
        auto actor = handle.get();
        if (!actor) { std::unique_lock lock(mtx_actorInDebuff); actorInDebuff.erase(handle); ownedPerks.erase(handle); ownedUI.erase(handle); continue; }
        const float maximum = actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kStamina) +
            actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kStamina);
        if (!settings::bStaminaDebuffToggle || Utils::Actor::isDead(actor.get()) || actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina) >= maximum) {
            stopDebuff(actor.get()); continue;
        }
        if (settings::bUIAlert && !ownedUI.contains(handle)) initDebuffUI(actor.get());
        else if (!settings::bUIAlert && ownedUI.contains(handle)) revertDebuffUI(actor.get());
        if (flash && settings::bUIAlert && actor->IsPlayerRef()) TrueHUDUtils::flashActorValue(actor.get(), RE::ActorValue::kStamina);
    }
    { std::shared_lock lock(mtx_actorInDebuff);
      if (actorInDebuff.empty()) ValhallaCombat::GetSingleton()->deactivateUpdate(ValhallaCombat::debuffHandler); }
}
void debuffHandler::initStaminaDebuff(RE::Actor* actor)
{
    if (!actor || !CombatRuntime::get().ready() || !settings::bStaminaDebuffToggle) return;
    const auto handle = actor->GetHandle();
    { std::unique_lock lock(mtx_actorInDebuff); if (!actorInDebuff.insert(handle).second) return; }
    if (actor->IsPlayerRef() && data::debuffPerk && (!actor->HasPerk(data::debuffPerk) || OwnedEffects::contains(actor,OwnedEffects::perk))) {
        if (!actor->HasPerk(data::debuffPerk)) Utils::Actor::safeApplyPerk(data::debuffPerk, actor);
        if (actor->HasPerk(data::debuffPerk)) { ownedPerks.insert(handle); OwnedEffects::add(actor,OwnedEffects::perk); }
    }
    bool exhausted=false; actor->GetGraphVariableBool(gv_bExhausted,exhausted);
    if ((exhausted && OwnedEffects::contains(actor,OwnedEffects::exhausted)) ||
        (!exhausted && actor->SetGraphVariableBool(gv_bExhausted, true))) { ownedGraphs.insert(handle); OwnedEffects::add(actor,OwnedEffects::exhausted); }
    if (settings::bUIAlert) initDebuffUI(actor);
    ValhallaCombat::GetSingleton()->activateUpdate(ValhallaCombat::debuffHandler);
}
void debuffHandler::initDebuffUI(RE::Actor* actor)
{
    if (!actor) return;
    TrueHUDUtils::greyoutAvMeter(actor, RE::ActorValue::kStamina);
    ownedUI.insert(actor->GetHandle());
}
void debuffHandler::revertDebuffUI(RE::Actor* actor)
{
    if (actor && ownedUI.erase(actor->GetHandle())) TrueHUDUtils::revertAvMeter(actor, RE::ActorValue::kStamina);
}
void debuffHandler::stopDebuff(RE::Actor* actor)
{
    if (!actor) return;
    const auto handle = actor->GetHandle();
    { std::unique_lock lock(mtx_actorInDebuff); actorInDebuff.erase(handle); }
    revertDebuffUI(actor);
    if (ownedPerks.erase(handle)) { Utils::Actor::safeRemovePerk(data::debuffPerk, actor); OwnedEffects::remove(actor,OwnedEffects::perk); }
    if (ownedGraphs.erase(handle)) {
        if (actor->SetGraphVariableBool(gv_bExhausted, false)) OwnedEffects::remove(actor,OwnedEffects::exhausted);
        else OwnedEffects::retry(actor,OwnedEffects::exhausted);
    }
}
void debuffHandler::reset()
{
    std::vector<RE::ActorHandle> handles;
    { std::shared_lock lock(mtx_actorInDebuff); handles.assign(actorInDebuff.begin(), actorInDebuff.end()); }
    for (const auto handle : handles) { auto actor = handle.get(); if (actor) stopDebuff(actor.get()); }
    { std::unique_lock lock(mtx_actorInDebuff); actorInDebuff.clear(); }
    ownedPerks.clear(); ownedUI.clear(); ownedGraphs.clear(); flashCountdown = 0;
}
bool debuffHandler::isInDebuff(RE::Actor* actor)
{
    if (!actor) return false;
    const auto handle = actor->GetHandle();
    std::shared_lock lock(mtx_actorInDebuff);
    return actorInDebuff.contains(handle);
}
