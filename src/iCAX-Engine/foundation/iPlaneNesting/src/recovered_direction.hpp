#pragma once
#include "recovered_offset.hpp"
#include <span>
namespace study::recovered {
// Context field names remain explicit until their owning producers are recovered.
std::vector<Point> baseDirections(Random &, double contextField218, double contextField220, int randomCount);
std::vector<Point> randomDirections(Random &, double contextField218, double contextField220);
struct DirectionPair { std::vector<Point> directions; int batch = 1, tag = 0; std::vector<size_t> keys; };
std::vector<DirectionPair> directionGenerator14(Random &, double contextField218, double contextField220, int batch);
struct Direction10Pair { DirectionPair candidate; Complexity complexity; std::vector<double> valuesA; };
// 4401d0 + 3660f0 fixed-exponent branch; B keeps the incoming Value object.
std::vector<Direction10Pair> directionGenerator10(Random &,double field218,double field220,int batch,int count10,
    double alpha50,double gamma58,double perimeterProbability,double perimeterScale,
    const std::vector<ValuePart> &,double totalArea);
struct DirectionGroup { size_t key; int32_t quantity, placed; bool optional = false; };
std::vector<double> noiseValues(Random &, std::span<const ValuePart>, std::span<const double>, double totalArea, double amplitude);
double uniformValue(Random &, double base, double low = .89, double high = 1.);
struct Extremal1cPair {
    int batch=1, tag=4, extremaA=3, extremaB=3;
    int64_t front=0;
    bool uniformA=false, inspectProcessesA=false, inspectProcessesB=false;
    std::vector<double> noise; // Constructed even for pair zero, which uses incoming values.
};
// 441100, 367070, 366670, 44bbc0 (empty process-list branch).
std::vector<Extremal1cPair> extremalGenerator1c(Random &, int batch, int count, int64_t front,
    bool forceProcessInspection, std::span<const ValuePart>, std::span<const double>, double totalArea);
std::vector<int> btGeneratorOrder(int count10,int flag14,int limit18,int count1c);
int64_t fixedFrontStripEnd(int64_t front,int64_t minX,int64_t maxX,int64_t pitch);
// 441860: draw all 11 directions first; append before testing the signed limit.
std::vector<DirectionPair> directionGenerator18(Random &, double contextField218, double contextField220,
                                              int32_t batch, int32_t limit18, std::span<const DirectionGroup>);
struct DirectionScan {
    std::optional<std::array<int64_t,2>> point;
    std::vector<size_t> visited; // Optional original-instruction differential trace.
};
// Unwrapped map path: 3da160 -> 3dad90/3db240. Bound units are integer Y ticks.
DirectionScan scanDirection(const Rows &, size_t first, size_t end, double xStep,
                            double dy, int64_t boundY, bool lower, bool trace = false);
std::vector<std::array<int64_t,2>> directionPoints(const Rows &, int64_t pitchX,
                                                std::span<const Point>, int64_t lowerY, int64_t upperY);
}
