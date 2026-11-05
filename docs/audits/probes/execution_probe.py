"""Compile unchanged selected mod functions against controlled engine substitutes.

This probes source-level decisions. It does not emulate Skyrim animation or ABI.
Requires Python 3, clang++, and Visual Studio C++ Build Tools on Windows.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
BASELINE = "364ff1740d0dfbce24bf8d8139c6635408f2e69f"


def source_at_baseline(path):
    return subprocess.check_output(["git", "show", f"{BASELINE}:{path}"], cwd=ROOT).decode("utf-8-sig")


def extract(text, marker):
    start = text.index(marker)
    brace = text.index("{", start)
    depth = 0
    # Mask strings and comments while retaining character offsets.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: " " * len(m.group()), text, flags=re.S)
    for end in range(brace, len(text)):
        depth += (masked[end] == "{") - (masked[end] == "}")
        if depth == 0:
            return text[start:end + 1]
    raise ValueError(f"Unterminated function: {marker}")


PRELUDE = r'''
#include <cmath>
#include <cstdio>
#include <vector>
namespace RE {
struct Position {
    float x = 0;
    float GetDistance(Position other) { return std::fabs(x - other.x); }
};
struct Process { bool InHighProcess() { return true; } };
struct Actor {
    bool dead = false, loaded = true, killmove = false;
    Position pos;
    Process process;
    struct Runtime { Process* currentProcess; } runtime { &process };
    bool Is3DLoaded() { return loaded; }
    bool IsInKillMove() { return killmove; }
    Runtime& GetActorRuntimeData() { return runtime; }
    Position GetPosition() { return pos; }
};
struct Handle {
    Actor* actor;
    explicit operator bool() const { return actor != nullptr; }
    struct Ptr { Actor* actor; Actor* get() { return actor; } };
    Ptr get() const { return {actor}; }
};
struct PlayerCharacter : Actor {
    static PlayerCharacter* GetSingleton() { static PlayerCharacter pc; return &pc; }
};
}
namespace Utils::Actor { bool isDead(RE::Actor* actor) { return actor->dead; } }
namespace inlineUtils {}
struct stunHandler {
    std::vector<RE::Handle> candidates;
    static stunHandler* GetSingleton() { static stunHandler h; return &h; }
    auto getStunBrokenActors() { return candidates; }
};
struct executionHandler {
    int attempts = 0;
    void tryPcExecution();
    bool attemptExecute(RE::Actor*, RE::Actor*) { ++attempts; return true; }
};
'''


MAIN = r'''
int main() {
    RE::Actor invalid, valid;
    invalid.dead = true;
    valid.pos.x = 100;
    auto* stun = stunHandler::GetSingleton();
    executionHandler handler;
    stun->candidates = {{&valid}};
    handler.tryPcExecution();
    const bool control = handler.attempts == 1;
    handler.attempts = 0;
    stun->candidates = {{&invalid}, {&valid}};
    handler.tryPcExecution();
    std::printf("control_valid_only: %s\n", control ? "PASS" : "FAIL");
    std::printf("valid_target_after_dead_candidate: %s (attempts=%d, expected=1)\n",
                handler.attempts == 1 ? "PASS" : "FAIL", handler.attempts);
    const bool dead_case = handler.attempts == 1;
    invalid.dead = false;
    invalid.loaded = false;
    handler.attempts = 0;
    handler.tryPcExecution();
    std::printf("valid_target_after_unloaded_candidate: %s (attempts=%d, expected=1)\n",
                handler.attempts == 1 ? "PASS" : "FAIL", handler.attempts);
    return control && dead_case && handler.attempts == 1 ? 0 : 1;
}
'''


def numeric_program():
    header = source_at_baseline("src/include/stunHandler.h")
    source = source_at_baseline("src/bin/stunHandler.cpp")
    config = source_at_baseline("src/include/settings.h")
    config = re.sub(r"//[^\n]*|/\*.*?\*/", "", config, flags=re.S)
    settings = "namespace settings {\n" + "\n".join(re.findall(
        r"static inline (?:bool|float|int|uint32_t) \w+ = [^;]+;", config)) + "\n}\n"
    stub = r'''
#include <cstdint>
#include <cstdio>
namespace RE {
enum class ActorValue { kHealth, kStamina, kBlock };
enum class WEAPON_TYPE { kHandToHandMelee, kOneHandDagger, kOneHandSword,
    kOneHandAxe, kOneHandMace, kTwoHandAxe, kTwoHandSword };
struct TESObjectWEAP {
    WEAPON_TYPE type = WEAPON_TYPE::kOneHandSword;
    WEAPON_TYPE GetWeaponType() { return type; }
};
struct Actor {
    float health = 100, stamina = 100;
    bool combat = true;
    Actor* AsActorValueOwner() { return this; }
    float GetPermanentActorValue(ActorValue av) { return av == ActorValue::kHealth ? health : stamina; }
    float GetActorValue(ActorValue) { return 100; }
    bool IsPlayerRef() { return false; }
    bool IsPlayerTeammate() { return false; }
    bool IsInCombat() { return combat; }
    bool IsInKillMove() { return false; }
};
struct ActorHandle {
    Actor* actor;
    explicit operator bool() const { return actor != nullptr; }
    struct Ptr { Actor* actor; Actor* get() { return actor; } };
    Ptr get() const { return {actor}; }
};
namespace Offset { inline float dt = 1.0f / 60; inline float* g_deltaTime = &dt; }
}
namespace logger { template<class... T> void error(T...) {} }
namespace inlineUtils { void offsetRealDamage(float&, RE::Actor*, RE::Actor*) {} }
struct reactionHandler { static void triggerDownedState(RE::Actor*) {} };
'''
    handler = r'''
struct stunHandler {
    public:
    enum class STUNSOURCE { timedBlock, bash, powerBash, parry, lightAttack, powerAttack };
'''
    handler += extract(header, "struct actorStunData {") + ";\n"
    handler += r'''
    actorStunData* active = nullptr;
    int damage_calls = 0;
    bool broken = false;
    void damageStun(RE::Actor*, RE::Actor*, float damage) {
        ++damage_calls;
        broken = active->damageStun(damage) || broken;
    }
    bool safeGet_isStunBroken(RE::Actor*) { return broken; }
    void processStunDamage(STUNSOURCE, RE::TESObjectWEAP*, RE::Actor*, RE::Actor*, float);
};
'''
    tests = r'''
int main() {
    RE::Actor attacker, victim;
    RE::TESObjectWEAP sword;
    stunHandler handler;
    stunHandler::actorStunData ordinary({&victim});
    handler.active = &ordinary;
    for (int i = 0; i != 10; ++i)
        handler.processStunDamage(stunHandler::STUNSOURCE::lightAttack, &sword, &attacker, &victim, 100);
    std::printf("default_light_10x100: stun=%.1f/%.1f damage_calls=%d (expect unchanged with multiplier=0)\n",
                ordinary.getCurrentStun(), ordinary.getMaxStun(), handler.damage_calls);
    bool ok = ordinary.getCurrentStun() == ordinary.getMaxStun() && handler.damage_calls == 0;
    settings::fStunNormalAttackMult = 1;
    handler.processStunDamage(stunHandler::STUNSOURCE::lightAttack, &sword, &attacker, &victim, 20);
    std::printf("light_multiplier_1_control: stun=%.1f (expected=80)\n", ordinary.getCurrentStun());
    ok = ok && ordinary.getCurrentStun() == 80;
    victim.stamina = 300;
    stunHandler::actorStunData durable({&victim});
    handler.active = &durable;
    for (int i = 0; i != 5; ++i)
        handler.processStunDamage(stunHandler::STUNSOURCE::powerAttack, &sword, &attacker, &victim, 20);
    std::printf("health100_stamina300_power_budget100: stun=%.1f/%.1f (100 health-damage budget assumed)\n",
                durable.getCurrentStun(), durable.getMaxStun());
    ok = ok && durable.getCurrentStun() == 100;
    durable.damageStun(50);
    const float after_damage = durable.getCurrentStun();
    durable.modStun(50);
    std::printf("restore_hook_arithmetic: stun %.1f -> %.1f after modStun(+50)\n",
                after_damage, durable.getCurrentStun());
    ok = ok && durable.getCurrentStun() == 100;
    stunHandler::actorStunData recovery({&victim});
    recovery.damageStun(100000);
    recovery.setCombatRegeneration(true);
    const bool can_regen = recovery.canRegen();
    for (int i = 0; i != 420; ++i) recovery.regenStun();
    std::printf("broken_recovery_after_countdown: in_combat=%d can_regen=%d stun=%.1f/%.1f after7s\n",
                victim.combat, can_regen, recovery.getCurrentStun(), recovery.getMaxStun());
    ok = ok && can_regen && recovery.getCurrentStun() > recovery.getMaxStun() - 0.01f;
    return ok ? 0 : 2;
}
'''
    return stub + settings + handler + extract(source, "void stunHandler::processStunDamage(") + tests


def dispatch_program():
    source = source_at_baseline("src/bin/executionHandler.cpp")
    attempt = extract(source, "bool executionHandler::attemptExecute(")
    names = re.findall(r"case RACE::\w+: (execute\w+)\(", attempt)
    categories = re.findall(r"case RACE::(\w+):", attempt)
    declaration = "\n".join(f"void {name}(RE::Actor*, RE::Actor*, RE::WEAPON_TYPE);" for name in names)
    definitions = "\n".join(
        f"void executionHandler::{name}(RE::Actor*, RE::Actor*, RE::WEAPON_TYPE) {{ ++queued; }}"
        for name in names if name != "executeDraugr")
    stub = r'''
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
namespace RE {
enum class WEAPON_TYPE { kHandToHandMelee, kTwoHandAxe, kTwoHandSword, kOneHandSword, kBow, kCrossbow };
struct TESObjectWEAP { WEAPON_TYPE type; WEAPON_TYPE GetWeaponType() { return type; } };
struct TESRace {};
struct Position { float x = 0; float GetDistance(Position p) { return std::fabs(x-p.x); } };
struct Process { bool InHighProcess() { return true; } };
struct MagicTarget {
    enum class Archetype { kParalysis };
    bool HasEffectWithArchetype(Archetype) { return false; }
};
struct Actor {
    bool dead = false, essential = false;
    Position pos;
    Process process;
    struct Runtime { Process* currentProcess; } runtime {&process};
    TESRace* race = nullptr;
    TESObjectWEAP* weapon = nullptr;
    bool Is3DLoaded() { return true; }
    bool IsInKillMove() { return false; }
    bool IsOnMount() { return false; }
    bool IsPlayerRef() { return false; }
    bool IsPlayerTeammate() { return false; }
    bool IsEssential() { return essential; }
    Runtime& GetActorRuntimeData() { return runtime; }
    Position GetPosition() { return pos; }
    TESRace* GetRace() { return race; }
    MagicTarget* AsMagicTarget() { static MagicTarget magic; return &magic; }
};
struct PlayerCharacter : Actor {
    static PlayerCharacter* GetSingleton() { static PlayerCharacter pc; return &pc; }
};
struct Handle {
    Actor* actor;
    explicit operator bool() const { return actor != nullptr; }
    struct Ptr { Actor* actor; Actor* get() { return actor; } };
    Ptr get() const { return {actor}; }
};
}
namespace settings { inline bool bStunToggle = true; }
namespace Utils::Actor {
    bool isHumanoid(RE::Actor*) { return true; }
    bool isDead(RE::Actor* p) { return p->dead; }
    RE::TESObjectWEAP* getWieldingWeapon(RE::Actor* p) { return p->weapon; }
}
namespace inlineUtils {}
struct stunHandler {
    std::vector<RE::Handle> candidates;
    static stunHandler* GetSingleton() { static stunHandler h; return &h; }
    auto getStunBrokenActors() { return candidates; }
};
'''
    stub += "struct data { enum raceCatagory { " + ",".join(categories) + " };\n"
    stub += "static inline std::map<RE::TESRace*, raceCatagory> raceMapping;\n"
    stub += "static inline int KM_Undead_2hw=1, KM_Undead_2hm=2, KM_Undead_1hm=3; };\n"
    stub += "struct executionHandler { int queued=0; bool attemptExecute(RE::Actor*, RE::Actor*);\n"
    stub += "void tryPcExecution();\nvoid queueExecutionIdle(RE::Actor*,RE::Actor*,int) { ++queued; }\n"
    stub += declaration + "\n};\n"
    tests = r'''
int main() {
    RE::TESRace mapped, custom;
    data::raceMapping[&mapped] = data::Undead;
    RE::TESObjectWEAP sword {RE::WEAPON_TYPE::kOneHandSword};
    auto* pc = RE::PlayerCharacter::GetSingleton();
    RE::Actor good, incompatible;
    good.race = &mapped; good.pos.x = 100;
    incompatible.race = &custom; incompatible.pos.x = 50;
    executionHandler handler;
    pc->weapon = &sword;
    bool result = handler.attemptExecute(pc, &good);
    std::printf("mapped_sword_control: accepted=%d queued=%d (expected=1,1)\n", result, handler.queued);
    bool ok = result && handler.queued == 1;
    handler.queued = 0;
    result = handler.attemptExecute(pc, &incompatible);
    std::printf("unmapped_mod_race: accepted=%d queued=%d (expected rejection)\n", result, handler.queued);
    ok = ok && !result && handler.queued == 0;
    pc->weapon = nullptr;
    result = handler.attemptExecute(pc, &good);
    std::printf("mapped_undead_unarmed: accepted=%d queued=%d (accepted without dispatch)\n", result, handler.queued);
    const bool noop = result && handler.queued == 0;
    pc->weapon = &sword;
    auto* stun = stunHandler::GetSingleton();
    stun->candidates = {{&incompatible}, {&good}};
    handler.tryPcExecution();
    std::printf("near_unmapped_masks_far_mapped: queued=%d (expected=1)\n", handler.queued);
    const bool masked = handler.queued == 0;
    handler.queued = 0;
    incompatible.race = &mapped;
    incompatible.essential = true;
    handler.tryPcExecution();
    std::printf("near_essential_masks_far_valid: queued=%d (expected=1)\n", handler.queued);
    if (!ok) return 2;
    return noop || masked || handler.queued == 0 ? 1 : 0;
}
'''
    return (stub + attempt + definitions + extract(source, "void executionHandler::executeDraugr(")
            + extract(source, "void executionHandler::tryPcExecution()") + tests)


def flow_program():
    source = source_at_baseline("src/bin/hitProcessor.cpp")
    stub = r'''
#include <cstdio>
namespace RE {
enum class ActorValue { kStamina };
struct Actor {
    bool player = false;
    bool IsPlayerRef() { return player; }
    Actor* AsActorValueOwner() { return this; }
    float GetPermanentActorValue(ActorValue) { return 100; }
    bool GetGraphVariableBool(const char*, bool&) { return false; }
};
struct TESObjectWEAP { bool IsMelee() { return true; } };
struct HitData {
    enum class Flag { kBlocked = 1, kPowerAttack = 2, kBash = 4 };
    struct Flags { int bits=0; bool any(Flag f) { return (bits & int(f)) != 0; } } flags;
    float totalDamage = 100;
    TESObjectWEAP* weapon;
};
}
namespace settings {
inline bool bAttackStaminaToggle=false, bBlockedHitRegenStamina=false, bAutoExecution=true;
inline float fMeleeRewardLightHit_Percent=0;
}
namespace data { inline const char* GraphBool_IsGuardCountering="test"; }
namespace inlineUtils { void offsetRealDamage(float&, RE::Actor*, RE::Actor*) {} }
namespace Utils::Actor { void restoreav(RE::Actor*, RE::ActorValue, float) {} }
struct debuffHandler {
    static debuffHandler* GetSingleton() { static debuffHandler h; return &h; }
    bool isInDebuff(RE::Actor*) { return false; }
};
struct reactionHandler { enum { kLargest }; static void triggerStagger(RE::Actor*,RE::Actor*,int) {} };
struct blockHandler {
    static blockHandler* GetSingleton() { static blockHandler h; return &h; }
    void processPhysicalBlock(RE::Actor*, RE::Actor*, RE::HitData::Flags, RE::HitData&) {}
};
struct attackHandler {
    static attackHandler* GetSingleton() { static attackHandler h; return &h; }
    void OnLightHit(RE::Actor*) {}
};
struct stunHandler {
    enum class STUNSOURCE { powerBash, bash, powerAttack, lightAttack };
    bool broken=false;
    static stunHandler* GetSingleton() { static stunHandler h; return &h; }
    bool getIsStunBroken(RE::Actor*) { return broken; }
    void processStunDamage(STUNSOURCE, RE::TESObjectWEAP*, RE::Actor*, RE::Actor*, float) { broken=true; }
};
struct executionHandler {
    int attempts=0;
    static executionHandler* GetSingleton() { static executionHandler h; return &h; }
    bool attemptExecute(RE::Actor*, RE::Actor*) { ++attempts; return true; }
};
struct hitProcessor { void processHit(RE::Actor*, RE::Actor*, RE::HitData&); };
'''
    tests = r'''
int main() {
    RE::Actor attacker, victim;
    attacker.player=true;
    RE::TESObjectWEAP sword;
    RE::HitData hit;
    hit.weapon=&sword;
    hit.flags.bits=int(RE::HitData::Flag::kPowerAttack);
    hitProcessor handler;
    auto* stun=stunHandler::GetSingleton();
    auto* execution=executionHandler::GetSingleton();
    handler.processHit(&attacker, &victim, hit);
    std::printf("auto_breaking_hit: broken=%d attempts=%d (current rule: no same-hit attempt)\n",
                stun->broken, execution->attempts);
    const bool late=stun->broken && execution->attempts==0;
    handler.processHit(&attacker, &victim, hit);
    std::printf("auto_following_hit: attempts=%d (control: expected=1)\n", execution->attempts);
    const bool control = execution->attempts == 1;
    execution->attempts=0;
    stun->broken=false;
    hit.flags.bits=int(RE::HitData::Flag::kBash);
    handler.processHit(&attacker, &victim, hit);
    std::printf("auto_breaking_bash: broken=%d attempts=%d\n", stun->broken, execution->attempts);
    handler.processHit(&attacker, &victim, hit);
    std::printf("auto_following_bash: attempts=%d (bash path returns before auto check)\n", execution->attempts);
    if (!control) return 2;
    // This case records current semantics; same-hit auto execution is a design choice.
    return late && execution->attempts==0 ? 0 : 2;
}
'''
    return stub + extract(source, "void hitProcessor::processHit(") + tests


def timing_program():
    source = source_at_baseline("src/bin/executionHandler.cpp")
    stub = r'''
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>
namespace RE {
struct TESIdleForm {};
enum class DEFAULT_OBJECT { kActionIdle };
struct Process {};
struct Actor {
    bool dead=false, loaded=true;
    Process process;
    struct Runtime { Process* currentProcess; } runtime {&process};
    Runtime& GetActorRuntimeData() { return runtime; }
};
namespace Offset {
inline int calls=0, stale_calls=0;
bool playPairedIdle(Process* process, Actor* executor, DEFAULT_OBJECT, TESIdleForm*, bool, bool, Actor* victim) {
    ++calls;
    if (!process || executor->dead || victim->dead || !executor->loaded || !victim->loaded) {
        ++stale_calls;
        return false;
    }
    return true;
}
}
}
namespace SKSE {
struct TaskInterface {
    std::vector<std::function<void()>> pending;
    void AddTask(std::function<void()> task) { pending.push_back(task); }
    void drain() { for (auto& task : pending) task(); pending.clear(); }
};
TaskInterface* GetTaskInterface() { static TaskInterface tasks; return &tasks; }
}
struct executionHandler {
    void async_queueExecutionThreadFun(RE::Actor*, RE::Actor*, RE::TESIdleForm*, int);
};
'''
    tests = r'''
int main() {
    RE::Actor executor, victim;
    RE::TESIdleForm idle;
    executionHandler handler;
    auto* tasks=SKSE::GetTaskInterface();
    handler.async_queueExecutionThreadFun(&executor, &victim, &idle, 1);
    tasks->drain();
    const bool control=RE::Offset::calls==1 && RE::Offset::stale_calls==0;
    std::printf("queued_valid_control: calls=%d stale=%d (expected=1,0)\n", RE::Offset::calls, RE::Offset::stale_calls);
    RE::Offset::calls=0;
    handler.async_queueExecutionThreadFun(&executor, &victim, &idle, 1);
    victim.dead=true;
    victim.loaded=false;
    executor.runtime.currentProcess=nullptr;
    tasks->drain();
    std::printf("state_changed_before_task_drain: calls=%d stale=%d (expected no stale engine call)\n",
                RE::Offset::calls, RE::Offset::stale_calls);
    if (!control) return 2;
    return RE::Offset::stale_calls ? 1 : 0;
}
'''
    return (stub + extract(source, "inline void playExecutionIdle(")
            + extract(source, "void executionHandler::async_queueExecutionThreadFun(") + tests)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--case", choices=["selection", "numeric", "dispatch", "flow", "timing"], default="selection")
    args = parser.parse_args()
    source = source_at_baseline("src/bin/executionHandler.cpp")
    program = {"selection": lambda: PRELUDE + extract(source, "void executionHandler::tryPcExecution()") + MAIN,
               "numeric": numeric_program, "dispatch": dispatch_program, "flow": flow_program,
               "timing": timing_program}[args.case]()
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output([str(vswhere), "-latest", "-products", "*",
                                       "-property", "installationPath"], text=True).strip()
    vcvars = Path(install) / "VC/Auxiliary/Build/vcvars64.bat"
    with tempfile.TemporaryDirectory(prefix="valhalla-execution-probe-") as scratch:
        scratch = Path(scratch)
        cpp = scratch / "probe.cpp"
        exe = scratch / "probe.exe"
        cpp.write_text(program, encoding="utf-8")
        build = scratch / "build.cmd"
        build.write_text(f'@echo off\ncall "{vcvars}" >nul\n'
                         'if errorlevel 1 exit /b %errorlevel%\n'
                         f'clang++ -std=c++20 -O0 "{cpp}" -o "{exe}"\n', encoding="utf-8")
        built = subprocess.run(["cmd.exe", "/d", "/c", str(build)],
                               capture_output=True, text=True)
        if built.returncode:
            print(built.stdout + built.stderr)
            return built.returncode
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        report = (f"Case: {args.case}; selected source function bodies are unchanged\n"
                  "Boundary: controlled engine substitutes; no game runtime\n"
                  + result.stdout + result.stderr)
        print(report, end="")
        if args.output:
            args.output.write_text(report, encoding="utf-8")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
