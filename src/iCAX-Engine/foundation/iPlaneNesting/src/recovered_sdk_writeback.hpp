#pragma once
#include "core.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace study::recovered {
// 650150, 6511b7..65123f. Extent slots belong to the predefined owner;
// ordinary modes use the current native right extent rather than width08.
std::array<double,4> shakerSdkBounds(int32_t mode,double originX70,double originY78,
    double width08,double height10,std::optional<int64_t> occupiedRight);

struct SdkWritebackPosition {
    int64_t x=0;
    int32_t y=0;
    bool operator==(const SdkWritebackPosition &) const = default;
};
// 650150, 651fa7..651fe7. The native placement uses asymmetric X/Y grids.
SdkWritebackPosition shakerSdkUnchangedPosition(Point solved);
// 650150, 651dfe..651e34 + 34e2b0. The changed-module correction returned
// by 387640 is added in floating coordinates before either quantization.
SdkWritebackPosition shakerSdkChangedPosition(Point solved,Point correction);
Point shakerSdkFloatingPosition(SdkWritebackPosition);
}
