#include <cstdio>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_map>
namespace RE {
using FormID=unsigned;
struct Actor;
struct ActorPtr { Actor* actor; Actor* get() const { return actor; } Actor* operator->() const { return actor; } explicit operator bool() const { return actor!=nullptr; } };
struct ActorHandle { Actor* actor; ActorPtr get() const { return {actor}; } bool operator<(const ActorHandle& other) const { return std::less<Actor*>{}(actor,other.actor); } };
struct Actor {
 bool loaded=false,exhausted=true;
 ActorHandle GetHandle() { return {this}; }
 FormID GetFormID() { return 1; }
 bool SetGraphVariableBool(const char*,bool value) { if (!loaded) return false; exhausted=value; return true; }
 bool Is3DLoaded() { return loaded; }
 void RemoveSpell(int) {}
};
inline Actor* current=nullptr;
struct TESForm { template<class T> static T* LookupByID(FormID) { return current; } };
}
struct CombatRuntime { static CombatRuntime& get() { static CombatRuntime r; return r; } bool ready() { return true; } };
struct blockHandler { struct EldenCounterCompatibility { static int triggerSpell() { return 1; } }; };
struct reactionHandler { static bool recoverDownedState(RE::Actor*) { return true; } };
namespace data { inline int debuffPerk=1; }
namespace Utils::Actor { void safeRemovePerk(int,RE::Actor*) {} }
namespace { std::mutex mutex; std::unordered_map<RE::FormID,unsigned> owned,cleanup; }
namespace OwnedEffects {
enum Flag : unsigned { exhausted=1,perk=2,downed=4,counter=8 };
void add(RE::Actor*,Flag); void remove(RE::Actor*,Flag); void retry(RE::Actor*,Flag); bool pending(); void update();
#include "owned_functions.inc"
}
struct debuffHandler {
 std::shared_mutex mtx_actorInDebuff;
 std::set<RE::ActorHandle> actorInDebuff,ownedPerks,ownedGraphs;
 const char* gv_bExhausted="val_bExhausted";
 void revertDebuffUI(RE::Actor*) {}
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
 std::puts("failed graph cleanup retries on reload; unowned graph preserved: PASS");
}
