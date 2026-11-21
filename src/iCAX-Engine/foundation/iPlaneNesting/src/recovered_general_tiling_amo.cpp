#include "recovered_general_tiling_amo.hpp"
#include "recovered_amo_input.hpp"
namespace study::recovered {
std::vector<AmoTilingPatternResult> generalTilingOrientationPatterns(
    const GeneralTilingAmoSource &source,PlacementOrientation first,PlacementOrientation second){
    auto prepared=generalTilingPrepareOrientationInput(source,first,second,[&](std::span<const Point> points,double gap,bool flag){
        if(source.check)source.check();return amoTilingCreateInputShape(points,gap,flag);
    });
    if(!prepared)return {};
    return amoTilingPrepareOrientationPatterns(prepared->job,prepared->request,prepared->required,prepared->conversion);
}
}
