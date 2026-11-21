#pragma once
#include "recovered_offset.hpp"
#include <span>
namespace study::recovered {
struct RectangleConfig {
    uint32_t iterationLimit = 5000, requestedLimit = 0;
    double maximumAlpha = .2, maximumModule = .3, timeBudget = 0;
    bool minimumLength = true, objectiveMode = true;
};
uint32_t rectangleIterationLimit(size_t participatingGroups); // 404c55
double rectangleUniform(Random &, double low, double high);  // 753080
struct RectangleNoise { double alpha = 0, module = 0; };
RectangleNoise rectangleNoise(Random &, uint32_t iteration, const RectangleConfig &); // 7461b8
struct RectanglePriceGroup {
    int32_t required = 0, maximum = 0;
    double weight = 0;
    std::optional<std::array<double, 3>> referenceSpan;
};
struct RectangleComposition { size_t group = 0; int32_t coefficient = 1; };
struct RectanglePrices { std::vector<double> groups, modules; };
RectanglePrices rectanglePrices(Random &, std::span<const RectanglePriceGroup>,
    std::span<const std::vector<RectangleComposition>>, double maximumStockVolume,
    double alpha, double groupNoise, double moduleNoise); // 74daa0/74dfb0/74e230
struct RectangleStop {
    int callbackResult = 0;
    double elapsed = 0, timeBudget = 0;
    uint32_t iteration = 0, limit = 0;
};
bool rectangleStop(const RectangleStop &); // 745e10
struct RectangleCandidate {
    bool complete = false;
    double stockVolume = 0, width = 0, objective = 0;
    std::shared_ptr<void> payload;
};
bool rectanglePrefer(const RectangleCandidate &trial, const RectangleCandidate &best,
                     bool objectiveMode); // 747f50
struct RectangleTrace {
    uint32_t iteration = 0, activeLimit = 0;
    size_t mainTrials = 0, shorteningTrials = 0, shorteningSuccesses = 0, finalFills = 0;
    std::vector<double> targets;
};
class RectangleHost {
public:
    virtual ~RectangleHost() = default;
    virtual int callbackResult() { return 0; }
    virtual double elapsed() = 0;
    virtual std::optional<RectangleCandidate> construct(double target, RectangleNoise, Random &) = 0;
    virtual void finalFill(RectangleCandidate &) = 0;
};
std::optional<RectangleCandidate> rectangleSearch(RectangleHost &, Random &, const RectangleConfig &,
                                                RectangleTrace * = nullptr); // 747d00/747990
}
