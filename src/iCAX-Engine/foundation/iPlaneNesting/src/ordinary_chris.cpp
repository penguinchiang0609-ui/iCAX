#include "ordinary_chris.hpp"
#include "ordinary_pair_map.hpp"

namespace study {
BtProgram chrisProgram(const Input &input, const ChrisProgram &settings, recovered::ChrisFactoryResult *descriptor) {
    const auto &common = settings.common;
    require(common.contextBatch238 >= 0, PN2S_INVALID, "Chris5 context batch must be nonnegative");
    double totalArea = 0; int32_t population = 0;
    const auto parts = ordinaryValueParts(input, totalArea, population);
    recovered::ChrisFactoryInput factory;
    factory.contextBatch238 = common.contextBatch238; factory.config = common.config;
    factory.currentReference = common.currentReference;
    factory.config700 = settings.config700; factory.config710 = settings.config710; factory.managerId = settings.managerId;
    recovered::Random random=common.initialRandom?*common.initialRandom:recovered::Random(input.randomSeed);
    auto result = recovered::chrisFactory(random, factory, parts, totalArea);
    for (const auto value : result.evaluationValues)
        require(std::isfinite(value) && value > 0, PN2S_INVALID, "Chris5 value factory produced invalid weights");
    BtProgram program;
    program.initialRandom = random; program.evaluationValues = result.evaluationValues; program.reference = result.reference;
    program.dynamicQueue = common.dynamicQueue;
    program.extremal1c = BtProgram::Extremal1c{result.batch, result.count1c, common.fixedFront, common.forceProcessInspection};
    program.targetField14b8 = common.targetField14b8; program.queueAggregate20 = common.queueAggregate20;
    program.maximumLengthX = common.maximumLengthX; program.lengthMode = common.lengthMode;
    program.replayPrediction = common.replayPrediction; program.filterReplayAtFront = common.filterReplayAtFront;
    if (descriptor) *descriptor = std::move(result);
    return program;
}
int ordinaryChrisContinue(const Input &input, const ChrisProgram &settings, Budget &budget,
    OrdinaryState &state, BtTrace *trace) {
    require(ordinaryPreparedDomain(input), PN2S_UNSUPPORTED,
            "Chris5 requires finite grid-aligned single rings and zero spacing");
    if (input.parts.empty()) return 0;
    return ordinaryBtContinue(input, chrisProgram(input, settings), budget, state, trace);
}
Output ordinaryChris(const Input &input, const ChrisProgram &settings, Budget &budget, BtTrace *trace) {
    OrdinaryState state; ordinaryChrisContinue(input, settings, budget, state, trace); return std::move(state.output);
}
Output ordinaryReverseChris(const Input &input, const ChrisProgram &settings, Budget &budget,
    OrdinaryState *existing, ReverseTrace *trace) {
    auto reversed = settings; reversed.managerId = 15;
    return ordinaryReverse(input, [reversed](const Input &source) {
        const auto program = chrisProgram(source, reversed);
        return ReverseRunner([program](const Input &active, Budget &time, OrdinaryState &state) {
            return ordinaryBtContinue(active, program, time, state);
        });
    }, budget, existing, trace);
}
}
