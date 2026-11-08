#include "include/SettingsStore.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace RE {
enum class INPUT_DEVICE { kKeyboard, kMouse, kGamepad };
enum class INPUT_EVENT_TYPE { kButton, kOther };
struct InputEvent { INPUT_EVENT_TYPE eventType=INPUT_EVENT_TYPE::kButton; };
struct ButtonEvent : InputEvent {
 INPUT_DEVICE device=INPUT_DEVICE::kKeyboard;
 std::uint32_t code=0;
 bool down=true;
 std::uint32_t GetIDCode() const { return code; }
 INPUT_DEVICE GetDevice() const { return device; }
 bool IsDown() const { return down; }
 bool IsUp() const { return !down; }
};
struct BSWin32GamepadDevice {
 enum class Key : std::uint32_t { kUp=1,kDown=2,kLeft=4,kRight=8,kStart=16,kBack=32,kLeftThumb=64,kRightThumb=128,kLeftShoulder=256,kRightShoulder=512,kA=4096,kB=8192,kX=16384,kY=32768,kLeftTrigger=9,kRightTrigger=10 };
};
}
namespace logger {
template<class... Args> void info(const char*,Args&&...) {}
template<class... Args> void error(const char*,Args&&...) {}
}
class CombatRuntime {
public:
 static CombatRuntime& get() { static CombatRuntime runtime; return runtime; }
 template<class F> void control(F&& action) { action(); }
 std::uint64_t session() const { return 1; }
};
class executionHandler {
public:
 static executionHandler* GetSingleton() { static executionHandler handler; return &handler; }
 std::string status() const { return "Ready"; }
	std::string targetStatus() const { return "No target"; }
};
namespace encounters { void load() {} }
namespace settings {
namespace facts {
inline bool TrueHudAPI_Obtained=true,TrueHudAPI_HasSpecialBarControl=true,PrecisionAPI_Obtained=true;
}
void readSettings() { config::Values values; std::string error; if (!config::store().load(values,error)) throw std::runtime_error(error); }
void submit(config::Values values,std::uint64_t revision) {
 std::string error;
 if (!config::store().save(values,revision,error)) throw std::runtime_error(error);
 config::store().effective(false);
}
}
namespace ImGuiMCP {
inline std::vector<std::string> widgets,lines;
inline std::unordered_map<std::string,double> edits;
inline std::string click;
void frame(std::string button={}) { widgets.clear(); lines.clear(); click=std::move(button); }
bool edit(const char* label,double& value) {
 const std::string name=label; const auto id=name.find("##");
 if (id==std::string::npos) return false;
 const auto found=edits.find(name.substr(id+2));
 if (found==edits.end()) return false;
 value=found->second; edits.erase(found); return true;
}
bool Checkbox(const char* label,bool* value) { widgets.emplace_back(label); double number=*value?1:0; if (!edit(label,number)) return false; *value=number!=0; return true; }
bool SliderFloat(const char* label,float* value,float,float) { widgets.emplace_back(label); double number=*value; if (!edit(label,number)) return false; *value=static_cast<float>(number); return true; }
bool InputInt(const char* label,int* value) { widgets.emplace_back(label); double number=*value; if (!edit(label,number)) return false; *value=static_cast<int>(number); return true; }
bool Button(const char* label) { widgets.emplace_back(label); if (click!=label) return false; click.clear(); return true; }
void SameLine() {}
void Separator() {}
void SetNextItemWidth(float) {}
void TextUnformatted(const char* text) { lines.emplace_back(text); }
void formatted(const char* format,va_list args) { char buffer[2048]; vsnprintf(buffer,sizeof(buffer),format,args); lines.emplace_back(buffer); }
void Text(const char* format,...) { va_list args; va_start(args,format); formatted(format,args); va_end(args); }
void TextWrapped(const char* format,...) { va_list args; va_start(args,format); formatted(format,args); va_end(args); }
}
namespace SKSEMenuFramework {
using Callback=void(__stdcall*)();
inline std::vector<std::pair<std::string,Callback>> pages;
float GetMenuFrameworkVersion() { return 3.8f; }
void SetSection(const char*) {}
void AddSectionItem(const char* label,Callback callback) { pages.emplace_back(label,callback); }
namespace Model {
enum EventType { kCloseMenu,kOpenMenu };
struct InputEvent { explicit InputEvent(bool(__stdcall*)(RE::InputEvent*)) {} };
struct Event { Event(void(__stdcall*)(EventType),float) {} };
}
}
HMODULE testModule(const wchar_t*) { return reinterpret_cast<HMODULE>(std::uintptr_t{1}); }
std::uint32_t frameworkAPI() { return 1; }
FARPROC testExport(HMODULE,const char* name) { return std::string_view(name)=="SetSection"?nullptr:reinterpret_cast<FARPROC>(frameworkAPI); }
#define GetModuleHandleW testModule
#define GetProcAddress testExport
#include "menu_functions.inc"
#undef GetProcAddress
#undef GetModuleHandleW

void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); std::printf("PASS %s\n",message); }
bool shown(const char* id) { return std::ranges::any_of(ImGuiMCP::widgets,[id](const auto& widget) { return widget.ends_with(std::string("##")+id); }); }
void frame(std::string button={}) { ImGuiMCP::frame(std::move(button)); SKSEMenuFramework::pages.front().second(); }
int main()
{
 const auto previous=std::filesystem::current_path();
 const auto sandbox=previous/("valhalla-menu-test-"+std::to_string(GetCurrentProcessId()));
 try {
  check(!std::filesystem::exists(sandbox),"isolated menu sandbox");
  std::filesystem::create_directory(sandbox); std::filesystem::current_path(sandbox);
  settings::readSettings(); Menu::registerMenu();
  check(!SKSEMenuFramework::pages.empty(),"native menu registered");
  frame();
  for (const auto& entry:config::definitions()) check(shown(entry.name),entry.name);
  check(shown("iExecutionKey") && shown("bStunMeterToggle"),"execution binding and stun meter visible on entry");
  frame("绑定##iExecutionKey"); check(Menu::capturing(),"execution binding capture starts");
  RE::ButtonEvent keyboard; keyboard.code=45;
  check(capture(&keyboard),"captured press consumed");
  keyboard.down=false; check(capture(&keyboard) && !Menu::capturing(),"captured release consumed");
  ImGuiMCP::edits["fStunPowerAttackMult"]=2.5; ImGuiMCP::edits["bStunMeterToggle"]=0; frame();
  frame("应用"); frame();
  auto saved=config::store().snapshot();
  check(config::value(saved.values,config::Key::iExecutionKey)==45,"execution hotkey persisted");
  check(config::value(saved.values,config::Key::fStunPowerAttackMult)==2.5 && config::value(saved.values,config::Key::bStunMeterToggle)==0,"edited gameplay settings persisted");
  config::Store restarted("Data"); config::Values restored; std::string error;
  check(restarted.load(restored,error),"settings restored from canonical INI");
  for (std::size_t i=0;i<config::count;++i) check(std::abs(restored[i]-saved.values[i])<0.000001,"new process or save shares canonical settings");
  frame("解除##iExecutionKey"); frame("应用"); frame();
  check(config::value(config::store().snapshot().values,config::Key::iExecutionKey)==-1,"execution binding can be removed");
  frame("绑定##iExecutionKey"); keyboard.down=true; keyboard.code=1; capture(&keyboard);
  keyboard.down=false; capture(&keyboard); check(!Menu::capturing(),"Escape finishes binding capture");
  frame("绑定##iExecutionKey"); menuEvent(SKSEMenuFramework::Model::kCloseMenu);
  check(!Menu::capturing(),"closing menu cancels binding capture");
  frame("格挡 / 冲撞"); check(shown("iAltBlockKey") && !shown("iExecutionKey"),"blocking category exposes alternate binding");
  frame("眩晕 / 处决"); check(shown("iExecutionKey") && !shown("iAltBlockKey"),"stun category exposes execution binding");
  frame("全部设置"); check(shown("iAltBlockKey") && shown("iExecutionKey"),"all settings restores both bindings");
  ImGuiMCP::edits["bMenuChinese"]=0; frame(); frame("Apply"); frame();
  check(config::value(config::store().snapshot().values,config::Key::bMenuChinese)==0,"menu language saved globally");
  std::filesystem::current_path(previous);
  check(sandbox.parent_path()==previous && sandbox.filename().string().starts_with("valhalla-menu-test-"),"menu cleanup target verified");
  std::filesystem::remove_all(sandbox); return 0;
 } catch (const std::exception& error) { std::filesystem::current_path(previous); std::fprintf(stderr,"FAIL %s\n",error.what()); return 1; }
}
