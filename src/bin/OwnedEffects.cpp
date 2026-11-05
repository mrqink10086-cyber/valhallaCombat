#include "include/OwnedEffects.h"
#include "include/CombatRuntime.h"
#include "include/blockHandler.h"
#include "include/debuffHandler.h"
#include "include/reactionHandler.h"
#include "include/data.h"
#include "include/Utils.h"
#include <unordered_map>

namespace {
constexpr std::uint32_t record=0x4F574E44, plugin=0x564C484C;
std::mutex mutex;
std::unordered_map<RE::FormID,std::uint32_t> owned;
std::unordered_map<RE::FormID,std::uint32_t> cleanup;
void save(SKSE::SerializationInterface* serialization)
{
 std::lock_guard lock(mutex);
 if (!serialization->OpenRecord(record,1)) { logger::error("Cannot save owned-effect record"); return; }
 auto combined=owned; for (const auto& [id,flags]:cleanup) combined[id]|=flags;
 const auto count=static_cast<std::uint32_t>(combined.size());
 if (!serialization->WriteRecordData(count)) return;
 for (const auto& [id,flags]:combined) if (!serialization->WriteRecordData(id) || !serialization->WriteRecordData(flags)) { logger::error("Cannot save owned effect {}",id); break; }
}
void load(SKSE::SerializationInterface* serialization)
{
 std::lock_guard lock(mutex); owned.clear(); cleanup.clear();
 std::uint32_t type=0,version=0,length=0;
 while (serialization->GetNextRecordInfo(type,version,length)) {
  if (type!=record || version!=1) continue;
  std::uint32_t count=0;
  if (length<4 || !serialization->ReadRecordData(count) || count>(length-4)/8) { logger::error("Invalid owned-effect record"); continue; }
  for (std::uint32_t i=0;i<count;++i) {
   RE::FormID old=0,id=0; std::uint32_t flags=0;
   if (!serialization->ReadRecordData(old) || !serialization->ReadRecordData(flags)) break;
   if (serialization->ResolveFormID(old,id) && (flags&15)) cleanup[id]|=flags&15;
  }
 }
}
void revert(SKSE::SerializationInterface*) { std::lock_guard lock(mutex); owned.clear(); cleanup.clear(); }
}
namespace OwnedEffects {
void initialize()
{
 if (auto serialization=SKSE::GetSerializationInterface()) { serialization->SetUniqueID(plugin); serialization->SetSaveCallback(save); serialization->SetLoadCallback(load); serialization->SetRevertCallback(revert); }
}
void add(RE::Actor* actor, Flag effect)
{
 if (!actor) return;
 std::lock_guard lock(mutex);
 const auto id=actor->GetFormID();
 owned[id]|=effect;
 if (auto it=cleanup.find(id);it!=cleanup.end()) { it->second&=~effect; if (!it->second) cleanup.erase(it); }
}
bool contains(RE::Actor* actor, Flag effect)
{
 if (!actor) return false;
 std::lock_guard lock(mutex);
 const auto id=actor->GetFormID();
 const auto active=owned.find(id), pending=cleanup.find(id);
 return (active!=owned.end() && (active->second&effect)) || (pending!=cleanup.end() && (pending->second&effect));
}
void remove(RE::Actor* actor, Flag effect)
{
 if (!actor) return; std::lock_guard lock(mutex);
 const auto id=actor->GetFormID();
 if (auto it=owned.find(id);it!=owned.end()) { it->second&=~effect; if (!it->second) owned.erase(it); }
 if (auto it=cleanup.find(id);it!=cleanup.end()) { it->second&=~effect; if (!it->second) cleanup.erase(it); }
}
void restore() { std::lock_guard lock(mutex); for (const auto& [id,flags]:owned) cleanup[id]|=flags; owned.clear(); }
void retry(RE::Actor* actor, Flag effect)
{
 if (!actor) return;
 std::lock_guard lock(mutex);
 const auto id=actor->GetFormID();
 cleanup[id]|=effect;
 if (auto it=owned.find(id);it!=owned.end()) { it->second&=~effect; if (!it->second) owned.erase(it); }
}
bool pending() { std::lock_guard lock(mutex); return !cleanup.empty(); }
void update()
{
 if (!CombatRuntime::get().ready()) return;
 std::unordered_map<RE::FormID,std::uint32_t> copy;
 { std::lock_guard lock(mutex); copy=cleanup; }
 for (const auto& [id,flags]:copy) {
  auto raw=RE::TESForm::LookupByID<RE::Actor>(id); if (!raw) continue;
  auto actor=raw->GetHandle().get(); if (!actor) continue;
  if ((flags&exhausted) && actor->Is3DLoaded() && actor->SetGraphVariableBool("val_bExhausted",false)) remove(actor.get(),exhausted);
  if ((flags&perk) && data::debuffPerk) { Utils::Actor::safeRemovePerk(data::debuffPerk,actor.get()); remove(actor.get(),perk); }
  if (auto spell=blockHandler::EldenCounterCompatibility::triggerSpell(); (flags&counter) && spell) { actor->RemoveSpell(spell); remove(actor.get(),counter); }
  if ((flags&downed) && reactionHandler::recoverDownedState(actor.get())) remove(actor.get(),downed);
 }
}
}
