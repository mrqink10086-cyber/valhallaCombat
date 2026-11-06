#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <unordered_set>
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
	bool blocking=true;
	bool IsBlocking() { return blocking; }
 FormType GetFormType() { return FormType::ActorCharacter; }
 template<class T> T* As() { return static_cast<T*>(this); }
 Actor* AsActorState() { return this; }
 ATTACK_STATE_ENUM GetAttackState() { return ATTACK_STATE_ENUM::kSwing; }
};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton() { static PlayerCharacter pc; return &pc; } };
struct HitData { enum class Flag { kPowerAttack }; float totalDamage=100; };
namespace Offset { inline float delta=0; inline float* g_deltaTime=&delta; }
}
namespace REX { template<class E,class T> struct EnumSet { bool any(E) { return false; } }; }
namespace settings { inline bool bTimedBlockToggle=true,bTimedBlockProjectileToggle=true,bTackleToggle=true,bGuardBreak=false; inline float fTimedBlockWindow=.3f,fTimedBlockCooldownTime=.5f,fTackleCooldownTime=2; inline std::uint32_t uPerfectBlockAccum=3; }
struct CombatRuntime { static CombatRuntime& get() { static CombatRuntime r; return r; } bool ready() { return true; } };
namespace Utils::Actor {
bool canBlock(RE::Actor*) { return true; }
void damageav(RE::Actor* actor, RE::ActorValue, float amount) { actor->stamina=std::max(0.f,actor->stamina-amount); }
}
namespace inlineUtils { void offsetRealDamage(float&,RE::Actor*,RE::Actor*) {} }
struct ValhallaCombat { enum HANDLER { blockHandler }; bool active=false; static ValhallaCombat* GetSingleton() { static ValhallaCombat v; return &v; } void activateUpdate(int) { active=true; } void deactivateUpdate(int) { active=false; } };
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
	bool isPcBlockingCoolDown=false,isPcHot=false,isPcTackling=false,isPcTackleCooldown=false;
	float pcTimedBlockCooldownTimer=0,pcHotTimer=3,pcTackleTimer=0,pcTackleCooldownTimer=0;
	std::uint32_t pcPerfectBlockCountdown=3;
	std::unordered_set<int> pendingProjectiles;
 std::uint64_t blockGeneration=0;
 float pcTimedBlockTimer=0;
 int tackles=0;
 static blockHandler* GetSingleton() { static blockHandler b; return &b; }
 void onBlockKeyDown(); void onBlockKeyUp();
	void update(); void onPcTimedBlockEnd();
	void reset(); void init() { pcPerfectBlockCountdown=settings::uPerfectBlockAccum; }
	static void pcBlockWindowPenalty_update(blockWindowPenaltyLevel&,bool);
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
	h->isPcTimedBlocking=true; h->isPcTimedBlockSuccess=false; h->pcTimedBlockTimer=.2f;
	RE::PlayerCharacter::GetSingleton()->blocking=false;
	RE::Offset::delta=.3f; h->update();
	require(!h->isPcTimedBlocking && h->isPcBlockingCoolDown && std::abs(h->pcTimedBlockCooldownTimer-.4f)<.0001f);
	RE::Offset::delta=.4f; h->update();
	require(!h->isPcBlockingCoolDown && h->pcBlockWindowPenalty==blockHandler::none && !ValhallaCombat::GetSingleton()->active);
	h->isPcTackling=true; h->pcTackleTimer=.2f; RE::Offset::delta=.3f; h->update();
	require(h->isPcTackleCooldown && std::abs(h->pcTackleCooldownTimer-1.9f)<.0001f);
	RE::Offset::delta=1.9f; h->update(); require(!h->isPcTackleCooldown);
	h->isPcBlockingCoolDown=true; h->pcBlockWindowPenalty=blockHandler::heavy; h->pcTimedBlockCooldownTimer=0;
	settings::fTimedBlockCooldownTime=0; h->update(); require(!h->isPcBlockingCoolDown && h->pcBlockWindowPenalty==blockHandler::none);
	settings::fTimedBlockCooldownTime=.5f;
	h->isPcBlockingCoolDown=true; h->pcBlockWindowPenalty=blockHandler::heavy; h->pcTimedBlockCooldownTimer=.5f;
	RE::PlayerCharacter::GetSingleton()->blocking=true; RE::Offset::delta=2; h->update();
	require(h->pcBlockWindowPenalty==blockHandler::heavy && h->pcTimedBlockCooldownTimer==.5f);
	RE::PlayerCharacter::GetSingleton()->blocking=false; RE::Offset::delta=1.1f; h->update();
	require(h->pcBlockWindowPenalty==blockHandler::light && std::abs(h->pcTimedBlockCooldownTimer-.4f)<.0001f);
	RE::Offset::delta=.4f; h->update(); require(!h->isPcBlockingCoolDown);
	h->pendingProjectiles.insert(1); h->isPcHot=h->isPcTackling=h->isBlockButtonPressed=true;
	ValhallaCombat::GetSingleton()->active=true; const auto beforeReset=h->blockGeneration; h->reset();
	require(h->pendingProjectiles.empty() && h->blockGeneration!=beforeReset && !h->isPcHot && !h->isPcTackling && !h->isBlockButtonPressed);
	require(!ValhallaCombat::GetSingleton()->active && h->pcPerfectBlockCountdown==settings::uPerfectBlockAccum);
 std::puts("repress/release generations, tackle-only, exact zero, proportional damage: PASS");
}
