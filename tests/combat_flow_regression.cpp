#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RE {
enum class ActorValue { kHealth, kStamina };
template <class E>
struct Flags {
	std::uint32_t bits{};
	bool any(E flag) const { return (bits & (std::uint32_t{1} << static_cast<std::uint32_t>(flag))) != 0; }
	void set(E flag) { bits |= std::uint32_t{1} << static_cast<std::uint32_t>(flag); }
};
struct Actor;
struct ActorPtr {
	Actor* actor{};
	Actor* get() const { return actor; }
	Actor* operator->() const { return actor; }
	explicit operator bool() const { return actor != nullptr; }
};
struct ActorHandle {
	Actor* actor{};
	ActorPtr get() const { return { actor }; }
	bool operator==(const ActorHandle&) const = default;
};
struct ActorValueOwner {
	float stamina{ 100.0f };
	float maximum{ 100.0f };
	float GetActorValue(ActorValue) const { return stamina; }
	float GetPermanentActorValue(ActorValue) const { return maximum; }
	void RestoreActorValue(ActorValue, float value) { stamina += value; }
};
struct AttackData {
	enum class AttackFlag { kBashAttack, kPowerAttack };
	struct Details { Flags<AttackFlag> flags; } data;
};
struct HighProcess { AttackData* attackData{}; };
struct CurrentProcess { HighProcess* high{}; };
struct ActorRuntimeData { CurrentProcess* currentProcess{}; };
struct Weapon { bool IsMelee() const { return true; } };
struct Actor {
	bool player{};
	bool inDebuff{};
	ActorValueOwner values;
	ActorRuntimeData runtime;
	ActorHandle GetHandle() { return { this }; }
	bool IsPlayerRef() const { return player; }
	bool IsInCombat() const { return true; }
	ActorRuntimeData& GetActorRuntimeData() { return runtime; }
	ActorValueOwner* AsActorValueOwner() { return &values; }
	float GetPermanentActorValue(ActorValue value) { return values.GetPermanentActorValue(value); }
	bool GetGraphVariableBool(const char*, bool& value) const { value = false; return true; }
};
struct HitData {
	enum class Flag { kBlocked, kBash, kPowerAttack };
	Flags<Flag> flags;
	float totalDamage{};
	Weapon* weapon{};
};
}

template <> struct std::hash<RE::ActorHandle> {
	std::size_t operator()(RE::ActorHandle handle) const noexcept { return std::hash<RE::Actor*>{}(handle.actor); }
};

namespace settings {
inline bool bAttackStaminaToggle = true;
inline bool bAttackStaminaToggle_NPC = false;
inline bool bStaminaDebuffToggle = true;
inline bool bBlockedHitRegenStamina = true;
inline float fMeleeRewardLightHit_Percent = 0.2f;
inline bool bAutoExecution = false;
}

class CombatRuntime {
public:
	using Ticket = std::uint64_t;
	static CombatRuntime& get() { static CombatRuntime runtime; return runtime; }
	bool ready() const { return active_ && owner_ == std::this_thread::get_id(); }
	std::uint64_t session() const { return session_; }
	Ticket schedule(float, std::function<void()> callback)
	{
		if (!active_) return 0;
		std::lock_guard lock(mutex_);
		++scheduleCount_;
		const auto ticket = ++nextTicket_;
		queue_.push_back(std::move(callback));
		return ticket;
	}
	std::size_t scheduleCount() const { return scheduleCount_; }
	void drain()
	{
		if (!ready()) throw std::runtime_error("task drain must run on owner thread");
		std::vector<std::function<void()>> due;
		{ std::lock_guard lock(mutex_); due.swap(queue_); }
		for (auto& callback : due) callback();
	}
	void endSession() { active_ = false; ++session_; }
	void beginSession() { owner_ = std::this_thread::get_id(); active_ = true; ++session_; }
private:
	CombatRuntime() : owner_(std::this_thread::get_id()) {}
	std::thread::id owner_;
	bool active_{ true };
	std::uint64_t session_{ 1 };
	Ticket nextTicket_{};
	std::size_t scheduleCount_{};
	std::mutex mutex_;
	std::vector<std::function<void()>> queue_;
};

namespace {
struct PendingAnimation { RE::ActorHandle actor; std::uint32_t tag; std::uint64_t session; };
std::mutex animationMutex;
std::vector<PendingAnimation> pendingAnimations;
std::vector<PendingAnimation> animationBatch;
bool animationFlushing = false;
bool animationFlushQueued = false;
std::uint64_t animationFlushSession = 0;
}

class animEventHandler {
public:
	static void reset();
	static void submit(RE::Actor*, std::uint32_t);
	static void flush();
};

class attackHandler {
private:
	std::mutex mtx;
public:
	static attackHandler* GetSingleton() { static attackHandler instance; return &instance; }
#include "attack_enabled.inc"
	#include "attack_reset.inc"
	RE::Actor* actorToRegenStamina{};
	std::unordered_set<RE::ActorHandle> cachedAttackers;
	void cacheAttack(RE::Actor*);
	void OnLightHit(RE::Actor*);
	void checkout(RE::Actor*);
};

class staminaHandler {
public:
	inline static int hitRewards{};
	inline static int missedSwings{};
	inline static int staminaChecks{};
	inline static int exhaustionChecks{};
	inline static std::function<void()> onCheck;
	static void staminaLightHit(RE::Actor* actor) { ++hitRewards; actor->values.stamina += 20.0f; }
	static void staminaLightMiss(RE::Actor*) { ++missedSwings; }
	static void checkStamina(RE::Actor* actor) { ++staminaChecks; if (actor->values.stamina <= 0.0f) ++exhaustionChecks; if (onCheck) onCheck(); }
};

class debuffHandler {
public:
	static debuffHandler* GetSingleton() { static debuffHandler instance; return &instance; }
	bool isInDebuff(RE::Actor* actor) const { return actor->inDebuff; }
};
class reactionHandler {
public:
	inline static int staggers{};
	static constexpr int kLargest = 1;
	static void triggerStagger(RE::Actor*, RE::Actor*, int) { ++staggers; }
};
class blockHandler {
public:
	static blockHandler* GetSingleton() { static blockHandler instance; return &instance; }
	void processPhysicalBlock(RE::Actor*, RE::Actor*, RE::Flags<RE::HitData::Flag>, RE::HitData&) {}
};
class stunHandler {
public:
	enum class STUNSOURCE { powerBash, bash, counterAttack, powerAttack, lightAttack };
	static stunHandler* GetSingleton() { static stunHandler instance; return &instance; }
	bool getIsStunBroken(RE::Actor*) const { return false; }
	inline static float lastBase{};
	void processStunDamage(STUNSOURCE, RE::Weapon*, RE::Actor*, RE::Actor*, float base) { lastBase=base; }
};
class executionHandler {
public:
	static executionHandler* GetSingleton() { static executionHandler instance; return &instance; }
	bool tryAutomatic(RE::Actor*, RE::Actor*) { return false; }
	inline static float openingMultiplier=1;
	float openingDamageMultiplier(RE::Actor*) { return openingMultiplier; }
};
namespace data { inline constexpr const char* GraphBool_IsGuardCountering = "bIsGuardCountering"; }
namespace inlineUtils { inline void offsetRealDamage(float&, RE::Actor*, RE::Actor*) {} }
namespace Utils::Actor {
inline void restoreav(RE::Actor* actor, RE::ActorValue value, float amount) { actor->AsActorValueOwner()->RestoreActorValue(value, amount); }
}
class hitProcessor {
public:
	void processHit(RE::Actor*, RE::Actor*, RE::HitData&);
};

#include "combat_functions.inc"

namespace {
void require(bool condition, const char* label)
{
	if (!condition) throw std::runtime_error(label);
	std::cout << "PASS " << label << '\n';
}
RE::Actor makeActor(bool player, RE::AttackData& attackData)
{
	RE::Actor actor;
	actor.player = player;
	static RE::HighProcess high;
	static RE::CurrentProcess process;
	high.attackData = &attackData;
	process.high = &high;
	actor.runtime.currentProcess = &process;
	return actor;
}
}

int main()
{
	try {
		auto* attacks = attackHandler::GetSingleton();
		RE::AttackData lightAttack;
		for (bool playerToggle : { false, true }) {
			for (bool npcToggle : { false, true }) {
				settings::bAttackStaminaToggle = playerToggle;
				settings::bAttackStaminaToggle_NPC = npcToggle;
				for (bool isPlayer : { false, true }) {
					auto actor = makeActor(isPlayer, lightAttack);
					const bool expected = isPlayer ? playerToggle : npcToggle;
					require(attackHandler::enabledFor(&actor) == expected, "enabledFor follows the actor-specific toggle");
					const int missesBefore = staminaHandler::missedSwings;
					const int hitsBefore = staminaHandler::hitRewards;
					attacks->cacheAttack(&actor);
					require(attacks->cachedAttackers.contains(actor.GetHandle()) == expected, "cacheAttack registers only enabled light attacks");
					attacks->OnLightHit(&actor);
					require(staminaHandler::hitRewards == hitsBefore + (expected ? 1 : 0), "OnLightHit rewards only enabled attackers");
					attacks->checkout(&actor);
					require(staminaHandler::missedSwings == missesBefore, "a rewarded hit cannot later charge a miss");
					attacks->OnLightHit(&actor);
					require(staminaHandler::hitRewards == hitsBefore + (expected ? 1 : 0), "one swing rewards stamina only once");
					require(staminaHandler::missedSwings == missesBefore, "multiple successful hits do not create a pending miss charge");
					attacks->cacheAttack(&actor);
					attacks->checkout(&actor);
					require(staminaHandler::missedSwings == missesBefore + (expected ? 1 : 0), "checkout charges only a cached enabled miss");
				}
			}
		}

		settings::bAttackStaminaToggle = true;
		settings::bAttackStaminaToggle_NPC = false;
		settings::bStaminaDebuffToggle = true;
		auto eventActor = makeActor(true, lightAttack);
		processActor(&eventActor, "preHitFrame"_h);
		require(attacks->cachedAttackers.contains(eventActor.GetHandle()), "animation preHitFrame registers the light swing");
		const auto checksBeforeStop = staminaHandler::staminaChecks;
		processActor(&eventActor, "attackStop"_h);
		require(!attacks->cachedAttackers.contains(eventActor.GetHandle()) && staminaHandler::missedSwings > 0, "animation attackStop checks out an unhit swing");
		require(staminaHandler::staminaChecks == checksBeforeStop + 1, "attackStop performs the exhaustion check");
		settings::bAttackStaminaToggle = false;
		const auto checksBeforeDisabled = staminaHandler::staminaChecks;
		processActor(&eventActor, "preHitFrame"_h);
		require(staminaHandler::staminaChecks == checksBeforeDisabled + 1, "disabled attack stamina still checks exhaustion at preHitFrame");
		const auto checksBeforeNested = staminaHandler::staminaChecks;
		bool nestedSubmitted = false;
		staminaHandler::onCheck = [&] {
			if (!nestedSubmitted) {
				nestedSubmitted = true;
				animEventHandler::submit(&eventActor, "Dodge"_h);
			}
		};
		animEventHandler::submit(&eventActor, "Dodge"_h);
		staminaHandler::onCheck = {};
		require(staminaHandler::staminaChecks == checksBeforeNested + 1, "submit during animation flush is deferred");
		CombatRuntime::get().drain();
		require(staminaHandler::staminaChecks == checksBeforeNested + 2, "deferred nested submit runs on the next drain");

		settings::bAttackStaminaToggle = true;
		settings::bAttackStaminaToggle_NPC = true;
		settings::fMeleeRewardLightHit_Percent = 0.2f;
		attacks->reset();
		staminaHandler::hitRewards = 0;
		staminaHandler::missedSwings = 0;
		RE::Actor attacker = makeActor(true, lightAttack);
		attacker.values.stamina = 50.0f;
		RE::Actor victim;
		victim.player = false;
		victim.inDebuff = true;
		victim.values.stamina = 10.0f;
			const auto schedulesBefore = CombatRuntime::get().scheduleCount();
			constexpr int workerEvents = 8;
			std::vector<std::thread> preHitWorkers;
			for (int i = 0; i < workerEvents; ++i) {
				preHitWorkers.emplace_back([&] { animEventHandler::submit(&attacker, "preHitFrame"_h); });
			}
			for (auto& worker : preHitWorkers) worker.join();
			require(CombatRuntime::get().scheduleCount() == schedulesBefore + 1, "worker animation submissions coalesce to one scheduled flush");
		RE::Weapon weapon;
		RE::HitData hit;
		hit.totalDamage = 12.0f;
		hit.weapon = &weapon;
		hitProcessor processor;
		processor.processHit(&attacker, &victim, hit);
		processor.processHit(&attacker, &victim, hit);
		require(staminaHandler::hitRewards == 1 && attacker.values.stamina == 70.0f, "multiple hit callbacks for one swing grant one reward");
		require(victim.values.stamina == 50.0f && reactionHandler::staggers == 2, "each hit on an exhausted victim runs its stamina reward and stagger path");
		const auto missesBeforeStop = staminaHandler::missedSwings;
			const auto schedulesBeforeOwnerSubmit = CombatRuntime::get().scheduleCount();
			animEventHandler::submit(&attacker, "attackStop"_h);
			require(CombatRuntime::get().scheduleCount() == schedulesBeforeOwnerSubmit, "owner animation submit adds no redundant scheduled flush");
			require(!attacks->cachedAttackers.contains(attacker.GetHandle()), "owner hit settles queued preHitFrames before attackStop");
			require(staminaHandler::missedSwings == missesBeforeStop, "a hit before owner attackStop does not become a miss");

			CombatRuntime::get().drain();
			attacks->reset();
			const auto missesBeforeOwnerStop = staminaHandler::missedSwings;
			std::thread queuedPreHit([&] { animEventHandler::submit(&attacker, "preHitFrame"_h); });
			queuedPreHit.join();
			const auto scheduleCountBeforeOrderedOwner = CombatRuntime::get().scheduleCount();
			animEventHandler::submit(&attacker, "attackStop"_h);
			require(CombatRuntime::get().scheduleCount() == scheduleCountBeforeOrderedOwner, "owner submit reuses the queued worker flush");
			require(staminaHandler::missedSwings == missesBeforeOwnerStop + 1 && !attacks->cachedAttackers.contains(attacker.GetHandle()), "queued worker preHit runs before owner attackStop");
			CombatRuntime::get().drain();

			attacks->reset();
			const auto missesBeforeWorkerStop = staminaHandler::missedSwings;
			std::thread workerPreHit([&] { animEventHandler::submit(&attacker, "preHitFrame"_h); });
			workerPreHit.join();
			processor.processHit(&attacker, &victim, hit);
			std::thread stopWorker([&] { animEventHandler::submit(&attacker, "attackStop"_h); });
			stopWorker.join();
			CombatRuntime::get().drain();
			require(staminaHandler::missedSwings == missesBeforeWorkerStop, "owner hit flushes worker preHit before worker attackStop drains");

		attacks->reset();
		RE::Actor resetActor = makeActor(true, lightAttack);
		std::thread resetWorker([&] { animEventHandler::submit(&resetActor, "preHitFrame"_h); });
		resetWorker.join();
		animEventHandler::reset();
		CombatRuntime::get().drain();
		require(!attacks->cachedAttackers.contains(resetActor.GetHandle()), "animation reset clears a queued preHitFrame");

		attacks->reset();
		RE::Actor staleActor = makeActor(true, lightAttack);
		std::thread staleWorker([&] { animEventHandler::submit(&staleActor, "preHitFrame"_h); });
		staleWorker.join();
		CombatRuntime::get().endSession();
		CombatRuntime::get().beginSession();
		CombatRuntime::get().drain();
		require(!attacks->cachedAttackers.contains(staleActor.GetHandle()), "animation queued in an old session is discarded");

		RE::AttackData powerAttack;
		powerAttack.data.flags.set(RE::AttackData::AttackFlag::kPowerAttack);
		RE::Actor powerActor = makeActor(true, powerAttack);
		RE::Actor powerVictim;
		for (bool blockedReward : {false, true}) {
			settings::bBlockedHitRegenStamina = blockedReward;
			for (bool blocked : {false, true}) {
				for (bool registered : {false, true}) {
					attacks->reset();
					const auto rewardsBefore = staminaHandler::hitRewards;
					const auto powerMissesBefore = staminaHandler::missedSwings;
					if (registered) processActor(&powerActor, "preHitFrame"_h);
					RE::HitData powerHit;
					powerHit.flags.set(RE::HitData::Flag::kPowerAttack);
					if (blocked) powerHit.flags.set(RE::HitData::Flag::kBlocked);
					powerHit.weapon = &weapon;
					processor.processHit(&powerActor, &powerVictim, powerHit);
					processor.processHit(&powerActor, &powerVictim, powerHit);
					processActor(&powerActor, "attackStop"_h);
					require(powerActor.values.stamina == 100.0f && staminaHandler::hitRewards == rewardsBefore &&
						staminaHandler::missedSwings == powerMissesBefore && attacks->cachedAttackers.empty(),
						"power hits, blocked hits and stop add no plugin stamina settlement with or without preHitFrame");
				}
			}
		}
		executionHandler::openingMultiplier=1.5f;
		RE::HitData openingHit;
		openingHit.totalDamage=12.0f;
		openingHit.weapon=&weapon;
		processor.processHit(&powerActor,&powerVictim,openingHit);
		require(openingHit.totalDamage==18.0f && stunHandler::lastBase==12.0f,
			"a broken opening increases health damage without feeding the bonus into stun damage");
		executionHandler::openingMultiplier=1;
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL " << exception.what() << '\n';
		return 1;
	}
}
