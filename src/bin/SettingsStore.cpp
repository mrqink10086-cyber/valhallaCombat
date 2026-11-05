#include "include/SettingsStore.h"
#include "include/lib/SimpleIni.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <windows.h>

namespace config {
namespace {
template<class T> constexpr Kind kind() { if constexpr (std::is_same_v<T,bool>) return Kind::boolean; else if constexpr (std::is_floating_point_v<T>) return Kind::real; else return Kind::integer; }
constexpr std::array<Definition,count> entries{{
#define VC_SETTING(type,name,initial,section,page,label,low,high) {#name,#section,#page,label,kind<type>(),static_cast<double>(initial),low,high},
#include "include/Settings.def"
#undef VC_SETTING
}};
bool parse(const char* text, double& result)
{
 if (!text) return false;
 const std::string_view input(text);
 auto end = std::from_chars(input.data(), input.data()+input.size(),result);
 return end.ec == std::errc{} && end.ptr == input.data()+input.size() && std::isfinite(result);
}
bool valid(double number, const Definition& entry)
{
 return std::isfinite(number) && number >= entry.minimum && number <= entry.maximum && (entry.kind == Kind::real || std::floor(number)==number);
}
bool overlay(const std::filesystem::path& path, Values& values, std::string& notes)
{
 if (!std::filesystem::exists(path)) return true;
 CSimpleIniA ini; ini.SetUnicode();
 if (ini.LoadFile(path.string().c_str()) < 0) { notes = "Cannot read " + path.string(); return false; }
 for (std::size_t i=0;i<count;++i) {
  const auto& entry=entries[i]; const char* text=ini.GetValue(entry.section,entry.name);
  if (!text) continue;
  double number=0;
  if (parse(text,number) && valid(number,entry)) values[i]=number;
  else notes += " Ignored invalid " + std::string(entry.name) + " in " + path.string() + ";";
 }
 if (ini.GetValue("TimedBlocking","fPerfectBlockWindow")) notes += " Legacy perfect-block seconds are unused; accumulation controls perfect blocks.;";
 for (auto name : {"bExecutionLimit","bPlayerExecution"}) if (ini.GetValue("Stun",name)) notes += " Unsupported legacy setting retained in original file: " + std::string(name) + ";";
 return true;
}
}
const std::array<Definition,count>& definitions() { return entries; }
Values defaults() { Values values{}; for (std::size_t i=0;i<count;++i) values[i]=entries[i].initial; return values; }
double value(const Values& values, Key key) { return values[static_cast<std::size_t>(key)]; }
bool validate(const Values& values, std::string& error)
{
 for (std::size_t i=0;i<count;++i) if (!valid(values[i],entries[i])) { error="Invalid " + std::string(entries[i].name); return false; }
 if (value(values,Key::fStaminaRegenMin)>value(values,Key::fStaminaRegenLimit)) { error="Minimum regeneration exceeds maximum"; return false; }
 const auto block=value(values,Key::iAltBlockKey), execute=value(values,Key::iExecutionKey);
 if (block>=0 && block==execute) { error="Block and execution bindings conflict"; return false; }
 return true;
}
Store::Store(std::filesystem::path data) : root(std::move(data)), canonical(root/"SKSE/Plugins/ValhallaCombat/Settings.ini") { state.values=defaults(); }
Snapshot Store::snapshot() const { std::lock_guard lock(mutex); return state; }
bool Store::load(Values& values, std::string& error)
{
 std::lock_guard lock(mutex);
 try {
  CSimpleIniA ini; ini.SetUnicode();
  const bool exists=std::filesystem::exists(canonical);
  if (exists && ini.LoadFile(canonical.string().c_str()) < 0) { error="Cannot read settings"; return false; }
  double schema=0; const char* marker=ini.GetValue("Meta","Schema");
  if (marker && (!parse(marker,schema) || schema!=1)) { futureSchema=true; error="Unsupported settings schema; file preserved"; state.message=error; return false; }
  futureSchema=false; values=defaults(); std::string notes;
  if (!marker) {
   if (!overlay(root/"MCM/Config/ValhallaCombat/settings.ini",values,notes) || !overlay(canonical,values,notes) || !overlay(root/"MCM/Settings/ValhallaCombat.ini",values,notes)) { error=notes; return false; }
   if (!validate(values,error)) return false;
   if (exists) { auto backup=canonical; backup += ".legacy.bak"; if (!std::filesystem::exists(backup)) std::filesystem::copy_file(canonical,backup); }
   if (!write(values,error)) return false;
   notes="Migrated legacy settings. " + notes;
  } else if (!overlay(canonical,values,notes) || !validate(values,error)) { if (error.empty()) error=notes; return false; }
  state.values=values; ++state.saved; state.message=notes.empty()?"Settings loaded":notes;
  return true;
 } catch (const std::exception& exception) { error=exception.what(); return false; }
}
bool Store::write(const Values& values, std::string& error)
{
 std::filesystem::create_directories(canonical.parent_path());
 CSimpleIniA ini; ini.SetUnicode(); ini.SetLongValue("Meta","Schema",1);
 for (std::size_t i=0;i<count;++i) {
  const auto& entry=entries[i];
  if (entry.kind==Kind::real) ini.SetDoubleValue(entry.section,entry.name,values[i]);
  else ini.SetLongValue(entry.section,entry.name,static_cast<long>(values[i]));
 }
 auto temporary=canonical; temporary += ".tmp";
 if (ini.SaveFile(temporary.string().c_str())<0) { error="Cannot write temporary settings file"; return false; }
 if (!MoveFileExW(temporary.c_str(),canonical.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { error="Cannot replace settings file: " + std::to_string(GetLastError()); std::error_code ignored; std::filesystem::remove(temporary,ignored); return false; }
 return true;
}
bool Store::save(const Values& values, std::uint64_t expected, std::string& error)
{
 std::lock_guard lock(mutex);
 try {
  if (futureSchema) { error="Future schema is read-only"; return false; }
  if (expected!=state.saved) { error="Settings changed; reload the draft"; return false; }
  if (!validate(values,error) || !write(values,error)) return false;
  state.values=values; ++state.saved; state.message="Saved; applying"; return true;
 } catch (const std::exception& exception) { error=exception.what(); return false; }
}
void Store::effective(bool pending) { std::lock_guard lock(mutex); state.effective=state.saved; state.pendingCleanup=pending; state.message=pending?"Applied; actor cleanup pending":"Saved and applied"; }
void Store::cleanupFinished() { std::lock_guard lock(mutex); state.pendingCleanup=false; }
void Store::failure(std::string message) { std::lock_guard lock(mutex); state.message=std::move(message); }
Store& store() { static Store instance; return instance; }
}
