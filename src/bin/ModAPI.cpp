#include "include/ModAPI.h"
#include "include/debuffHandler.h"
#include "include/stunHandler.h"
#include "include/blockHandler.h"
#include "include/CombatRuntime.h"
void ModAPI::VALInterface::processStunDamage(VAL_API::STUNSOURCE source,RE::TESObjectWEAP* weapon,RE::Actor* aggressor,RE::Actor* victim,float damage) noexcept
{
 try {
  if (!aggressor || !victim || !std::isfinite(damage)) return;
  if (CombatRuntime::get().ready()) { stunHandler::GetSingleton()->processStunDamage(static_cast<stunHandler::STUNSOURCE>(source),weapon,aggressor,victim,damage); return; }
  const auto a=aggressor->GetHandle(),b=victim->GetHandle(); const auto weaponID=weapon?weapon->GetFormID():0;
  CombatRuntime::get().schedule(0,[a,b,weaponID,source,damage] {
   if (!CombatRuntime::get().ready()) return;
   auto attacker=a.get(),target=b.get(); auto equipped=weaponID?RE::TESForm::LookupByID<RE::TESObjectWEAP>(weaponID):nullptr;
   if (attacker && target && (!weaponID || equipped)) stunHandler::GetSingleton()->processStunDamage(static_cast<stunHandler::STUNSOURCE>(source),equipped,attacker.get(),target.get(),damage);
  });
 } catch (...) { logger::error("processStunDamage API request failed"); }
}
bool ModAPI::VALInterface::getIsPCTimedBlocking() noexcept { try { return blockHandler::GetSingleton()->getIsPcTimedBlocking(); } catch (...) { return false; } }
bool ModAPI::VALInterface::getIsPCPerfectBlocking() noexcept { try { return blockHandler::GetSingleton()->getIsPcPerfectBlocking(); } catch (...) { return false; } }
void ModAPI::VALInterface::triggerPcTimedBlockSuccess() noexcept
{
 try {
  if (CombatRuntime::get().ready()) blockHandler::GetSingleton()->OnPcSuccessfulTimedBlock();
  else CombatRuntime::get().schedule(0,[] { if (CombatRuntime::get().ready()) blockHandler::GetSingleton()->OnPcSuccessfulTimedBlock(); });
 } catch (...) { logger::error("Timed block API request failed"); }
}
bool ModAPI::VALInterface::isActorStunned(RE::Actor* actor) noexcept { try { return actor && stunHandler::GetSingleton()->getIsStunBroken(actor); } catch (...) { return false; } }
bool ModAPI::VALInterface::isActorExhausted(RE::Actor* actor) noexcept { try { return actor && debuffHandler::GetSingleton()->isInDebuff(actor); } catch (...) { return false; } }
