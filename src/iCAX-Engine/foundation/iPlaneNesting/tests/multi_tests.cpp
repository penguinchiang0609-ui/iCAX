#include "recovered_multi.hpp"
#include "recovered_multi_modify.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <random>
using namespace study::recovered;
namespace {
size_t checks = 0;
void check(bool passed, const char *message) {
    ++checks;
    if (!passed) throw std::runtime_error(message);
}
bool close(double first, double second) { return std::abs(first - second) <= 1e-7; }
MultiLnsSheet sheet(uint64_t identity, double area, double cost) {
    MultiLnsSheet result;
    result.identity = identity;
    result.requiredAreas = {area};
    result.informerRequiredArea = area;
    result.primaryCost = cost;
    return result;
}
}
int main() {
    try {
        const std::vector<MultiDemand> demand{{5, 1}};
        const std::vector<MultiPattern> patterns{{100, 0, 10, {{0, 2}}}};
        std::vector<MultiStock> stock{{3, 10}};
        auto model = multiMasterModel(stock, demand, patterns);
        auto solution = multiSolveMaster(model, stock, patterns, 10);
        check(solution.status == 0 && close(solution.objective, 25), "Fractional pattern master optimum");
        check(solution.usage.size() == 1 && close(solution.usage[0].quantity, 2.5), "Master retains fractional usage");
        check(close(solution.shortages[0], 0) && close(solution.partPrices[0], 5), "Master demand price");
        stock[0].quantity = 2;
        model = multiMasterModel(stock, demand, patterns);
        solution = multiSolveMaster(model, stock, patterns, 10);
        check(solution.status == 0 && close(solution.objective, 31), "Stock-limited master optimum");
        check(close(solution.shortages[0], 1) && close(solution.partPrices[0], 11), "Artificial shortage penalty and dual");
        check(close(solution.stockPrices[0], 22), "Negative inventory dual raises stock price");
        auto optionalDemand = demand;
        optionalDemand[0].materialValue = 0;
        model = multiMasterModel(stock, optionalDemand, patterns);
        solution = multiSolveMaster(model, stock, patterns, 10);
        check(solution.status == 0 && close(solution.objective, 0) && solution.usage.empty(), "Optional shortage remains unpenalized");

        const std::vector<MultiStock> mixedStock{{2, 6}};
        const std::vector<MultiDemand> mixedDemand{{2, 10}, {2, 10}};
        const std::vector<MultiPattern> mixedPatterns{{101, 0, 6, {{0, 2}}},
            {102, 0, 6, {{1, 2}}}, {103, 0, 4, {{0, 1}, {1, 1}}}};
        model = multiMasterModel(mixedStock, mixedDemand, mixedPatterns);
        solution = multiSolveMaster(model, mixedStock, mixedPatterns, 10);
        check(solution.status == 0 && close(solution.objective, 8), "Joint part demand selects mixed pattern");
        check(solution.usage.size() == 1 && solution.usage[0].pattern == 2 && close(solution.usage[0].quantity, 2),
              "Pattern identity survives the LP mapping");

        const std::vector<MultiStock> boundaryStock{{1, 100000}, {0, 2}};
        const std::vector<MultiPattern> unorderedPatterns{{10, 1, -1, {{0, 1}}}, {11, 0, 0, {{0, 1}}}};
        model = multiMasterModel(boundaryStock, demand, unorderedPatterns);
        check(model.patternColumns == std::vector<int32_t>({1, 0}), "Pattern columns follow stock order");
        check(model.minimumPatternCost == .001 && model.linear.columns[0].cost == .001,
              "Cost threshold includes exactly 100000");
        ShakerSolution raw;
        raw.status = 0;
        raw.primal = {.01, .01000001, 0};
        raw.dual = {7, .000100001, -3};
        size_t diagnostics = 0;
        auto decoded = multiReadMasterSolution(model, boundaryStock, unorderedPatterns, raw,
            [&](size_t index, double value) { check(index == 0 && value == raw.dual[1], "Stock dual diagnostic payload"); ++diagnostics; });
        check(decoded.usage.size() == 1 && decoded.usage[0].pattern == 0, "Usage publication is strictly above .01");
        check(diagnostics == 1 && decoded.stockPrices == std::vector<double>({100000, 5}), "Stock dual sign correction");
        auto largeStock = boundaryStock;
        largeStock[0].cost = std::nextafter(100000., INFINITY);
        check(multiMasterModel(largeStock, demand, unorderedPatterns).minimumPatternCost == 1, "Large cost floor");
        raw.status = 3;
        check(multiReadMasterSolution(model, boundaryStock, unorderedPatterns, raw).usage.empty(), "Stopped LP publishes no usage");
        std::mt19937 random(0x4f69f0);
        for (size_t trial = 0; trial < 64; ++trial) {
            const int32_t requirement = int32_t(random() % 50);
            const int32_t capacity = int32_t(random() % 9 + 1);
            const int32_t available = int32_t(random() % 20);
            const double cost = double(random() % 30 + 1);
            const double value = double(random() % 20 + 1);
            const std::vector<MultiStock> oneStock{{available, cost}};
            const std::vector<MultiDemand> oneDemand{{requirement, value}};
            const std::vector<MultiPattern> onePattern{{trial, 0, cost, {{0, capacity}}}};
            const auto oneModel = multiMasterModel(oneStock, oneDemand, onePattern);
            const auto oneSolution = multiSolveMaster(oneModel, oneStock, onePattern, 10);
            const double penalty = std::min(20. * value, 1.1 * cost);
            const double copies = cost < double(capacity) * penalty
                ? std::min(double(available), double(requirement) / capacity) : 0.;
            const double shortage = double(requirement) - copies * capacity;
            check(oneSolution.status == 0 && close(oneSolution.objective, cost * copies + penalty * shortage),
                  "Restricted master matches one-pattern analytic optimum");
            if (cost != double(capacity) * penalty)
                check(close(oneSolution.shortages[0], shortage), "Restricted master artificial demand balance");
            else
                check(oneSolution.shortages[0] >= 0 && oneSolution.shortages[0] <= requirement,
                      "Equal-cost LP solutions preserve the feasible shortage interval");
        }
        const std::vector<MultiPatternUse> rounding{{0, .998999}, {1, .999}, {2, 2.999}, {3, 1.6}, {4, 2}};
        std::vector<size_t> attempts;
        MultiRoundingHost roundingHost;
        roundingHost.stopped = []() { return false; };
        roundingHost.addPattern = [&](size_t pattern) {
            attempts.push_back(pattern);
            return pattern != 2 || attempts.size() == 1;
        };
        const auto rounded = multiRoundMasterUses(rounding, roundingHost);
        check(attempts == std::vector<size_t>({2, 2, 4, 4, 3, 1}), "Master integerization threshold and pattern failure sequence");
        check(rounded.attempted == 6 && rounded.added == 5 && rounded.failedPatterns == 1,
              "Master integerization reports actual copies");
        roundingHost.stopped = []() { return true; };
        const auto stopped = multiRoundMasterUses(rounding, roundingHost);
        check(stopped.stopped && stopped.attempted == 0, "Stopped integerization preserves the current plan");

        MultiPricingHost pricing;
        std::vector<double> prices{0, .001, std::nextafter(.001, INFINITY), -1, NAN};
        const std::vector<int32_t> quantities{2, 3, 4, 0, 1};
        std::vector<size_t> masks;
        std::vector<uint32_t> maximumTrials;
        std::string pricingTrace;
        pricing.snapshotPrices = [&] { pricingTrace += 'V'; return prices; };
        pricing.availableQuantity = [&](size_t part) { return quantities.at(part); };
        pricing.mask = [&](size_t part, int32_t count) {
            check(count == quantities.at(part), "Pricing masks the available instance quantity");
            masks.push_back(part); pricingTrace += 'M';
        };
        pricing.restore = [&] { pricingTrace += 'R'; };
        pricing.sessionStopped = [&] { pricingTrace += 'S'; return false; };
        pricing.externallyStopped = [&] { pricingTrace += 'E'; return false; };
        pricing.generate = [&](std::span<const double> copied, uint32_t maximum) {
            pricingTrace += 'G'; maximumTrials.push_back(maximum);
            check(copied[0] == 0, "Pricing uses a stable copy of the master prices");
            prices[0] = 99;
            if (maximumTrials.size() == 1) pricing.sessionMaximumTrials = 17;
        };
        check(!multiGenerateColumns(pricing).enabled && pricingTrace.empty(), "Disabled pattern pool bypasses all pricing providers");
        pricing.patternStorageEnabled = true;
        check(!multiGenerateColumns(pricing).enabled, "Pricing requires a source layout snapshot");
        pricing.existingLayout = true;
        pricing.oneByOneDisabled = true;
        check(!multiGenerateColumns(pricing).enabled, "Pricing honors OneByOne-disabled flag");
        pricing.oneByOneDisabled = false;
        const auto priced = multiGenerateColumns(pricing);
        check(priced.enabled && !priced.stopped && priced.attempts == 10 && priced.maskedTypes == 3,
              "Pricing uses ten attempts and the inclusive .001 price gate");
        check(masks == std::vector<size_t>({0, 1, 3}) && pricingTrace == "VMMMSEGSEGSEGSEGSEGSEGSEGSEGSEGSEGR",
              "Pricing provider order includes masks, both stop checks and restoration");
        check(maximumTrials.front() == 1000 && maximumTrials.back() == 17, "Pricing rereads the session trial budget per attempt");
        pricingTrace.clear();
        pricing.sessionStopped = [&] { pricingTrace += 'S'; return true; };
        const auto cancelledPricing = multiGenerateColumns(pricing);
        check(cancelledPricing.stopped && cancelledPricing.attempts == 0 && pricingTrace == "VMMSR",
              "Session cancellation precedes external cancellation and restores masks");
        pricingTrace.clear();
        pricing.sessionStopped = {};
        pricing.externallyStopped = [&] { pricingTrace += 'E'; return true; };
        check(multiGenerateColumns(pricing).stopped && pricingTrace == "VMMER", "External cancellation works without a session");
        pricingTrace.clear();
        pricing.externallyStopped = {};
        pricing.generate = [&](std::span<const double>, uint32_t) { pricingTrace += 'G'; throw std::runtime_error("Pricing fixture failure"); };
        try { multiGenerateColumns(pricing); check(false, "Throwing generator must propagate"); }
        catch (const std::runtime_error &) { check(pricingTrace == "VMMGR", "Generator failure restores all temporary masks"); }

        check(multiPricingWeight({}) == 1e9, "Unused pricing algorithms receive the original exploration weight");
        MultiPricingStats timing{2, 3, 4, 5};
        check(close(multiPricingWeight(timing), (4. / 5.) / (5. / 3. + .001)), "Pricing weight combines accepted rate and nonempty duration");
        MultiPricingSelectionHost choice;
        bool selectedByInteger = false, selectedByWeights = false;
        choice.unit = [] { return std::nextafter(.05, 0.); };
        choice.integer = [&](int32_t first, int32_t last) { check(first == 0 && last == 1, "Pricing uniform index bounds"); selectedByInteger = true; return 1; };
        choice.weighted = [&](std::span<const double> weights) { check(weights.size() == 2 && weights[0] == 1e9, "Pricing weighted input retains algorithm order"); selectedByWeights = true; return 0; };
        const std::array<MultiPricingStats, 2> algorithms{{{}, timing}};
        check(multiSelectPricing(algorithms, choice) == 1 && selectedByInteger && !selectedByWeights, "Pricing explores below .05");
        choice.unit = [] { return .05; };
        check(multiSelectPricing(algorithms, choice) == 0 && selectedByWeights, "Pricing exact .05 boundary uses weighted selection");
        choice.weighted = [](std::span<const double>) { return -1; };
        check(!multiSelectPricing(algorithms, choice), "Pricing rejects invalid signed algorithm indices");
        multiRecordPricing(timing, {true, true, true}, .125);
        check(timing.missingLayouts == 2 && timing.nonemptyLayouts == 4 && timing.acceptedLayouts == 5 && timing.elapsedSeconds == 5.125,
              "Pricing success updates original counters and time ledger");
        multiRecordPricing(timing, {}, .25);
        check(timing.missingLayouts == 3 && timing.nonemptyLayouts == 4 && timing.acceptedLayouts == 5 && timing.elapsedSeconds == 5.375,
              "Missing pricing layout updates only its counter and time");
        timing.missingLayouts = INT32_MAX;
        multiRecordPricing(timing, {}, 0);
        check(timing.missingLayouts == INT32_MIN, "Pricing counters retain native 32-bit wrap");
        size_t perturbDraw = 0;
        const std::array<double, 4> perturbDraws{.5, .2, .3, .4};
        const std::vector<MultiPatternUse> fractionalUses{{7, 2}, {8, 0}, {9, -2}};
        const auto perturbed = multiPerturbMasterUses(fractionalUses, [&] { return perturbDraws.at(perturbDraw++); });
        check(perturbDraw == 4 && perturbed[0].pattern == 7 && perturbed[0].quantity == .4 &&
              perturbed[1].quantity == 0 && perturbed[2].quantity == -.8, "Master usage perturbation retains identities and draw order");
        perturbDraw = 0;
        const auto unperturbed = multiPerturbMasterUses(fractionalUses, [&] { ++perturbDraw; return .49; });
        check(perturbDraw == 1 && unperturbed[0].quantity == 2 && unperturbed[2].quantity == -2,
              "Unperturbed master branch consumes only the gate draw");
        const std::vector<MultiPatternUse> reuseUses{{0, .09999}, {1, .1}, {2, 1}, {3, 1.9}, {4, 2}, {5, 0}};
        check(multiReusePatternCandidates(reuseUses) == std::vector<size_t>({4, 4, 4, 3, 3, 2, 2, 1}),
              "Pattern reuse includes exact .1 and one candidate beyond each integer usage");
        check(multiReusePatternCandidates(reuseUses, [](size_t pattern) { return pattern == 4 || pattern == 2; }) ==
              std::vector<size_t>({3, 3, 1}), "Pattern reuse omits blocked identities without changing order");
        const std::vector<size_t> reuseCandidates{7, 8, 9, 10, 11, 12};
        const std::array<double, 6> reuseDraws{0, .01, .5, .9, NAN, .5};
        size_t reuseDrawIndex = 0, reuseApplyIndex = 0, reuseRemoved = 0;
        const std::array<std::optional<double>, 5> reuseQualities{{{}, .989999, .99, NAN, 1.}};
        MultiPatternReuseHost reuseHost;
        reuseHost.unit = [&] { return reuseDraws.at(reuseDrawIndex++); };
        reuseHost.apply = [&](size_t pattern) { check(pattern == reuseCandidates[reuseApplyIndex + 1], "Pattern reuse provider order"); return reuseQualities.at(reuseApplyIndex++); };
        reuseHost.removeLast = [&] { ++reuseRemoved; };
        const auto reused = multiApplyReuseCandidates(reuseCandidates, reuseHost);
        check(reused.skipped == 1 && reused.attempted == 5 && reused.retained == 3 && reused.rejected == 1 && reuseRemoved == 1,
              "Pattern reuse probability, failed providers, exact quality boundary and native unordered handling");

        std::vector<MultiLnsSheet> boards{sheet(1, 50, 50), sheet(2, 50, 50)};
        std::vector<uint64_t> removed;
        size_t randomIndices = 0, units = 0;
        MultiLnsHost host;
        host.sheets = [&]() { return std::span<const MultiLnsSheet>(boards); };
        host.unit = [&]() { ++units; return .9; };
        host.randomIndex = [&](size_t) { ++randomIndices; return size_t(0); };
        host.remove = [&](size_t index) { removed.push_back(boards.at(index).identity); boards.erase(boards.begin() + ptrdiff_t(index)); };
        check(multiLnsDestroy(host, false, 0) == 1 && removed == std::vector<uint64_t>({2}),
              "Initial final-board removal precedes zero destruction gate");
        check(boards.size() == 1 && randomIndices == 0 && units == 1, "Destroy stop consumes one choice draw");
        boards = {sheet(1, 10, 10), sheet(2, 10, 10)};
        removed.clear();
        check(multiLnsDestroy(host, true, .5) == 1 && removed == std::vector<uint64_t>({1}),
              "Area gate equality accepts the first tied ratio");
        boards = {sheet(1, 10, .000999), sheet(2, 10, .001)};
        check(multiLnsLowestRatio(boards) == 1, "Ratio filter includes exact .001");
        boards[0].primaryCost = 10;
        boards[1].primaryCost = 10;
        boards[0].costComponents = {2, 3}; boards[0].costWeights = {5, 7};
        boards[0].secondaryCost = 20;
        check(multiLnsSheetCost(boards[0]) == 31, "LNS cost uses two weighted components and nonnegative difference");
        check(multiLnsLowestRatio(boards) == 0, "Cost-weighted ratio selects lower required value per cost");
        size_t draw = 0;
        const std::array<double, 3> draws{.1, .1, .5};
        check(multiLnsDestructionFraction([&]() { return draws.at(draw++); }) == .1 && draw == 3,
              "Destruction fraction combines the three ordered draws");
        size_t modifications = 0;
        MultiLnsPartialHost partial{host};
        partial.modify = [&](size_t index) { check(index == 0, "Partial modification selected ratio board"); ++modifications; };
        multiLnsPartial(partial);
        check(modifications == 10, "Partial helper attempts ten modifications");
        partial.multiheads = true;
        multiLnsPartial(partial);
        check(modifications == 10, "Partial helper checks multihead technology before selection");

        std::vector<MultiManagedStrategy> strategies{{1, false, 1, 20, 2, {}},
            {2, true, 100, 100, 1, {}}, {3, true, 2, 50, 1, {}}};
        const auto clock = []() { return 100.; };
        check(multiSelectManaged(strategies, 5, false, clock) == 2, "Preferred strategy and duration eligibility");
        strategies[2].estimate = 100;
        strategies[2].accumulated = 1;
        check(multiSelectManaged(strategies, 5, false, clock) == 2, "Equal eligibility uses normalized time ledger");
        check(!multiSelectManaged(strategies, 0, true, clock), "Configured scheduler rejects insufficient remaining time");
        MultiManagedStrategy active{4, false, 0, 3, 2, {90, 95}};
        check(multiManagedPriority(active, clock) == 9, "Manager priority includes each active task duration");
        strategies = {{1, false, 10, 1e-10, 1, {}}};
        check(multiSelectManaged(strategies, 0, true, clock) == 0, "Initial manager rejection threshold is strict");
        strategies[0].accumulated = std::nextafter(1e-10, INFINITY);
        check(!multiSelectManaged(strategies, 0, true, clock), "Manager rejects time shortage beyond initial threshold");
        std::string trace;
        MultiLnsRunHost run;
        run.hasExistingLayout = []() { return false; };
        run.oneByOne = [&]() { trace += 'O'; };
        const auto fresh = multiLnsRun(run);
        check(!fresh.usedExisting && trace == "O", "LNS starts with OneByOne when the pool has no layout");
        boards = {sheet(1, 10, 10), sheet(2, 10, 10)};
        run.hasExistingLayout = []() { return true; };
        run.copyExistingLayout = [&]() { trace += 'C'; };
        run.partial = [&](double a, double b) { check(a == 25 && b == 75, "LNS partial weights"); trace += 'P'; };
        run.destroy = host;
        run.destroy.unit = []() { return .1; };
        trace.clear();
        const auto existing = multiLnsRun(run);
        check(existing.usedExisting && existing.usedPartial && existing.removed == 1 && trace == "CPO",
              "LNS copies, destroys, optionally modifies and rebuilds in source order");
        #include "multi_modify_tests.inl"
        std::cout << "{\"pass\":true,\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
