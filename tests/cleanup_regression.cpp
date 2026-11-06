#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace RE {
using FormID=unsigned;
enum class ActorValue { kStamina };
enum class ACTOR_VALUE_MODIFIER { kTemporary };
struct ActorValueOwner {
 float permanent=100,current=50;
 float GetPermanentActorValue(ActorValue) const { return permanent; }
 float GetActorValue(ActorValue) const { return current; }
};
struct Actor;
struct ActorPtr { Actor* actor; Actor* get() const { return actor; } Actor* operator->() const { return actor; } explicit operator bool() const { return actor!=nullptr; } };
struct ActorHandle { Actor* actor; ActorPtr get() const { return {actor}; } bool operator<(const ActorHandle& other) const { return std::less<Actor*>{}(actor,other.actor); } };
struct Actor {
 bool loaded=false,exhausted=true;
 ActorValueOwner values;
 float temporary=20;
 ActorHandle GetHandle() { return {this}; }
 ActorValueOwner* AsActorValueOwner() { return &values; }
 float GetActorValueModifier(ACTOR_VALUE_MODIFIER,ActorValue) const { return temporary; }
 bool IsPlayerRef() const { return false; }
 FormID GetFormID() { return 1; }
 bool SetGraphVariableBool(const char*,bool value) { if (!loaded) return false; exhausted=value; return true; }
 bool Is3DLoaded() { return loaded; }
 void RemoveSpell(int) {}
};
inline Actor* current=nullptr;
struct TESForm { template<class T> static T* LookupByID(FormID) { return current; } };
namespace Offset { inline float delta=0; inline float* g_deltaTime=&delta; }
}
struct CombatRuntime { static CombatRuntime& get() { static CombatRuntime r; return r; } bool ready() { return true; } float activeDelta() { return 0; } };
struct blockHandler { struct EldenCounterCompatibility { static int triggerSpell() { return 1; } }; };
struct reactionHandler { static bool recoverDownedState(RE::Actor*) { return true; } };
namespace data { inline int debuffPerk=1; }
namespace Utils::Actor { void safeRemovePerk(int,RE::Actor*) {} bool isDead(RE::Actor*) { return false; } }
namespace settings { inline bool bStaminaDebuffToggle=true,bUIAlert=false; }
namespace TrueHUDUtils { void flashActorValue(RE::Actor*,RE::ActorValue) {} }
struct ValhallaCombat { enum { debuffHandler }; static ValhallaCombat* GetSingleton() { static ValhallaCombat v; return &v; } void deactivateUpdate(int) {} };
namespace { std::mutex mutex; std::unordered_map<RE::FormID,unsigned> owned,cleanup; }
namespace OwnedEffects {
enum Flag : unsigned { exhausted=1,perk=2,downed=4,counter=8 };
void add(RE::Actor*,Flag); void remove(RE::Actor*,Flag); void retry(RE::Actor*,Flag); bool pending(); void update();
#include "owned_functions.inc"
}
struct debuffHandler {
 std::shared_mutex mtx_actorInDebuff;
 std::set<RE::ActorHandle> actorInDebuff,ownedPerks,ownedGraphs,ownedUI;
 float flashCountdown=0;
 const char* gv_bExhausted="val_bExhausted";
 void revertDebuffUI(RE::Actor*) {}
 void initDebuffUI(RE::Actor*) {}
 void update(float);
 void stopDebuff(RE::Actor*);
};
#include "debuff_functions.inc"
void require(bool value) { if (!value) throw std::runtime_error("cleanup regression"); }
int main() {
 RE::Actor actor; RE::current=&actor;
 debuffHandler h; h.actorInDebuff.insert(actor.GetHandle()); h.ownedGraphs.insert(actor.GetHandle());
 OwnedEffects::add(&actor,OwnedEffects::exhausted);
 h.stopDebuff(&actor); require(h.actorInDebuff.empty() && OwnedEffects::pending() && actor.exhausted);
 OwnedEffects::update(); require(OwnedEffects::pending() && actor.exhausted);
 actor.loaded=true; OwnedEffects::update(); require(!OwnedEffects::pending() && !actor.exhausted);
 actor.exhausted=true; h.stopDebuff(&actor); require(actor.exhausted);
	OwnedEffects::add(&actor,OwnedEffects::exhausted);
	OwnedEffects::retry(&actor,OwnedEffects::exhausted);
	OwnedEffects::add(&actor,OwnedEffects::exhausted);
	OwnedEffects::update(); require(!OwnedEffects::pending() && actor.exhausted);
 actor.exhausted=true;
 h.actorInDebuff.insert(actor.GetHandle()); h.ownedGraphs.insert(actor.GetHandle());
 OwnedEffects::add(&actor,OwnedEffects::exhausted);
 actor.values.current=119;
 h.update(0); require(!h.actorInDebuff.empty() && actor.exhausted);
 actor.values.current=120;
 h.update(0); require(h.actorInDebuff.empty() && !actor.exhausted);
 std::puts("failed graph cleanup retries on reload; unowned graph preserved: PASS");
}
