#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include "include/SettingsStore.h"
class settings {
public:
#define VC_SETTING(type,name,initial,...) static inline type name=initial;
#include "include/Settings.def"
#undef VC_SETTING
 static void apply(const config::Values&);
 static void updateGameSettings() {}
};
namespace RE {
struct TESRace { struct { float staminaRegen=1; } data; };
struct TESDataHandler {
 static TESDataHandler* GetSingleton() { return nullptr; }
 template<class T> std::vector<T*> GetFormArray() { return {}; }
};
}
namespace { std::unordered_map<RE::TESRace*,float> originalRegeneration; void setGameSetting(const char*,float) {} }
struct CombatRuntime { static CombatRuntime& get() { static CombatRuntime r; return r; } bool ready() { return true; } void clearSlowTime() {} };
struct blockHandler { int resets=0; static blockHandler* GetSingleton() { static blockHandler b; return &b; } void reset() { ++resets; } };
struct attackHandler { bool cached=true; static attackHandler* GetSingleton() { static attackHandler a; return &a; } void reset() { cached=false; } };
struct debuffHandler { static debuffHandler* GetSingleton() { static debuffHandler d; return &d; } void update(float) {} };
inline bool hudControl=true;
inline int colors=1;
struct stunHandler {
 static stunHandler* GetSingleton() { static stunHandler s; return &s; }
 void refreshMeter(bool enabled) { if (!hudControl) throw std::runtime_error("HUD colors applied/reverted without control"); colors=enabled?1:0; }
 void update(float) {}
 bool pendingCleanup() { return false; }
};
struct ValhallaCombat {
 static ValhallaCombat* GetSingleton() { static ValhallaCombat v; return &v; }
 void requestTrueHudSpecialBarControl() { hudControl=true; }
 void releaseTrueHudSpecialBarControl() { hudControl=false; }
};
namespace OwnedEffects { bool pending() { return false; } }
#include "apply_functions.inc"
void require(bool value) { if (!value) throw std::runtime_error("hot apply regression"); }
int main() {
 auto values=config::defaults();
 values[static_cast<std::size_t>(config::Key::bAttackStaminaToggle)]=0;
 values[static_cast<std::size_t>(config::Key::bStunMeterToggle)]=0;
 settings::apply(values);
 require(!attackHandler::GetSingleton()->cached && colors==0 && !hudControl);
 values[static_cast<std::size_t>(config::Key::bStunMeterToggle)]=1;
 const auto resets=blockHandler::GetSingleton()->resets;
 values[static_cast<std::size_t>(config::Key::uPerfectBlockAccum)]=2;
 settings::apply(values);
 require(colors==1 && hudControl && blockHandler::GetSingleton()->resets>resets);
 attackHandler::GetSingleton()->cached=true;
 values[static_cast<std::size_t>(config::Key::bAttackStaminaToggle_NPC)]=!settings::bAttackStaminaToggle_NPC;
 settings::apply(values); require(!attackHandler::GetSingleton()->cached);
 std::puts("hot attack switch cancels cache; HUD cleanup precedes release; accumulation applies: PASS");
}
