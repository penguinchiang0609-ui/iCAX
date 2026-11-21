#include "ordinary_chris6.hpp"

namespace study {
OffsetProgram chris6Program(const Input &input, const recovered::UserEvaluation &evaluation,
                           recovered::OffsetFactoryInput settings, const recovered::Random *initialRandom) {
    require(evaluation.values.size() == input.parts.size(), PN2S_INVALID,
            "Chris6 requires the problem UserEvaluation for every normalized part");
    std::vector<recovered::OffsetSparsePart> parts;
    for (const auto &part : input.parts)
        parts.push_back({part.quantity, 0, false, material_area(part)});
    recovered::OffsetSparseInput sparse;
    sparse.parts = parts; sparse.sheetWidth = input.width; sparse.sheetHeight = input.height;
    sparse.specialSheet = settings.candidater.specialSheet;
    settings.candidater.sparseLayout = recovered::offsetSparseLayout(sparse);
    OffsetProgram result;
    result.random = initialRandom ? *initialRandom : recovered::Random(input.randomSeed);
    result.reverseReferenceScale = settings.reverseReferenceScale290;
    result.auxiliaryReference = false; // 44f260's param5 = false.
    result.children = recovered::chris6Factory(result.random, evaluation, settings);
    return result;
}
int ordinaryChris6Continue(const Input &input, const OffsetProgram &program, Budget &budget,
                          OrdinaryState &state, OffsetTrace *trace) {
    require(program.children.size() == 1 && !program.auxiliaryReference, PN2S_INVALID,
            "Chris6 supplied-evaluator child plan");
    return ordinaryOffsetContinue(input, program, budget, state, trace);
}
Output ordinaryChris6(const Input &input, const recovered::UserEvaluation &evaluation, Budget &budget,
                      recovered::OffsetFactoryInput settings, OffsetTrace *trace) {
    OrdinaryState state;
    ordinaryChris6Continue(input, chris6Program(input, evaluation, settings), budget, state, trace);
    return std::move(state.output);
}
Output ordinaryReverseChris6(const Input &input, const recovered::UserEvaluation &evaluation, Budget &budget,
                            recovered::OffsetFactoryInput settings, OrdinaryState *existing,
                            ReverseTrace *trace) {
    return ordinaryReverse(input, [evaluation, settings](const Input &reversed) {
        const auto program = chris6Program(reversed, evaluation, settings);
        return ReverseRunner([program](const Input &source, Budget &time, OrdinaryState &state) {
            return ordinaryChris6Continue(source, program, time, state);
        });
    }, budget, existing, trace);
}
}
