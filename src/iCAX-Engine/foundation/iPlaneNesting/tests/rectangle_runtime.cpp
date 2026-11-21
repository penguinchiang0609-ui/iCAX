#include "rectangle_runtime.hpp"
#include "ordinary_rectangle_tiling.hpp"
#include <iostream>
#include <random>
using namespace study;
using namespace study::recovered;
namespace {
size_t checks = 0;
void check(bool result, const char *message) { ++checks; if (!result) throw std::runtime_error(message); }
struct ScriptHost final : RectangleHost {
    std::vector<std::optional<RectangleCandidate>> trials;
    std::vector<double> targets;
    size_t next = 0, filled = 0;
    double elapsed() override { return 0; }
    int callbackResult() override { return next >= trials.size() ? 1 : 0; }
    std::optional<RectangleCandidate> construct(double target, RectangleNoise, Random &) override {
        targets.push_back(target); return trials.at(next++);
    }
    void finalFill(RectangleCandidate &) override { ++filled; }
};
PartInput rectangle(double width, double height, int quantity, bool rotate = true) {
    PartInput result; result.polygon = {{0, 0}, {width, 0}, {width, height}, {0, height}};
    result.quantity = quantity; result.poses = {{0, false}};
    if (rotate) result.poses.push_back({90, false});
    return result;
}
}
int main() {
    try {
        check(rectangleIterationLimit(1) == 10000 && rectangleIterationLimit(3) == 3333 &&
              rectangleIterationLimit(2000) == 10, "Original bridge iteration producer");
        check(!rectangleStop({0, 9.5, 10, 2, 3}) && rectangleStop({0, std::nextafter(9.5, 10.), 10, 2, 3}),
              "Original 95 percent stop is strict");
        check(!rectangleStop({2, 0, 0, UINT32_MAX, 0}) && rectangleStop({1, 0, 0, 0, 0}),
              "Original callback accepts exactly one");
        check(rectangleStop({0, 0, 0, 0x80000000, 0x7fffffff}), "Original iteration limit is unsigned");
        RectangleConfig config; config.iterationLimit = 4;
        Random random(71), unchanged = random;
        const auto first = rectangleNoise(random, 0, config);
        check(first.alpha == .1 && first.module == 0 && random.words == unchanged.words, "First trial has no RNG draws");
        check(rectangleUniform(random, 2., 2.) == 2. && random.words == unchanged.words, "Equal uniform bounds consume no RNG");
        ScriptHost scripted;
        scripted.trials = {RectangleCandidate{true, 100, 90, 7}, RectangleCandidate{false, 100, 70, 8},
                           RectangleCandidate{true, 100, 80, 7}, std::nullopt, std::nullopt};
        RectangleTrace trace;
        const auto best = rectangleSearch(scripted, random, config, &trace);
        check(best && best->width == 80 && scripted.filled == 1, "Shortening retains last successful tree");
        check(trace.iteration == 4 && trace.mainTrials == 1 && trace.shorteningTrials == 4 && trace.shorteningSuccesses == 1,
              "Only failed shortening consumes iteration limit");
        check(scripted.targets[1] == 90 * .999 && scripted.targets[2] == scripted.targets[1] &&
              scripted.targets[3] == 80 * .999 && scripted.targets[4] == scripted.targets[3],
              "Failed shortening retries same target and successful shortening follows actual width");
        check(rectanglePrefer({true, 99, 20, 1}, {true, 100, 10, 10}, true), "Complete comparison uses stock volume");
        check(!rectanglePrefer({false, 100, 20, 10}, {false, 100, 10, 10}, true), "Objective ties retain incumbent");
        check(rectanglePrefer({true, 100, 20, 1}, {false, 100, 10, 10}, false), "Nonobjective mode prefers completion");
        check(!rectanglePrefer({true, 100, 20, 10}, {true, 100, 10, 1}, false),
              "Nonobjective mode retains first complete tree at equal stock volume");
        Input input; input.width = 40; input.height = 24;
        input.parts = {rectangle(7, 4, 12), rectangle(3, 9, 8)};
        RectangleProgram settings; settings.config.iterationLimit = 12;
        OrdinaryRectangleTrace actual;
        Output output; Budget budget(10);
        check(ordinaryRectangle(input, budget, output, settings, &actual), "Rectangle real recursive host accepts rectangles");
        check(output.placements.size() == 20 && valid_layout(input, output), "Real recursive tree emits valid full layout");
        check(actual.recursiveTrials > 1 && actual.search.shorteningTrials > 0, "Real recursive solver runs shortening trials");
        check(actual.modulePrices.size() > 1 && actual.modulePrices[0] != actual.modulePrices[1], "Prices perturb actual recursive trials");
        Input oriented; oriented.width = 9; oriented.height = 13;
        auto translated = rectangle(13, 7, 1, false);
        for (auto &point : translated.polygon) { point.x += 20; point.y -= 17; }
        translated.pivot = {26, -14}; translated.poses = {{-110, true, -70}};
        oriented.parts = {translated}; Budget orientedBudget(2); Output orientedOutput;
        check(ordinaryRectangle(oriented, orientedBudget, orientedOutput, settings) &&
              orientedOutput.placements.size() == 1 && orientedOutput.placements[0].solvedAngle == -90. &&
              valid_layout(oriented, orientedOutput), "Actual source pose, mirror and pivot mapping");
        Budget rangeBudget(2); Output rangeOutput;
        check(rectangle_range_nest(oriented, rangeBudget, rangeOutput) && rangeOutput.placements.size() == 1 &&
              rangeOutput.placements[0].solvedAngle == -90. && valid_layout(oriented, rangeOutput),
              "Continuous range public adapter uses recovered Rectangle driver");
        Input partial; partial.width = partial.height = 4;
        partial.parts = {rectangle(3, 3, 2, false)};
        Budget partialBudget(2); Output partialOutput;
        check(ordinaryRectangle(partial, partialBudget, partialOutput, settings) && partialOutput.placements.size() == 1 &&
              valid_layout(partial, partialOutput), "Ordinary Rectangle exposes valid partial tree");
        Budget incompleteBudget(2); Output preserved; preserved.pitchX = 17;
        check(!rectangle_range_nest(partial, incompleteBudget, preserved) && preserved.pitchX == 17 && preserved.placements.empty(),
              "Continuous range adapter preserves complete-only success and failed output");
        Input disallowed = oriented; disallowed.parts[0].poses = {{10, false, 20}};
        Budget disallowedBudget(2); Output disallowedOutput;
        check(!ordinaryRectangle(disallowed, disallowedBudget, disallowedOutput, settings),
              "Rectangle cannot invent quarter-turn poses outside supplied ranges");
        Input alternatives; alternatives.width = alternatives.height = 10;
        alternatives.parts = {rectangle(4, 7, 1)};
        RectangleCoreOptions low, high; low.modulePrices = {1000, 1}; high.modulePrices = {1, 1000};
        Budget lowerTime(2), higherTime(2);
        const auto lower = rectangleCore(alternatives, lowerTime, low), higher = rectangleCore(alternatives, higherTime, high);
        check(lower.output.placements[0].poseIndex == 0 && higher.output.placements[0].poseIndex == 1,
              "Per-module prices reach actual recursive knapsack");
        Input composite; composite.width = 16; composite.height = 10; composite.gap = .5;
        composite.parts = {rectangle(3, 2, 4, false), rectangle(2, 3, 4, false)};
        RectangleTilingModel pair;
        pair.members = {{0, 0, 0, 0, 0}, {1, 0, 0, 3500000, 0}};
        pair.u = {6., 0.}; pair.v = {0., 3.5};
        RectangleProgram compositeProgram; compositeProgram.modules = {pair}; compositeProgram.config.iterationLimit = 3;
        Budget compositeBudget(3); Output compositeOutput;
        check(ordinaryRectangle(composite, compositeBudget, compositeOutput, compositeProgram) &&
              compositeOutput.placements.size() == 8 && valid_layout(composite, compositeOutput),
              "Actual multi-group members consume composed demand and preserve gap");
        Input pinwheel; pinwheel.width = pinwheel.height = 30; pinwheel.parts = {rectangle(3, 5, 20)};
        Budget pinwheelBudget(5); const auto pinwheelModels = ordinaryRectangleTilingModelsForInput(pinwheel, pinwheelBudget);
        check(std::any_of(pinwheelModels.begin(), pinwheelModels.end(), [](const auto &model) { return model.members.size() > 1; }),
              "498580 builds real gammadion prototypes for qualifying rectangle quantities");
        RectangleProgram pinwheelProgram; pinwheelProgram.modules = pinwheelModels; pinwheelProgram.config.iterationLimit = 3;
        Budget pinwheelSolveBudget(5); Output pinwheelOutput;
        check(ordinaryRectangle(pinwheel, pinwheelSolveBudget, pinwheelOutput, pinwheelProgram) &&
              pinwheelOutput.placements.size() == 20 && valid_layout(pinwheel, pinwheelOutput),
              "Gammadion catalog drives actual recursive placement and homogeneous coefficient accounting");
        const auto compositeModel = *std::find_if(pinwheelModels.begin(), pinwheelModels.end(), [](const auto &model) {
            return model.members.size() > 1;
        });
        auto forcedPinwheel = pinwheel; forcedPinwheel.parts[0].quantity = int(compositeModel.members.size() * 2);
        RectangleCoreOptions forcedOptions; forcedOptions.modules = {compositeModel}; Budget forcedBudget(3);
        const auto forcedTree = rectangleCore(forcedPinwheel, forcedBudget, forcedOptions);
        check(forcedTree.complete && forcedTree.output.placements.size() == compositeModel.members.size() * 2 &&
              valid_layout(forcedPinwheel, forcedTree.output),
              "Actual recursive tree expands repeated gammadion members without singleton fallback");
        Input mixed = oriented; mixed.width = mixed.height = 50; mixed.parts[0].quantity = 4;
        PartInput concave; concave.quantity = 3; concave.poses = {{0, false}};
        concave.polygon = {{0, 0}, {5, 0}, {5, 1}, {1, 1}, {1, 5}, {0, 5}};
        mixed.parts.insert(mixed.parts.begin(), concave);
        Budget mixedCatalogBudget(3); auto mixedModels = ordinaryRectangleTilingModelsForInput(mixed, mixedCatalogBudget);
        check(!mixedModels.empty() && std::all_of(mixedModels.begin(), mixedModels.end(), [](const auto &model) {
                  return std::all_of(model.members.begin(), model.members.end(), [](const auto &member) {
                      return member.group == 1 && member.poseIndex == 0 && member.solvedAngle == -90.;
                  }); }), "Mixed order catalog keeps actual source group, pivot, mirror and solved angle");
        RectangleProgram mixedProgram; mixedProgram.modules = mixedModels; mixedProgram.config.iterationLimit = 3;
        Budget mixedSolveBudget(3); Output mixedOutput; OrdinaryRectangleTrace mixedTrace;
        check(ordinaryRectangle(mixed, mixedSolveBudget, mixedOutput, mixedProgram, &mixedTrace) && mixedOutput.placements.size() == 4 &&
              valid_layout(mixed, mixedOutput), "Supplied models accept a mixed order and leave unmatched demand for other strategies");
        check(mixedTrace.participatingGroups == 1 && mixedTrace.search.finalFills == 1 && mixedTrace.search.shorteningTrials > 0,
              "496990 skips groups without tiling models and completion shortens the represented subset");
        RectangleTilingModel limited; limited.maximumX = 3; limited.maximumY = 4; limited.maximumTotal = 10;
        const auto expanded = rectangleExpandModelLimits({limited});
        check(expanded.size() == 2 && expanded[0].maximumX == 2 && expanded[0].maximumY == 4 &&
              expanded[1].maximumX == 3 && expanded[1].maximumY == 2 && !expanded[0].maximumTotal && !expanded[1].maximumTotal,
              "497030 expands total repetition constraint into its two original axis-limited modules");
        std::mt19937 samples(0x747990);
        for (size_t sample = 0; sample < 120; ++sample) {
            Input caseInput; caseInput.width = 30; caseInput.height = 20;
            caseInput.randomSeed = samples(); caseInput.gap = sample % 3 == 0 ? .5 : 0.;
            for (size_t group = 0; group < 1 + sample % 4; ++group)
                caseInput.parts.push_back(rectangle(1 + samples() % 9, 1 + samples() % 8, 1 + samples() % 4, sample % 2));
            Budget caseBudget(5); Output caseOutput; settings.config.iterationLimit = 5;
            check(ordinaryRectangle(caseInput, caseBudget, caseOutput, settings) && valid_layout(caseInput, caseOutput),
                  "Randomized recursive Rectangle geometry, demand and spacing");
        }
        std::cout << "{\"pass\":true,\"checks\":" << checks << ",\"randomizedGeometryCases\":120}\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
