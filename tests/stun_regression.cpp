#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> trackAllocations{false};
std::atomic<std::size_t> allocationCount{0};
}

void* operator new(std::size_t size)
{
 if (trackAllocations.load(std::memory_order_relaxed)) allocationCount.fetch_add(1, std::memory_order_relaxed);
 if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
 throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete[](memory); }
struct CombatRuntime {
 std::vector<std::function<void()>> tasks;
 static CombatRuntime& get() { static CombatRuntime r; return r; }
 bool ready() { return true; } float activeDelta() { return 0; }
 int schedule(float,std::function<void()> task) { tasks.push_back(std::move(task)); return 1; }
 void drain() { auto copy=std::exchange(tasks,{}); for (auto& task:copy) task(); }
};
namespace RE {
enum class ActorValue { kHealth,kStamina,kBlock };
enum class WEAPON_TYPE { kHandToHandMelee,kOneHandDagger,kOneHandSword,kOneHandAxe,kOneHandMace,kTwoHandAxe,kTwoHandSword };
struct TESObjectWEAP { int GetFormID() { return 1; } WEAPON_TYPE GetWeaponType() { return WEAPON_TYPE::kOneHandSword; } };
struct TESForm { template<class T> static T* LookupByID(int) { return nullptr; } };
struct Actor;
struct ActorPtr { Actor* actor; Actor* get() const { return actor; } Actor* operator->() const { return actor; } explicit operator bool() const { return actor!=nullptr; } };
struct ActorHandle { Actor* actor; ActorPtr get() const { return {actor}; } bool operator==(const ActorHandle&) const=default; };
struct ActorValueOwner {
 float health=100,stamina=100,block=50;
 float GetPermanentActorValue(ActorValue value) const { return value==ActorValue::kHealth?health:stamina; }
 float GetActorValue(ActorValue value) const { return value==ActorValue::kBlock?block:stamina; }
};
struct Actor {
 bool loaded=true,dead=false,downed=false,inCombat=true;
 ActorValueOwner values;
 ActorHandle GetHandle() { return {this}; }
 ActorValueOwner* AsActorValueOwner() { return &values; }
 bool Is3DLoaded() { return loaded; } bool IsInKillMove() { return false; }
 Actor* AsActorState() { return this; } bool IsSwimming() { return false; }
 bool IsInCombat() { return inCombat; } bool IsPlayerRef() { return false; } bool IsPlayerTeammate() { return false; }
 void GetGraphVariableBool(const char*,bool& value) { value=downed; }
};
namespace Offset { inline float dt=0; inline float* g_deltaTime=&dt; }
}
template<> struct std::hash<RE::ActorHandle> { std::size_t operator()(RE::ActorHandle handle) const { return std::hash<RE::Actor*>{}(handle.actor); } };
namespace settings {
#define VC_SETTING(type,name,initial,...) inline type name=initial;
#include "include/Settings.def"
#undef VC_SETTING
}
inline std::function<void()> hook;
void callback() { auto invoke=std::exchange(hook,{}); if (invoke) invoke(); }
namespace data { enum { Humanoid,Undead }; inline int soundStunBreak=0; bool isRaceType(RE::Actor*,int) { return true; } }
namespace OwnedEffects { enum Flag { downed }; inline int ownership=0; bool contains(RE::Actor*,Flag) { return false; } void add(RE::Actor*,Flag) { ++ownership; } void remove(RE::Actor*,Flag) { --ownership; } }
namespace Utils::Actor { bool isDead(RE::Actor* actor) { return actor->dead; } }
namespace inlineUtils { void offsetRealDamage(float&,RE::Actor*,RE::Actor*) {} }
namespace ValhallaUtils { void playSound(RE::Actor*,int) {} }
namespace TrueHUDUtils {
inline int overrides=0;
void revertSpecialMeter(RE::Actor*) { --overrides; callback(); }
void flashActorValue(RE::Actor*,RE::ActorValue) { callback(); }
bool greyOutSpecialMeter(RE::Actor*) { ++overrides; callback(); return true; }
}
struct reactionHandler {
 static bool recoverDownedState(RE::Actor* actor) { if (!actor->loaded) return false; actor->downed=false; callback(); return true; }
 static bool triggerDownedState(RE::Actor* actor) { actor->downed=true; callback(); return true; }
};
struct ValhallaCombat { enum { stunHandler }; static ValhallaCombat* GetSingleton() { static ValhallaCombat v; return &v; } void activateUpdate(int) {} void deactivateUpdate(int) {} };
#include "include/stunHandler.h"
#include "stun_functions.inc"
void require(bool value) { if (!value) throw std::runtime_error("stun regression"); }
int main() {
 auto h=stunHandler::GetSingleton(); RE::Actor attacker,target,other;
 target.values.health=800;
 target.values.stamina=200;
 h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&target,1);
 require(h->getMaxStun(&target)==500 && h->getCurrentStun(&target)==499 && !h->getIsStunBroken(&target));
 h->reset();
 attacker.values.block=80;
 for (const auto [source,multiplier] : {
     std::pair{stunHandler::bash,settings::fStunBashMult},
     std::pair{stunHandler::powerBash,settings::fStunPowerBashMult},
     std::pair{stunHandler::parry,settings::fStunParryMult}}) {
  h->processStunDamage(source,nullptr,&attacker,&target,1);
  require(std::abs(h->getCurrentStun(&target)-(500-80*multiplier))<.0001f);
  require(!h->getIsStunBroken(&target));
  h->reset();
 }
 target.values={};
 attacker.values={};
 settings::bDownedStateToggle=true;
 hook=[&] { h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&other,5); };
 h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&target,100);
 require(h->getIsStunBroken(&target) && target.downed && h->getMaxStun(&other)==1);
 CombatRuntime::get().drain(); require(h->getMaxStun(&other)==100 && h->getCurrentStun(&other)==95);
	require(TrueHUDUtils::overrides==1);
	h->refreshMeter(false); require(TrueHUDUtils::overrides==0);
	h->refreshMeter(true); require(TrueHUDUtils::overrides==1);
 settings::bDownedStateToggle=false; target.loaded=false; h->update(0); require(h->pendingCleanup() && target.downed);
 target.loaded=true; h->update(0); require(!h->pendingCleanup() && !target.downed);
	h->reset();
	settings::bDownedStateToggle=true;
	settings::bStunToggle=true;
	h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&target,100);
	settings::bStunToggle=false;
	target.loaded=false;
	h->update(0);
	require(h->pendingCleanup() && h->getMaxStun(&target)==1 && target.downed);
	target.loaded=true;
	h->update(0);
	require(!h->pendingCleanup() && h->getMaxStun(&target)==1 && !target.downed);
	settings::bStunToggle=true;
	h->reset();
	h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&target,100);
 settings::bStunToggle=false; hook=[&] { h->reset(); }; h->update(0);
	require(h->getMaxStun(&target)==1 && h->getMaxStun(&other)==1 && OwnedEffects::ownership==0 && TrueHUDUtils::overrides==0);
	settings::bStunToggle=true;
	h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&target,100);
	h->update(3.5f); require(std::abs(h->getCurrentStun(&target)-100.f/14)<.0001f);
	h->update(6.5f); require(h->getMaxStun(&target)==1);

 // Existing-state modifications should update one published entry without allocating a replacement map.
 std::vector<RE::Actor> population(32);
 for (auto& actor : population) h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&actor,1);
 allocationCount.store(0, std::memory_order_relaxed);
 trackAllocations.store(true, std::memory_order_relaxed);
 h->modStun(&population.front(), 0.25f);
 trackAllocations.store(false, std::memory_order_relaxed);
 const auto updateAllocations = allocationCount.load(std::memory_order_relaxed);
 std::printf("steady modStun allocations=%zu\n", updateAllocations);

 // Warm reusable batch storage, then measure a frame that changes one published actor.
 population.front().inCombat = false;
 for (auto& actor : population) h->modStun(&actor, -49.0f);
 h->update(3.1f);
 h->update(0.1f);
 allocationCount.store(0, std::memory_order_relaxed);
 trackAllocations.store(true, std::memory_order_relaxed);
 h->update(0.1f);
 trackAllocations.store(false, std::memory_order_relaxed);
 const auto updateFrameAllocations = allocationCount.load(std::memory_order_relaxed);
 std::printf("steady one-entry update allocations=%zu\n", updateFrameAllocations);
 std::fflush(stdout);

 // Snapshot readers may run concurrently with owner-thread updates, reset, and cleanup publication.
 std::atomic<bool> stopReaders{false};
 std::atomic<bool> invalidSnapshot{false};
 std::vector<std::thread> readers;
 for (int i = 0; i < 4; ++i) {
  readers.emplace_back([&] {
   while (!stopReaders.load(std::memory_order_relaxed)) {
    const float current = h->getCurrentStun(&population.front());
    const float maximum = h->getMaxStun(&population.front());
    (void)h->getIsStunBroken(&population.front());
    if (!std::isfinite(current) || current < 0 || current > 100 ||
        !std::isfinite(maximum) || (maximum != 1 && maximum != 100)) {
     invalidSnapshot.store(true, std::memory_order_relaxed);
    }
   }
  });
 }
 for (int i = 0; i < 10000; ++i) h->modStun(&population.front(), (i & 1) ? 1.0f : -1.0f);
 h->reset();
 require(h->getCurrentStun(&population.front()) == 1 && h->getMaxStun(&population.front()) == 1 &&
         !h->getIsStunBroken(&population.front()));
 settings::bDownedStateToggle = false;
 h->processStunDamage(stunHandler::powerAttack,nullptr,&attacker,&target,100);
 settings::bStunToggle = false;
 h->update(0);
 require(h->getCurrentStun(&target) == 1 && h->getMaxStun(&target) == 1 && !h->getIsStunBroken(&target));
 stopReaders.store(true, std::memory_order_relaxed);
 for (auto& reader : readers) reader.join();
 require(!invalidSnapshot.load(std::memory_order_relaxed));
 require(updateAllocations == 0);
 require(updateFrameAllocations == 0);
 std::puts("reentrant damage/reset serialized; unavailable downed graph cleanup retained: PASS");
}
