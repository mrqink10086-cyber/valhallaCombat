#pragma once
#include <mutex>
#include <unordered_map>
#include <unordered_set>
class stunHandler {
public:
    enum STUNSOURCE { lightAttack, powerAttack, bash, powerBash, timedBlock, parry, counterAttack };
    static stunHandler* GetSingleton() { static stunHandler value; return &value; }
    void update(float delta = -1);
    bool pendingCleanup() const;
    void reset();
    void refreshMeter(bool enabled);
    void processStunDamage(STUNSOURCE, RE::TESObjectWEAP*, RE::Actor*, RE::Actor*, float);
    void modStun(RE::Actor*, float);
    float getCurrentStun(RE::Actor*);
    float getMaxStun(RE::Actor*);
    bool getIsStunBroken(RE::Actor*);
    static float getCurrentStun_static(RE::Actor* actor) { return GetSingleton()->getCurrentStun(actor); }
    static float getMaxStun_static(RE::Actor* actor) { return GetSingleton()->getMaxStun(actor); }
    std::unordered_set<RE::ActorHandle> getStunBrokenActors();
private:
    bool mutating{false}, resetPending{false};
    struct Mutation {
        stunHandler& owner;
        explicit Mutation(stunHandler& value) : owner(value) { owner.mutating=true; }
        ~Mutation() { owner.mutating=false; if (std::exchange(owner.resetPending,false)) owner.reset(); }
    };
    struct State {
        float maximum{1}, current{1}, countdown{3};
        bool broken{false}, downed{false}, cleanup{false}, meterOwned{false};
    };
    struct Snapshot { float maximum, current; bool broken; };
    std::unordered_map<RE::ActorHandle, State> states;
    std::mutex snapshotMutex;
    std::unordered_map<RE::ActorHandle, Snapshot> snapshots;
    float flashCountdown{0};
    void publish();
    void damageStun(RE::Actor*, RE::Actor*, float);
    bool recover(RE::Actor*, State&);
};
