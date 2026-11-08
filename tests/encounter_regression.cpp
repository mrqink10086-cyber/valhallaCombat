#include "include/EnemyRules.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
namespace RE {
struct TESForm {
    std::uint32_t id=0;
    virtual ~TESForm()=default;
    std::uint32_t GetFormID() const { return id; }
    template<class T> T* As() { return dynamic_cast<T*>(this); }
    static inline std::map<std::uint32_t,TESForm*> forms;
    template<class T> static T* LookupByID(std::uint32_t id) { auto it=forms.find(id); return it==forms.end()?nullptr:it->second->As<T>(); }
};
struct TESNPC : TESForm {};
struct BGSKeyword : TESForm {};
struct BGSLocationRefType : TESForm {};
struct BGSLocation { struct Entry { BGSLocationRefType* type; struct { std::uint32_t refID; } refData; }; BGSLocation* parentLoc=nullptr; std::vector<Entry> specialRefs; };
struct ExtraLeveledCreature { TESNPC* originalBase=nullptr; };
struct Actor : TESForm {
    TESNPC* base=nullptr; BGSLocation* location=nullptr; std::set<std::uint32_t> keywords;
    struct Extras { ExtraLeveledCreature* leveled=nullptr; template<class T> T* GetByType() { return leveled; } } extraList;
    TESNPC* GetActorBase() { return base; } BGSLocation* GetCurrentLocation() { return location; }
    bool HasKeyword(const BGSKeyword* keyword) { return keywords.contains(keyword->id); }
};
enum class DEFAULT_OBJECT { kLocRefTypeBoss };
struct BGSDefaultObjectManager { BGSLocationRefType* boss=nullptr; static BGSDefaultObjectManager* GetSingleton() { static BGSDefaultObjectManager m; return &m; } template<class T> T* GetObject(DEFAULT_OBJECT) { return boss; } };
struct TESDataHandler { static TESDataHandler* GetSingleton() { static TESDataHandler h; return &h; } TESForm* LookupForm(std::uint32_t id,std::string_view plugin) { return plugin=="Test.esp"?TESForm::LookupByID<TESForm>(id):nullptr; } };
}
namespace logger { template<class... T> void warn(const char*,T&&...) {} template<class... T> void info(const char*,T&&...) {} }
namespace fmt { std::string format(const char*,const char* tier,const char* source,std::uint32_t id) { return std::string(tier)+source+std::to_string(id); } }
#include "../src/bin/EnemyClassifier.cpp"
static void check(bool value) { if (!value) { std::cerr<<"encounter rule failed\n"; std::exit(1); } }
int main()
{
    using namespace encounters;
    Rules priorityRules;
    auto none=[](std::uint32_t) { return false; };
    check(priorityRules.classify(1,2,3,none,false).tier==Tier::normal);
    check(priorityRules.classify(1,2,3,none,true).source==Source::location);
    priorityRules.keywords[4]=Tier::elite;
    auto keyword=[](std::uint32_t) { return true; };
    check(priorityRules.classify(1,2,3,keyword,true).tier==Tier::elite);
    priorityRules.keywords[5]=Tier::boss;
    check(priorityRules.classify(1,2,3,keyword,false).tier==Tier::boss);
    priorityRules.bases[3]=Tier::normal;
    check(priorityRules.classify(1,2,3,keyword,true).source==Source::originalBase);
    priorityRules.bases[2]=Tier::elite;
    check(priorityRules.classify(1,2,3,keyword,true).source==Source::base);
    priorityRules.references[1]=Tier::normal;
    check(priorityRules.classify(1,2,3,keyword,true).source==Source::reference);
    check(canFinish(Tier::normal,true,1,0.2f));
    check(canFinish(Tier::normal,false,0.2f,0.2f));
    check(!canFinish(Tier::normal,false,0.21f,0.2f));
    check(!canFinish(Tier::boss,true,1,0.2f));
    check(!canFinish(Tier::elite,true,0.21f,0.2f));
    check(canFinish(Tier::boss,false,0.2f,0.2f));
    check(!canFinish(Tier::boss,true,0,0.2f));
    check(!canFinish(Tier::boss,true,std::numeric_limits<float>::quiet_NaN(),0.2f));
    const auto previous=std::filesystem::current_path();
    const auto sandbox=previous/"valhalla-classifier-test";
    check(!std::filesystem::exists(sandbox));
    std::filesystem::create_directory(sandbox); std::filesystem::current_path(sandbox);
    const auto directory=std::filesystem::path("Data/SKSE/Plugins/ValhallaCombat/EnemyClassification");
    std::filesystem::create_directories(directory);
    RE::Actor actor; actor.id=0x100;
    RE::TESNPC base,original; base.id=0x200; original.id=0x300; actor.base=&base;
    RE::BGSKeyword tag; tag.id=0x400; actor.keywords.insert(tag.id);
    RE::BGSLocationRefType bossType; RE::BGSLocation location,parent;
    parent.specialRefs.push_back({&bossType,{actor.id}}); location.parentLoc=&parent; actor.location=&location;
    RE::BGSDefaultObjectManager::GetSingleton()->boss=&bossType;
    RE::TESForm::forms={{actor.id,&actor},{base.id,&base},{original.id,&original},{tag.id,&tag}};
    load(); check(classify(&actor).source==Source::location);
    std::ofstream(directory/"00-base.ini")<<"[Keywords]\nTest.esp|0x400=Elite\n[NPCs]\nTest.esp|0x300=Normal\n";
    load(); check(classify(&actor).tier==Tier::elite);
    RE::ExtraLeveledCreature leveled{&original}; actor.extraList.leveled=&leveled;
    check(classify(&actor).source==Source::originalBase && classify(&actor).tier==Tier::normal);
    std::ofstream(directory/"10-overrides.ini")<<"[References]\nTest.esp|0x100=Boss\nTest.esp|garbage=Elite\n[NPCs]\nTest.esp|0x400=Boss\n";
    load(); check(classify(&actor).source==Source::reference && classify(&actor).tier==Tier::boss);
    std::ofstream(directory/"90-user.ini")<<"[References]\nTest.esp|0x100=Normal\n";
    load(); check(classify(&actor).tier==Tier::normal);
    std::filesystem::remove_all(directory); load(); actor.location=nullptr;
    check(classify(&actor).source==Source::fallback && classify(&actor).tier==Tier::normal);
    std::filesystem::current_path(previous); std::filesystem::remove_all(sandbox);
    std::cout<<"encounter priorityRules passed\n";
}
