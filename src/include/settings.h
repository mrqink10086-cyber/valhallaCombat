#pragma once
#include "SettingsStore.h"
#include <atomic>

class settings {
public:
 struct facts {
  static inline std::atomic<bool> TrueHudAPI_HasSpecialBarControl{false};
  static inline std::atomic<bool> TrueHudAPI_Obtained{false};
  static inline std::atomic<bool> PrecisionAPI_Obtained{false};
  static inline std::atomic<bool> EldenParry_ObtainedAPI{false};
 };
#define VC_SETTING(type,name,initial,section,page,label,low,high) static inline type name=initial;
#include "Settings.def"
#undef VC_SETTING
 static inline float fMeleeCostHeavyMiss_Percent=0.4f;
 static inline float fMeleeCostHeavyHit_Percent=0.333f;
 static inline bool bSpiritToggle=true, bSpiritGuardStanceToggle=true;
 static inline float fHeavyAttackSpiritGain=20, fLightAttackSpiritLoss=5;
 static inline float fSpiritDamageBuff_1=1.2f, fSpiritDamageBuff_2=1.4f, fSpiritDamageBuff_3=1.6f;
 static inline bool bPlayerExecution=false, bExecutionLimit=true, bBalanceToggle=true;
 static inline bool bExhaustionAIToggle=true, bCirclingAIToggle=true, bPerilousAttackAIToggle=true;
 static void readSettings();
 static void submit(config::Values values, std::uint64_t expected);
 static void updateGameSettings();
 static void cleanupReceipt();
private:
 static void apply(const config::Values& values);
};
