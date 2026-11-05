#include "include/Menu.h"
#include "include/settings.h"
#include "include/CombatRuntime.h"
#include "include/executionHandler.h"
#include <memory>
#include <unordered_map>
#pragma warning(push, 0)
#include "include/vendor/MenuFramework/SKSEMenuFramework.h"
#pragma warning(pop)

namespace {
std::mutex draftMutex;
config::Values draft=config::defaults();
std::uint64_t revision=0;
bool dirty=false, chinese=true;
std::atomic<int> binding{-1};
std::atomic<int> consumed{-1};
std::unique_ptr<SKSEMenuFramework::Model::InputEvent> input;
std::unique_ptr<SKSEMenuFramework::Model::Event> events;
float frameworkVersion=0;
const char* text(const char* english, const char* zh) { return chinese?zh:english; }
int keyCode(RE::ButtonEvent* button)
{
 const auto code=button->GetIDCode();
 switch (button->GetDevice()) {
 case RE::INPUT_DEVICE::kKeyboard: return static_cast<int>(code);
 case RE::INPUT_DEVICE::kMouse: return static_cast<int>(code)+256;
 case RE::INPUT_DEVICE::kGamepad: {
  using K=RE::BSWin32GamepadDevice::Key;
  constexpr K keys[]{K::kUp,K::kDown,K::kLeft,K::kRight,K::kStart,K::kBack,K::kLeftThumb,K::kRightThumb,K::kLeftShoulder,K::kRightShoulder,K::kA,K::kB,K::kX,K::kY,K::kLeftTrigger,K::kRightTrigger};
  for (int i=0;i<16;++i) if (static_cast<std::uint32_t>(keys[i])==code) return 266+i;
  return -1;
 }
 default: return -1;
 }
}
bool __stdcall capture(RE::InputEvent* event)
{
 if (!event || event->eventType!=RE::INPUT_EVENT_TYPE::kButton) return binding.load()>=0;
 auto button=static_cast<RE::ButtonEvent*>(event); const int code=keyCode(button);
 if (code<0) return binding.load()>=0;
 if (consumed.load()==code) { if (button->IsUp()) consumed=-1; return true; }
 if (binding.load()<0) return false;
 if (button->IsDown()) {
  const int index=binding.exchange(-1);
  if (index>=0) { std::lock_guard lock(draftMutex); draft[static_cast<std::size_t>(index)]=code==1?-1:code; dirty=true; consumed=code; }
 }
 return true;
}
void __stdcall menuEvent(SKSEMenuFramework::Model::EventType event)
{
 if (event==SKSEMenuFramework::Model::kCloseMenu) { binding=-1; consumed=-1; }
}
const char* translated(const config::Definition& entry)
{
 static const std::unordered_map<std::string,const char*> labels{
 {"bUIAlert","耗尽提示"},{"bNonCombatStaminaCost","战斗外消耗耐力"},{"fStaminaRegenMult","种族耐力恢复倍率"},{"fStaminaRegenLimit","恢复率上限"},{"fStaminaRegenMin","恢复率下限"},
 {"fCombatStaminaRegenMult","战斗耐力恢复倍率"},{"fBlockingStaminaRegenMult","格挡耐力恢复倍率"},{"fStaminaRegenDelay","恢复延迟（秒）"},{"bBlockedHitRegenStamina","被格挡命中时恢复耐力"},{"bBlockStaminaToggle","耐力格挡"},{"bGuardBreak","耗尽时破防"},
 {"fBckShdStaminaMult_PC_Block_NPC","玩家盾牌格挡消耗倍率"},{"fBckWpnStaminaMult_PC_Block_NPC","玩家武器格挡消耗倍率"},{"fBckShdStaminaMult_NPC_Block_PC","NPC 盾挡玩家消耗倍率"},{"fBckWpnStaminaMult_NPC_Block_PC","NPC 武器挡玩家消耗倍率"},{"fBckShdStaminaMult_NPC_Block_NPC","NPC 盾挡 NPC 消耗倍率"},{"fBckWpnStaminaMult_NPC_Block_NPC","NPC 武器挡 NPC 消耗倍率"},
 {"bAttackStaminaToggle","玩家攻击耐力"},{"bAttackStaminaToggle_NPC","NPC 攻击耐力"},{"bStaminaDebuffToggle","耐力耗尽惩罚"},{"fMeleeCostLightMiss_Point","轻击未命中消耗（点）"},{"fMeleeRewardLightHit_Percent","轻击命中奖励（比例）"},
 {"bBlockProjectileToggle","格挡投射物"},{"bTimedBlockToggle","近战限时格挡"},{"bTackleToggle","强击冲撞"},{"bTimedBlockProjectileToggle","投射物限时格挡"},{"bBlockCommitmentToggle","格挡承诺"},{"fBlockCommitmentTime","格挡承诺（有效实秒）"},{"fTimedBlockWindow","限时格挡窗口（游戏秒）"},{"uPerfectBlockAccum","完美格挡累计次数"},{"fTimedBlockCooldownTime","格挡冷却（游戏秒）"},{"fTimedBlockStaminaCostMult","限时格挡消耗倍率"},{"fTackleWindow","冲撞窗口（游戏秒）"},{"fTackleCooldownTime","冲撞冷却（游戏秒）"},
 {"fTimedBlockMeleeExp","限时近战格挡经验"},{"fTimedBlockProjectileExp","限时投射物格挡经验"},{"fBlockProjectileExp","投射物格挡经验"},{"iAltBlockKey","替代格挡按键"},{"bTimedBlockScreenShake","格挡屏幕震动"},{"bTimeBlockSFX","格挡音效"},{"fTimedBlockSFXVolume","音量"},{"uTimedBlockSparkType","火花：0 幻想 / 1 写实 / 2 关闭"},{"bTimedBlockSlowTime","格挡慢时间"},{"fTimedBlockSlowTime_Timed","限时慢时间（有效实秒）"},{"fTimedBlockSlowTime_Perfect","完美慢时间（有效实秒）"},
 {"bStunToggle","眩晕与处决"},{"bStunMeterToggle","TrueHUD 眩晕条"},{"bFriendlyStunToggle","队友眩晕"},{"bDownedStateToggle","破防倒地"},{"fStunTimedBlockMult","限时格挡眩晕倍率"},{"fStunParryMult","招架眩晕倍率"},{"fStunBashMult","盾击眩晕倍率"},{"fStunPowerBashMult","强力盾击眩晕倍率"},{"fStunNormalAttackMult","轻击眩晕倍率"},{"fStunPowerAttackMult","强击眩晕倍率"},{"fStunCounterAttackMult","防御反击眩晕倍率"},
 {"fStunUnarmedMult","徒手眩晕倍率"},{"fStunDaggerMult","匕首眩晕倍率"},{"fStunSwordMult","单手剑眩晕倍率"},{"fStunWarAxeMult","战斧眩晕倍率"},{"fStunMaceMult","钉锤眩晕倍率"},{"fStunGreatSwordMult","双手剑眩晕倍率"},{"fStun2HBluntMult","双手钝器眩晕倍率"},{"bAutoExecution","后续命中时自动处决"},{"iExecutionKey","处决按键"},{"bPoiseCompatibility","Poise 兼容"}};
 const auto found=labels.find(entry.name); return chinese && found!=labels.end()?found->second:entry.label;
}
void controls()
{
 const auto current=config::store().snapshot();
 if (!dirty || (current.saved!=revision && current.values==draft)) { draft=current.values; revision=current.saved; dirty=false; }
 ImGuiMCP::Checkbox("中文 / English",&chinese);
 if (ImGuiMCP::Button(text("Apply","应用"))) settings::submit(draft,revision);
 ImGuiMCP::SameLine();
 if (ImGuiMCP::Button(text("Undo draft","撤销草稿"))) { draft=current.values; revision=current.saved; dirty=false; binding=-1; }
 ImGuiMCP::SameLine();
 if (ImGuiMCP::Button(text("Defaults","恢复默认"))) { draft=config::defaults(); dirty=true; }
 ImGuiMCP::SameLine();
 if (ImGuiMCP::Button(text("Reload file","重新读文件"))) CombatRuntime::get().control([] { settings::readSettings(); });
 ImGuiMCP::Text("%s: %llu / %s: %llu / %s: %s",text("Saved","已保存"),static_cast<unsigned long long>(current.saved),text("Effective","已生效"),static_cast<unsigned long long>(current.effective),text("Cleanup pending","待清理"),current.pendingCleanup?"YES":"NO");
 ImGuiMCP::TextUnformatted(current.message.c_str());
 std::string error; if (!config::validate(draft,error)) ImGuiMCP::TextUnformatted(error.c_str());
 if (binding.load()>=0) ImGuiMCP::TextUnformatted(text("Press a key/button; Escape unbinds. Close menu to cancel.","按下键盘、鼠标或手柄键；Esc 解除绑定，关闭菜单取消。"));
 ImGuiMCP::Separator();
}
void render(const char* page)
{
 std::lock_guard lock(draftMutex); controls();
 for (std::size_t i=0;i<config::count;++i) {
  const auto& entry=config::definitions()[i]; if (std::string_view(entry.page)!=page) continue;
  const std::string label=std::string(translated(entry))+"##"+entry.name;
  if (entry.kind==config::Kind::boolean) { bool value=draft[i]!=0; if (ImGuiMCP::Checkbox(label.c_str(),&value)) { draft[i]=value?1:0; dirty=true; } }
  else if (entry.kind==config::Kind::real) { float value=static_cast<float>(draft[i]); if (ImGuiMCP::SliderFloat(label.c_str(),&value,static_cast<float>(entry.minimum),static_cast<float>(entry.maximum))) { draft[i]=value; dirty=true; } }
  else { int value=static_cast<int>(draft[i]); if (ImGuiMCP::InputInt(label.c_str(),&value)) { draft[i]=value; dirty=true; }
   if (entry.minimum==-1) {
    ImGuiMCP::SameLine(); if (ImGuiMCP::Button((std::string(text("Capture##","绑定##"))+entry.name).c_str())) binding=static_cast<int>(i);
    ImGuiMCP::SameLine(); if (ImGuiMCP::Button((std::string(text("Unbind##","解除##"))+entry.name).c_str())) { draft[i]=-1; dirty=true; }
   }
  }
 }
}
void __stdcall overview() { std::lock_guard lock(draftMutex); controls(); ImGuiMCP::Text("SKSE Menu Framework %.1f / API 1",frameworkVersion); ImGuiMCP::Text("TrueHUD: %s / Special bar: %s / Precision: %s",settings::facts::TrueHudAPI_Obtained?"YES":"NO",settings::facts::TrueHudAPI_HasSpecialBarControl?"YES":"NO",settings::facts::PrecisionAPI_Obtained?"YES":"NO"); ImGuiMCP::TextUnformatted(text("Light attack stun defaults to zero. Execution requires a supported race and weapon animation.","轻击默认不扣眩晕。处决需要种族映射与武器动作支持。")); }
void __stdcall stamina() { render("Stamina"); }
void __stdcall blocking() { render("Blocking"); }
void __stdcall stun() { render("Stun"); }
void __stdcall effects() { render("Effects"); }
void __stdcall diagnostics() { std::lock_guard lock(draftMutex); controls(); ImGuiMCP::Text("Session: %llu",static_cast<unsigned long long>(CombatRuntime::get().session())); ImGuiMCP::TextUnformatted(config::store().path().string().c_str()); ImGuiMCP::TextUnformatted(executionHandler::GetSingleton()->status().c_str()); }
}
namespace Menu {
bool capturing() { return binding.load()>=0 || consumed.load()>=0; }
void registerMenu()
{
 const auto module=GetModuleHandleW(L"SKSEMenuFramework.dll");
 if (!module) { logger::info("SKSE Menu Framework unavailable; file settings remain active"); return; }
 for (auto name : {"GetMenuFrameworkVersion","GetMenuFrameworkAPIVersion","AddSectionItem","RegisterInpoutEvent","UnregisterInputEvent","RegisterEventPriority","UnregisterEvent","igCheckbox","igButton","igSameLine","igSeparator","igText","igTextUnformatted","igSliderFloat","igInputInt"}) if (!GetProcAddress(module,name)) { logger::error("Menu Framework missing export {}",name); return; }
 const auto api=reinterpret_cast<std::uint32_t(*)()>(GetProcAddress(module,"GetMenuFrameworkAPIVersion"));
 if (api()!=1) { logger::error("Unsupported Menu Framework API {}",api()); return; }
 frameworkVersion=SKSEMenuFramework::GetMenuFrameworkVersion();
 SKSEMenuFramework::SetSection("Valhalla Combat");
 SKSEMenuFramework::AddSectionItem("Overview 总览",overview);
 SKSEMenuFramework::AddSectionItem("Stamina 耐力",stamina);
 SKSEMenuFramework::AddSectionItem("Blocking 格挡与冲撞",blocking);
 SKSEMenuFramework::AddSectionItem("Stun 眩晕与处决",stun);
 SKSEMenuFramework::AddSectionItem("Effects 效果与兼容",effects);
 SKSEMenuFramework::AddSectionItem("Diagnostics 诊断",diagnostics);
 input=std::make_unique<SKSEMenuFramework::Model::InputEvent>(capture);
 events=std::make_unique<SKSEMenuFramework::Model::Event>(menuEvent,0.0f);
 logger::info("Registered Menu Framework {} / API 1",frameworkVersion);
}
}
