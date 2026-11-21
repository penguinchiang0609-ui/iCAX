#include "ordinary_runtime.hpp"
#include <iostream>

namespace {
size_t checks = 0, cases = 0, placements = 0;
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
study::Input makeInput(size_t trial) {
    study::Input input;
    input.width = 15.; input.height = 12.; input.pitch = trial % 3 == 0 ? .4 : trial % 3 == 1 ? .7 : .25;
    input.randomSeed = uint32_t(trial + 3);
    study::PartInput a;
    a.polygon = {{0, 0}, {3, 0}, {3, 2}, {0, 2}};
    a.quantity = 3;
    a.poses = {{0, false}, {90, false}, {0, true}, {90, true}};
    study::PartInput b;
    b.polygon = trial % 2 ? study::Polygon{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}}
                          : study::Polygon{{0, 0}, {2, 0}, {2, 4}, {0, 4}};
    b.quantity = 3;
    b.poses = {{0, false}, {90, false}, {180, false}, {270, false}};
    if (trial % 4 == 0) {
        for (auto &point : a.polygon) { point.x += 6.; point.y -= 3.; }
        a.pivot = {7., -2.};
        for (auto &point : b.polygon) { point.x -= 4.; point.y += 5.; }
        b.pivot = {-3., 7.};
    }
    input.parts = {a, b};
    return input;
}
void runCase(size_t trial, int strategy) {
    using namespace study;
    auto input = makeInput(trial);
    Budget budget(15.);
    OrdinaryState state;
    OrdinaryControl initialize;
    initialize.placementLimit = trial % 2 ? 2 : 0;
    for (const auto &part : input.parts) initialize.values.push_back(area(part.polygon));
    ordinaryRun(input, budget, state, &initialize);
    const auto initialCount = state.output.placements.size();
    ReverseTrace trace;
    Output output;
    if (strategy == 1) {
        recovered::OffsetFactoryInput settings;
        settings.count = trial % 3 == 0 ? 1 : 10;
        settings.dynamicEvaluation = trial % 2 != 0;
        output = ordinaryReverseOffset(input, settings, budget, &state, &trace);
    } else if (strategy == 2) {
        MultiProgram settings;
        const auto context = ordinaryContext(input);
        settings.contextBatch238 = std::max(context.batch238, 1);
        settings.contextField218 = context.width218; settings.contextField220 = context.height220;
        for (const auto &part : input.parts) settings.targetField14b8 += part.quantity * area(part.polygon);
        output = ordinaryReverseMulti(input, settings, budget, &state, &trace);
    } else if (strategy == 3) {
        recovered::ColumnConfig settings;
        settings.tries = 5;
        output = ordinaryReverseColumn(input, settings, budget, &state, &trace);
    } else {
        ReverseRuntime runtime;
        runtime.configuredSeed = 27.; runtime.salt = input.randomSeed;
        runtime.trial = int32_t(trial + 23); runtime.bound = 12000000;
        runtime.shake = .25; runtime.lineage = {{3, 19, 2}}; runtime.fraction = .625;
        runtime.lineageFields = {17, 19, 2, 0, 0};
        output = ordinaryReverse(input, [](const Input &reversed) {
            OrdinaryControl control;
            for (const auto &part : reversed.parts) control.values.push_back(area(part.polygon));
            return ReverseRunner([control](const Input &active, Budget &time, OrdinaryState &target) {
                return ordinaryRun(active, time, target, &control);
            });
        }, budget, &state, &trace, &runtime);
        check(trace.salts == std::array<uint32_t, 2>{runtime.salt, runtime.salt} &&
              trace.nestingTrials == std::array<int32_t, 2>{runtime.trial, runtime.trial},
              "Reverse retains the true salt/trial independently of configured seed");
        auto random = recovered::Random(input.randomSeed);
        recovered::resetTrialRandom(random, runtime.configuredSeed, runtime.salt);
        const auto &metadata = state.placementHost->nesting;
        check(metadata.random.words == random.words, "Reverse writes back its reset normal random state");
        check(metadata.seed13b8 == runtime.salt && metadata.trial13bc == runtime.trial &&
              metadata.bound14a8 == runtime.bound, "Reverse salt, trial and bound writeback");
        check(metadata.lineage14f8 == runtime.lineage && metadata.fraction1510 == runtime.fraction &&
              metadata.source1518 == 17 && metadata.sourceTrial151c == 19 && metadata.depth1520 == 2,
              "Reverse preserves actual normal lineage and warm fraction");
    }
    check(output.placements.size() == 6, "Reverse fills the actual two-type order");
    check(valid_layout(input, output), "Reverse analytic material, orientation and sheet validation");
    check(trace.factories == 1 && trace.trials == 1, "Reverse creates and runs its actual child once");
    check(trace.transferred == initialCount + output.placements.size(), "Reverse transfers warm prefix and complete child result");
    check(state.catalog && size_t(std::count_if(state.catalog->instances.begin(), state.catalog->instances.end(),
        [](const auto &instance) { return instance.placed; })) == output.placements.size(), "Reverse state and catalog agree");
    check(state.placementHost && state.placementHost->nesting.placements.size() == output.placements.size(),
          "Reverse placement lifecycle remains coherent");
    placements += output.placements.size(); ++cases;
}
}
int main() {
    try {
        for (size_t trial = 0; trial < 12; ++trial)
            for (int strategy = 0; strategy < 4; ++strategy) runCase(trial, strategy);
        study::Budget emptyBudget(1.);
        const auto empty = study::ordinaryReverseOffset({}, {}, emptyBudget);
        check(empty.placements.empty(), "Empty reverse layout");
        std::cout << "Reverse actual child, warm transfer, phase, mirror and pivot: " << cases << " cases, "
                  << placements << " placements, " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
