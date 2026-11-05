#pragma once
#include <cstdint>
namespace OwnedEffects {
enum Flag : std::uint32_t { exhausted=1, perk=2, downed=4, counter=8 };
void initialize();
void add(RE::Actor* actor, Flag effect);
bool contains(RE::Actor* actor, Flag effect);
void remove(RE::Actor* actor, Flag effect);
void retry(RE::Actor* actor, Flag effect);
void restore();
bool pending();
void update();
}
