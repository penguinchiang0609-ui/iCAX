#include "recovered_multi.hpp"
#include <cmath>
#include <algorithm>
#include <bit>
#include <numeric>
#include <stdexcept>

namespace study::recovered {
namespace {
class PatternUsageSort {
    std::span<const MultiPatternUse> uses;
    std::vector<size_t> order;
    bool before(size_t first, size_t second) const { return uses[first].quantity > uses[second].quantity; }
    void insertion(size_t first, size_t last) {
        for (size_t index = first + 1; index < last; ++index) {
            const auto item = order[index];
            size_t hole = index;
            if (before(item, order[first])) {
                std::move_backward(order.begin() + first, order.begin() + index, order.begin() + index + 1);
                hole = first;
            } else {
                while (hole > first && before(item, order[hole - 1])) {
                    order[hole] = order[hole - 1];
                    --hole;
                }
            }
            order[hole] = item;
        }
    }
    void median(size_t first, size_t middle, size_t last) {
        if (before(order[middle], order[first])) std::swap(order[middle], order[first]);
        if (before(order[last], order[middle])) {
            std::swap(order[last], order[middle]);
            if (before(order[middle], order[first])) std::swap(order[middle], order[first]);
        }
    }
    std::pair<size_t, size_t> partition(size_t first, size_t last) {
        const size_t middle = first + (last - first) / 2;
        if (last - first > 41) {
            const size_t step = (last - first) / 8;
            median(first, first + step, first + step * 2);
            median(middle - step, middle, middle + step);
            median(last - 1 - step * 2, last - 1 - step, last - 1);
            median(first + step, middle, last - 1 - step);
        } else median(first, middle, last - 1);
        size_t equalFirst = middle, equalLast = middle + 1;
        while (first < equalFirst && !before(order[equalFirst - 1], order[equalFirst]) &&
               !before(order[equalFirst], order[equalFirst - 1])) --equalFirst;
        while (equalLast < last && !before(order[equalLast], order[equalFirst]) &&
               !before(order[equalFirst], order[equalLast])) ++equalLast;
        size_t right = equalLast, left = equalFirst;
        for (;;) {
            for (; right < last; ++right) {
                if (before(order[equalFirst], order[right])) continue;
                if (before(order[right], order[equalFirst])) break;
                if (equalLast != right) std::swap(order[equalLast], order[right]);
                ++equalLast;
            }
            for (; first < left; --left) {
                if (before(order[left - 1], order[equalFirst])) continue;
                if (before(order[equalFirst], order[left - 1])) break;
                --equalFirst;
                if (equalFirst != left - 1) std::swap(order[equalFirst], order[left - 1]);
            }
            if (left == first && right == last) return {equalFirst, equalLast};
            if (left == first) {
                if (equalLast != right) std::swap(order[equalFirst], order[equalLast]);
                ++equalLast;
                std::swap(order[equalFirst++], order[right++]);
            } else if (right == last) {
                --left;
                --equalFirst;
                if (left != equalFirst) std::swap(order[left], order[equalFirst]);
                std::swap(order[equalFirst], order[--equalLast]);
            } else std::swap(order[right++], order[--left]);
        }
    }
    void down(size_t base, size_t hole, size_t count, size_t item) {
        const size_t top = hole, limit = (count - 1) / 2;
        while (hole < limit) {
            size_t child = hole * 2 + 2;
            if (before(order[base + child], order[base + child - 1])) --child;
            order[base + hole] = order[base + child];
            hole = child;
        }
        if (hole == limit && count % 2 == 0) {
            order[base + hole] = order[base + count - 1];
            hole = count - 1;
        }
        while (top < hole) {
            const size_t parent = (hole - 1) / 2;
            if (!before(order[base + parent], item)) break;
            order[base + hole] = order[base + parent];
            hole = parent;
        }
        order[base + hole] = item;
    }
    void heap(size_t first, size_t last) {
        size_t count = last - first;
        for (size_t index = count / 2; index > 0; --index) down(first, index - 1, count, order[first + index - 1]);
        while (count > 1) {
            const auto item = order[first + count - 1];
            order[first + count - 1] = order[first];
            down(first, 0, --count, item);
        }
    }
    void sort(size_t first, size_t last, size_t depth) {
        while (last - first > 32) {
            if (depth == 0) { heap(first, last); return; }
            const auto [begin, end] = partition(first, last);
            depth = (depth >> 1) + (depth >> 2);
            if (begin - first < last - end) { sort(first, begin, depth); first = end; }
            else { sort(end, last, depth); last = begin; }
        }
        if (first < last) insertion(first, last);
    }
public:
    explicit PatternUsageSort(std::span<const MultiPatternUse> input) : uses(input), order(input.size()) {
        std::iota(order.begin(), order.end(), 0);
    }
    std::vector<size_t> run(size_t depth) { sort(0, order.size(), depth); return std::move(order); }
};
std::optional<size_t> selectLnsSheet(MultiLnsHost &host, double probability) {
    if (probability <= host.unit()) return multiLnsLowestRatio(host.sheets());
    const auto sheets = host.sheets();
    if (sheets.empty()) return {};
    return host.randomIndex(sheets.size());
}
}
std::vector<size_t> multiPatternUsageOrder(std::span<const MultiPatternUse> uses, std::optional<size_t> depth) {
    return PatternUsageSort(uses).run(depth.value_or(uses.size()));
}
int32_t multiRandomIndex(Random &random, int32_t first, int32_t last) {
    return random.integer(first,last);
}
MultiRoundingResult multiRoundMasterUses(std::span<const MultiPatternUse> uses, MultiRoundingHost &host) {
    std::vector<MultiPatternUse> selected;
    for (const auto &use : uses) if (.999 <= use.quantity) selected.push_back(use);
    const auto order = multiPatternUsageOrder(selected);
    MultiRoundingResult result;
    for (const auto index : order) {
        const auto &use = selected[index];
        for (int32_t count = 0; double(count) < std::floor(use.quantity + .001); ++count) {
            if (host.stopped()) { result.stopped = true; break; }
            ++result.attempted;
            if (!host.addPattern(use.pattern)) {
                ++result.failedPatterns;
                if (host.failedPattern) host.failedPattern(use.pattern, use.quantity);
                break;
            }
            ++result.added;
            if (count == INT32_MAX) throw std::length_error("Multi pattern copy count");
        }
    }
    return result;
}
MultiMasterModel multiMasterModel(std::span<const MultiStock> stock,
                                  std::span<const MultiDemand> demand,
                                  std::span<const MultiPattern> patterns) {
    MultiMasterModel result;
    for (const auto &sheet : stock)
        result.maximumSheetCost = sheet.cost > result.maximumSheetCost ? sheet.cost : result.maximumSheetCost;
    result.minimumPatternCost = result.maximumSheetCost <= 100000 ? .001 : 1;
    result.patternColumns.resize(patterns.size(), -1);
    result.stockRows.resize(stock.size(), -1);
    for (size_t sheet = 0; sheet < stock.size(); ++sheet) {
        for (size_t pattern = 0; pattern < patterns.size(); ++pattern) {
            const auto &entry = patterns[pattern];
            if (entry.sheet != sheet) continue;
            const double cost = result.minimumPatternCost > entry.cost ? result.minimumPatternCost : entry.cost;
            result.patternColumns[pattern] = result.linear.addColumn(0, INFINITY, cost);
            result.patternOrder.push_back(pattern);
        }
    }
    for (const auto column : result.patternColumns)
        if (column < 0) throw std::out_of_range("Multi pattern stock index");
    for (const auto &part : demand) {
        const double materialPenalty = part.materialValue * 20;
        const double sheetPenalty = result.maximumSheetCost * 1.1;
        const double penalty = sheetPenalty < materialPenalty ? sheetPenalty : materialPenalty;
        result.shortageColumns.push_back(result.linear.addColumn(0, INFINITY, penalty));
        result.shortagePenalties.push_back(penalty);
    }
    for (size_t part = 0; part < demand.size(); ++part) {
        std::vector<LinearTerm> terms;
        for (size_t sheet = 0; sheet < stock.size(); ++sheet)
            for (size_t pattern = 0; pattern < patterns.size(); ++pattern) {
                if (patterns[pattern].sheet != sheet) continue;
                for (const auto &coefficient : patterns[pattern].parts)
                    if (coefficient.part == part)
                        terms.push_back({result.patternColumns[pattern], double(coefficient.quantity)});
            }
        terms.push_back({result.shortageColumns[part], 1});
        result.demandRows.push_back(result.linear.addLowerRow(std::move(terms), double(demand[part].quantity)));
    }
    for (size_t sheet = 0; sheet < stock.size(); ++sheet) {
        std::vector<LinearTerm> terms;
        for (size_t pattern = 0; pattern < patterns.size(); ++pattern)
            if (patterns[pattern].sheet == sheet) terms.push_back({result.patternColumns[pattern], 1});
        result.stockRows[sheet] = result.linear.addRow(std::move(terms), -INFINITY, double(stock[sheet].quantity));
    }
    for (const auto &pattern : patterns)
        for (const auto &coefficient : pattern.parts)
            if (coefficient.part >= demand.size()) throw std::out_of_range("Multi pattern part index");
    return result;
}
MultiMasterSolution multiReadMasterSolution(const MultiMasterModel &model, std::span<const MultiStock> stock,
    std::span<const MultiPattern> patterns, const ShakerSolution &solution, const MultiDualDiagnostic &diagnostic) {
    MultiMasterSolution result;
    result.status = solution.status == 0 ? 0 : 3;
    if (solution.status != 0) return result;
    if (patterns.size() != model.patternColumns.size() || stock.size() != model.stockRows.size())
        throw std::invalid_argument("Multi master result input changed");
    result.objective = solution.objective;
    for (const size_t pattern : model.patternOrder) {
        const double quantity = solution.primal.at(size_t(model.patternColumns[pattern]));
        if (.01 < quantity) result.usage.push_back({pattern, quantity});
    }
    for (const auto column : model.shortageColumns) result.shortages.push_back(solution.primal.at(size_t(column)));
    for (const auto row : model.demandRows) result.partPrices.push_back(solution.dual.at(size_t(row)));
    for (size_t sheet = 0; sheet < stock.size(); ++sheet) {
        double dual = solution.dual.at(size_t(model.stockRows[sheet]));
        if (.0001 < dual) {
            if (diagnostic) diagnostic(sheet, dual);
            dual = 0;
        }
        const double adjusted = stock[sheet].cost - dual;
        result.stockPrices.push_back(adjusted > stock[sheet].cost ? adjusted : stock[sheet].cost);
    }
    return result;
}
MultiMasterSolution multiSolveMaster(const MultiMasterModel &model, std::span<const MultiStock> stock,
    std::span<const MultiPattern> patterns, double seconds, const MultiDualDiagnostic &diagnostic) {
    return multiReadMasterSolution(model, stock, patterns, solveShakerHighs(model.linear, seconds), diagnostic);
}
MultiPricingResult multiGenerateColumns(MultiPricingHost &host) {
    MultiPricingResult result;
    if (!host.patternStorageEnabled || host.oneByOneDisabled || !host.existingLayout) return result;
    result.enabled = true;
    const auto prices = host.snapshotPrices();
    struct Restore {
        std::function<void()> &restore;
        ~Restore() { restore(); }
    } restore{host.restore};
    for (size_t part = 0; part < prices.size(); ++part) {
        if (prices[part] <= .001) {
            host.mask(part, host.availableQuantity(part));
            ++result.maskedTypes;
        }
    }
    do {
        if (host.sessionStopped && host.sessionStopped()) { result.stopped = true; break; }
        if (host.externallyStopped && host.externallyStopped()) { result.stopped = true; break; }
        host.generate(prices, host.sessionMaximumTrials.value_or(1000));
        ++result.attempts;
    } while (result.attempts < 10);
    return result;
}
double multiPricingWeight(const MultiPricingStats &stats) {
    // Keep the original comparison order: an unordered elapsed time follows
    // the division branch, just as COMISD/JBE does in 4bdf01.
    const double rate = stats.elapsedSeconds < 1e-9 ? 1000000. :
        double(stats.acceptedLayouts) / stats.elapsedSeconds;
    const double average = stats.nonemptyLayouts == 0 ? 0. :
        stats.elapsedSeconds / double(stats.nonemptyLayouts);
    return rate / (average + .001);
}
std::optional<size_t> multiSelectPricing(std::span<const MultiPricingStats> stats, MultiPricingSelectionHost &host) {
    int32_t selected;
    if (host.unit() < .05) selected = host.integer(0, int32_t(stats.size()) - 1);
    else {
        std::vector<double> weights;
        weights.reserve(stats.size());
        for (const auto &entry : stats) weights.push_back(multiPricingWeight(entry));
        selected = host.weighted(weights);
    }
    if (selected < 0 || size_t(selected) >= stats.size()) return {};
    return size_t(selected);
}
void multiRecordPricing(MultiPricingStats &stats, MultiPricingTrial trial, double elapsedSeconds) {
    const auto increment = [](int32_t &counter) {
        counter = std::bit_cast<int32_t>(std::bit_cast<uint32_t>(counter) + 1);
    };
    if (trial.layout && trial.nonempty) increment(stats.nonemptyLayouts);
    stats.elapsedSeconds = elapsedSeconds + stats.elapsedSeconds;
    if (trial.accepted) increment(stats.acceptedLayouts);
    if (!trial.layout) increment(stats.missingLayouts);
}
std::vector<MultiPatternUse> multiPerturbMasterUses(std::span<const MultiPatternUse> uses,
    const std::function<double()> &unit) {
    std::vector<MultiPatternUse> result(uses.begin(), uses.end());
    if (!(unit() < .5))
        for (auto &use : result) use.quantity = unit() * (use.quantity - 0.) + 0.;
    return result;
}
std::vector<size_t> multiReusePatternCandidates(std::span<const MultiPatternUse> uses,
    const std::function<bool(size_t)> &blocked) {
    std::vector<size_t> result;
    for (const auto index : multiPatternUsageOrder(uses)) {
        const auto &use = uses[index];
        if (.1 > use.quantity) break;
        if ((blocked && blocked(use.pattern)) || !(use.quantity >= 0)) continue;
        if (!std::isfinite(use.quantity) || use.quantity >= double(INT32_MAX))
            throw std::length_error("Multi pattern reuse candidate count");
        int32_t count = 0;
        do {
            result.push_back(use.pattern);
            ++count;
        } while (use.quantity >= double(count));
    }
    return result;
}
MultiPatternReuseResult multiApplyReuseCandidates(std::span<const size_t> candidates, MultiPatternReuseHost &host) {
    MultiPatternReuseResult result;
    for (const auto pattern : candidates) {
        if (.01 > host.unit()) { ++result.skipped; continue; }
        ++result.attempted;
        const auto quality = host.apply(pattern);
        if (!quality) continue;
        if (.99 > *quality) {
            host.removeLast();
            ++result.rejected;
        } else ++result.retained;
    }
    return result;
}
double multiRequiredArea(std::span<const MultiLnsSheet> sheets) {
    double result = 0;
    for (const auto &sheet : sheets)
        for (const auto area : sheet.requiredAreas) result = result + area;
    return result;
}
double multiLnsSheetCost(const MultiLnsSheet &sheet) {
    double difference = sheet.primaryCost - sheet.secondaryCost;
    if (!(difference > 0)) difference = 0;
    return ((sheet.costComponents[0] * sheet.costWeights[0] + 0.) +
            sheet.costComponents[1] * sheet.costWeights[1]) + difference;
}
std::optional<size_t> multiLnsLowestRatio(std::span<const MultiLnsSheet> sheets) {
    std::optional<size_t> result;
    double lowest = 0;
    for (size_t index = 0; index < sheets.size(); ++index) {
        const double cost = multiLnsSheetCost(sheets[index]);
        if (.001 > cost) continue;
        const double ratio = sheets[index].informerRequiredArea / cost;
        if (!result || lowest > ratio) {
            result = index;
            lowest = ratio;
        }
    }
    return result;
}
double multiLnsDestructionFraction(const std::function<double()> &unit) {
    double upper = .1;
    if (unit() < .25) upper = .15000000000000002;
    if (unit() < .25) upper = upper + .05;
    return unit() * (upper - 0.) + 0.;
}
uint32_t multiLnsDestroy(MultiLnsHost &host, bool preserveInitialLastSheet, double fraction,
                        double randomWeight, double ratioWeight) {
    if (host.sheets().empty()) {
        if (host.diagnostic) host.diagnostic(MultiLnsDiagnostic::EmptyLayout);
        return 0;
    }
    const double initialArea = multiRequiredArea(host.sheets());
    uint32_t removed = 0;
    if (!preserveInitialLastSheet) {
        host.remove(host.sheets().size() - 1);
        removed = 1;
    }
    const double probability = randomWeight / (randomWeight + ratioWeight);
    for (;;) {
        const auto candidate = selectLnsSheet(host, probability);
        if (!candidate) return removed;
        const auto sheets = host.sheets();
        if (*candidate >= sheets.size()) {
            if (host.diagnostic) host.diagnostic(MultiLnsDiagnostic::InvalidCandidate);
            return removed;
        }
        const double currentArea = multiRequiredArea(sheets);
        if ((1. - fraction) * initialArea > currentArea - sheets[*candidate].informerRequiredArea)
            return removed;
        host.remove(*candidate);
        ++removed;
    }
}
void multiLnsPartial(MultiLnsPartialHost &host, double randomWeight, double ratioWeight) {
    if (host.specialProblem || !host.hasTechnology || host.multiheads || host.selection.sheets().empty()) return;
    const double probability = randomWeight / (randomWeight + ratioWeight);
    for (size_t iteration = 0; iteration < 10; ++iteration) {
        const auto candidate = selectLnsSheet(host.selection, probability);
        if (candidate) host.modify(*candidate);
    }
}
MultiLnsRunResult multiLnsRun(MultiLnsRunHost &host) {
    MultiLnsRunResult result;
    if (!host.hasExistingLayout()) {
        host.oneByOne();
        return result;
    }
    host.copyExistingLayout();
    result.usedExisting = true;
    const double fraction = multiLnsDestructionFraction(host.destroy.unit);
    result.removed = multiLnsDestroy(host.destroy, host.preserveInitialLastSheet, fraction, 25, 75);
    if (host.destroy.unit() < .5) {
        host.partial(25, 75);
        result.usedPartial = true;
    }
    host.oneByOne();
    return result;
}
double multiManagedPriority(const MultiManagedStrategy &strategy, const std::function<double()> &clock) {
    double active = 0;
    for (const auto started : strategy.activeStartSeconds) active = active + (clock() - started);
    return (active + strategy.accumulated) / strategy.weight;
}
std::optional<size_t> multiSelectManaged(std::span<const MultiManagedStrategy> strategies, double remaining,
    bool allowTimeRejection, const std::function<double()> &clock) {
    if (strategies.empty()) return {};
    size_t chosen = 0;
    for (size_t candidate = 1; candidate < strategies.size(); ++candidate) {
        const auto &a = strategies[candidate], &b = strategies[chosen];
        bool prefer = a.preferred;
        if (a.preferred == b.preferred) {
            prefer = a.estimate <= remaining;
            if (prefer == (b.estimate <= remaining)) {
                const double first = multiManagedPriority(a, clock);
                const double second = multiManagedPriority(b, clock);
                prefer = first < second;
            }
        }
        if (prefer) chosen = candidate;
    }
    if (allowTimeRejection && remaining < strategies[chosen].estimate) {
        double total = 0;
        for (const auto &strategy : strategies) total = total + multiManagedPriority(strategy, clock);
        if (1e-10 < total) return {};
    }
    return chosen;
}
}
