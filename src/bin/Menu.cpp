#include "include/Menu.h"
#include "include/settings.h"
#include "include/CombatRuntime.h"
#include "include/executionHandler.h"
#include "include/EnemyClassifier.h"
#include <memory>
#include <unordered_map>
#pragma warning(push, 0)
#include "include/vendor/MenuFramework/SKSEMenuFramework.h"
#pragma warning(pop)

namespace {
std::mutex draftMutex;
config::Values draft=config::defaults();
std::uint64_t revision=0;
bool dirty=false;
std::atomic<int> binding{-1};
std::atomic<int> consumed{-1};
std::unique_ptr<SKSEMenuFramework::Model::InputEvent> input;
std::unique_ptr<SKSEMenuFramework::Model::Event> events;
float frameworkVersion=0;
bool chinese() { return config::value(draft,config::Key::bMenuChinese)!=0; }
const char* text(const char* english, const char* zh) { return chinese()?zh:english; }
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
	 {"bFollowerAutoExecution","队友自动处决"},{"bExecutionPrompt","处决机会提示"},{"fExecutionHealthThreshold","处决血量阈值（比例）"},{"fStunBrokenWindow","破防窗口（有效实秒）"},{"fBossStunResistance","精英和 Boss 恢复后的破防抗性（秒）"},{"fBossOpeningDamageMult","精英和 Boss 破防期间受伤倍率"},
 {"bUIAlert","耗尽提示"},{"bNonCombatStaminaCost","战斗外消耗耐力"},{"fStaminaRegenMult","种族耐力恢复倍率"},{"fStaminaRegenLimit","恢复率上限"},{"fStaminaRegenMin","恢复率下限"},
 {"fCombatStaminaRegenMult","战斗耐力恢复倍率"},{"fBlockingStaminaRegenMult","格挡耐力恢复倍率"},{"fStaminaRegenDelay","恢复延迟（秒）"},{"bBlockedHitRegenStamina","被格挡的轻击获得耐力奖励"},{"bBlockStaminaToggle","耐力格挡"},{"bGuardBreak","耗尽时破防"},
 {"fBckShdStaminaMult_PC_Block_NPC","玩家盾牌格挡消耗倍率"},{"fBckWpnStaminaMult_PC_Block_NPC","玩家武器格挡消耗倍率"},{"fBckShdStaminaMult_NPC_Block_PC","NPC 盾挡玩家消耗倍率"},{"fBckWpnStaminaMult_NPC_Block_PC","NPC 武器挡玩家消耗倍率"},{"fBckShdStaminaMult_NPC_Block_NPC","NPC 盾挡 NPC 消耗倍率"},{"fBckWpnStaminaMult_NPC_Block_NPC","NPC 武器挡 NPC 消耗倍率"},
 {"bAttackStaminaToggle","玩家攻击耐力"},{"bAttackStaminaToggle_NPC","NPC 攻击耐力"},{"bStaminaDebuffToggle","耐力耗尽惩罚"},{"fMeleeCostLightMiss_Point","轻击未命中消耗（点）"},{"fMeleeRewardLightHit_Percent","轻击命中奖励（比例）"},
 {"bBlockProjectileToggle","格挡投射物"},{"bTimedBlockToggle","近战限时格挡"},{"bTackleToggle","强击冲撞"},{"bTimedBlockProjectileToggle","投射物限时格挡"},{"bBlockCommitmentToggle","格挡承诺"},{"fBlockCommitmentTime","格挡承诺（有效实秒）"},{"fTimedBlockWindow","限时格挡窗口（游戏秒）"},{"uPerfectBlockAccum","完美格挡累计次数"},{"fTimedBlockCooldownTime","格挡冷却（游戏秒）"},{"fTimedBlockStaminaCostMult","限时格挡消耗倍率"},{"fTackleWindow","冲撞窗口（游戏秒）"},{"fTackleCooldownTime","冲撞冷却（游戏秒）"},
 {"fTimedBlockMeleeExp","限时近战格挡经验"},{"fTimedBlockProjectileExp","限时投射物格挡经验"},{"fBlockProjectileExp","投射物格挡经验"},{"iAltBlockKey","替代格挡按键"},{"bTimedBlockScreenShake","格挡屏幕震动"},{"bTimeBlockSFX","格挡音效"},{"fTimedBlockSFXVolume","音量"},{"uTimedBlockSparkType","火花：0 幻想 / 1 写实 / 2 关闭"},{"bTimedBlockSlowTime","格挡慢时间"},{"fTimedBlockSlowTime_Timed","限时慢时间（有效实秒）"},{"fTimedBlockSlowTime_Perfect","完美慢时间（有效实秒）"},
 {"bStunToggle","眩晕与处决"},{"bStunMeterToggle","TrueHUD 眩晕条"},{"bFriendlyStunToggle","队友眩晕"},{"bDownedStateToggle","破防倒地"},{"fStunTimedBlockMult","限时格挡眩晕倍率"},{"fStunParryMult","招架眩晕倍率"},{"fStunBashMult","盾击眩晕倍率"},{"fStunPowerBashMult","强力盾击眩晕倍率"},{"fStunNormalAttackMult","轻击眩晕倍率"},{"fStunPowerAttackMult","强击眩晕倍率"},{"fStunCounterAttackMult","防御反击眩晕倍率"},
 {"fStunUnarmedMult","徒手眩晕倍率"},{"fStunDaggerMult","匕首眩晕倍率"},{"fStunSwordMult","单手剑眩晕倍率"},{"fStunWarAxeMult","战斧眩晕倍率"},{"fStunMaceMult","钉锤眩晕倍率"},{"fStunGreatSwordMult","双手剑眩晕倍率"},{"fStun2HBluntMult","双手钝器眩晕倍率"},{"bAutoExecution","自动处决"},{"iExecutionKey","处决按键"},{"bPoiseCompatibility","Poise 兼容"}};
 const auto found=labels.find(entry.name); return chinese() && found!=labels.end()?found->second:entry.label;
}
void controls()
{
 const auto current=config::store().snapshot();
 if (!dirty || (current.saved!=revision && current.values==draft)) { draft=current.values; revision=current.saved; dirty=false; }
 bool language=chinese();
 if (ImGuiMCP::Checkbox("中文 / English##bMenuChinese",&language)) { draft[static_cast<std::size_t>(config::Key::bMenuChinese)]=language?1:0; dirty=true; }
 ImGuiMCP::TextWrapped("%s",text("Global settings for every save. Click Apply to save changes.","全局设置，对所有存档生效。修改后点击应用保存。"));
 if (ImGuiMCP::Button(text("Apply","应用"))) settings::submit(draft,revision);
 ImGuiMCP::SameLine();
 if (ImGuiMCP::Button(text("Undo draft","撤销草稿"))) { draft=current.values; revision=current.saved; dirty=false; binding=-1; }
 ImGuiMCP::SameLine();
 if (ImGuiMCP::Button(text("Defaults","恢复默认"))) { draft=config::defaults(); dirty=true; binding=-1; }
 ImGuiMCP::SameLine();
	 if (ImGuiMCP::Button(text("Reload file","重新读文件"))) { dirty=false; binding=-1; CombatRuntime::get().control([] { settings::readSettings(); encounters::load(); }); }
 if (dirty) ImGuiMCP::TextUnformatted(text("Unsaved changes","有未保存的修改"));
 else if (current.pendingCleanup) ImGuiMCP::TextUnformatted(text("Saved; waiting for active effects to finish.","已保存，正在等待现有效果结束。"));
 if (!current.message.empty() && current.message!="Saved and applied" && current.message!="Settings loaded") ImGuiMCP::TextUnformatted(current.message.c_str());
 std::string error; if (!config::validate(draft,error)) ImGuiMCP::TextUnformatted(error.c_str());
 if (binding.load()>=0) ImGuiMCP::TextUnformatted(text("Press a key/button; Escape unbinds. Close menu to cancel.","按下键盘、鼠标或手柄键；Esc 解除绑定，关闭菜单取消。"));
 ImGuiMCP::Separator();
}
std::string keyName(int code)
{
 if (code<0) return text("Unbound","未绑定");
 if (code>=266 && code<=281) {
  constexpr const char* names[]{"D-pad Up","D-pad Down","D-pad Left","D-pad Right","Start","Back","L3","R3","LB","RB","A","B","X","Y","LT","RT"};
  return std::string(text("Gamepad ","手柄 "))+names[code-266];
 }
 if (code>=256) return std::string(text("Mouse ","鼠标 "))+std::to_string(code-255);
 wchar_t wide[64]{}; char name[256]{};
 const LONG scan=((code&0x7f)<<16)|(code>=128?(1<<24):0);
 if (GetKeyNameTextW(scan,wide,64)>0 && WideCharToMultiByte(CP_UTF8,0,wide,-1,name,256,nullptr,nullptr)>0) return name;
 return std::string(text("Key ","按键 "))+std::to_string(code);
}
void setting(config::Key key)
{
 const auto i=static_cast<std::size_t>(key); const auto& entry=config::definitions()[i];
 const std::string id="##"+std::string(entry.name);
 if (entry.kind==config::Kind::boolean) {
  bool value=draft[i]!=0;
  if (ImGuiMCP::Checkbox((std::string(translated(entry))+id).c_str(),&value)) { draft[i]=value?1:0; dirty=true; }
  return;
 }
 if (entry.minimum==-1) {
  ImGuiMCP::Text("%s: %s",translated(entry),keyName(static_cast<int>(draft[i])).c_str());
  if (ImGuiMCP::Button((std::string(binding.load()==static_cast<int>(i)?text("Press a key","请按键"):text("Capture","绑定"))+id).c_str())) binding=static_cast<int>(i);
  ImGuiMCP::SameLine();
  if (ImGuiMCP::Button((std::string(text("Unbind","解除"))+id).c_str())) { draft[i]=-1; dirty=true; binding=-1; }
  return;
 }
 ImGuiMCP::TextUnformatted(translated(entry)); ImGuiMCP::SetNextItemWidth(-1.0f);
 if (entry.kind==config::Kind::real) {
  float value=static_cast<float>(draft[i]);
  if (ImGuiMCP::SliderFloat(id.c_str(),&value,static_cast<float>(entry.minimum),static_cast<float>(entry.maximum))) { draft[i]=value; dirty=true; }
 } else {
  int value=static_cast<int>(draft[i]);
  if (ImGuiMCP::InputInt(id.c_str(),&value)) { draft[i]=value; dirty=true; }
 }
}
void section(const char* page,const char* english,const char* zh)
{
 ImGuiMCP::Separator(); ImGuiMCP::TextUnformatted(text(english,zh));
 if (std::string_view(page)=="Stun") {
	  ImGuiMCP::TextWrapped("%s",text("Normal: broken stun OR low health. Elite/Boss: low health only; breaking stun grants a damage opening.","普通敌人：破防或低血量即可处决。精英和 Boss：需要低血量，破防提供增伤窗口。"));
	  ImGuiMCP::TextWrapped("%s",executionHandler::GetSingleton()->targetStatus().c_str());
  setting(config::Key::iExecutionKey); setting(config::Key::bStunToggle); setting(config::Key::bStunMeterToggle); setting(config::Key::bAutoExecution);
  if (!settings::facts::TrueHudAPI_Obtained || !settings::facts::TrueHudAPI_HasSpecialBarControl)
   ImGuiMCP::TextWrapped("%s",text("Stun meter needs TrueHUD special bar control. Check TrueHUD installation and special bar settings.","眩晕条需要 TrueHUD 特殊条控制权。请检查 TrueHUD 安装和特殊条设置。"));
 }
 for (std::size_t i=0;i<config::count;++i) {
  const auto key=static_cast<config::Key>(i); const auto& entry=config::definitions()[i];
  if (std::string_view(entry.page)!=page) continue;
  if (key==config::Key::iExecutionKey || key==config::Key::bStunToggle || key==config::Key::bStunMeterToggle || key==config::Key::bAutoExecution) continue;
  setting(key);
 }
}
void __stdcall allSettings()
{
 std::lock_guard lock(draftMutex); controls();
 static int category=0;
 if (ImGuiMCP::Button(text("All settings","全部设置"))) category=0;
 ImGuiMCP::SameLine(); if (ImGuiMCP::Button(text("Stun / execution","眩晕 / 处决"))) category=1;
 ImGuiMCP::SameLine(); if (ImGuiMCP::Button(text("Stamina","耐力"))) category=2;
 if (ImGuiMCP::Button(text("Blocking / tackle","格挡 / 冲撞"))) category=3;
 ImGuiMCP::SameLine(); if (ImGuiMCP::Button(text("Effects / compatibility","效果 / 兼容"))) category=4;
 if (category==0 || category==1) section("Stun","Stun and execution","眩晕与处决");
 if (category==0 || category==2) section("Stamina","Stamina","耐力");
 if (category==0 || category==3) section("Blocking","Blocking and tackle","格挡与冲撞");
 if (category==0 || category==4) section("Effects","Effects and compatibility","效果与兼容");
}
void __stdcall diagnostics()
{
 std::lock_guard lock(draftMutex);
 const auto current=config::store().snapshot();
 ImGuiMCP::Text("SKSE Menu Framework %.1f / API 1",frameworkVersion);
 ImGuiMCP::Text("TrueHUD: %s / Special bar: %s / Precision: %s",settings::facts::TrueHudAPI_Obtained?"YES":"NO",settings::facts::TrueHudAPI_HasSpecialBarControl?"YES":"NO",settings::facts::PrecisionAPI_Obtained?"YES":"NO");
 ImGuiMCP::Text("Session: %llu",static_cast<unsigned long long>(CombatRuntime::get().session()));
 ImGuiMCP::Text("Saved: %llu / Effective: %llu / Cleanup pending: %s",static_cast<unsigned long long>(current.saved),static_cast<unsigned long long>(current.effective),current.pendingCleanup?"YES":"NO");
 ImGuiMCP::TextUnformatted(current.message.c_str());
 ImGuiMCP::TextUnformatted(config::store().path().string().c_str());
 ImGuiMCP::TextUnformatted(executionHandler::GetSingleton()->status().c_str());
	 ImGuiMCP::TextWrapped("%s",executionHandler::GetSingleton()->targetStatus().c_str());
}
}
namespace Menu {
bool capturing() { return binding.load()>=0 || consumed.load()>=0; }
void registerMenu()
{
 const auto module=GetModuleHandleW(L"SKSEMenuFramework.dll");
 if (!module) { logger::info("SKSE Menu Framework unavailable; file settings remain active"); return; }
 for (auto name : {"GetMenuFrameworkVersion","GetMenuFrameworkAPIVersion","AddSectionItem","RegisterInpoutEvent","UnregisterInputEvent","RegisterEventPriority","UnregisterEvent","igCheckbox","igButton","igSameLine","igSeparator","igTextV","igTextWrappedV","igTextUnformatted","igSetNextItemWidth","igSliderFloat","igInputInt"}) if (!GetProcAddress(module,name)) { logger::error("Menu Framework missing export {}",name); return; }
 const auto api=reinterpret_cast<std::uint32_t(*)()>(GetProcAddress(module,"GetMenuFrameworkAPIVersion"));
 if (api()!=1) { logger::error("Unsupported Menu Framework API {}",api()); return; }
 frameworkVersion=SKSEMenuFramework::GetMenuFrameworkVersion();
 SKSEMenuFramework::SetSection("Valhalla Combat");
 SKSEMenuFramework::AddSectionItem("Settings 设置",allSettings);
 SKSEMenuFramework::AddSectionItem("Diagnostics 诊断",diagnostics);
 input=std::make_unique<SKSEMenuFramework::Model::InputEvent>(capture);
 events=std::make_unique<SKSEMenuFramework::Model::Event>(menuEvent,0.0f);
 logger::info("Registered Menu Framework {} / API 1",frameworkVersion);
}
}
