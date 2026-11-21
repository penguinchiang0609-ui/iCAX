#include "recovered_multiheads_sdk.hpp"

namespace study::recovered {
int32_t multiheadsSetNumber(MultiheadsSdkState &state,int32_t value) noexcept {
    if(value<1)return -973;
    if(value>1)state.session39c=true;
    state.hardware.heads48=value;return 0;
}
int32_t multiheadsSetMinimumGap(MultiheadsSdkState &state,double value) noexcept {
    if(value<0.)return -973;
    state.hardware.minimumGap58=value;return 0;
}
int32_t multiheadsSetMaximumGap(MultiheadsSdkState &state,double value) noexcept {
    if(value<0.)return -973;
    state.hardware.maximumGap60=state.hardware.minimumGap58>value?state.hardware.minimumGap58:value;
    return 0;
}
int32_t multiheadsSetAdditionalCuttingCost(MultiheadsSdkState &state,double value) noexcept {
    if(value<0.)return -973;
    state.additionalHeadCuttingUnitCost20=value;return 0;
}
}
