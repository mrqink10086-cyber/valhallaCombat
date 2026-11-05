#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
struct CombatRuntime {
 std::vector<std::function<void()>> tasks;
 static CombatRuntime& get() { static CombatRuntime r; return r; }
 bool ready() { return true; }
 std::uint64_t schedule(float,std::function<void()> callback) { tasks.push_back(std::move(callback)); return tasks.size(); }
 void drain() { auto copy=std::exchange(tasks,{}); for (auto& task:copy) task(); }
};
namespace RE {
struct Actor;
struct ActorPtr { Actor* actor; Actor* get() const { return actor; } Actor* operator->() const { return actor; } explicit operator bool() const { return actor!=nullptr; } };
struct ActorHandle { Actor* actor; ActorPtr get() const { return {actor}; } explicit operator bool() const { return actor!=nullptr; } bool operator<(const ActorHandle& other) const { return std::less<Actor*>{}(actor,other.actor); } };
struct Position { float x=0; float GetDistance(Position other) const { return std::abs(x-other.x); } };
struct Process { bool InHighProcess() { return true; } };
struct TESIdleForm {};
struct Weapon { int GetFormID() { return 1; } };
struct MagicTarget { enum class Archetype { kParalysis }; bool HasEffectWithArchetype(Archetype) { return false; } };
struct Actor {
 bool dead=false,loaded=true,eligible=true,broken=true;
 Position pos;
 Process process;
 struct Runtime { Process* currentProcess; } runtime{&process};
 ActorHandle GetHandle() { return {this}; }
 bool Is3DLoaded() { return loaded; } bool IsInKillMove() { return false; }
 bool IsOnMount() { return false; } bool IsEssential() { return false; }
 bool IsPlayerTeammate() { return false; } bool IsPlayerRef() { return false; }
 Actor* GetRace() { return this; } Position GetPosition() { return pos; }
 Runtime& GetActorRuntimeData() { return runtime; }
 MagicTarget* AsMagicTarget() { static MagicTarget magic; return &magic; }
 int GetFormID() { return 1; }
};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton() { static PlayerCharacter player; return &player; } };
enum class DEFAULT_OBJECT { kActionIdle };
namespace Offset {
inline int plays=0;
inline std::function<void()> reenter;
bool playPairedIdle(Process*,Actor*,DEFAULT_OBJECT,TESIdleForm*,bool,bool,Actor*) { ++plays; if (reenter) reenter(); return true; }
}
}
namespace logger { template<class... T> void debug(T...) {} template<class... T> void warn(T...) {} }
namespace settings { inline bool bStunToggle=true; }
namespace Utils::Actor { bool isDead(RE::Actor* actor) { return actor->dead; } RE::Weapon* getWieldingWeapon(RE::Actor*) { return nullptr; } }
namespace inlineUtils { template<class T> T select_randomly(T begin,T) { return begin; } }
struct stunHandler {
 std::vector<RE::ActorHandle> candidates;
 static stunHandler* GetSingleton() { static stunHandler h; return &h; }
 auto getStunBrokenActors() { return candidates; }
 bool getIsStunBroken(RE::Actor* actor) { return actor->broken; }
};
struct executionHandler {
 std::set<RE::ActorHandle> pending;
 std::uint64_t queued=0;
 int attempts=0;
 void report(const std::string&) {}
 void tryPcExecution();
 bool attemptExecute(RE::Actor* actor,RE::Actor* target) { ++attempts; if (!target->eligible) return false; static RE::TESIdleForm idle; const auto before=queued; queueExecutionIdle(actor,target,&idle,1); return queued!=before; }
 void queueExecutionIdle(RE::Actor*,RE::Actor*,RE::TESIdleForm*,int);
 void queueExecutionIdle(RE::Actor*,RE::Actor*,std::vector<RE::TESIdleForm*>,int);
};
#include "execution_functions.inc"
void require(bool value) { if (!value) throw std::runtime_error("execution regression"); }
int main() {
 executionHandler h; RE::Actor dead,near,valid; dead.dead=true; near.eligible=false; near.pos.x=10; valid.pos.x=100;
 auto& runtime=CombatRuntime::get();
 stunHandler::GetSingleton()->candidates={{&dead},{&near},{&valid}};
 h.tryPcExecution(); require(h.queued==1 && h.attempts==2);
 h.tryPcExecution(); require(h.queued==1);
 valid.dead=true; runtime.drain(); require(RE::Offset::plays==0 && h.pending.empty());
 valid.dead=false; h.tryPcExecution(); valid.loaded=false; runtime.drain(); require(RE::Offset::plays==0);
 valid.loaded=true; h.tryPcExecution(); RE::PlayerCharacter::GetSingleton()->runtime.currentProcess=nullptr; runtime.drain(); require(RE::Offset::plays==0);
 RE::PlayerCharacter::GetSingleton()->runtime.currentProcess=&RE::PlayerCharacter::GetSingleton()->process;
 h.tryPcExecution(); const auto queued=h.queued;
 RE::Offset::reenter=[&h] { h.tryPcExecution(); }; runtime.drain();
 require(RE::Offset::plays==1 && h.queued==queued && h.pending.size()==2);
 runtime.drain(); require(h.pending.empty());
 h.queueExecutionIdle(RE::PlayerCharacter::GetSingleton(),&valid,std::vector<RE::TESIdleForm*>{},1);
 require(h.queued==queued);
 std::puts("candidate fallback, empty idles, stale playback, dedup including reentry: PASS");
}
