#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
namespace RE {
struct Actor;
struct ActorPtr { Actor* actor; Actor* get() const { return actor; } Actor* operator->() const { return actor; } explicit operator bool() const { return actor!=nullptr; } };
struct ActorHandle {
 Actor* actor;
 ActorPtr get() const { return {actor}; }
 bool operator<(const ActorHandle& other) const { return std::less<Actor*>{}(actor,other.actor); }
};
struct Actor {
 bool spell=false;
 bool HasSpell(int) { return spell; }
 bool AddSpell(int) { spell=true; return true; }
 bool RemoveSpell(int) { spell=false; return true; }
 ActorHandle GetHandle() { return {this}; }
 Actor* get() { return this; }
};
}
struct CombatRuntime {
 std::vector<std::function<void()>> tasks;
 static CombatRuntime& get() { static CombatRuntime runtime; return runtime; }
 bool ready() { return true; }
 void schedule(float,std::function<void()> task) { tasks.push_back(std::move(task)); }
};
namespace OwnedEffects { enum Flag { counter }; void add(RE::Actor*,Flag) {} void remove(RE::Actor*,Flag) {} }
struct blockHandler {
 struct EldenCounterCompatibility {
  static inline int ec_triggerSpell=1;
  static inline float ec_Time=1;
  static inline std::uint64_t generation=0;
  static inline std::map<RE::ActorHandle,std::uint64_t> ownedActors;
  static void triggerCounter(RE::Actor*);
  static void async_removeECTriggerSpell(RE::Actor*);
  static void reset();
 };
};
#include "counter_functions.inc"
void require(bool value) { if (!value) throw std::runtime_error("counter generation regression"); }
int main() {
 using Counter=blockHandler::EldenCounterCompatibility;
 auto& tasks=CombatRuntime::get().tasks;
 RE::Actor actor;
 Counter::triggerCounter(&actor); require(actor.spell && tasks.size()==1);
 actor.spell=false;
 Counter::triggerCounter(&actor); require(actor.spell && tasks.size()==2);
 tasks[0](); require(actor.spell);
 tasks[1](); require(!actor.spell);
 Counter::triggerCounter(&actor); Counter::reset(); require(!actor.spell);
 std::puts("old counter expiry preserves fresh instance; reset removes owned spell: PASS");
}
