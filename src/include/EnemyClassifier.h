#pragma once
#include "EnemyRules.h"
#include <string>
namespace encounters {
void load();
Classification classify(RE::Actor* actor);
std::string describe(const Classification& value);
}
