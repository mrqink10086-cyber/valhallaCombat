#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

inline bool onOwner=false;
inline int started=0,ended=0,loaded=0,reapplied=0;
inline bool sessionActive=false,hooksInstalled=true;
inline std::vector<int> transitions;
inline int globalSetting=77,effectiveSetting=-1;
class CombatRuntime {
public:
 using Task=std::function<void()>;
 static CombatRuntime& get() { static CombatRuntime runtime; return runtime; }
 bool ownerThread() const { return onOwner; }
 void beginSession() {
  if (!onOwner) throw std::runtime_error("session start on worker");
  ++started; sessionActive=true; transitions.push_back(1);
 }
};
namespace logger { void error(const char*) {} }
namespace SKSE {
 struct MessagingInterface {
  enum { kPostPostLoad,kDataLoaded,kPostLoad,kPostLoadGame,kPreLoadGame,kNewGame };
  struct Message { int type; void* data; };
 };
 struct TaskInterface {
  std::vector<CombatRuntime::Task> pending;
  void AddTask(CombatRuntime::Task task) { pending.push_back(std::move(task)); }
  void drain() {
   onOwner=true;
   auto batch=std::move(pending);
   pending.clear();
   for (auto& task:batch) task();
  }
 } tasks;
 TaskInterface* GetTaskInterface() { return &tasks; }
}
namespace config {
 struct Snapshot { int values; };
 struct Store { Snapshot snapshot() const { return {globalSetting}; } };
 Store& store() { static Store instance; return instance; }
}
namespace OwnedEffects { void update() {} }
namespace settings {
 void updateGameSettings() {}
 void apply(int values) { effectiveSetting=values; ++reapplied; }
 void reapplySettings() { apply(config::store().snapshot().values); }
}
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
namespace Menu { void registerMenu() {} }
void onDataLoaded() { check(onOwner,"DataLoaded initialization on worker"); ++loaded; }
void onPostLoad() {}
void endCombatSession() { check(onOwner,"session reset on worker"); ++ended; sessionActive=false; transitions.push_back(0); }
#include "message_functions.inc"
int main() {
 SKSE::MessagingInterface::Message data{SKSE::MessagingInterface::kDataLoaded,nullptr};
 MessageHandler(&data);
 check(loaded==0 && SKSE::tasks.pending.size()==1,"worker DataLoaded must defer initialization");
 SKSE::tasks.drain();
 check(loaded==1,"DataLoaded task did not initialize");

 SKSE::MessagingInterface::Message success{SKSE::MessagingInterface::kPostLoadGame,reinterpret_cast<void*>(std::uintptr_t{1})};
 MessageHandler(&success);
 SKSE::MessagingInterface::Message failure{SKSE::MessagingInterface::kPostLoadGame,nullptr};
 MessageHandler(&failure);
 check(started==0 && ended==0 && SKSE::tasks.pending.size()==2,"owner lifecycle must use the same queue as worker lifecycle");
 SKSE::tasks.drain();
 check(started==1 && ended==1,"load result encoding regression");
 check(reapplied==1 && effectiveSetting==globalSetting,"successful load must restore global settings");

 onOwner=false;
 SKSE::MessagingInterface::Message preload{SKSE::MessagingInterface::kPreLoadGame,nullptr};
 SKSE::MessagingInterface::Message newGame{SKSE::MessagingInterface::kNewGame,nullptr};
 MessageHandler(&preload);
 MessageHandler(&success);
 MessageHandler(&newGame);
 globalSetting=88; effectiveSetting=-1;
 check(started==1 && ended==1 && SKSE::tasks.pending.size()==3,"worker lifecycle ran inline");
 SKSE::tasks.drain();
 check(started==3 && ended==3,"queued lifecycle transitions must all survive session changes");
 check(reapplied==3 && effectiveSetting==88,"new game and another save must restore shared settings");

 transitions.clear();
 onOwner=false;
 MessageHandler(&preload);
 onOwner=true;
 MessageHandler(&success);
 MessageHandler(&newGame);
 check(started==3 && ended==3 && SKSE::tasks.pending.size()==3,"owner lifecycle overtook an earlier queued transition");
 SKSE::tasks.drain();
 check(transitions==std::vector<int>{0,1,0,1} && sessionActive,"mixed-thread lifecycle dispatch must preserve reset/start order");
 hooksInstalled=false; onPostLoadGame();
 check(reapplied==5 && started==5,"disabled hooks must not apply combat settings");
 std::puts("SKSE load encoding and owner-thread lifecycle dispatch: PASS");
}
