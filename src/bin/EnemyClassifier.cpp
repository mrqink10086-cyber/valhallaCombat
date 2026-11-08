#include "include/EnemyClassifier.h"
#include "include/lib/SimpleIni.h"
#include <charconv>
#include <filesystem>
#undef GetObject

namespace encounters {
namespace {
Rules rules;
RE::TESForm* resolve(std::string_view key)
{
    const auto separator=key.find('|');
    if (separator==std::string_view::npos) return nullptr;
    auto number=key.substr(separator+1);
    if (number.starts_with("0x") || number.starts_with("0X")) number.remove_prefix(2);
    std::uint32_t id=0;
    const auto parsed=std::from_chars(number.data(),number.data()+number.size(),id,16);
    if (parsed.ec!=std::errc{} || parsed.ptr!=number.data()+number.size()) return nullptr;
    auto handler=RE::TESDataHandler::GetSingleton();
    return handler ? handler->LookupForm(id,key.substr(0,separator)) : nullptr;
}
bool locationBoss(RE::Actor* actor)
{
    auto manager=RE::BGSDefaultObjectManager::GetSingleton();
    auto boss=manager ? manager->GetObject<RE::BGSLocationRefType>(RE::DEFAULT_OBJECT::kLocRefTypeBoss) : nullptr;
    if (!boss) return false;
    auto location=actor->GetCurrentLocation();
    for (int depth=0;location && depth<16;++depth,location=location->parentLoc)
        for (const auto& ref:location->specialRefs)
            if (ref.type==boss && ref.refData.refID==actor->GetFormID()) return true;
    return false;
}
}
void load()
{
    Rules loaded;
    const std::filesystem::path directory="Data/SKSE/Plugins/ValhallaCombat/EnemyClassification";
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    if (std::filesystem::is_directory(directory,error))
        for (const auto& entry:std::filesystem::directory_iterator(directory,error))
            if (entry.path().extension()==".ini") paths.push_back(entry.path());
    std::sort(paths.begin(),paths.end());
    for (const auto& path:paths) {
        CSimpleIniA ini; ini.SetUnicode();
        if (ini.LoadFile(path.string().c_str())<0) { logger::warn("Cannot load enemy rules {}",path.string()); continue; }
        for (const auto* section:{"References","NPCs","Keywords"}) {
            CSimpleIniA::TNamesDepend keys; ini.GetAllKeys(section,keys);
            for (const auto& key:keys) {
                const std::string_view text=ini.GetValue(section,key.pItem,"");
                Tier tier;
                if (text=="Normal") tier=Tier::normal;
                else if (text=="Elite") tier=Tier::elite;
                else if (text=="Boss") tier=Tier::boss;
                else { logger::warn("Invalid enemy class {} in {}",text,path.string()); continue; }
                auto form=resolve(key.pItem);
                const std::string_view kind=section;
                const bool valid=form && ((kind=="References" && form->As<RE::Actor>()) ||
                    (kind=="NPCs" && form->As<RE::TESNPC>()) || (kind=="Keywords" && form->As<RE::BGSKeyword>()));
                if (!valid) { logger::warn("Unresolved or wrong-type enemy rule {} in {}",key.pItem,path.string()); continue; }
                auto& table=kind=="References" ? loaded.references : kind=="NPCs" ? loaded.bases : loaded.keywords;
                table.insert_or_assign(form->GetFormID(),tier);
            }
        }
    }
    rules=std::move(loaded);
    logger::info("Enemy rules: {} references, {} NPCs, {} keywords",rules.references.size(),rules.bases.size(),rules.keywords.size());
}
Classification classify(RE::Actor* actor)
{
    if (!actor) return {};
    auto base=actor->GetActorBase();
    auto leveled=actor->extraList.GetByType<RE::ExtraLeveledCreature>();
    auto original=leveled ? leveled->originalBase : nullptr;
    return rules.classify(actor->GetFormID(),base?base->GetFormID():0,original?original->GetFormID():0,
        [actor](std::uint32_t id) { auto keyword=RE::TESForm::LookupByID<RE::BGSKeyword>(id); return keyword && actor->HasKeyword(keyword); },locationBoss(actor));
}
std::string describe(const Classification& value)
{
    const char* source="unmarked default";
    switch (value.source) {
    case Source::reference: source="reference override"; break;
    case Source::base: source="NPC override"; break;
    case Source::originalBase: source="leveled NPC override"; break;
    case Source::keyword: source="keyword rule"; break;
    case Source::location: source="location boss reference"; break;
    default: break;
    }
    return fmt::format("{} / {} / {:08X}",tierName(value.tier),source,value.match);
}
}
