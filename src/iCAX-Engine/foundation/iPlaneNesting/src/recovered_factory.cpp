#include "recovered_factory.hpp"
#include "recovered_bt.hpp"
#include <bit>

namespace study::recovered {
namespace {
// MSVC may commute an inlined sum even with /fp:precise. Keeping the
// accumulator in the left argument preserves the original ADD(S)D NaN source.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
double columnAreaAdd(double accumulated,double term){return accumulated+term;}
}
std::optional<StrategyManager> strategyManager(int32_t baseId, bool reversed,
    uintptr_t reverseSource, const ManagerFactoryConfig &config) {
    if (baseId < 1 || baseId > 10) return std::nullopt;
    StrategyManager manager;
    manager.baseId = baseId;
    manager.reversed = reversed;
    manager.reverseSource = reversed ? reverseSource : 0;
    manager.candidateLimit = truncateInt32(config.candidateLimit1c0);
    manager.trials.count60 = std::bit_cast<int32_t>(
        uint32_t(truncateInt32(config.initialTrialCount7c0)) - 1U);
    if (baseId == 1) manager.offsetTryCount = truncateInt32(config.offsetTryCount278);
    return manager;
}
int32_t strategyManagerId(const StrategyManager &manager) {
    return manager.baseId + (manager.reversed ? 10 : 0);
}
int32_t strategyManagerCalibration(const StrategyManager &manager, const ManagerFactoryConfig &config) {
    return strategyCalibration(manager.baseId, manager.offsetTryCount, config.columnTryCount2b0);
}
bool reverseStrategyEligible(double mode, int32_t population88, int32_t problemScale) {
    const int32_t selected = truncateInt32(mode);
    if (selected == 0) return false;
    if (selected == 2) return true;
    const int32_t population = std::bit_cast<int32_t>(uint32_t(population88) * uint32_t(problemScale));
    return population <= 5000;
}
bool strategyManagerApplicable(StrategyManager &manager, FactoryApplicabilityContext &context,
                               const ManagerFactoryConfig &config) {
    if (manager.reversed) {
        if (manager.baseId == 7 || manager.baseId == 9) return false;
        if (!reverseStrategyEligible(config.reverseProblem1d0, context.population88, context.mapInput.problemScale)) return false;
    }
    switch (manager.baseId) {
    case 1: case 4: case 5: case 6: return true;
    case 2:
        return mapEligible(context.map, context.mapInput) &&
            columnEligible(true, context.population88, context.mapInput.problemScale, context.specialSheet);
    case 3: return mapEligible(context.map, context.mapInput);
    case 7: return escherEligible(manager.escherEligibility, context.input);
    case 8: return rectangleEligible(context.input);
    case 9: return multiheadsEligible(context.input);
    case 10: return unplacedEligible(context.unplacedModules);
    default: return false;
    }
}
namespace {
void appendManager(FactoryPreset &output, StrategyManager manager, double weight,
                   const ManagerFactoryConfig &config) {
    if (weight == 0.) return;
    const auto id = strategyManagerId(manager);
    addPreset(output.preset, {id, strategyManagerCalibration(manager, config), weight, manager.trials.budget58});
    output.managers[id] = std::move(manager);
}
}
void appendFactoryPreset(FactoryPreset &output, uint64_t encoded, bool reversed,
    uintptr_t reverseSource, const ManagerFactoryConfig &config, const ManagerApplicability &applicable) {
    int32_t id = 1;
    while (encoded != 0) {
        const int32_t weight = int32_t(encoded % 10);
        encoded /= 10;
        if (weight != 0) {
            auto manager = strategyManager(id, reversed, reverseSource, config);
            if (!manager) throw Failure(PN2S_UNSUPPORTED, "Decimal preset references an unknown manager ID");
            if (!applicable) throw Failure(PN2S_INVALID, "Preset manager applicability is required");
            if (applicable(*manager)) appendManager(output, std::move(*manager), double(weight), config);
        }
        ++id;
    }
}
FactoryPreset initialFactoryPreset(uint64_t direct, uint64_t reverse, uintptr_t reverseSource,
    const ManagerFactoryConfig &config, const ManagerApplicability &applicable) {
    FactoryPreset output;
    appendFactoryPreset(output, direct, false, 0, config, applicable);
    if (reverse > 0 && reverseSource != 0)
        appendFactoryPreset(output, reverse, true, reverseSource, config, applicable);
    return output;
}
FactoryPreset selectedFactoryPreset(std::span<const StrategyResult> pool, int32_t trials,
    double config198, double config190, uint64_t fastPreset, uintptr_t reverseSource,
    const ManagerFactoryConfig &config, const ManagerApplicability &applicable) {
    const auto selected = selectResultWeights(pool, trials, config198, config190);
    FactoryPreset output;
    for (const auto [id, weight] : selected.weights) {
        auto manager = strategyManager(id < 11 ? id : id - 10, id >= 11, reverseSource, config);
        if (!manager) throw Failure(PN2S_UNSUPPORTED, "Selected preset references an unknown manager ID");
        appendManager(output, std::move(*manager), weight, config);
    }
    if (output.preset.entries.empty())
        appendFactoryPreset(output, fastPreset, false, 0, config, applicable);
    return output;
}
OffsetCandidaterPlan offsetCandidaterPlan(Random &random, const OffsetCandidaterInput &input) {
    OffsetCandidaterPlan result;
    result.maximumForwardStrips = extremalForwardLimit(input.maximumForwardStrips280, input.stripsPerPart9d8);
    bool rightToLeft = false;
    if (truncateInt32(input.enableRightToLeft) != 0 && !input.nestingFlag14b0) rightToLeft = random.unit() < .5;
    if (input.priorityGroups > 1) rightToLeft = false;
    if (input.sparseLayout || input.specialSheet) rightToLeft = false;
    result.field92 = !(input.sparseLayout && !input.specialSheet);
    result.rightToLeft = rightToLeft;
    result.field93 = rightToLeft;
    return result;
}
int32_t extremalForwardLimit(double maximum280, double stripsPerPart9d8) {
    const int32_t doubled = std::bit_cast<int32_t>(uint32_t(truncateInt32(stripsPerPart9d8)) * 2U);
    return std::min(doubled, truncateInt32(maximum280));
}
bool offsetSparseLayout(const OffsetSparseInput &input) {
    int32_t count = 0;
    double area = 0;
    for (const auto &part : input.parts) {
        const int32_t remaining = std::bit_cast<int32_t>(
            uint32_t(part.optional ? 0 : part.required) - uint32_t(part.placed));
        count = std::bit_cast<int32_t>(uint32_t(count) + uint32_t(remaining));
        area = area + double(remaining) * (part.optional ? 0. : part.area);
    }
    count = std::bit_cast<int32_t>(uint32_t(input.placementCount) + uint32_t(count));
    area = area + input.placedArea;
    const double threshold = input.sheetHeight * input.sheetWidth * .9;
    const double width = (input.sheetWidth - input.margins[0]) - input.margins[1];
    const double height = (input.sheetHeight - input.margins[2]) - input.margins[3];
    const double span = width < height ? width : height;
    if (count < 8 && threshold > area) return true;
    if (span * span * .5 > area && count < 75) return true;
    return input.specialSheet && threshold > area;
}
DecreasingRandomEvaluation decreasingRandomEvaluation(double amplitude, int32_t totalCount) {
    DecreasingRandomEvaluation result;
    result.amplitude = amplitude;
    result.totalCount = totalCount;
    decreasingRandomSetPlaced(result, 0);
    return result;
}
void decreasingRandomSetPlaced(DecreasingRandomEvaluation &state, int32_t placed) {
    const double fraction = double(placed) / double(state.totalCount);
    const double limited = fraction < 1. ? fraction : 1.;
    state.minimum = 1. - (1. - limited) * state.amplitude;
    state.inverse = 1. / state.minimum;
    state.normalization = std::pow(state.inverse, state.inverse);
    state.range = state.inverse - state.minimum;
}
double decreasingRandomValue(const DecreasingRandomEvaluation &state, Random &random,
                             const std::function<double()> &baseValue) {
    const double extent = state.randomHigh - state.randomLow;
    const double draw = random.unit();
    const double transformed = ((draw * extent + state.randomLow) * state.range) + state.minimum;
    const double value = baseValue();
    return (std::pow(transformed, state.inverse) / state.normalization) * value;
}
OffsetValuePlan offsetValue(Random &random, const OffsetFactoryInput &input,
                            const std::vector<ValuePart> &parts, double totalArea) {
    OffsetValuePlan result;
    result.reference = selectReference(random.unit() + 0.);
    result.complexity = drawComplexity(random, input.complexity);
    result.values = values(parts, totalArea, result.complexity);
    if (input.dynamicEvaluation) {
        const int32_t population = std::max(input.population88, 1);
        const double drawn = (random.unit() * .25 + 0.) * (50. / double(population));
        const double amplitude = .9 <= drawn ? .9 : drawn;
        result.decreasing = decreasingRandomEvaluation(amplitude, input.population88);
    }
    return result;
}
std::vector<OffsetChildPlan> offsetFactory(Random &random, const OffsetFactoryInput &input,
                                         const std::vector<ValuePart> &parts, double totalArea) {
    require(input.count >= 0, PN2S_RESOURCE_LIMIT, "Negative Offset factory count exceeds original allocation range");
    std::vector<OffsetValuePlan> evaluations;
    evaluations.reserve(size_t(input.count));
    for (int32_t index = 0; index < input.count; ++index) {
        evaluations.push_back(offsetValue(random,input,parts,totalArea));
    }
    return offsetChildren(random,input,evaluations);
}
std::vector<OffsetChildPlan> offsetChildren(Random &random,const OffsetFactoryInput &input,std::span<const OffsetValuePlan> evaluations) {
    std::vector<OffsetChildPlan> result;
    for(const auto &evaluation:evaluations){OffsetChildPlan child;child.evaluation=evaluation;result.push_back(std::move(child));}
    for (auto &child : result) {
        child.candidater = offsetCandidaterPlan(random, input.candidater);
        if (child.candidater.rightToLeft && child.evaluation.reference == ReferenceKind::Normal)
            child.evaluationReferenceScale = input.reverseReferenceScale290;
        child.inspectProcesses = input.forceProcessInspection || random.unit() < .33;
    }
    return result;
}
bool finalizeLess(const FinalizeKey &a, const FinalizeKey &b) {
    if (a.complete != b.complete) return a.complete > b.complete;
    if (a.complete == 0 || b.complete == 0) {
        if (metricBetter(a.value, b.value)) return true;
        if (metricBetter(b.value, a.value)) return false;
    }
    return a.correctedLength < b.correctedLength;
}
int runFinalize(FinalizeHost &host) {
    const size_t count = host.childCount();
    if (count == 1) return host.runChild(0);
    host.checkpoint();
    struct Candidate { FinalizeKey key; std::vector<Placement> placements; };
    CandidatePool<Candidate> pool(1, [](const Candidate &a, const Candidate &b) { return finalizeLess(a.key, b.key); });
    try {
        for (size_t child = 0; child < count; ++child) {
            // Ordered comparison deliberately lets NaN remaining time through.
            if (host.remainingTime() <= 0. || host.stopRequested()) break;
            const int reported = host.runChild(child);
            Candidate trial;
            trial.key = host.evaluate(-1234);
            trial.placements = btCapture(host.placements(), reported);
            pool.insert(trial);
            host.restore();
        }
    } catch (...) { host.restore(); throw; }
    if (const auto best = pool.best()) {
        for (const auto &placement : best->placements) host.commit(placement);
        return int(best->placements.size());
    }
    return 0;
}
int32_t runCombined(CombinedHost &host) {
    const uint64_t total=host.totalInstanceCount();
    uint32_t count=uint32_t(host.runChild(0));
    if(!(host.remainingTime()<=0.) && !host.stopRequested()) {
        if(count!=uint32_t(total))count+=uint32_t(host.runChild(1));
    }
    return std::bit_cast<int32_t>(count);
}
ColumnTrialPlan columnTrialPlan(Random &random,double context218,double context220,bool flag119,
                                const ColumnTrialConfig &config) {
    ColumnTrialPlan result;result.mode=random.integer(0,9);
    const double longFraction=random.unit()*(config.longLengthRatio2b8-0.)+0.;
    const double smallFraction=random.unit()*(config.smallLengthRatio2d0-0.)+0.;
    const double superFraction=random.unit()*(config.superLongLengthRatio2c8-0.)+0.;
    result.lengthFraction=longFraction;
    const auto append=[&](double scale,bool down){result.directions.push_back({context220*scale,down?context218*-1.:context218});};
    switch(result.mode) {
    case 0:append(-.5,true);result.lengthFraction=smallFraction;break;
    case 1:append(-.5,false);result.lengthFraction=smallFraction;break;
    case 2:case 3:append(-.2,true);append(-.2,false);result.lengthFraction=smallFraction;break;
    case 4:append(-.01,true);break;
    case 5:append(-.01,false);break;
    case 6:append(.01,true);break;
    case 7:append(.01,false);break;
    case 8:case 9:append(-.01,true);append(-.01,false);break;
    default:throw Failure(PN2S_INTERNAL,"Invalid original Column trial mode");
    }
    if(flag119 && random.unit()<config.superLongOverrideRate2c0)result.lengthFraction=superFraction;
    return result;
}
ColumnPartition columnPartition(std::span<const ColumnPart> parts,double widthThreshold,double areaThreshold) {
    ColumnPartition result;
    for(const auto &part:parts) {
        const bool wide=part.width1b0>widthThreshold;
        const double areaValue=part.optional3c1?0.:part.area58;
        const bool largeArea=areaValue>areaThreshold;
        if(wide && largeArea)result.primary.push_back(part.key);
        else if(wide || largeArea)result.secondary.push_back(part.key);
        else continue;
        const int32_t remaining=std::bit_cast<int32_t>(uint32_t(part.optional3c1?0:part.required254)-uint32_t(part.placed));
        result.remainingArea=columnAreaAdd(result.remainingArea,double(remaining)*areaValue);
    }
    return result;
}
std::vector<size_t> columnSelectGroups(Random &random,std::span<const ColumnPart> parts,std::span<const size_t> input,
                                      uint32_t limit,bool weighted) {
    if(limit==0)return {};
    std::vector<size_t> remaining(input.begin(),input.end());
    if(input.size()<=limit)return remaining;
    std::vector<double> weights;
    for(size_t key:remaining) {
        if(!weighted){weights.push_back(1.);continue;}
        const auto found=std::find_if(parts.begin(),parts.end(),[&](const auto &part){return part.key==key;});
        require(found!=parts.end(),PN2S_INVALID,"Column selection refers to an absent group");
        const int32_t count=std::bit_cast<int32_t>(uint32_t(found->optional3c1?0:found->required254)-uint32_t(found->placed));
        weights.push_back(double(count)*(found->optional3c1?0.:found->area58));
    }
    std::vector<size_t> result;
    for(uint32_t count=0;count<limit;++count) {
        const auto index=aliasDraw(random,weights);
        result.push_back(remaining.at(index));
        remaining.erase(remaining.begin()+index);weights.erase(weights.begin()+index);
    }
    return result;
}
}
