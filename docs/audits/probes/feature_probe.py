"""Run unchanged feature functions with controlled engine substitutes.

Exit 0 means the documented observations reproduced, including defects.
This is a source audit probe, not a Skyrim integration test.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from execution_probe import ROOT, extract, source_at_baseline


def bodies(path, *markers):
    source = source_at_baseline(path)
    return "\n".join(extract(source, marker) for marker in markers)


def block_program():
    stub = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
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
struct PlayerCharacter : Actor {
    static PlayerCharacter* GetSingleton() { static PlayerCharacter pc; return &pc; }
};
struct HitData { enum class Flag { kPowerAttack }; float totalDamage=100; };
}
namespace REX { template<class E, class T> struct EnumSet { bool any(E) { return false; } }; }
namespace settings {
inline bool bTimedBlockToggle=true,bTimedBlockProjectileToggle=true,bTackleToggle=true,bGuardBreak=false;
inline float fTimedBlockWindow=.3f;
}
namespace Utils::Actor {
bool canBlock(RE::Actor*) { return true; }
void damageav(RE::Actor* actor, RE::ActorValue, float value) { actor->stamina=std::max(0.f,actor->stamina-value); }
}
namespace inlineUtils { void offsetRealDamage(float&,RE::Actor*,RE::Actor*) {} }
struct ValhallaCombat {
    enum HANDLER { blockHandler };
    static ValhallaCombat* GetSingleton() { static ValhallaCombat h; return &h; }
    void activateUpdate(HANDLER) {}
};
struct debuffHandler {
    int calls=0;
    static debuffHandler* GetSingleton() { static debuffHandler h; return &h; }
    void initStaminaDebuff(RE::Actor*) { ++calls; }
};
struct reactionHandler { enum { kLarge,kMedium }; static void triggerStagger(RE::Actor*,RE::Actor*,int) {} };
namespace PRECISION_API {
struct PrecisionHitData { RE::Actor* attacker; RE::Actor* target; };
struct PreHitCallbackReturn { bool bIgnoreHit=false; };
}
inline float costMult=1;
float getBlockStaminaCostMult(RE::Actor*,RE::Actor*,REX::EnumSet<RE::HitData::Flag,std::uint32_t>) { return costMult; }
struct blockHandler {
    enum blockWindowPenaltyLevel { none,light,medium,heavy };
    blockWindowPenaltyLevel pcBlockWindowPenalty=none;
    bool isPcTimedBlocking=false,isBlockButtonPressed=false,isPcTimedBlockSuccess=false;
    float pcTimedBlockTimer=0;
    int tackles=0;
    static blockHandler* GetSingleton() { static blockHandler h; return &h; }
    void onBlockKeyDown(); void onBlockKeyUp();
    void onPcTimedBlockEnd() { isPcTimedBlocking=false; }
    void processStaminaBlock(RE::Actor*,RE::Actor*,REX::EnumSet<RE::HitData::Flag,std::uint32_t>,RE::HitData&);
    static PRECISION_API::PreHitCallbackReturn precisionPrehitCallbackFunc(const PRECISION_API::PrecisionHitData&);
    bool processMeleeTimedBlock(RE::Actor*,RE::Actor*,bool,bool) { return false; }
    bool processMeleeTackle(RE::Actor*,RE::Actor*) { ++tackles; return true; }
    bool getIsForcedTimedBlocking(RE::Actor*) { return false; }
    bool getIsForcedPerfectBlocking(RE::Actor*) { return false; }
};
using HITFLAG=RE::HitData::Flag;
'''
    tests = r'''
int main() {
    bool ok=true;
    auto* h=blockHandler::GetSingleton();
    h->onBlockKeyDown();
    ok &= h->isBlockButtonPressed;
    h->onBlockKeyUp(); h->onBlockKeyDown();
    std::printf("repress_inside_window: held=%d (new physical down should set true)\n",h->isBlockButtonPressed);
    ok &= !h->isBlockButtonPressed;
    h->isPcTimedBlocking=false; h->onBlockKeyDown();
    settings::bTimedBlockToggle=false; settings::bTimedBlockProjectileToggle=false;
    h->onBlockKeyUp();
    std::printf("disable_before_keyup: held=%d (physical release should clear)\n",h->isBlockButtonPressed);
    ok &= h->isBlockButtonPressed;
    RE::Actor blocker,attacker;
    PRECISION_API::PrecisionHitData contact{&attacker,&blocker};
    h->precisionPrehitCallbackFunc(contact);
    std::printf("precision_tackle_only: calls=%d (tackle enabled, timed block disabled)\n",h->tackles);
    ok &= h->tackles==0;
    settings::bTimedBlockToggle=true;
    h->precisionPrehitCallbackFunc(contact);
    std::printf("precision_tackle_with_timed_block_control: calls=%d\n",h->tackles);
    ok &= h->tackles==1;
    auto* d=debuffHandler::GetSingleton();
    RE::HitData hit;
    blocker.stamina=100; h->processStaminaBlock(&blocker,&attacker,{},hit);
    std::printf("exact_depletion: stamina=%.1f debuff_calls=%d\n",blocker.stamina,d->calls);
    ok &= blocker.stamina==0 && d->calls==0;
    blocker.stamina=99; hit.totalDamage=100;
    h->processStaminaBlock(&blocker,&attacker,{},hit);
    std::printf("insufficient_control: stamina=%.1f debuff_calls=%d health_damage=%.1f\n",blocker.stamina,d->calls,hit.totalDamage);
    ok &= d->calls==1 && hit.totalDamage==1;
    costMult=.5f; blocker.stamina=40; hit.totalDamage=100;
    h->processStaminaBlock(&blocker,&attacker,{},hit);
    std::printf("partial_block_mult_0.5: health_damage=%.1f (linear conversion would be20)\n",hit.totalDamage);
    ok &= hit.totalDamage==-60;
    costMult=2; blocker.stamina=50; hit.totalDamage=100;
    h->processStaminaBlock(&blocker,&attacker,{},hit);
    std::printf("partial_block_mult_2: health_damage=%.1f (linear conversion would be75)\n",hit.totalDamage);
    ok &= hit.totalDamage==87.5f;
    return ok ? 0 : 2;
}
'''
    return stub + bodies("src/bin/blockHandler.cpp", "void blockHandler::onBlockKeyDown()",
        "void blockHandler::onBlockKeyUp()", "void blockHandler::processStaminaBlock(",
        "PRECISION_API::PreHitCallbackReturn blockHandler::precisionPrehitCallbackFunc(") + tests


def debuff_program():
    stub = r'''
#include <cstdio>
#include <mutex>
#include <set>
#include <shared_mutex>
namespace RE {
enum class ActorValue { kStamina };
enum class ACTOR_VALUE_MODIFIER { kTemporary };
struct Actor;
struct ActorHandle {
    Actor* actor;
    explicit operator bool() const { return actor!=nullptr; }
    struct Ptr { Actor* actor; Actor* get() { return actor; } };
    Ptr get() const { return {actor}; }
    bool operator<(const ActorHandle& other) const { return std::less<Actor*>{}(actor,other.actor); }
};
struct Actor {
    float stamina=100;
    bool exhausted=true;
    Actor* AsActorValueOwner() { return this; }
    float GetActorValue(ActorValue) { return stamina; }
    float GetPermanentActorValue(ActorValue) { return 100; }
    float GetActorValueModifier(ACTOR_VALUE_MODIFIER,ActorValue) { return 0; }
    bool IsPlayerRef() { return true; }
    ActorHandle GetHandle() { return {this}; }
    void SetGraphVariableBool(const char*,bool value) { exhausted=value; }
};
}
namespace settings { inline bool bUIAlert=true; }
namespace data { inline int debuffPerk=1; }
namespace Utils::Actor { void safeRemovePerk(int,RE::Actor*) {} }
namespace TrueHUDUtils { void revertAvMeter(RE::Actor*,RE::ActorValue) {} }
struct ValhallaCombat {
    enum { debuffHandler };
    static ValhallaCombat* GetSingleton() { static ValhallaCombat h; return &h; }
    void deactivateUpdate(int) {}
};
using uniqueLocker=std::unique_lock<std::shared_mutex>;
struct debuffHandler {
    std::set<RE::ActorHandle> actorInDebuff;
    std::shared_mutex mtx_actorInDebuff;
    bool async_pcStaminaMeterFlash_b=true;
    const char* gv_bExhausted="exhausted";
    void update(); void stopDebuff(RE::Actor*); void revertDebuffUI(RE::Actor*);
};
'''
    tests = r'''
int main() {
    debuffHandler h;
    RE::Actor actor;
    h.actorInDebuff.insert(actor.GetHandle());
    h.update();
    std::printf("natural_recovery: tracked=%zu graph_exhausted=%d\n",h.actorInDebuff.size(),actor.exhausted);
    bool ok=h.actorInDebuff.empty() && actor.exhausted;
    h.stopDebuff(&actor);
    std::printf("stop_after_natural_recovery: graph_exhausted=%d\n",actor.exhausted);
    ok &= actor.exhausted;
    h.actorInDebuff.insert(actor.GetHandle());
    h.stopDebuff(&actor);
    std::printf("explicit_stop_control: graph_exhausted=%d\n",actor.exhausted);
    ok &= !actor.exhausted;
    h.async_pcStaminaMeterFlash_b=true;
    settings::bUIAlert=false;
    h.revertDebuffUI(&actor);
    std::printf("ui_disabled_before_cleanup: worker_flag=%d\n",h.async_pcStaminaMeterFlash_b);
    ok &= h.async_pcStaminaMeterFlash_b;
    settings::bUIAlert=true;
    h.revertDebuffUI(&actor);
    std::printf("ui_enabled_cleanup_control: worker_flag=%d\n",h.async_pcStaminaMeterFlash_b);
    return ok && !h.async_pcStaminaMeterFlash_b ? 0 : 2;
}
'''
    return stub+bodies("src/bin/debuffHandler.cpp", "void debuffHandler::update()",
        "void debuffHandler::stopDebuff(", "void debuffHandler::revertDebuffUI(")+tests


def timing_program():
    stub = r'''
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
namespace RE {
enum class ATTACK_STATE_ENUM { kNone,kBash };
struct ButtonEvent {};
struct PlayerControlsData {};
struct PlayerCharacter {
    int stops=0;
    static PlayerCharacter* GetSingleton() { static PlayerCharacter pc; return &pc; }
    bool IsBlocking() { return true; }
    PlayerCharacter* AsActorState() { return this; }
    ATTACK_STATE_ENUM GetAttackState() { return ATTACK_STATE_ENUM::kNone; }
    bool IsStaggered() { return false; }
    void NotifyAnimationGraph(const char*) { ++stops; }
};
struct AttackBlockHandler { int releases=0; void ProcessButton(ButtonEvent*,PlayerControlsData*) { ++releases; } };
namespace Offset {
inline std::mutex mutex;
inline std::condition_variable cv;
inline float multiplier=.4f;
inline int resets=0;
void SGTM(float value) {
    std::lock_guard lock(mutex);
    multiplier=value;
    if (value==1) ++resets;
    cv.notify_all();
}
}
}
namespace SKSE {
struct TaskInterface {
    std::vector<std::function<void()>> pending;
    void AddTask(std::function<void()> task) { pending.push_back(task); }
    void drain() { for(auto& task:pending) task(); pending.clear(); }
};
TaskInterface* GetTaskInterface() { static TaskInterface tasks; return &tasks; }
}
struct blockHandler {
    bool held=false;
    static blockHandler* GetSingleton() { static blockHandler h; return &h; }
    bool isBlockKeyHeld() { return held; }
};
'''
    tests = r'''
int main() {
    RE::AttackBlockHandler handler;
    RE::PlayerControlsData data;
    auto* h=blockHandler::GetSingleton();
    auto* pc=RE::PlayerCharacter::GetSingleton();
    unblock_delayed_threadfunc(&handler,new RE::ButtonEvent,&data,0);
    SKSE::GetTaskInterface()->drain();
    std::printf("unblock_released_control: releases=%d stops=%d\n",handler.releases,pc->stops);
    bool ok=handler.releases==1 && pc->stops==1;
    handler.releases=0; pc->stops=0;
    unblock_delayed_threadfunc(&handler,new RE::ButtonEvent,&data,0);
    h->held=true;
    SKSE::GetTaskInterface()->drain();
    std::printf("unblock_repressed_before_drain: releases=%d stops=%d\n",handler.releases,pc->stops);
    ok &= handler.releases==0 && pc->stops==1;
    slowTime(.05f,.1f);
    slowTime(.3f,.1f);
    std::unique_lock lock(RE::Offset::mutex);
    bool first=RE::Offset::cv.wait_for(lock,std::chrono::seconds(2),[]{return RE::Offset::resets>=1;});
    const float at_first=RE::Offset::multiplier;
    const int count=RE::Offset::resets;
    std::printf("overlap_after_first_reset: multiplier=%.1f resets=%d (second request still pending)\n",at_first,count);
    bool done=RE::Offset::cv.wait_for(lock,std::chrono::seconds(2),[]{return RE::Offset::resets>=2;});
    std::printf("prior_multiplier_0.4_after_all_resets: multiplier=%.1f\n",RE::Offset::multiplier);
    return ok && first && done && count==1 && at_first==1 && RE::Offset::multiplier==1 ? 0 : 2;
}
'''
    return stub+bodies("src/bin/Hooks.cpp","static void unblock_delayed_threadfunc(")+bodies(
        "src/include/Utils.h","inline void slowTime(")+tests


def stun_program():
    stub = r'''
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
namespace RE {
struct Actor { bool downed=true; };
struct ActorHandle {
    Actor* actor;
    explicit operator bool() const { return actor!=nullptr; }
    struct Ptr { Actor* actor; Actor* get() { return actor; } };
    Ptr get() const { return {actor}; }
    bool operator<(const ActorHandle& other) const { return std::less<Actor*>{}(actor,other.actor); }
};
namespace Offset { inline float dt=1.f/60; inline float* g_deltaTime=&dt; }
}
namespace Utils::Actor { bool isDead(RE::Actor*) { return false; } }
namespace TrueHUDUtils { void revertSpecialMeter(RE::Actor*) {} }
namespace settings { inline bool bDownedStateToggle=true; }
struct reactionHandler {
    static void recoverDownedState(RE::Actor* actor) { actor->downed=false; }
};
struct ValhallaCombat {
    enum { stunHandler };
    static ValhallaCombat* GetSingleton() { static ValhallaCombat h; return &h; }
    void deactivateUpdate(int) {}
};
using uniqueLocker=std::unique_lock<std::mutex>;
struct stunHandler {
    struct Data {
        float getRegenCountDown() { return 0; }
        void modRegenCountDown(float) {}
        bool canRegen() { return true; }
        bool regenStun() { return true; }
        void setCombatRegeneration(bool) {}
    };
    std::map<RE::ActorHandle,std::shared_ptr<Data>> actorStunDataMap;
    std::mutex mtx_ActorStunDataMap;
    void safeErase_StunBrokenActors(RE::ActorHandle) {}
    void update();
};
'''
    tests = r'''
int main() {
    stunHandler h;
    RE::Actor actor;
    h.actorStunDataMap.emplace(RE::ActorHandle{&actor},std::make_shared<stunHandler::Data>());
    h.update();
    std::printf("downed_recovery_toggle_on_control: tracked=%zu downed=%d\n",h.actorStunDataMap.size(),actor.downed);
    bool ok=h.actorStunDataMap.empty() && !actor.downed;
    actor.downed=true;
    h.actorStunDataMap.emplace(RE::ActorHandle{&actor},std::make_shared<stunHandler::Data>());
    settings::bDownedStateToggle=false;
    h.update();
    std::printf("disable_downed_before_recovery: tracked=%zu downed=%d\n",h.actorStunDataMap.size(),actor.downed);
    return ok && h.actorStunDataMap.empty() && actor.downed ? 0 : 2;
}
'''
    return stub+bodies("src/bin/stunHandler.cpp","void stunHandler::update()")+tests


def worker_program():
    stub = r'''
#include <chrono>
#include <cstdio>
#include <thread>
namespace RE {
struct Actor {
    std::thread::id caller;
    void RemoveSpell(int) { caller=std::this_thread::get_id(); }
};
}
struct blockHandler {
    struct EldenCounterCompatibility {
        static inline float ec_Time=0;
        static inline int ec_triggerSpell=1;
        static void async_removeECTriggerSpell(RE::Actor*);
    };
};
'''
    tests = r'''
int main() {
    RE::Actor actor;
    const auto main_id=std::this_thread::get_id();
    std::jthread worker(blockHandler::EldenCounterCompatibility::async_removeECTriggerSpell,&actor);
    const auto worker_id=worker.get_id();
    worker.join();
    std::printf("elden_counter_remove_spell: on_worker=%d on_main=%d\n",actor.caller==worker_id,actor.caller==main_id);
    return actor.caller==worker_id && actor.caller!=main_id ? 0 : 2;
}
'''
    return stub+bodies("src/bin/blockHandler.cpp",
        "void blockHandler::EldenCounterCompatibility::async_removeECTriggerSpell(")+tests


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case",choices=["block","debuff","timing","stun","worker"],required=True)
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    program={"block":block_program,"debuff":debuff_program,"timing":timing_program,
             "stun":stun_program,"worker":worker_program}[args.case]()
    vswhere=Path(os.environ["ProgramFiles(x86)"])/"Microsoft Visual Studio/Installer/vswhere.exe"
    install=subprocess.check_output([str(vswhere),"-latest","-products","*","-property","installationPath"],text=True).strip()
    vcvars=Path(install)/"VC/Auxiliary/Build/vcvars64.bat"
    with tempfile.TemporaryDirectory(prefix="valhalla-feature-probe-") as scratch:
        scratch=Path(scratch)
        cpp=scratch/"probe.cpp"
        exe=scratch/"probe.exe"
        cpp.write_text(program,encoding="utf-8")
        build=scratch/"build.cmd"
        build.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
                         f'clang++ -std=c++20 -O0 "{cpp}" -o "{exe}"\n',encoding="utf-8")
        built=subprocess.run(["cmd.exe","/d","/c",str(build)],capture_output=True,text=True)
        if built.returncode:
            print(built.stdout+built.stderr)
            return built.returncode
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
        report=(f"Case: {args.case}; selected source function bodies are unchanged\n"
                "Boundary: controlled engine substitutes; no game runtime\n"
                "Exit 0 confirms observations reproduced; it does not mean defects fixed\n"
                +result.stdout+result.stderr)
        print(report,end="")
        if args.output:
            args.output.write_text(report,encoding="utf-8")
        return result.returncode


if __name__=="__main__":
    raise SystemExit(main())
