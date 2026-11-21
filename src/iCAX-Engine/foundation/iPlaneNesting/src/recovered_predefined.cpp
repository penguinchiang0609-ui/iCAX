#include "recovered_predefined.hpp"
#include <bit>
#include <immintrin.h>

namespace study::recovered {
bool preNestCountValid(int32_t count) { return count > 0; }
PredefinedCountAction nestMoreCountAction(int32_t count) {
    return count < 0 ? PredefinedCountAction::Invalid : count == 0 ?
        PredefinedCountAction::ReturnExisting : PredefinedCountAction::Proceed;
}
bool predefinedOrientationValid(SdkOrientation pose) {
    // Follow the actual COMISD/JA branches. An unordered minimum does not take
    // either JA rejection; the final JBE rejects an unordered maximum.
    if(!(1e-9>std::abs(pose.maximum-pose.minimum)) && pose.minimum>pose.maximum)return false;
    if(!(1e-9>std::abs(pose.minimum)) && 0.>pose.minimum)return false;
    if(1e-9>std::abs(pose.maximum-360.))return false;
    return pose.maximum<360.;
}
bool predefinedMovable(uint32_t mode, bool moveDefault) { return mode == 1 || (mode == 2 && moveDefault); }
PredefinedConstraintRecord predefinedConstraints(const PredefinedSdkRecord &part, Point origin, bool moveDefault) {
    const auto round=[](double coordinate,double scale){const double value=coordinate*scale;return value<0?value-.5:value+.5;};
    PredefinedConstraintRecord result;result.partIdentity=part.partIdentity;
    result.x=_mm_cvttsd_si64(_mm_set_sd(round(part.position.x-origin.x,1e6)));
    result.y=_mm_cvttsd_si32(_mm_set_sd(round(part.position.y-origin.y,1000.)));
    result.orientation=quantizeSdkOrientation(part.orientation,false);
    result.movable=predefinedMovable(part.glueMode,moveDefault);return result;
}
double predefinedRound(double value) {
    auto bits=std::bit_cast<uint64_t>(value);const auto exponent=(bits>>52)&0x7ff;
    if(exponent<0x433){
        if(exponent<0x3ff){bits&=0x8000000000000000ULL;if(exponent==0x3fe)bits|=0x3ff0000000000000ULL;}
        else {const auto shift=uint32_t(0x433-exponent);bits=((bits>>shift)+((bits>>(shift-1))&1))<<shift;}
    }
    return std::bit_cast<double>(bits);
}
int32_t predefinedMinimumDifficulty(double x,double y) {
    return _mm_cvttsd_si32(_mm_set_sd(predefinedRound(y*50.+x*50.)));
}
int32_t predefinedDifficulty(const PredefinedDifficulty &input,std::array<double,3> weights,bool orientation) {
    if(input.disabled)return 0;
    uint32_t result=0;
    for(size_t axis=0;axis<(orientation?3:2);++axis){
        int32_t maximum=input.cached[axis];for(const auto value:input.dynamic[axis])maximum=std::max(maximum,value);
        const double value=double(maximum)*weights[axis];
        result+=uint32_t(_mm_cvttsd_si32(_mm_set_sd(value<0?value-.5:value+.5)));
    }
    return std::bit_cast<int32_t>(result);
}
bool predefinedUnaligned(int64_t x,int64_t left,int64_t pitch,int64_t origin) {
    if(!pitch)throw std::invalid_argument("predefined placement requires nonzero pitch");
    const auto coordinate=std::bit_cast<int64_t>(uint64_t(x)+uint64_t(left));
    const auto relative=std::bit_cast<int64_t>(uint64_t(coordinate)-uint64_t(origin));
    if(relative==INT64_MIN && pitch==-1)throw std::invalid_argument("predefined placement division overflow");
    auto quotient=std::bit_cast<int32_t>(uint32_t(relative/pitch));
    if(relative<0 && relative%pitch)quotient=std::bit_cast<int32_t>(uint32_t(quotient)-1);
    const auto aligned=std::bit_cast<int64_t>(uint64_t(int64_t(quotient))*uint64_t(pitch)+uint64_t(origin));
    return aligned!=coordinate;
}
uint32_t runPredefinedNester(PredefinedNesterHost &host,std::span<const uintptr_t> parts,
                             const PredefinedNesterOptions &options) {
    uint32_t total=0;
    for(;;){
        std::vector<uintptr_t> selected;
        for(const auto part:parts)for(const auto instance:host.instances(part))
            if(options.minimumDifficulty<=host.difficulty(instance))selected.push_back(instance);
        uint32_t added=0;
        for(const auto instance:selected){
            if(host.placed(instance) || options.minimumDifficulty>host.difficulty(instance))continue;
            for(const auto &variant:host.variants(instance)){
                const auto x=host.solveX(instance,variant,options.checkConstraints4e0,options.flag68);if(!x)continue;
                const auto y=host.solveY(instance,variant,options.checkConstraints4e0,options.flag68);if(!y)continue;
                const bool unaligned=predefinedUnaligned(*x,variant.leftE0,options.pitch17f0,options.origin17f8);
                host.commit(instance,variant,*x,*y,unaligned);++added;break;
            }
        }
        if(!added)return total;
        total+=added;
    }
}
}
