#pragma once
#include "rectangle_runtime.hpp"
namespace study {
// 498580's full-rectangle branch. Input variants are finite and canonical;
// continuous source intervals are mapped by ordinaryRectangle's adapter.
std::vector<RectangleTilingModel> ordinaryRectangleComplexModels(
    const Input &, const std::vector<size_t> &, Budget &, bool contextDisabled = false);
std::vector<RectangleTilingModel> ordinaryRectangleTilingModels(const Input &, Budget &);
struct OrdinaryGeneralTilingTrace;
std::vector<RectangleTilingModel> ordinaryRectangleTilingModelsForInput(const Input &, Budget &,
    double contextSeconds=NAN,OrdinaryGeneralTilingTrace * = nullptr);
std::vector<RectangleTilingModel> rectangleExpandModelLimits(const std::vector<RectangleTilingModel> &);
}
