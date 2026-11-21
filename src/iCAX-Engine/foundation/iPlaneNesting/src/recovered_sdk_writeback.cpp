#include "recovered_sdk_writeback.hpp"
#include <immintrin.h>

namespace study::recovered {
namespace {
double add(double a,double b){return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double scale(double a,double b){return _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double rounded(double value,double factor){
    const auto scaled=scale(value,factor);
    // COMISD/JB takes this subtraction branch for an unordered operand too.
    return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(scaled),_mm_set_sd(scaled>=0?.5:-.5)));
}
}
std::array<double,4> shakerSdkBounds(int32_t mode,double x,double y,double width,double height,
                                    std::optional<int64_t> occupiedRight){
    const double right=mode==5||mode==9?add(x,width):
        _mm_cvtsd_f64(_mm_div_sd(_mm_cvtsi64_sd(_mm_setzero_pd(),occupiedRight.value_or(0)),_mm_set_sd(1e6)));
    return {x,add(right,5e-7),y,add(add(y,height),5e-7)};
}
SdkWritebackPosition shakerSdkUnchangedPosition(Point position){
    // Preserve the original Y-before-X conversion order.
    const auto y=_mm_cvttsd_si32(_mm_set_sd(rounded(position.y,1000.)));
    const auto x=_mm_cvttsd_si64(_mm_set_sd(rounded(position.x,1e6)));
    return {x,y};
}
SdkWritebackPosition shakerSdkChangedPosition(Point position,Point correction){
    const auto x=_mm_cvttsd_si64(_mm_set_sd(rounded(add(position.x,correction.x),1e6)));
    const auto y=_mm_cvttsd_si32(_mm_set_sd(rounded(add(position.y,correction.y),1000.)));
    return {x,y};
}
Point shakerSdkFloatingPosition(SdkWritebackPosition position){
    return {double(position.x)/1e6,double(position.y)/1000.};
}
}
