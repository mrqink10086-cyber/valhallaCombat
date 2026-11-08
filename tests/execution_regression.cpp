#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "include/EnemyRules.h"
struct CombatRuntime {
 std::vector<std::function<void()>> tasks;
 bool active=true;
 static CombatRuntime& get() { static CombatRuntime r; return r; }
 bool ready() { return active; }
 std::uint64_t schedule(float,std::function<void()> callback) { if (!active) return 0; tasks.push_back(std::move(callback)); return tasks.size(); }
 void drain() { auto copy=std::exchange(tasks,{}); for (auto& task:copy) task(); }
};
namespace RE {
struct Actor;
struct ActorPtr { Actor* actor; Actor* get() const { return actor; } Actor* operator->() const { return actor; } explicit operator bool() const { return actor!=nullptr; } };
struct ActorHandle {
 Actor* actor=nullptr; ActorPtr get() const { return {actor}; }
 bool operator==(const ActorHandle&) const=default;
 bool operator<(const ActorHandle& other) const { return std::less<Actor*>{}(actor,other.actor); }
};
struct Position { float x=0,y=0; float GetDistance(Position other) const { return std::hypot(x-other.x,y-other.y); } };
struct Process { bool high=true; bool InHighProcess() { return high; } };
struct TESIdleForm {};
enum class WEAPON_TYPE { kHandToHandMelee,kOneHandSword,kOneHandDagger,kOneHandAxe,kOneHandMace,kTwoHandSword,kTwoHandAxe,kBow,kCrossbow };
struct Weapon { int id=1; WEAPON_TYPE type=WEAPON_TYPE::kOneHandSword; bool melee=true; bool IsMelee() { return melee; } int GetFormID() { return id; } WEAPON_TYPE GetWeaponType() { return type; } };
struct MagicTarget { enum class Archetype { kParalysis }; bool paralyzed=false; bool HasEffectWithArchetype(Archetype) { return paralyzed; } };
enum class ActorValue { kHealth };
struct Values { float health=100; float GetActorValue(ActorValue) { return health; } };
struct Actor {
 bool dead=false,loaded=true,broken=true,player=false,teammate=false,essential=false,protectedActor=false,child=false;
 bool hostile=true,los=true,killmove=false,mounted=false,humanoid=true,dual=false,back=false;
 int race=1; float maximum=100; encounters::Tier tier=encounters::Tier::normal;
 Position pos; Process process; MagicTarget magic; Values values; Weapon weapon; bool armed=true;
 struct Runtime { Process* currentProcess; } runtime{&process};
 template<class T> T* As() { return static_cast<T*>(this); }
 ActorHandle GetHandle() { return {this}; }
 bool Is3DLoaded() { return loaded; } bool IsInKillMove() { return killmove; }
 bool IsOnMount() { return mounted; } bool IsEssential() { return essential; }
 bool IsProtected() { return protectedActor; } bool IsChild() { return child; }
 bool IsPlayerTeammate() { return teammate; } bool IsPlayerRef() { return player; }
 bool IsHostileToActor(Actor*) { return hostile; } bool HasLineOfSight(Actor*,bool&) { return los; }
 float GetHeadingAngle(Position other,bool absolute) { const float angle=std::atan2(other.y-pos.y,other.x-pos.x)*180/3.14159265f; return absolute?std::abs(angle):angle; }
 int GetRace() { return race; } Position GetPosition() { return pos; }
 Runtime& GetActorRuntimeData() { return runtime; }
 MagicTarget* AsMagicTarget() { return &magic; } Values* AsActorValueOwner() { return &values; }
 float GetActorValueMax(ActorValue) { return maximum; }
 int GetFormID() { return 1; } const char* GetName() { return "target"; }
};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton() { static PlayerCharacter p; p.player=true; return &p; } };
struct CrosshairPickData { ActorHandle target; static CrosshairPickData* GetSingleton() { static CrosshairPickData p; return &p; } ActorHandle GetActiveTarget() { return target; } };
namespace BSContainer { enum class ForEachResult { kContinue }; }
struct ProcessLists { std::vector<Actor*> actors; static ProcessLists* GetSingleton() { static ProcessLists p; return &p; } template<class F> void ForEachHighActor(F fn) { for (auto actor:actors) fn(actor); } };
enum class DEFAULT_OBJECT { kActionIdle };
namespace Offset {
inline int plays=0; inline bool accepted=true;
inline std::function<void()> reenter;
bool playPairedIdle(Process*,Actor*,DEFAULT_OBJECT,TESIdleForm*,bool,bool,Actor*) { ++plays; if (reenter) reenter(); return accepted; }
}
}
namespace logger { template<class... T> void debug(T...) {} template<class... T> void warn(T...) {} }
namespace settings {
inline bool bStunToggle=true,bAutoExecution=false,bFollowerAutoExecution=false,bExecutionPrompt=true,bMenuChinese=false;
inline float fExecutionHealthThreshold=.2f,fBossOpeningDamageMult=1.5f;
inline int iExecutionKey=45;
}
namespace encounters {
Classification classify(RE::Actor* actor) { return {actor->tier,Source::fallback,0}; }
std::string describe(const Classification& value) { return tierName(value.tier); }
}
namespace fmt { std::string format(const char*,const char* name,const std::string& tier,const char* reason) { return std::string(name)+tier+reason; } }
namespace Utils::Actor {
bool isDead(RE::Actor* actor) { return actor->dead; }
bool isHumanoid(RE::Actor* actor) { return actor->humanoid; }
bool isDualWielding(RE::Actor* actor) { return actor->dual; }
bool isBackFacing(RE::Actor* victim,RE::Actor*) { return victim->back; }
RE::Weapon* getWieldingWeapon(RE::Actor* actor) { return actor->armed?&actor->weapon:nullptr; }
}
namespace inlineUtils { template<class T> T select_randomly(T begin,T) { return begin; } }
namespace data {
enum Race { Humanoid,Undead,Falmer,Spider,Gargoyle,Giant,Bear,SabreCat,Wolf,Troll,Hagraven,Spriggan,Boar,Riekling,AshHopper,SteamCenturion,DwarvenBallista,ChaurusFlyer,Lurker,Dragon };
inline std::map<int,Race> raceMapping{{1,Humanoid},{2,SteamCenturion}};
inline RE::TESIdleForm idle;
#define POOL(name) inline std::vector<RE::TESIdleForm*> name{&idle};
#define RACE_POOLS(name) POOL(name##_1hm) POOL(name##_2hm) POOL(name##_2hw)
POOL(KM_Humanoid_DW) POOL(KM_Humanoid_2hw_Back) POOL(KM_Humanoid_2hm_Back) POOL(KM_Humanoid_H2H_Back) POOL(KM_Humanoid_1hm_Back)
POOL(KM_Humanoid_Sword) POOL(KM_Humanoid_Dagger) POOL(KM_Humanoid_Axe) POOL(KM_Humanoid_Mace) POOL(KM_Humanoid_2hw) POOL(KM_Humanoid_GreatSword) POOL(KM_Humanoid_H2H)
RACE_POOLS(KM_Undead) RACE_POOLS(KM_Falmer) RACE_POOLS(KM_Spider) RACE_POOLS(KM_Gargoyle) RACE_POOLS(KM_Giant) RACE_POOLS(KM_Bear) RACE_POOLS(KM_SabreCat) RACE_POOLS(KM_Wolf) RACE_POOLS(KM_Troll) RACE_POOLS(KM_Hagraven) RACE_POOLS(KM_Spriggan) RACE_POOLS(KM_Boar) RACE_POOLS(KM_Riekling) RACE_POOLS(KM_AshHopper) RACE_POOLS(KM_Centurion) RACE_POOLS(KM_Ballista) RACE_POOLS(KM_ChaurusFlyer) RACE_POOLS(KM_Lurker) RACE_POOLS(KM_Dragon)
#undef RACE_POOLS
#undef POOL
}
inline int notifications=0;
void notifyExecution(const char*) { ++notifications; }
struct stunHandler { static stunHandler* GetSingleton() { static stunHandler h; return &h; } bool getIsStunBroken(RE::Actor* actor) { return actor->broken; } };
struct executionHandler {
 std::set<RE::ActorHandle> pending; std::uint64_t queued=0;
 std::mutex statusMutex; std::string targetInfo,lastStatus; RE::ActorHandle promptTarget; float promptCountdown=0;
 struct Eligibility { bool allowed; const char* reason; };
 void report(std::string text) { lastStatus=std::move(text); }
 void tryPcExecution(); bool attemptExecute(RE::Actor*,RE::Actor*); bool tryAutomatic(RE::Actor*,RE::Actor*);
 float openingDamageMultiplier(RE::Actor*); void updatePrompt(float);
 Eligibility evaluate(RE::Actor*,RE::Actor*,bool checkPending=true);
 const std::vector<RE::TESIdleForm*>* idlesFor(RE::Actor*,RE::Actor*);
 RE::Actor* selectTarget(RE::Actor*);
 void queueExecutionIdle(RE::Actor*,RE::Actor*,RE::TESIdleForm*,int);
 void queueExecutionIdle(RE::Actor*,RE::Actor*,std::vector<RE::TESIdleForm*>,int);
};
#include "execution_functions.inc"
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
 try {
 executionHandler h; auto player=RE::PlayerCharacter::GetSingleton(); RE::Actor behind,near,front,follower;
 auto& runtime=CombatRuntime::get(); behind.pos.x=-10; near.pos={10,80}; front.pos.x=100;
 auto& actors=RE::ProcessLists::GetSingleton()->actors; actors={&behind,&near,&front};
 require(h.selectTarget(player)==&front,"manual target must be in front");
 near.pos={40,20}; RE::CrosshairPickData::GetSingleton()->target={&near};
 require(h.selectTarget(player)==&near,"eligible crosshair wins");
 near.essential=true; require(h.selectTarget(player)==&front,"invalid crosshair falls back");
 front.broken=false; require(!h.evaluate(player,&front).allowed,"healthy normal needs stun");
 front.values.health=20; require(h.evaluate(player,&front).allowed,"normal low HP works without stun");
 front.tier=encounters::Tier::boss; front.broken=true; front.values.health=100;
 require(!h.evaluate(player,&front).allowed,"broken high HP boss protected");
 require(h.openingDamageMultiplier(&front)==1.5f,"boss break grants damage opening");
 front.tier=encounters::Tier::elite; require(!h.evaluate(player,&front).allowed,"high HP elite protected");
 front.values.health=20; require(h.evaluate(player,&front).allowed,"elite low HP can finish");
 front.tier=encounters::Tier::normal; require(h.openingDamageMultiplier(&front)==1,"normal no bonus");
 front.essential=true; require(!h.evaluate(player,&front).allowed,"essential protected"); front.essential=false;
 front.los=false; player->los=false; require(!h.evaluate(player,&front).allowed,"wall blocks execution"); player->los=true;
 front.race=99; require(!h.evaluate(player,&front).allowed,"unknown race has no prompt"); front.race=1;
 player->weapon.melee=false; require(!h.evaluate(player,&front).allowed,"non melee weapon unsupported"); player->weapon.melee=true;
 data::KM_Humanoid_Sword={nullptr}; require(!h.evaluate(player,&front).allowed,"empty idle pool has no prompt"); data::KM_Humanoid_Sword={&data::idle};
 near.essential=false; near.tier=encounters::Tier::boss;
 h.updatePrompt(.25f);
 require(h.targetInfo.find("Normal")!=std::string::npos && h.targetInfo.find("Boss")==std::string::npos,"diagnostic follows eligible fallback instead of ineligible crosshair");
 near.essential=true;
 follower.teammate=true; require(!h.tryAutomatic(&follower,&front),"followers default to manual permission");
 require(!h.tryAutomatic(player,&front),"player auto disabled");
 settings::bFollowerAutoExecution=true; require(h.tryAutomatic(&follower,&front),"follower option works"); runtime.drain(); runtime.drain();
 RE::Offset::plays=0; h.pending.clear(); settings::bFollowerAutoExecution=false;
 h.tryPcExecution(); require(h.queued==2 && h.pending.contains(front.GetHandle()),"manual queues selected target");
 h.tryPcExecution(); require(h.queued==2,"duplicate suppressed");
 front.dead=true; runtime.drain(); require(RE::Offset::plays==0 && h.pending.empty(),"stale dead target cancelled");
 front.dead=false; h.tryPcExecution(); front.loaded=false; runtime.drain(); require(RE::Offset::plays==0,"unloaded target cancelled");
 front.loaded=true; h.tryPcExecution(); front.tier=encounters::Tier::boss; front.values.health=100;
 runtime.drain(); require(RE::Offset::plays==0,"queued boss threshold revalidated");
 front.values.health=20; h.tryPcExecution(); const auto queued=h.queued;
 RE::Offset::reenter=[&h] { h.tryPcExecution(); }; runtime.drain();
 require(RE::Offset::plays==1 && h.queued==queued && h.pending.size()==2,"playback reentry deduplicated");
 runtime.drain(); RE::Offset::reenter={}; require(h.pending.empty(),"pending released");
 RE::CrosshairPickData::GetSingleton()->target={&front};
 h.updatePrompt(.25f); const int before=notifications; h.updatePrompt(.25f);
 require(notifications==before && h.targetInfo.find("Boss")!=std::string::npos,"prompt stable and classification visible");
 front.values.health=100; h.updatePrompt(.25f); front.values.health=20; h.updatePrompt(.25f);
 require(notifications==before+1,"new eligibility prompts again");
 std::puts("shared eligibility, boss protection, targeting, followers, prompt and queued revalidation: PASS"); return 0;
 } catch (const std::exception& error) { std::fprintf(stderr,"FAIL %s\n",error.what()); return 1; }
}
