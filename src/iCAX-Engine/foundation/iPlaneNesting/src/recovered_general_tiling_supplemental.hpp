#pragma once
#include "recovered_general_tiling_templates.hpp"
#include "recovered_escher_geometry_producer.hpp"
#include "recovered_escher_boundary.hpp"
namespace study::recovered {
// Owns the prepared catalogs, not the integer shape records they reference.
// Keep the actual owners stable for this object's entire lifetime.
class GeneralTilingSupplementalCatalog {
    std::vector<EscherPreparedSinglePattern> ordinary_;
    std::vector<EscherPreparedLatticePattern> restricted_;
public:
    explicit GeneralTilingSupplementalCatalog(const EscherGeometryCatalog &);
    std::span<const EscherPreparedSinglePattern> ordinary()const{return ordinary_;}
    std::span<const EscherPreparedLatticePattern> restricted()const{return restricted_;}
};
// The provider and its real catalog host retain their caches across upper
// controller calls. No substitute layout provider or geometric fallback.
class GeneralTilingSupplementalProvider {
    EscherProvider &provider_;
    EscherProviderHost &host_;
    GeneralTilingTemplateGeometry geometry_;
    double seed178_;
    std::function<void()> check_;
public:
    GeneralTilingSupplementalProvider(EscherProvider &provider,EscherProviderHost &host,
        GeneralTilingTemplateGeometry geometry,double seed178,std::function<void()> check={})
        :provider_(provider),host_(host),geometry_(std::move(geometry)),seed178_(seed178),check_(std::move(check)){}
    EscherPatternLayout prepare(const EscherProviderRequest &); //493b00
    std::vector<TilingMember> layout(const EscherProviderRequest &); //493b00 ->4939a0
    std::vector<std::shared_ptr<TilingPattern>> templates(int32_t,Point usable); //4a1d80
};
}
