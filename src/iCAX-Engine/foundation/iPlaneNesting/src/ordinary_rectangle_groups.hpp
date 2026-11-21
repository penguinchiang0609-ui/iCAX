#pragma once
#include "rectangle_runtime.hpp"
#include "recovered_rectangle_groups.hpp"
namespace study {
struct RectanglePrimitiveSettings {
    double remaining = std::numeric_limits<double>::max();
    bool specialPreNest = false, forceB0 = false;
};
struct RectanglePrimitiveGroups {
    std::vector<RectangleTilingModel> models;
    std::vector<size_t> complexGroups;
};
// Input poses are finite SDK variants; range inputs must first use Rectangle's
// existing canonical-angle normalization and retain its source pose mapping.
RectanglePrimitiveGroups ordinaryRectanglePrimitiveGroups(const Input &, const RectanglePrimitiveSettings & = {});
}
