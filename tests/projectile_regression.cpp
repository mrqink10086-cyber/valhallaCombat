#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
namespace RE {
enum class ActorValue { kBlock };
template<class T> struct Ptr {
    T* value{};
    explicit operator bool() const { return value != nullptr; }
    T* get() const { return value; }
    T* operator->() const { return value; }
};
struct Actor;
struct Projectile;
struct ActorHandle { Actor* value{}; Ptr<Actor> get() const; };
struct ObjectRefHandle {
    Projectile* value{};
    Ptr<Projectile> get() const;
    bool operator==(const ObjectRefHandle&) const = default;
};
struct Actor {
    bool live{true}, loaded{true}, dead{false}, player{true}, blocking{true};
    int animations{};
    ActorHandle GetHandle() { return {this}; }
    bool Is3DLoaded() const { return loaded; }
    bool IsPlayerRef() const { return player; }
    bool IsBlocking() const { return blocking; }
    void NotifyAnimationGraph(const char*) { ++animations; }
};
struct Projectile {
    enum class Flags { kDestroyed=1, kFading=2 };
    struct FlagSet { int bits{}; bool any(Flags f) const { return (bits & static_cast<int>(f)) != 0; } };
    struct Runtime { FlagSet flags; ActorHandle shooter; } runtime;
    bool live{true}, loaded{true};
    ObjectRefHandle GetHandle() { return {this}; }
    Projectile* AsProjectile() { return this; }
    bool Is3DLoaded() const { return loaded; }
    Runtime& GetProjectileRuntimeData() { return runtime; }
};
Ptr<Actor> ActorHandle::get() const { return {value && value->live ? value : nullptr}; }
Ptr<Projectile> ObjectRefHandle::get() const { return {value && value->live ? value : nullptr}; }
struct PlayerCharacter {
    static PlayerCharacter* GetSingleton() { static PlayerCharacter player; return &player; }
    float experience{};
    void AddSkillExperience(ActorValue, float amount) { experience+=amount; }
};
namespace Offset {
int destroyed{};
std::function<void()> onDestroy;
void destroyProjectile(Projectile*) { ++destroyed; if (onDestroy) onDestroy(); }
}
}
template<> struct std::hash<RE::ObjectRefHandle> {
    std::size_t operator()(RE::ObjectRefHandle h) const { return std::hash<void*>{}(h.value); }
};
namespace Utils::Actor { bool isDead(RE::Actor* actor) { return actor->dead; } }
namespace ValhallaUtils {
int owners{}, retargeted{}, reflected{};
void resetProjectileOwner(RE::Projectile*, RE::Actor*) { ++owners; }
void RetargetProjectile(RE::Projectile*, RE::Actor*) { ++retargeted; }
void ReflectProjectile(RE::Projectile*) { ++reflected; }
}
class CombatRuntime {
public:
    bool active{true};
    std::vector<std::function<void()>> work;
    static CombatRuntime& get() { static CombatRuntime runtime; return runtime; }
    std::uint64_t schedule(float, std::function<void()> task) {
        if (!active) return 0;
        work.push_back(std::move(task)); return work.size();
    }
    void drain() { auto batch=std::move(work); work.clear(); for (auto& task : batch) task(); }
};
class blockHandler {
public:
    enum class blockType { timed };
    std::uint64_t blockGeneration{};
    std::unordered_set<RE::ObjectRefHandle> pendingProjectiles;
    int effects{};
    void playBlockEffects(RE::Actor*, RE::Actor*, blockType) { ++effects; }
    bool queueProjectileBlock(RE::Actor*, RE::Projectile*, bool, float);
};
#include "projectile.inc"

int main()
{
    try {
        auto& runtime=CombatRuntime::get();
        auto* player=RE::PlayerCharacter::GetSingleton();
        blockHandler block;
        RE::Actor actor, shooter;
        RE::Projectile projectile;
        projectile.runtime.shooter={&shooter};
        auto submit=[&](bool deflect) {
            check(block.queueProjectileBlock(&actor,&projectile,deflect,3),"schedule accepted");
            block.pendingProjectiles.insert(projectile.GetHandle());
            check(runtime.work.size()==1,"one task per projectile");
        };
        submit(true);
        check(ValhallaUtils::owners==0 && block.effects==0,"collision stack does not mutate engine");
        runtime.drain();
        check(ValhallaUtils::owners==1 && ValhallaUtils::retargeted==1,"retarget valid shooter");
        check(block.effects==1 && actor.animations==1 && player->experience==3,"effects and experience");
        check(block.pendingProjectiles.empty(),"cleanup after deflection");

        shooter.live=false;
        submit(true); runtime.drain();
        check(ValhallaUtils::reflected==1,"missing shooter reflects");

        submit(false); runtime.drain();
        check(RE::Offset::destroyed==1 && block.effects==3,"destroy and effects share one task");

        submit(false); ++block.blockGeneration; block.pendingProjectiles.clear(); runtime.drain();
        check(RE::Offset::destroyed==1 && block.effects==3,"reset cancels stale engine command");

        submit(false); projectile.live=false; runtime.drain();
        check(RE::Offset::destroyed==1 && block.effects==4 && block.pendingProjectiles.empty(),"expired projectile still permits actor effect and clears dedup");
        projectile.live=true;

        submit(true); actor.live=false; runtime.drain();
        check(ValhallaUtils::owners==2 && block.effects==4 && block.pendingProjectiles.empty(),"expired blocker drops deflection and effects");
        actor.live=true;

        projectile.runtime.flags.bits=static_cast<int>(RE::Projectile::Flags::kDestroyed);
        submit(false); runtime.drain();
        check(RE::Offset::destroyed==1 && block.pendingProjectiles.empty(),"already destroyed projectile ignored");
        projectile.runtime.flags.bits=0;

        RE::Offset::onDestroy=[] { throw 7; };
        submit(false);
        try { runtime.drain(); } catch (int) {}
        check(block.pendingProjectiles.empty(),"exception cleans pending handle");
        RE::Offset::onDestroy={};

        runtime.active=false;
        check(!block.queueProjectileBlock(&actor,&projectile,false,3),"rejected scheduling reported");
        check(runtime.work.empty(),"no rejected command retained");
        std::cout << "projectile regressions passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
