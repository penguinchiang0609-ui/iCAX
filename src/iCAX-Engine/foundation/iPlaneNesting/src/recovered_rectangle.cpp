#include "recovered_rectangle.hpp"
namespace study::recovered {
uint32_t rectangleIterationLimit(size_t groups) {
    require(groups != 0, PN2S_INVALID, "Rectangle requires a participating group");
    return static_cast<uint32_t>(std::max(10., std::trunc(10000. / double(groups))));
}
double rectangleUniform(Random &random, double low, double high) {
    return low == high ? high : random.unit() * (high - low) + low;
}
RectangleNoise rectangleNoise(Random &random, uint32_t iteration, const RectangleConfig &config) {
    if (!iteration) return {config.maximumAlpha * .5, 0};
    const double module = rectangleUniform(random, 0., config.maximumModule);
    return {rectangleUniform(random, 0., config.maximumAlpha), module};
}
RectanglePrices rectanglePrices(Random &random, std::span<const RectanglePriceGroup> groups,
    std::span<const std::vector<RectangleComposition>> modules, double stockVolume,
    double alpha, double groupNoise, double moduleNoise) {
    RectanglePrices result;
    double oldRequired = 0, oldMaximum = 0, newRequired = 0, newMaximum = 0;
    for (const auto &group : groups) {
        oldRequired += double(group.required) * group.weight;
        oldMaximum += double(group.maximum) * group.weight;
    }
    for (const auto &group : groups) {
        double weight = group.weight;
        if (group.referenceSpan) {
            const auto &span = *group.referenceSpan;
            const double ratio = (span[0] * span[1] * span[2]) / stockVolume;
            weight = (std::pow(ratio, alpha) * weight) *
                     rectangleUniform(random, 1. - groupNoise, groupNoise + 1.);
        }
        result.groups.push_back(weight);
        newRequired += double(group.required) * weight;
        newMaximum += double(group.maximum) * weight;
    }
    double multiplier = 1.;
    if (newMaximum > 0.) multiplier = oldMaximum / newMaximum;
    if (newRequired > 0.) multiplier = oldRequired / newRequired;
    for (auto &weight : result.groups) weight = multiplier * weight;
    for (const auto &module : modules) {
        double weight = 0;
        for (const auto &entry : module)
            weight += double(entry.coefficient) * result.groups.at(entry.group);
        result.modules.push_back(rectangleUniform(random, 1. - moduleNoise, moduleNoise + 1.) * weight);
    }
    return result;
}
bool rectangleStop(const RectangleStop &input) {
    return input.callbackResult == 1 ||
           (input.timeBudget != 0. && input.elapsed > input.timeBudget * .95) ||
           (input.limit != 0 && input.iteration >= input.limit);
}
bool rectanglePrefer(const RectangleCandidate &trial, const RectangleCandidate &best, bool mode) {
    if (trial.complete && best.complete) {
        if (trial.stockVolume - best.stockVolume > 1e-9) return false;
        if (best.stockVolume - trial.stockVolume > 1e-9) return true;
    }
    // In nonobjective mode an already-complete incumbent also wins the
    // equal-stock-volume complete/complete case (748015..748030).
    if (!mode && best.complete) return false;
    if (!mode && trial.complete) return true;
    return trial.objective > best.objective;
}
std::optional<RectangleCandidate> rectangleSearch(RectangleHost &host, Random &random,
                                                const RectangleConfig &config, RectangleTrace *record) {
    RectangleTrace trace;
    trace.activeLimit = config.iterationLimit;
    if (config.requestedLimit) trace.activeLimit = std::min(trace.activeLimit, config.requestedLimit);
    const auto stop = [&] {
        return rectangleStop({host.callbackResult(), host.elapsed(), config.timeBudget,
                              trace.iteration, trace.activeLimit});
    };
    std::optional<RectangleCandidate> best;
    while (!stop()) {
        ++trace.mainTrials; trace.targets.push_back(0.);
        auto candidate = host.construct(0., rectangleNoise(random, trace.iteration, config), random);
        ++trace.iteration;
        if (!candidate) continue;
        if (rectanglePrefer(*candidate, best.value_or(RectangleCandidate{}), config.objectiveMode)) best = *candidate;
        if (!candidate->complete || !config.minimumLength) continue;
        // 747990 takes the current trial, including when the previous incumbent
        // won the comparison. Only failures consume the remaining trial limit.
        best = std::move(candidate);
        double target = best->width * .999;
        while (!stop()) {
            ++trace.shorteningTrials; trace.targets.push_back(target);
            auto shortened = host.construct(target, rectangleNoise(random, trace.iteration, config), random);
            if (shortened && shortened->complete) {
                best = std::move(shortened);
                target = best->width * .999;
                ++trace.shorteningSuccesses;
            } else ++trace.iteration;
        }
        host.finalFill(*best); ++trace.finalFills;
        break;
    }
    if (record) *record = std::move(trace);
    return best;
}
}
