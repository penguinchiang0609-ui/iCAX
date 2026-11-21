#include "recovered_general_tiling_supplemental.hpp"
namespace study::recovered {
GeneralTilingSupplementalCatalog::GeneralTilingSupplementalCatalog(const EscherGeometryCatalog &catalog){
    ordinary_.reserve(catalog.ordinary.size());
    for(const auto &descriptor:catalog.ordinary)ordinary_.push_back(escherBindPreparedPattern(descriptor));
    restricted_.reserve(catalog.restricted.size());
    for(const auto &descriptor:catalog.restricted){
        EscherPreparedLatticePattern pattern;pattern.records=descriptor.records;
        pattern.candidates.reserve(descriptor.normalCandidates.size());
        for(const auto &candidate:descriptor.normalCandidates)pattern.candidates.push_back(candidate.basis);
        restricted_.push_back(std::move(pattern));
    }
}
EscherPatternLayout GeneralTilingSupplementalProvider::prepare(const EscherProviderRequest &request){
    if(check_)check_();auto result=provider_.prepare(request,host_);if(check_)check_();return result;
}
std::vector<TilingMember> GeneralTilingSupplementalProvider::layout(const EscherProviderRequest &request){
    const auto prepared=prepare(request);return escherConvertProviderLayout(prepared.primary,geometry_.conversion);
}
std::vector<std::shared_ptr<TilingPattern>> GeneralTilingSupplementalProvider::templates(int32_t count,Point usable){
    auto tree=generalTilingSupplementalTemplates(count,usable,seed178_,[&](const EscherProviderRequest &request){
        return generalTilingTemplate(std::make_shared<TilingPattern>(generalTilingConvertEscherPattern(prepare(request),geometry_)));
    });
    std::vector<std::shared_ptr<TilingPattern>> output;output.reserve(tree.size());
    for(auto &entry:tree)output.push_back(std::move(entry.pattern));return output;
}
}
