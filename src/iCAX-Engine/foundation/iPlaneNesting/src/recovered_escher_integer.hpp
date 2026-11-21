#pragma once
#include "recovered_convolution_polygon.hpp"
namespace study::recovered {
// Integer-job scale is isotropic and uses polygon diagonals. It is distinct
// from the independent X/Y scale used by Minkowski operand preparation.
double escherIntegerScale(std::span<const ConvolutionPolygon> parts,
    std::span<const ConvolutionPolygon> sheets,int64_t limit,double gap); // 7f0920
std::vector<ShakerPoint> escherIntegerReferences(std::span<const ConvolutionPolygon>); // 7eebe0
struct EscherIntegerGeometry {
    double scale=0;
    std::vector<ShakerPoint> partReferences,sheetReferences;
    std::vector<ConvolutionIntegerPolygon> parts,sheets;
};
// Straight, zero-protection geometry branch of 568170 -> 7fa900 -> 7f8bd0.
// The surrounding technology/pricing metadata is not fabricated here.
EscherIntegerGeometry escherIntegerGeometry(std::span<const ConvolutionPolygon> parts,
    std::span<const ConvolutionPolygon> sheets,int64_t limit=INT32_MAX,double gap=0);
}
