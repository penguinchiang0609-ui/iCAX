#include "recovered_convolution_floating.hpp"
#include "recovered_convolution_offset.hpp"
#include "recovered_convolution_integer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace study::recovered {
ConvolutionPolygon convolutionFloatingMinkowski(const ConvolutionRing &a,const ConvolutionRing &b,const ConvolutionHierarchyDiagnostic &diagnostic){
    std::vector<ConvolutionMultiPolygon> operands{{ConvolutionPolygon{a}},{ConvolutionPolygon{b}}};
    const auto scale=convolutionScaleConfig(operands,INT32_MAX,false);
    const auto quantized=convolutionQuantizeOperands(operands,scale);
    const auto integer=convolutionIntegerMinkowski(quantized.at(0).at(0),quantized.at(1).at(0),diagnostic);
    auto result=convolutionDequantizePolygon(integer.rings);
    // 7f3b16..7f3b24 precompute these sums before the loop. The decompiler
    // expands them into misleading left-associated expressions.
    const double dx=scale.references[0][0]+scale.references[1][0];
    const double dy=scale.references[1][1]+scale.references[0][1];
    for(auto &ring:result)for(auto &p:ring){
        p[0]=p[0]/scale.factors[0]+dx;
        p[1]=p[1]/scale.factors[1]+dy;
    }
    return result;
}
ConvolutionPolygon convolutionFloatingMinkowski(const ConvolutionTopology &a,const ConvolutionTopology &b,const ConvolutionHierarchyDiagnostic &diagnostic){
    return convolutionFloatingMinkowski(convolutionTopologyPoints(a),convolutionTopologyPoints(b),diagnostic);
}

ConvolutionFloatingOffsetResult convolutionFloatingOffset(
    const ConvolutionPolygon &rings, double radius, int32_t resolution,
    const ConvolutionHierarchyDiagnostic &diagnostic) {
    if (std::all_of(rings.begin(), rings.end(), [](const auto &ring) { return ring.empty(); }))
        return {};
    // 7f0110 includes this temporary operand when choosing a common scale.
    // Its size bounds the radius even when the offset exceeds the part size.
    const ConvolutionRing radiusTriangle{{0,0}, {0,radius}, {radius,0}};
    std::vector<ConvolutionMultiPolygon> operands{{rings}, {{radiusTriangle}}};
    const auto scale = convolutionScaleConfig(operands, INT32_MAX, true);
    const auto quantized = convolutionQuantizeOperands(operands, scale);
    const double roundedRadius = std::round(scale.factors[0] * radius);
    // The vendor CRT lround returns zero for nonfinite or out-of-int32 input.
    const int32_t integerRadius = roundedRadius >= double(INT32_MIN) && roundedRadius <= double(INT32_MAX)
        ? int32_t(roundedRadius) : 0;
    auto integer = convolutionIntegerOffset(quantized.at(0).at(0), integerRadius, resolution, diagnostic);
    auto output = convolutionDequantizePolygon(integer.rings);
    for (auto &ring : output)
        for (auto &point : ring)
            for (size_t axis = 0; axis < 2; ++axis)
                point[axis] = point[axis] / scale.factors[axis] + scale.references[0][axis];
    return {std::move(output), std::move(integer.roots)};
}

std::array<ConvolutionPolygon, 2> convolutionFloatingOffsetPair(
    const ConvolutionPolygon &first, const ConvolutionPolygon &second,
    double radius, int32_t resolution, const ConvolutionHierarchyDiagnostic &diagnostic) {
    if (first.empty() || first.front().empty())
        throw std::invalid_argument("Paired offset requires a nonempty first ring");
    const ConvolutionRing radiusTriangle{{0,0}, {0,radius}, {radius,0}};
    // The two polygons share one reference point and one quantization scale.
    std::vector<ConvolutionMultiPolygon> operands{{first, second}, {{radiusTriangle}}};
    const auto scale = convolutionScaleConfig(operands, INT32_MAX, true);
    const auto quantized = convolutionQuantizeOperands(operands, scale);
    const double roundedRadius = std::round(scale.factors[0] * radius);
    const int64_t integerRadius = roundedRadius >= double(INT32_MIN) && roundedRadius <= double(INT32_MAX)
        ? int32_t(roundedRadius) : 0;
    std::array<ConvolutionIntegerPolygon, 2> integer{
        convolutionIntegerOffset(quantized.at(0).at(0), integerRadius, resolution, diagnostic).rings,
        convolutionIntegerOffset(quantized.at(0).at(1), -integerRadius, resolution, diagnostic).rings};
    // 7f1d84 partitions by swapping from opposite ends, preserving its exact
    // traversal. Negative contours move to the second output and reverse.
    size_t begin = 0, end = integer[0].size();
    while (begin != end) {
        if (convolutionIntegerOrientation(integer[0][begin]) == 1) {
            ++begin;
            continue;
        }
        do {
            --end;
        } while (begin != end && convolutionIntegerOrientation(integer[0][end]) != 1);
        if (begin != end)
            std::swap(integer[0][begin++], integer[0][end]);
    }
    for (size_t index = begin; index < integer[0].size(); ++index) {
        auto &ring = integer[0][index];
        std::reverse(ring.begin(), ring.end());
        integer[1].push_back(std::move(ring));
    }
    integer[0].resize(begin);
    std::array<ConvolutionPolygon, 2> output;
    for (size_t index = 0; index < output.size(); ++index) {
        output[index] = convolutionDequantizePolygon(integer[index]);
        for (auto &ring : output[index])
            for (auto &point : ring)
                for (size_t axis = 0; axis < 2; ++axis)
                    point[axis] = point[axis] / scale.factors[axis] + scale.references[0][axis];
    }
    return output;
}
ConvolutionPolygon convolutionFloatingAlternatingOffset(
    const ConvolutionMultiPolygon &input, const ConvolutionHierarchyDiagnostic &diagnostic) {
    if (input.empty() || input.front().empty() || input.front().front().empty())
        throw std::invalid_argument("Alternating offset requires a nonempty first ring");
    std::vector<ConvolutionMultiPolygon> operands{input};
    const auto scale = convolutionScaleConfig(operands, INT32_MAX, false);
    auto integer = convolutionQuantizeOperands(operands, scale).front();
    for (auto &polygon : integer)
        polygon = convolutionIntegerOffset(polygon, 6, 4, diagnostic).rings;
    convolutionMergeIntegerOffsets(integer, diagnostic);
    for (auto &polygon : integer)
        polygon = convolutionIntegerOffset(polygon, -6, 4, diagnostic).rings;
    ConvolutionPolygon result;
    for (const auto &polygon : integer) {
        auto converted = convolutionDequantizePolygon(polygon);
        for (auto &ring : converted) {
            for (auto &point : ring)
                for (size_t axis = 0; axis < 2; ++axis)
                    point[axis] = point[axis] / scale.factors[axis] + scale.references[0][axis];
            result.push_back(std::move(ring));
        }
    }
    return result;
}
}
