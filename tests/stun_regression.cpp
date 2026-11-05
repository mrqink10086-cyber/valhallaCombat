#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>
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
struct Actor {
 bool loaded=true,dead=false,downed=false;
 ActorHandle GetHandle() { return {this}; }
 float GetPermanentActorValue(ActorValue) { return 100; }
 float GetActorValue(ActorValue) { return 50; }
 bool Is3DLoaded() { return loaded; } bool IsInKillMove() { return false; }
 Actor* AsActorState() { return this; } bool IsSwimming() { return false; }
 bool IsInCombat() { return true; } bool IsPlayerRef() { return false; } bool IsPlayerTeammate() { return false; }
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
 settings::bStunToggle=false; hook=[&] { h->reset(); }; h->update(0);
	require(h->getMaxStun(&target)==1 && h->getMaxStun(&other)==1 && OwnedEffects::ownership==0 && TrueHUDUtils::overrides==0);
 std::puts("reentrant damage/reset serialized; unavailable downed graph cleanup retained: PASS");
}
