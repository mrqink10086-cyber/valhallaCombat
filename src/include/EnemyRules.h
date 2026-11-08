#pragma once
#include <cmath>
#include <cstdint>
#include <map>

namespace encounters {
enum class Tier { normal, elite, boss };
enum class Source { fallback, reference, base, originalBase, keyword, location };
struct Classification { Tier tier{Tier::normal}; Source source{Source::fallback}; std::uint32_t match{0}; };
struct Rules {
    std::map<std::uint32_t, Tier> references, bases, keywords;
    template<class HasKeyword>
    Classification classify(std::uint32_t reference, std::uint32_t base, std::uint32_t original,
                            HasKeyword hasKeyword, bool locationBoss) const
    {
        if (auto it=references.find(reference); it!=references.end()) return {it->second,Source::reference,reference};
        if (auto it=bases.find(base); it!=bases.end()) return {it->second,Source::base,base};
        if (auto it=bases.find(original); original && it!=bases.end()) return {it->second,Source::originalBase,original};
        Classification result;
        bool matched=false;
        for (const auto& [id,tier] : keywords) if (hasKeyword(id) && (!matched || tier>result.tier)) {
            result={tier,Source::keyword,id}; matched=true;
        }
        if (matched) return result;
        return locationBoss ? Classification{Tier::boss,Source::location,reference} : result;
    }
};
inline bool canFinish(Tier tier, bool broken, float healthFraction, float threshold)
{
    return (tier==Tier::normal && broken) ||
        (std::isfinite(healthFraction) && healthFraction>0 && healthFraction<=threshold);
}
inline const char* tierName(Tier tier)
{
    switch (tier) { case Tier::elite: return "Elite"; case Tier::boss: return "Boss"; default: return "Normal"; }
}
}
