#include "include/SettingsStore.h"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <Windows.h>

namespace fs=std::filesystem;
void check(bool result,const char* label) { if (!result) throw std::runtime_error(label); std::cout<<"PASS "<<label<<'\n'; }
void file(const fs::path& path,const char* content) { fs::create_directories(path.parent_path()); std::ofstream out(path); out<<content; }
std::string contents(const fs::path& path) { std::ifstream in(path); return {std::istreambuf_iterator<char>(in),{}}; }
void put(config::Values& values,config::Key key,double value) { values[static_cast<std::size_t>(key)]=value; }
int main()
{
 const auto parent=fs::current_path();
 const auto base=parent/ ("valhalla-config-test-"+std::to_string(GetCurrentProcessId()));
 try {
  check(!fs::exists(base),"isolated sandbox");
  std::string error; config::Values values{};
  check(config::validate(config::defaults(),error),"declared defaults valid");
  config::Store fresh(base/"fresh");
  const bool loaded=fresh.load(values,error); if (!loaded) std::cerr<<error<<'\n';
  check(loaded,"fresh install loads and writes schema");
  check(config::value(values,config::Key::bBlockedHitRegenStamina)==1 && config::value(values,config::Key::fStunNormalAttackMult)==0,"shipped default and gameplay preserved");
  check(contents(fresh.path()).find("Schema = 1")!=std::string::npos,"canonical file marked with schema");
  const auto root=base/"legacy"; config::Store legacy(root);
  file(root/"MCM/Config/ValhallaCombat/settings.ini","[Stun]\nfStunPowerAttackMult=2\n");
  file(legacy.path(),"[Stun]\nfStunPowerAttackMult=3\n");
  file(root/"MCM/Settings/ValhallaCombat.ini","[Stun]\nfStunPowerAttackMult=4\nfStunNormalAttackMult=nan\n");
  check(legacy.load(values,error) && config::value(values,config::Key::fStunPowerAttackMult)==4,"legacy precedence defaults-SKSE-MCM");
  check(fs::exists(legacy.path().string()+".legacy.bak") && config::value(values,config::Key::fStunNormalAttackMult)==0,"legacy backup and invalid key fallback");
  file(root/"MCM/Settings/ValhallaCombat.ini","[Stun]\nfStunPowerAttackMult=9\n");
  check(legacy.load(values,error) && config::value(values,config::Key::fStunPowerAttackMult)==4,"schema prevents repeat migration");
  auto revision=legacy.snapshot().saved;
  put(values,config::Key::fStunPowerAttackMult,5);
  check(legacy.save(values,revision,error),"valid draft saved atomically");
  check(!legacy.save(values,revision,error),"stale draft rejected");
  revision=legacy.snapshot().saved;
  auto invalid=values; put(invalid,config::Key::fStunPowerAttackMult,std::numeric_limits<double>::quiet_NaN());
  check(!legacy.save(invalid,revision,error),"nonfinite rejected");
  invalid=values; put(invalid,config::Key::iAltBlockKey,42); put(invalid,config::Key::iExecutionKey,42);
  check(!legacy.save(invalid,revision,error),"binding conflict rejected");
  invalid=values; put(invalid,config::Key::fStaminaRegenMin,5); put(invalid,config::Key::fStaminaRegenLimit,1);
  check(!legacy.save(invalid,revision,error),"reversed regeneration limits rejected");
  fs::rename(legacy.path(),legacy.path().string()+".saved"); fs::create_directory(legacy.path());
  check(!legacy.save(values,revision,error) && legacy.snapshot().saved==revision,"disk failure keeps active revision");
  config::Store future(base/"future"); file(future.path(),"[Meta]\nSchema=99\n[Stun]\nfStunPowerAttackMult=7\n"); const auto original=contents(future.path());
  check(!future.load(values,error) && !future.save(config::defaults(),0,error) && contents(future.path())==original,"future schema preserved read-only");
  check(base.parent_path()==parent && base.filename().string().starts_with("valhalla-config-test-"),"sandbox cleanup target verified");
  fs::remove_all(base); return 0;
 } catch (const std::exception& exception) { std::cerr<<"FAIL "<<exception.what()<<'\n'; return 1; }
}
