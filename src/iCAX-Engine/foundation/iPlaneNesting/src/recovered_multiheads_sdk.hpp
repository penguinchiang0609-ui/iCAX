#pragma once
#include "recovered_multiheads.hpp"

namespace study::recovered {
struct MultiheadsSdkState {
    MultiheadsHardware hardware{1,-1,0.,0.};
    double materialUnitCost10=1,masterCuttingUnitCost18=17,additionalHeadCuttingUnitCost20=1;
    bool session39c=false;
};
// Public setters 6a1270 / 6a14a0 / 6a16e0 / 6a1970. Error -973 leaves
// state unchanged. Floating setters use the original COMISD/JA comparison,
// which accepts unordered NaN and infinity as well as both zero signs.
int32_t multiheadsSetNumber(MultiheadsSdkState &,int32_t) noexcept;
int32_t multiheadsSetMinimumGap(MultiheadsSdkState &,double) noexcept;
int32_t multiheadsSetMaximumGap(MultiheadsSdkState &,double) noexcept;
int32_t multiheadsSetAdditionalCuttingCost(MultiheadsSdkState &,double) noexcept;
}
