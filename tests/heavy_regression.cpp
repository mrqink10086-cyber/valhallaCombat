#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unordered_set>

namespace RE {
template <class E>
struct Flags {
	std::uint32_t bits{};
	bool any(E flag) const { return (bits & (std::uint32_t{1} << static_cast<std::uint32_t>(flag))) != 0; }
	void set(E flag) { bits |= std::uint32_t{1} << static_cast<std::uint32_t>(flag); }
};
struct Actor;
struct ActorHandle {
	Actor* actor{};
	bool operator==(const ActorHandle&) const = default;
};
struct ActorValueOwner { Actor* actor{}; float stamina{100.0f}; };
struct AttackData {
	enum class AttackFlag { kBashAttack, kPowerAttack };
	struct Details { Flags<AttackFlag> flags; } data;
};
using BGSAttackData = AttackData;
struct HighProcess { AttackData* attackData{}; };
struct CurrentProcess { HighProcess* high{}; };
struct ActorRuntimeData { CurrentProcess* currentProcess{}; };
struct Actor {
	bool player{};
	bool inCombat{true};
	ActorValueOwner values;
	HighProcess high;
	CurrentProcess process;
	ActorRuntimeData runtime;
	ActorHandle GetHandle() { return {this}; }
	bool IsPlayerRef() const { return player; }
	bool IsInCombat() const { return inCombat; }
	ActorRuntimeData& GetActorRuntimeData() { return runtime; }
};
}
template <> struct std::hash<RE::ActorHandle> {
	std::size_t operator()(RE::ActorHandle handle) const noexcept { return std::hash<RE::Actor*>{}(handle.actor); }
};
namespace settings {
inline bool bAttackStaminaToggle = true;
inline bool bAttackStaminaToggle_NPC = true;
inline bool bNonCombatStaminaCost = true;
}
class CombatRuntime {
public:
	static CombatRuntime& get() { static CombatRuntime runtime; return runtime; }
	bool ready() const { return ready_; }
	void setReady(bool ready) { ready_ = ready; }
private:
	bool ready_{true};
};
namespace Utils::AvOwner {
inline RE::Actor* asActor(RE::ActorValueOwner* owner) { return owner ? owner->actor : nullptr; }
}
class staminaHandler {
public:
	static void staminaLightMiss(RE::Actor* actor) { ++lightMisses; actor->values.stamina -= 30.0f; }
	inline static int lightMisses{};
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
	void checkout(RE::Actor*);
};
#include "heavy_attack_functions.inc"
namespace Hooks {
class Hook_OnGetAttackStaminaCost {
public:
	using CostFunction = float (*)(RE::ActorValueOwner*, RE::BGSAttackData*);
	inline static CostFunction _getAttackStaminaCost{};
	static float getAttackStaminaCost(RE::ActorValueOwner*, RE::BGSAttackData*);
};
#include "heavy_hook.inc"
}
namespace {
int vanillaCalls{};
RE::ActorValueOwner* forwardedOwner{};
RE::BGSAttackData* forwardedData{};
float vanillaCost(RE::ActorValueOwner* owner, RE::BGSAttackData* data)
{
	++vanillaCalls;
	forwardedOwner = owner;
	forwardedData = data;
	return 17.5f;
}
void require(bool condition, const char* label)
{
	if (!condition) throw std::runtime_error(label);
	std::cout << "PASS " << label << '\n';
}
void expectVanilla(RE::ActorValueOwner* owner, RE::BGSAttackData* data)
{
	const auto before = vanillaCalls;
	require(Hooks::Hook_OnGetAttackStaminaCost::getAttackStaminaCost(owner, data) == 17.5f &&
		vanillaCalls == before + 1 && forwardedOwner == owner && forwardedData == data,
		"native stamina calculation is called once with the original arguments");
}
void setAttack(RE::Actor& actor, RE::AttackData& data)
{
	actor.high.attackData = &data;
	actor.process.high = &actor.high;
	actor.runtime.currentProcess = &actor.process;
	actor.values.actor = &actor;
}
}
int main()
{
	try {
		auto* attacks = attackHandler::GetSingleton();
		Hooks::Hook_OnGetAttackStaminaCost::_getAttackStaminaCost = vanillaCost;
		RE::AttackData power;
		power.data.flags.set(RE::AttackData::AttackFlag::kPowerAttack);
		for (bool playerToggle : {false, true}) {
			for (bool npcToggle : {false, true}) {
				settings::bAttackStaminaToggle = playerToggle;
				settings::bAttackStaminaToggle_NPC = npcToggle;
				for (bool isPlayer : {false, true}) {
					RE::Actor actor;
					actor.player = isPlayer;
					setAttack(actor, power);
					attacks->reset();
					expectVanilla(&actor.values, &power);
					const auto missesBefore = staminaHandler::lightMisses;
					attacks->cacheAttack(&actor);
					attacks->checkout(&actor);
					require(attacks->cachedAttackers.empty() && actor.values.stamina == 100.0f &&
						staminaHandler::lightMisses == missesBefore,
						"power attacks never enter light-miss settlement for either actor toggle");
					actor.inCombat = false;
					settings::bNonCombatStaminaCost = false;
					if (isPlayer ? playerToggle : npcToggle) {
						const auto callsBefore = vanillaCalls;
						require(Hooks::Hook_OnGetAttackStaminaCost::getAttackStaminaCost(&actor.values, &power) == 0.0f &&
							vanillaCalls == callsBefore, "noncombat waiver follows the actor-specific enable switch");
					} else expectVanilla(&actor.values, &power);
					settings::bNonCombatStaminaCost = true;
					expectVanilla(&actor.values, &power);
				}
			}
		}
		settings::bAttackStaminaToggle = true;
		settings::bAttackStaminaToggle_NPC = true;
		RE::Actor player;
		player.player = true;
		setAttack(player, power);
		attacks->reset();
		expectVanilla(&player.values, &power);
		attacks->checkout(&player);
		require(player.values.stamina == 100.0f, "missing preHitFrame cannot suppress the native heavy cost");
		player.high.attackData = nullptr;
		expectVanilla(&player.values, nullptr);
		attacks->cacheAttack(&player);
		attacks->checkout(&player);
		require(attacks->cachedAttackers.empty(), "missing attack data creates no deferred charge");
		player.runtime.currentProcess = nullptr;
		expectVanilla(&player.values, &power);
		attacks->cacheAttack(&player);
		require(attacks->cachedAttackers.empty(), "native heavy cost does not require an animation process");
		RE::AttackData bashPower;
		bashPower.data.flags.set(RE::AttackData::AttackFlag::kPowerAttack);
		bashPower.data.flags.set(RE::AttackData::AttackFlag::kBashAttack);
		setAttack(player, bashPower);
		expectVanilla(&player.values, &bashPower);
		attacks->cacheAttack(&player);
		require(attacks->cachedAttackers.empty(), "bash attacks remain outside light settlement");
		RE::AttackData light;
		setAttack(player, light);
		attacks->cacheAttack(&player);
		const auto missesBefore = staminaHandler::lightMisses;
		setAttack(player, power);
		attacks->cacheAttack(&player);
		attacks->checkout(&player);
		require(staminaHandler::lightMisses == missesBefore + 1 && player.values.stamina == 70.0f &&
			attacks->cachedAttackers.empty(), "starting a heavy attack settles only the preceding light miss");
		CombatRuntime::get().setReady(false);
		expectVanilla(&player.values, &power);
		CombatRuntime::get().setReady(true);
		expectVanilla(nullptr, &power);
		RE::ActorValueOwner nonActor;
		expectVanilla(&nonActor, &power);
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL " << exception.what() << '\n';
		return 1;
	}
}
