#include "ordinary_chris.hpp"
#include <iostream>

namespace {
size_t checks = 0, cases = 0, placements = 0, pairs = 0;
void check(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
study::Input inputFor(size_t trial) {
    study::Input input;
    input.width = 15; input.height = 12; input.pitch = std::array<double, 3>{.4, .7, .25}[trial % 3];
    input.randomSeed = uint32_t(17 + trial);
    study::PartInput first, second;
    first.polygon = {{0, 0}, {3, 0}, {3, 2}, {0, 2}}; first.quantity = 3;
    first.poses = {{0, false}, {90, false}, {0, true}, {90, true}};
    second.polygon = trial % 2 ? study::Polygon{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}}
                               : study::Polygon{{0, 0}, {2, 0}, {2, 4}, {0, 4}};
    second.quantity = 3; second.poses = {{0, false}, {90, false}, {180, false}, {270, false}};
    if (trial % 4 == 0) {
        for (auto &point : first.polygon) { point.x += 6; point.y -= 3; }
        first.pivot = {7, -2};
        for (auto &point : second.polygon) { point.x -= 4; point.y += 5; }
        second.pivot = {-3, 7};
    }
    input.parts = {first, second}; return input;
}
void verifyState(const study::Input &input, const study::OrdinaryState &state) {
    check(state.output.placements.size() == 6, "Chris5 fills the actual two-type production order");
    check(study::valid_layout(input, state.output), "Chris5 actual layout is inside the sheet and material-disjoint");
    check(state.catalog && size_t(std::count_if(state.catalog->instances.begin(), state.catalog->instances.end(),
        [](const auto &instance) { return instance.placed; })) == state.output.placements.size(), "Chris5 catalog quantity conservation");
    check(state.placementHost && state.placementHost->nesting.placements.size() == state.output.placements.size(),
          "Chris5 actual placement lifecycle remains coherent");
}
}
int main() {
    using namespace study;
    try {
        for (size_t trial = 0; trial < 10; ++trial) {
            const auto input = inputFor(trial);
            ChrisProgram settings; settings.common.contextBatch238 = 1 + int32_t(trial % 2);
            settings.common.currentReference = recovered::ReferenceKind::Wide;
            recovered::ChrisFactoryResult descriptor;
            const auto program = chrisProgram(input, settings, &descriptor);
            check(!program.direction10 && !program.direction14 && !program.direction18 && program.extremal1c &&
                  program.extremal1c->count1c == 15 && descriptor.count10 == 0 && descriptor.flag14 == 0 && descriptor.limit18 == 0,
                  "Chris5 executes only its actual fifteen-pair Extremal1c program");
            check(program.reference == recovered::ReferenceKind::Normal && !descriptor.updateCurrentReference,
                  "Chris5 fixes the normal reference independently of Multi's current reference");
            for (bool reversed : {false, true}) {
                Budget budget(20.); OrdinaryState state; OrdinaryControl warm;
                warm.placementLimit = trial % 2 ? 2 : 0;
                for (const auto &part : input.parts) warm.values.push_back(area(part.polygon));
                ordinaryRun(input, budget, state, &warm);
                const auto initialCount = state.output.placements.size();
                if (reversed) {
                    ReverseTrace trace;
                    auto result = ordinaryReverseChris(input, settings, budget, &state, &trace);
                    check(result.placements.size() == 6 && trace.factories == 1 && trace.trials == 1 &&
                          trace.transferred == initialCount + result.placements.size(),
                          "ReverseChris5 creates the actual Chris child and transfers both layouts");
                } else {
                    BtTrace trace;
                    ordinaryChrisContinue(input, settings, budget, state, &trace);
                    check(trace.generatedPairs == trace.rounds * 15 && trace.perturbedStages == trace.rounds * 14 && trace.rounds > 0,
                          "Chris5 actual BT trials generate all fifteen original Noise/Uniform candidate pairs");
                    check(trace.dynamicEvaluations > 0 && trace.trials > 0 && trace.committed > 0,
                          "Chris5 consumes dynamic Uniform evaluations and commits actual recovered BT selections");
                    pairs += trace.generatedPairs;
                }
                verifyState(input, state); placements += state.output.placements.size(); ++cases;
            }
        }
        Budget emptyBudget(1.);
        check(ordinaryChris({}, {}, emptyBudget).placements.empty(), "Empty Chris5 input");
        check(ordinaryReverseChris({}, {}, emptyBudget).placements.empty(), "Empty ReverseChris5 input");
        std::cout << "{\"pass\":true,\"cases\":" << cases << ",\"placements\":" << placements
                  << ",\"generatedPairs\":" << pairs << ",\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
