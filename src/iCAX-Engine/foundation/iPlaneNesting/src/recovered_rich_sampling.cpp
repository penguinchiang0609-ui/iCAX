#include "recovered_rich_sampling.hpp"
#include <bit>
#include <cmath>
#include <climits>

namespace study::recovered {
namespace {
constexpr double tau = 6.283185307179586;
double quiet(double value) { return std::bit_cast<double>(std::bit_cast<uint64_t>(value) | 0x8000000000000ULL); }
double add(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a + b; }
double sub(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a - b; }
double mul(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a * b; }
double div(double a, double b) { if (std::isnan(a)) return quiet(a); if (std::isnan(b)) return quiet(b); return a / b; }
int32_t truncateCount(double value) {
    return value >= double(INT32_MIN) && value < 2147483648. ? int32_t(value) : INT32_MIN;
}
}
std::vector<ShakerPoint> richCircumscribedArcPoints(const RichCircle &circle, double beginAngle,
                                                  double endAngle, double error) {
    const double span = sub(endAngle, beginAngle);
    const double angle = std::acos(div(circle.radius, add(std::abs(error), circle.radius)));
    const double halfStep = .7853981633974483 < angle ? .7853981633974483 : angle;
    const int32_t count = truncateCount(std::ceil(div(std::abs(span), add(halfStep, halfStep))));
    const double step = div(span, double(count));
    const double cosine = std::cos(mul(step, .5));
    const double factor = div(add(mul(sub(div(1., cosine), 1.), circle.radius), circle.radius), circle.radius);
    RichCircle expanded;
    expanded.center = circle.center;
    expanded.radial = {mul(factor, circle.radial[0]), mul(factor, circle.radial[1])};
    expanded.radius = std::sqrt(add(mul(expanded.radial[1], expanded.radial[1]),
                                    mul(expanded.radial[0], expanded.radial[0])));
    const double unit = div(mul(expanded.radius, tau), tau);
    const double increment = mul(unit, step);
    double parameter = add(mul(unit, beginAngle), mul(increment, .5));
    std::vector<ShakerPoint> result;
    if (count > 0) result.reserve(size_t(count));
    for (int32_t index = 0; index < count; ++index) {
        result.push_back(richCircleSample(expanded, parameter).point);
        parameter = add(parameter, increment);
    }
    return result;
}
std::vector<ShakerPoint> richInscribedArcPoints(const RichCircle &circle,
                                              std::array<double, 2> parameters, double error) {
    const double innerRadius = sub(circle.radius, std::abs(error));
    const double nonnegative = innerRadius < 0. ? 0. : innerRadius;
    const double angle = std::acos(div(nonnegative, circle.radius));
    const double period = richCirclePeriod(circle);
    const double span = mul(div(tau, period), sub(parameters[1], parameters[0]));
    const double doubled = add(angle, angle);
    const double stepLimit = 1.5707963267948966 < doubled ? 1.5707963267948966 : doubled;
    const double quotient = div(std::abs(span), stepLimit);
    const double sign = sub(0., quotient) > 1e-6 ? -1. : double(sub(quotient, 0.) > 1e-6);
    const int32_t count = truncateCount(add(div(mul(sign, 1e-6), 10.), quotient));
    const int32_t denominator = std::bit_cast<int32_t>(uint32_t(count) + 1);
    const double increment = mul(div(period, tau), div(span, double(denominator)));
    double parameter = parameters[0];
    std::vector<ShakerPoint> result;
    if (count > 0) result.reserve(size_t(count));
    for (int32_t index = 0; index < count; ++index) {
        parameter = add(parameter, increment);
        result.push_back(richCircleSample(circle, parameter).point);
    }
    return result;
}
std::vector<ShakerPoint> richDiscretizeArc(const RichCircle &circle,
    std::array<double, 2> parameters, double error, bool alignQuadrants) {
    if (mul(sub(parameters[1], parameters[0]), error) < 0.)
        return richInscribedArcPoints(circle, parameters, error);
    return richCircumscribedArc(circle, parameters, error, alignQuadrants);
}
double richSamplingQuadrantRemainder(double angle) {
    constexpr double quadrant = 1.5707963267948966, tolerance = 1e-6;
    if (std::abs(sub(angle, 0.)) <= tolerance) angle = 0.;
    else if (std::abs(sub(angle, quadrant)) <= tolerance) angle = quadrant;
    const double quotient = div(sub(0., angle), quadrant);
    const double sign = sub(0., quotient) > 1e-6 ? -1. : double(sub(quotient, 0.) > 1e-6);
    int32_t count = truncateCount(add(div(mul(sign, tolerance), 10.), quotient));
    if (!(0. > quotient) && !(tolerance >= std::abs(sub(double(count), quotient))))
        count = std::bit_cast<int32_t>(uint32_t(count) + 1);
    return add(mul(double(count), quadrant), angle);
}
std::vector<ShakerPoint> richCircumscribedArc(const RichCircle &circle,
    std::array<double, 2> parameters, double error, bool alignQuadrants) {
    const double unit = div(tau, richCirclePeriod(circle));
    const double begin = mul(unit, parameters[0]), end = mul(unit, parameters[1]);
    if (!alignQuadrants) return richCircumscribedArcPoints(circle, begin, end, error);
    constexpr double quadrant = 1.5707963267948966, tolerance = 1e-6;
    const double remainder = richSamplingQuadrantRemainder(begin);
    double first = begin;
    if (tolerance < std::abs(sub(remainder, 0.))) first = sub(add(begin, quadrant), remainder);
    const double lastRemainder = richSamplingQuadrantRemainder(end);
    const double last = sub(end, lastRemainder);
    if (sub(first, end) > tolerance) return richCircumscribedArcPoints(circle, begin, end, error);
    std::vector<ShakerPoint> result;
    const auto append = [&](double a, double b) {
        auto points = richCircumscribedArcPoints(circle, a, b, error);
        result.insert(result.end(), points.begin(), points.end());
    };
    if (tolerance < std::abs(sub(first, begin))) append(begin, first);
    while (!(sub(first, sub(end, quadrant)) > tolerance)) {
        const double next = add(first, quadrant);
        append(first, next);
        first = next;
    }
    if (!(std::abs(sub(last, end)) <= tolerance)) append(last, end);
    return result;
}
}
