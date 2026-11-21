#include "ordinary_runtime.hpp"
#include "recovered_reverse.hpp"
#include "ordinary_pair_map.hpp"
#include <bit>

namespace study {
recovered::OrdinaryContextMeasures ordinaryContext(const Input &input) {
    std::vector<recovered::ContextPartMeasure> parts;
    int32_t population = 0;
    double totalArea = 0.;
    for (const auto &part : input.parts) {
        const auto box = bounds(part.polygon);
        const auto material = material_area(part);
        parts.push_back({box.x1 - box.x0, box.y1 - box.y0, material, part.quantity, false});
        population += part.quantity;
        totalArea += material * part.quantity;
    }
    return recovered::ordinaryContextMeasures(parts, population, totalArea, input.width, input.height);
}
namespace {
recovered::PlacementOrientation orientation(Pose pose) {
    return recovered::quantizeSdkOrientation({pose.flip ? 1 : 0, pose.angle, pose.angle}, true);
}
Pose pose(recovered::PlacementOrientation orientation) {
    const auto angle = orientation.angle < 0 ? -int64_t(orientation.angle) : int64_t(orientation.angle);
    return {double(angle == 36000000 ? 0 : angle) / 100000., orientation.angle < 0};
}
class OrdinaryReverseHost final : public recovered::ReverseHost {
    const Input &normalInput;
    Input reverseInput;
    const ReverseRunnerFactory &factory;
    Budget &budget;
    ReverseTrace &trace;
    double configuredSeed = 0;
    OrdinaryState &normalState;
    OrdinaryState reverseState;
    std::array<recovered::ReverseNesting, 2> states;
    std::optional<ReverseRunner> child;
    const Input &input(bool reversed) const { return reversed ? reverseInput : normalInput; }
    OrdinaryState &layout(bool reversed) { return reversed ? reverseState : normalState; }
    recovered::ReversePlacement describe(bool reversed, Placement placement) {
        const auto &part = input(reversed).parts.at(placement.group);
        auto value = part.poses.at(placement.poseIndex);
        if (std::isfinite(placement.solvedAngle)) value.angle = placement.solvedAngle;
        recovered::ReversePlacement result;
        result.placement = placement;
        result.orientation = orientation(value);
        result.x = placement.x;
        result.y = int32_t(placement.y);
        result.variant = placement.poseIndex;
        result.instance = placement.ordinal;
        return result;
    }
    void sync(bool reversed) {
        auto &state = side(reversed);
        std::map<std::pair<size_t, size_t>, std::map<std::string, std::string>> attributes;
        for (const auto &placement : state.placements)
            attributes[{placement.placement.group, placement.placement.ordinal}] = placement.attributes;
        state.placements.clear(); state.maximumRight = 0;
        for (const auto &placement : layout(reversed).output.placements) {
            auto value = describe(reversed, placement);
            value.attributes = attributes[{placement.group, placement.ordinal}];
            state.placements.push_back(std::move(value));
            const auto &part = input(reversed).parts.at(placement.group);
            const auto box = bounds(transform(part.polygon, part.pivot, part.poses.at(placement.poseIndex)));
            state.maximumRight = std::max(state.maximumRight, placement.x + rounded(box.x1 * 1e6));
        }
        state.count = int32_t(state.placements.size());
    }
public:
    OrdinaryReverseHost(const Input &source, const ReverseRunnerFactory &create, Budget &time,
                        OrdinaryState &state, ReverseTrace &record, const ReverseRuntime *runtime)
        : normalInput(source), reverseInput(source), factory(create), budget(time), trace(record), normalState(state) {
        require(bool(factory), PN2S_INVALID, "Reverse requires its actual child factory");
        require(ordinaryPreparedDomain(source), PN2S_UNSUPPORTED,
                "Recovered Reverse host requires finite grid-aligned single rings and zero spacing");
        reverseState.mapEligible=normalState.mapEligible;
        for (auto &part : reverseInput.parts)
            for (auto &value : part.poses) value = pose(recovered::composeOrientation(orientation(value), 18000000, true));
        for (bool reversed : {false, true}) {
            OrdinaryControl initialize; initialize.placementLimit = 0;
            for (const auto &part : input(reversed).parts) initialize.values.push_back(material_area(part));
            ordinaryRun(input(reversed), budget, layout(reversed), &initialize);
            side(reversed).seed = runtime ? runtime->salt : source.randomSeed;
            side(reversed).random = recovered::Random(source.randomSeed);
            sync(reversed);
        }
        if (runtime) {
            configuredSeed = runtime->configuredSeed;
            reverseInput.randomSeed = uint32_t(recovered::truncateInt32(configuredSeed)) + runtime->salt;
            auto &normal = side(false);
            normal.trial = runtime->trial; normal.bound = runtime->bound; normal.shake = runtime->shake;
            normal.lineage = runtime->lineage; normal.fraction = runtime->fraction;
            normal.lineageFields = runtime->lineageFields;
        }
    }
    recovered::ReverseNesting &side(bool reversed) override { return states[reversed ? 1 : 0]; }
    double configuredRandomSeed() override { return configuredSeed; }
    void reset(bool reversed) override {
        ordinaryRestore(layout(reversed), 0);
        auto &state = side(reversed);
        state.placements.clear(); state.count = 0; state.maximumRight = 0;
        state.lineage.clear(); state.fraction = 0.; state.lineageFields = {};
    }
    bool childExists() override { return child.has_value(); }
    void createChild() override {
        child = factory(reverseInput);
        require(bool(*child), PN2S_INVALID, "Reverse child factory returned no runner");
        ++trace.factories;
    }
    void runChild() override {
        ++trace.trials;
        (*child)(reverseInput, budget, reverseState);
        sync(true);
    }
    recovered::ReversePlacement clone(const recovered::ReversePlacement &source, bool toReversed, int64_t pivot) override {
        auto result = source;
        const auto &part = input(toReversed).parts.at(source.placement.group);
        const auto target = part.poses.at(source.placement.poseIndex);
        const auto actual = orientation(target);
        const auto requested = recovered::composeOrientation(source.orientation, 18000000, true);
        const auto box = bounds(part.polygon);
        const auto offset = recovered::reverseVariantOffset(
            {box.x0 - part.pivot.x, box.y0 - part.pivot.y, box.x1 - part.pivot.x, box.y1 - part.pivot.y}, requested, actual);
        const auto targetBox = bounds(transform(part.polygon, part.pivot, target));
        const auto coordinates = recovered::reversePlacementCoordinates(source.orientation, source.x, source.y,
            pivot, offset, rounded(targetBox.x0 * 1e6), layout(toReversed).output.pitchX, 0);
        result.orientation = actual; result.x = coordinates.x; result.y = coordinates.y;
        result.unaligned = coordinates.unaligned;
        result.placement.x = result.x; result.placement.y = result.y;
        result.placement.exactX = result.placement.exactY = result.placement.solvedAngle = NAN;
        result.attributes.clear();
        ++trace.transferred;
        return result;
    }
    void commit(bool reversed, const recovered::ReversePlacement &placement) override {
        ordinaryCommit(layout(reversed), placement.placement);
        side(reversed).placements.push_back(placement);
        ++side(reversed).count;
        const auto &part = input(reversed).parts.at(placement.placement.group);
        const auto box = bounds(transform(part.polygon, part.pivot, part.poses.at(placement.placement.poseIndex)));
        side(reversed).maximumRight = std::max(side(reversed).maximumRight, placement.x + rounded(box.x1 * 1e6));
    }
};
}
Output ordinaryReverse(const Input &input, const ReverseRunnerFactory &factory, Budget &budget,
                       OrdinaryState *existing, ReverseTrace *trace, const ReverseRuntime *runtime) {
    OrdinaryState local;
    auto &state = existing ? *existing : local;
    if (std::none_of(input.parts.begin(), input.parts.end(), [](const auto &part) { return part.quantity > 0; }))
        return state.output;
    const auto baseline = state.output;
    ReverseTrace record;
    try {
        auto &activeTrace = trace ? *trace : record;
        OrdinaryReverseHost host(input, factory, budget, state, activeTrace, runtime);
        recovered::runReverse(host);
        for (bool reversed : {false, true}) {
            activeTrace.salts[reversed ? 1 : 0] = host.side(reversed).seed;
            activeTrace.nestingTrials[reversed ? 1 : 0] = host.side(reversed).trial;
        }
        if (state.placementHost) {
            auto &normal = state.placementHost->nesting;
            const auto &restored = host.side(false);
            normal.random = restored.random;
            normal.seed13b8 = restored.seed; normal.trial13bc = restored.trial;
            normal.bound14a8 = restored.bound; normal.shake14e8 = restored.shake;
            normal.lineage14f8 = restored.lineage; normal.fraction1510 = restored.fraction;
            normal.source1518 = std::bit_cast<int32_t>(restored.lineageFields[0]);
            normal.sourceTrial151c = std::bit_cast<int32_t>(restored.lineageFields[1]);
            normal.depth1520 = std::bit_cast<int32_t>(restored.lineageFields[2]);
        }
    } catch (...) {
        if (state.catalog && state.placementHost) {
            ordinaryRestore(state, 0);
            for (const auto &placement : baseline.placements) ordinaryCommit(state, placement);
            state.output.pitchX = baseline.pitchX;
        }
        throw;
    }
    return state.output;
}
Output ordinaryReverseOffset(const Input &input, recovered::OffsetFactoryInput settings, Budget &budget,
                             OrdinaryState *existing, ReverseTrace *trace) {
    return ordinaryReverse(input, [settings](const Input &reversed) {
        const auto program = offsetProgram(reversed, settings);
        return ReverseRunner([program](const Input &active, Budget &time, OrdinaryState &state) {
            return ordinaryOffsetContinue(active, program, time, state);
        });
    }, budget, existing, trace);
}
Output ordinaryReverseMulti(const Input &input, const MultiProgram &settings, Budget &budget,
                            OrdinaryState *existing, ReverseTrace *trace) {
    return ordinaryReverse(input, [settings](const Input &reversed) {
        const auto program = multiProgram(reversed, settings);
        return ReverseRunner([program](const Input &active, Budget &time, OrdinaryState &state) {
            return ordinaryBtContinue(active, program, time, state);
        });
    }, budget, existing, trace);
}
Output ordinaryReverseColumn(const Input &input, const recovered::ColumnConfig &settings, Budget &budget,
                             OrdinaryState *existing, ReverseTrace *trace) {
    return ordinaryReverse(input, [settings](const Input &reversed) {
        const auto program = columnProgram(reversed, settings);
        return ReverseRunner([program](const Input &active, Budget &time, OrdinaryState &state) {
            return ordinaryColumnContinue(active, program, time, state);
        });
    }, budget, existing, trace);
}
}
