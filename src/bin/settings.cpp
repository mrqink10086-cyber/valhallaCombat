#include "include/settings.h"
#include "include/CombatRuntime.h"
#include "include/stunHandler.h"
#include "include/debuffHandler.h"
#include "include/blockHandler.h"
#include "include/OwnedEffects.h"
#include "include/attackHandler.h"
#include "ValhallaCombat.hpp"
#include <unordered_map>

namespace {
void setGameSetting(const char* name, float value)
{
 if (auto collection=RE::GameSettingCollection::GetSingleton()) if (auto setting=collection->GetSetting(name)) setting->data.f=value;
}
std::unordered_map<RE::TESRace*,float> originalRegeneration;
}
void settings::updateGameSettings()
{
 if (auto collection=RE::GameSettingCollection::GetSingleton()) {
  if (auto setting=collection->GetSetting("bVal_TrueHudAPI_Acq")) setting->SetBool(facts::TrueHudAPI_Obtained.load());
  if (auto setting=collection->GetSetting("bVal_TrueHUDAPI_SpecialMeter_Acq")) setting->SetBool(facts::TrueHudAPI_HasSpecialBarControl.load());
 }
 if (auto dataHandler=RE::TESDataHandler::GetSingleton()) {
  if (auto global=dataHandler->LookupForm<RE::TESGlobal>(0x56A25,"ValhallaCombat.esp")) global->value=facts::TrueHudAPI_Obtained?1.0f:0.0f;
  if (auto global=dataHandler->LookupForm<RE::TESGlobal>(0x56A26,"ValhallaCombat.esp")) global->value=facts::TrueHudAPI_HasSpecialBarControl?1.0f:0.0f;
 }
}
void settings::apply(const config::Values& values)
{
 const bool resetBlock=iAltBlockKey!=config::value(values,config::Key::iAltBlockKey) ||
  bTimedBlockToggle!=(config::value(values,config::Key::bTimedBlockToggle)!=0) ||
  bTimedBlockProjectileToggle!=(config::value(values,config::Key::bTimedBlockProjectileToggle)!=0) ||
  bTackleToggle!=(config::value(values,config::Key::bTackleToggle)!=0) ||
  bBlockCommitmentToggle!=(config::value(values,config::Key::bBlockCommitmentToggle)!=0) ||
  uPerfectBlockAccum!=config::value(values,config::Key::uPerfectBlockAccum);
 const bool resetAttacks=bAttackStaminaToggle!=(config::value(values,config::Key::bAttackStaminaToggle)!=0) ||
  bAttackStaminaToggle_NPC!=(config::value(values,config::Key::bAttackStaminaToggle_NPC)!=0);
#define VC_SETTING(type,name,initial,section,page,label,low,high) name=static_cast<type>(config::value(values,config::Key::name));
#include "include/Settings.def"
#undef VC_SETTING
 if (resetBlock) blockHandler::GetSingleton()->reset();
 if (resetAttacks) attackHandler::GetSingleton()->reset();
 if (!bTimedBlockSlowTime) CombatRuntime::get().clearSlowTime();
 setGameSetting("fDamagedStaminaRegenDelay",fStaminaRegenDelay);
 setGameSetting("fCombatStaminaRegenRateMult",fCombatStaminaRegenMult);
 if (auto handler=RE::TESDataHandler::GetSingleton()) for (auto race:handler->GetFormArray<RE::TESRace>()) {
  if (!race) continue;
  const auto [it,inserted]=originalRegeneration.try_emplace(race,race->data.staminaRegen);
  race->data.staminaRegen=std::clamp(it->second*fStaminaRegenMult,fStaminaRegenMin,fStaminaRegenLimit);
 }
 if (bStunMeterToggle && bStunToggle) {
  ValhallaCombat::GetSingleton()->requestTrueHudSpecialBarControl();
  stunHandler::GetSingleton()->refreshMeter(true);
 } else {
  stunHandler::GetSingleton()->refreshMeter(false);
  ValhallaCombat::GetSingleton()->releaseTrueHudSpecialBarControl();
 }
 if (CombatRuntime::get().ready()) { debuffHandler::GetSingleton()->update(0); stunHandler::GetSingleton()->update(0); }
 config::store().effective(!CombatRuntime::get().ready() || stunHandler::GetSingleton()->pendingCleanup() || OwnedEffects::pending());
 updateGameSettings();
}
void settings::readSettings()
{
 config::Values values; std::string error;
 if (config::store().load(values,error)) { logger::info("Settings: {}",config::store().snapshot().message); apply(values); }
 else { config::store().failure(error); logger::error("Settings load failed: {}",error); }
}
void settings::submit(config::Values values, std::uint64_t expected)
{
 CombatRuntime::get().control([values=std::move(values),expected] {
  std::string error;
  if (!config::store().save(values,expected,error)) { config::store().failure(error); logger::error("Settings not applied: {}",error); return; }
  apply(values);
 });
}
void settings::cleanupReceipt()
{
 if (CombatRuntime::get().ready() && !stunHandler::GetSingleton()->pendingCleanup() && !OwnedEffects::pending()) config::store().cleanupFinished();
}
