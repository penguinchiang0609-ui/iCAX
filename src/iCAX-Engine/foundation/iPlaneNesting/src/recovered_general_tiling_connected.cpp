#include "recovered_general_tiling_connected.hpp"
#include "recovered_escher_toroidal.hpp"
namespace study::recovered {
void GeneralTilingUpperSupplemental::improveToroidal(int32_t level,Budget &budget,EscherToroidalTrace *trace){
    escherImproveToroidalCatalog(catalog_,level,&budget,trace);
}
bool GeneralTilingUpperSupplemental::add(const TilingPattern &pattern){if(check_)check_();const auto result=generalTilingAppendSupplementalPattern(catalog_,pattern,owner_,geometry_.conversion.scale,geometry_.conversion.symmetryX,geometry_.conversion.symmetryY);if(check_)check_();return result;}
std::vector<std::shared_ptr<TilingPattern>> GeneralTilingUpperSupplemental::templates(int32_t count,Point usable){
    GeneralTilingSupplementalCatalog prepared(catalog_);EscherPreparedProviderHost host(prepared.ordinary(),prepared.restricted(),pairLimit_,0,escherBoundaryLengthProvider(boundaryServices_));
    GeneralTilingSupplementalProvider provider(provider_,host,geometry_,seed178_,check_);return provider.templates(count,usable);
}
std::vector<TilingMember> GeneralTilingUpperSupplemental::layout(const EscherProviderRequest &request){
    GeneralTilingSupplementalCatalog prepared(catalog_);EscherPreparedProviderHost host(prepared.ordinary(),prepared.restricted(),pairLimit_,0,escherBoundaryLengthProvider(boundaryServices_));
    GeneralTilingSupplementalProvider provider(provider_,host,geometry_,seed178_,check_);return provider.layout(request);
}
bool GeneralTilingConnectedControlHost::zeroVariantAvailable(){require(bool(source_.variant),PN2S_INVALID,"Connected general tiling needs its actual Part variant cache");const auto &lookup=source_.currentVariant?source_.currentVariant:source_.variant;return bool(lookup({}));}
bool GeneralTilingConnectedControlHost::stopped(){require(bool(stopped_),PN2S_INVALID,"Connected general tiling needs the actual stop state");return stopped_();}
bool GeneralTilingConnectedControlHost::hasSupplementalProvider(){if(source_.check)source_.check();if(!supplemental_&&acquire_)supplemental_=acquire_();return supplemental_!=nullptr;}
std::vector<AmoTilingPatternResult> GeneralTilingConnectedControlHost::orientationPatterns(PlacementOrientation a,PlacementOrientation b){return generalTilingOrientationPatterns(source_,a,b);}
void GeneralTilingConnectedControlHost::addSupplementalPattern(const std::shared_ptr<TilingPattern> &pattern){if(source_.check)source_.check();require(supplemental_&&pattern,PN2S_INVALID,"Connected supplemental pattern requires the live provider and model");supplemental_->add(*pattern);}
std::vector<std::shared_ptr<TilingPattern>> GeneralTilingConnectedControlHost::supplementalTemplates(int32_t count,Point usable){if(source_.check)source_.check();require(supplemental_!=nullptr,PN2S_INVALID,"Connected supplemental templates require the live provider");return supplemental_->templates(count,usable);}
std::vector<TilingMember> GeneralTilingConnectedControlHost::supplementalLayout(const EscherProviderRequest &request){if(source_.check)source_.check();require(supplemental_!=nullptr,PN2S_INVALID,"Connected supplemental layout requires the live provider");return supplemental_->layout(request);}
}
