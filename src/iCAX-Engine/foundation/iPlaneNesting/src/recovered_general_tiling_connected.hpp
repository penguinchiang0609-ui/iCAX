#pragma once
#include "recovered_general_tiling_supplemental_add.hpp"
#include "recovered_general_tiling_amo.hpp"
#include "recovered_escher_fuzzy_bridge.hpp"
namespace study::recovered {
struct EscherToroidalTrace;
class GeneralTilingUpperSupplemental {
    const EscherIntegerShapeRecord &owner_;
    GeneralTilingTemplateGeometry geometry_;
    EscherGeometryCatalog catalog_;
    EscherProvider provider_;
    uint32_t pairLimit_;
    double seed178_;
    EscherPlacementMapCache placementMaps_;
    EscherBoundaryLengthServices boundaryServices_;
    std::function<void()> check_;
public:
    GeneralTilingUpperSupplemental(const EscherIntegerShapeRecord &owner,
        GeneralTilingTemplateGeometry geometry,uint32_t candidateLimit,int32_t defaultCount,
        bool suppressDefaultCount,double seed178,std::function<void()> check={})
        :owner_(owner),geometry_(std::move(geometry)),catalog_(escherProduceGeometryCatalog(owner,candidateLimit)),
        provider_(geometry_.conversion.scale,defaultCount,suppressDefaultCount),pairLimit_(candidateLimit),seed178_(seed178),check_(std::move(check)){
        escherBindBoundaryGeometry(boundaryServices_,placementMaps_);
    }
    bool add(const TilingPattern &);
    void improveToroidal(int32_t level,Budget &,EscherToroidalTrace * =nullptr);
    std::vector<std::shared_ptr<TilingPattern>> templates(int32_t,Point usable);
    std::vector<TilingMember> layout(const EscherProviderRequest &);
    const EscherGeometryCatalog &catalog()const{return catalog_;}
};
class GeneralTilingConnectedControlHost final:public GeneralTilingControlHost {
    const GeneralTilingAmoSource &source_;
    GeneralTilingUpperSupplemental *supplemental_;
    std::function<bool()> stopped_;
    std::function<GeneralTilingUpperSupplemental *()> acquire_;
public:
    GeneralTilingConnectedControlHost(const GeneralTilingAmoSource &source,
        GeneralTilingUpperSupplemental *supplemental,std::function<bool()> stopped,
        std::function<GeneralTilingUpperSupplemental *()> acquire={})
        :source_(source),supplemental_(supplemental),stopped_(std::move(stopped)),acquire_(std::move(acquire)){}
    bool zeroVariantAvailable()override;
    bool stopped()override;
    std::vector<AmoTilingPatternResult> orientationPatterns(PlacementOrientation,PlacementOrientation)override;
    bool hasSupplementalProvider()override;
    void addSupplementalPattern(const std::shared_ptr<TilingPattern> &)override;
    std::vector<std::shared_ptr<TilingPattern>> supplementalTemplates(int32_t,Point usable)override;
    std::vector<TilingMember> supplementalLayout(const EscherProviderRequest &)override;
};
}
