#pragma once
#include "recovered_general_tiling_producer.hpp"
#include "recovered_escher_conversion.hpp"
namespace study::recovered {
// 49d4c0 compares shared placement handles, not their geometry or Part keys.
// Keep those identities separate from the recovered value representation.
struct GeneralTilingTemplate {
    std::shared_ptr<TilingPattern> pattern;
    std::vector<uintptr_t> memberIdentities;
};
GeneralTilingTemplate generalTilingTemplate(std::shared_ptr<TilingPattern>);
int8_t generalTilingCompareTemplates(const GeneralTilingTemplate &,
    const GeneralTilingTemplate &); //49d4c0 /4a1d20
using GeneralTilingTemplateProvider=std::function<GeneralTilingTemplate(const EscherProviderRequest &)>;
std::vector<GeneralTilingTemplate> generalTilingSupplementalTemplates(int32_t targetCount,
    Point usable,double seed178,const GeneralTilingTemplateProvider &); //4a1d80
struct GeneralTilingTemplateGeometry {
    EscherProviderConversionGeometry conversion;
    int32_t required=-1,maximumX=0,maximumY=0;
};
TilingPattern generalTilingConvertEscherPattern(const EscherPatternLayout &,
    const GeneralTilingTemplateGeometry &); //493120 /49cc90 /49c6c0
}
