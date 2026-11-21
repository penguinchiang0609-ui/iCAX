#include "recovered_convolution_line.hpp"
#include <bit>
#include <cmath>

namespace study::recovered {
namespace {
double quiet(double value) { return std::bit_cast<double>(std::bit_cast<uint64_t>(value) | 0x8000000000000ULL); }
double add(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a + b; }
double sub(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a - b; }
double mul(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a * b; }
double div(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a / b; }
}
ConvolutionCurveSegment ConvolutionCurves::createRadiusArc(ShakerPoint first, ShakerPoint last,
                                                          double signedRadius) {
    const double midpointY = mul(add(last[1], first[1]), .5);
    const double midpointX = mul(add(last[0], first[0]), .5);
    const double x = sub(last[0], first[0]), inverseY = sub(first[1], last[1]);
    const double y = sub(last[1], first[1]);
    const double square = sub(mul(signedRadius, signedRadius), mul(add(mul(y, y), mul(x, x)), .25));
    double offsetX = square <= 0 ? 0 : std::sqrt(square);
    const double length = std::sqrt(add(mul(inverseY, inverseY), mul(x, x)));
    double offsetY = 0;
    if (length >= 1e-9) {
        const double scale = div(offsetX, length);
        offsetX = mul(inverseY, scale);
        offsetY = mul(scale, x);
    }
    auto circle = std::make_shared<ConvolutionCircle>();
    if (signedRadius < 0)
        circle->center = {sub(midpointX, offsetX), sub(midpointY, offsetY)};
    else
        circle->center = {add(offsetX, midpointX), add(offsetY, midpointY)};
    circle->radius = std::abs(signedRadius);
    double begin = convolutionCircleParameter(*circle, first);
    double end = convolutionCircleParameter(*circle, last);
    const double period = mul(circle->radius, 6.283185307179586);
    if (signedRadius < 0) {
        while (begin < end) begin = add(begin, period);
    } else {
        while (end < begin) end = add(end, period);
    }
    ConvolutionCurveSegment source;
    source.circle = std::move(circle);
    return range(source, begin, end);
}
}
