#pragma once
#include "recovered_placement.hpp"
#include <functional>
namespace study::recovered {
struct AmoTilingOrientationRange {double maximum=0,step=0;};
// The original 54f0e0 module catalog is queried afresh for each tested angle.
// Each returned record is an actual module's orientation, not a flattened
// union of different modules' allowed intervals.
using AmoTilingModuleOrientations=std::function<std::vector<PlacementOrientation>()>;
AmoTilingOrientationRange amoTilingOrientationRange(const AmoTilingModuleOrientations &,
    PlacementOrientation first,PlacementOrientation second,bool symmetryX,bool symmetryY); //4918c0
}
