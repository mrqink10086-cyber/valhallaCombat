#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace RE {
enum class ActorValue { kStamina };
enum class FormType { ActorCharacter };
enum class ATTACK_STATE_ENUM { kNone, kSwing };
struct Actor {
 float stamina=100;
 Actor* AsActorValueOwner() { return this; }
 float GetActorValue(ActorValue) { return stamina; }
 bool IsPlayerRef() { return true; }
 bool Is3DLoaded() { return true; }
 bool IsBlocking() { return true; }
 FormType GetFormType() { return FormType::ActorCharacter; }
 template<class T> T* As() { return static_cast<T*>(this); }
 Actor* AsActorState() { return this; }
 ATTACK_STATE_ENUM GetAttackState() { return ATTACK_STATE_ENUM::kSwing; }
};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton() { static PlayerCharacter pc; return &pc; } };
struct HitData { enum class Flag { kPowerAttack }; float totalDamage=100; };
}
namespace REX { template<class E,class T> struct EnumSet { bool any(E) { return false; } }; }
namespace settings { inline bool bTimedBlockToggle=true,bTimedBlockProjectileToggle=true,bTackleToggle=true,bGuardBreak=false; inline float fTimedBlockWindow=.3f; }
struct CombatRuntime { static CombatRuntime& get() { static CombatRuntime r; return r; } bool ready() { return true; } };
namespace Utils::Actor {
bool canBlock(RE::Actor*) { return true; }
void damageav(RE::Actor* actor, RE::ActorValue, float amount) { actor->stamina=std::max(0.f,actor->stamina-amount); }
}
namespace inlineUtils { void offsetRealDamage(float&,RE::Actor*,RE::Actor*) {} }
struct ValhallaCombat { enum HANDLER { blockHandler }; static ValhallaCombat* GetSingleton() { static ValhallaCombat v; return &v; } void activateUpdate(int) {} };
struct debuffHandler { int calls=0; static debuffHandler* GetSingleton() { static debuffHandler d; return &d; } void initStaminaDebuff(RE::Actor*) { ++calls; } };
struct staminaHandler { static void checkStamina(RE::Actor* actor) { if (actor->stamina<=0) debuffHandler::GetSingleton()->initStaminaDebuff(actor); } };
struct reactionHandler { enum { kLarge,kMedium }; static void triggerStagger(RE::Actor*,RE::Actor*,int) {} };
namespace PRECISION_API { struct PrecisionHitData { RE::Actor* attacker; RE::Actor* target; }; struct PreHitCallbackReturn { bool bIgnoreHit=false; }; }
inline float costMult=1;
float getBlockStaminaCostMult(RE::Actor*,RE::Actor*,REX::EnumSet<RE::HitData::Flag,std::uint32_t>) { return costMult; }
struct blockHandler {
 enum blockWindowPenaltyLevel { none,light,medium,heavy };
 blockWindowPenaltyLevel pcBlockWindowPenalty=none;
 bool isPcTimedBlocking=false,isBlockButtonPressed=false,isPcTimedBlockSuccess=false;
 std::uint64_t blockGeneration=0;
 float pcTimedBlockTimer=0;
 int tackles=0;
 static blockHandler* GetSingleton() { static blockHandler b; return &b; }
 void onBlockKeyDown(); void onBlockKeyUp();
 void onPcTimedBlockEnd() { isPcTimedBlocking=false; }
 void processStaminaBlock(RE::Actor*,RE::Actor*,REX::EnumSet<RE::HitData::Flag,std::uint32_t>,RE::HitData&);
 static PRECISION_API::PreHitCallbackReturn precisionPrehitCallbackFunc(const PRECISION_API::PrecisionHitData&);
 bool processMeleeTimedBlock(RE::Actor*,RE::Actor*,bool,bool) { return false; }
 bool processMeleeTackle(RE::Actor*,RE::Actor*) { ++tackles; return true; }
 bool getIsForcedTimedBlocking(RE::Actor*) { return false; }
 bool getIsForcedPerfectBlocking(RE::Actor*) { return false; }
};
#include "block_functions.inc"
void require(bool value) { if (!value) throw std::runtime_error("block regression"); }
int main() {
 auto h=blockHandler::GetSingleton();
 h->onBlockKeyDown(); const auto first=h->blockGeneration;
 h->onBlockKeyUp(); h->onBlockKeyDown(); require(h->isBlockButtonPressed && h->blockGeneration!=first);
 settings::bTimedBlockToggle=false; settings::bTimedBlockProjectileToggle=false;
 h->onBlockKeyUp(); require(!h->isBlockButtonPressed);
 RE::Actor blocker,attacker;
 require(h->precisionPrehitCallbackFunc({&attacker,&blocker}).bIgnoreHit && h->tackles==1);
 RE::HitData hit;
 h->processStaminaBlock(&blocker,&attacker,{},hit);
 require(blocker.stamina==0 && hit.totalDamage==0 && debuffHandler::GetSingleton()->calls>0);
 costMult=.5f; blocker.stamina=40; hit.totalDamage=100;
 h->processStaminaBlock(&blocker,&attacker,{},hit); require(hit.totalDamage==20.f || std::abs(hit.totalDamage-20.f)<.001f);
 costMult=2; blocker.stamina=50; hit.totalDamage=100;
 h->processStaminaBlock(&blocker,&attacker,{},hit); require(hit.totalDamage==75.f);
 std::puts("repress/release generations, tackle-only, exact zero, proportional damage: PASS");
}
