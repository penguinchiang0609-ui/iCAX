#pragma once
#include "recovered_shaker.hpp"
#include "recovered_offset.hpp"
#include <optional>

namespace study::recovered {
struct MultiPatternPart {
    size_t part = 0;
    int32_t quantity = 0;
};
struct MultiPattern {
    uint64_t identity = 0;
    size_t sheet = 0;
    double cost = 0;
    std::vector<MultiPatternPart> parts;
};
struct MultiStock {
    int32_t quantity = 0;
    double cost = 0;
};
struct MultiDemand {
    int32_t quantity = 0;
    // 4f9a30 supplies required material area * material unit cost, or zero
    // for an optional part. This is distinct from candidate override prices.
    double materialValue = 0;
};
struct MultiMasterModel {
    LinearModel linear;
    double maximumSheetCost = 0;
    double minimumPatternCost = 0;
    std::vector<int32_t> patternColumns, shortageColumns, demandRows, stockRows;
    std::vector<size_t> patternOrder;
    std::vector<double> shortagePenalties;
};
// 4f69f0. Input order is the original stock traversal and per-stock pattern
// order. Columns are grouped by stock, then shortage columns by part index.
MultiMasterModel multiMasterModel(std::span<const MultiStock>, std::span<const MultiDemand>,
                                 std::span<const MultiPattern>);
struct MultiPatternUse {
    size_t pattern = 0;
    double quantity = 0;
};
std::vector<size_t> multiPatternUsageOrder(std::span<const MultiPatternUse>,
                                         std::optional<size_t> depth = {}); // 4b7e90
int32_t multiRandomIndex(Random &, int32_t first, int32_t last); // 39e660
struct MultiRoundingHost {
    std::function<bool()> stopped;
    std::function<bool(size_t)> addPattern;
    std::function<void(size_t, double)> failedPattern;
};
struct MultiRoundingResult { size_t attempted = 0, added = 0, failedPatterns = 0; bool stopped = false; };
// Integer-pattern publication loop in 4b6a30. Copies with usage >= .999,
// sorts descending, and tries floor(usage + .001) copies of each pattern.
// The host rebinds real part instances and returns false on exhausted stock
// or demand. One failed pattern stops that pattern's copies, then continues.
MultiRoundingResult multiRoundMasterUses(std::span<const MultiPatternUse>, MultiRoundingHost &);
struct MultiMasterSolution {
    int32_t status = 2;
    double objective = 0;
    std::vector<MultiPatternUse> usage;
    std::vector<double> shortages, partPrices, stockPrices;
};
using MultiDualDiagnostic = std::function<void(size_t, double)>;
// 4f7b80. Uses strictly > .01 for pattern publication and .0001 for a
// positive-stock-dual diagnostic. Source pattern records retain their order.
MultiMasterSolution multiReadMasterSolution(const MultiMasterModel &, std::span<const MultiStock>,
    std::span<const MultiPattern>, const ShakerSolution &, const MultiDualDiagnostic & = {});
MultiMasterSolution multiSolveMaster(const MultiMasterModel &, std::span<const MultiStock>,
    std::span<const MultiPattern>, double seconds,
    const MultiDualDiagnostic & = {});

struct MultiPricingHost {
    bool patternStorageEnabled = false, oneByOneDisabled = false, existingLayout = false;
    // Prices are copied once, before any queue state changes or generator call.
    std::function<std::vector<double>()> snapshotPrices;
    std::function<int32_t(size_t)> availableQuantity;
    // 36e240 changes each selected instance's queue state 0 -> 2 under a
    // counted mask; 36e430 restores those masks in insertion order.
    std::function<void(size_t, int32_t)> mask;
    std::function<void()> restore;
    std::function<bool()> sessionStopped, externallyStopped;
    std::optional<uint32_t> sessionMaximumTrials;
    std::function<void(std::span<const double>, uint32_t)> generate;
};
struct MultiPricingResult {
    size_t attempts = 0, maskedTypes = 0;
    bool enabled = false, stopped = false;
};
// 4b77f0: ten attempts, <= .001 price gate, session stop followed by external
// stop, and 1000 maximum trials when no session provider exists.
MultiPricingResult multiGenerateColumns(MultiPricingHost &);
struct MultiPricingStats {
    int32_t missingLayouts = 0, nonemptyLayouts = 0, acceptedLayouts = 0;
    double elapsedSeconds = 0;
};
double multiPricingWeight(const MultiPricingStats &); // arithmetic in 4bdd80
struct MultiPricingSelectionHost {
    std::function<double()> unit;
    std::function<int32_t(int32_t, int32_t)> integer;
    std::function<int32_t(std::span<const double>)> weighted;
};
// 4bdd80 selects uniformly for unit < .05; otherwise it builds the weights in
// algorithm traversal order. Invalid selections produce an empty optional.
std::optional<size_t> multiSelectPricing(std::span<const MultiPricingStats>, MultiPricingSelectionHost &);
struct MultiPricingTrial { bool layout = false, nonempty = false, accepted = false; };
// Statistics update in 4bbe60. Counters are original signed 32-bit fields.
void multiRecordPricing(MultiPricingStats &, MultiPricingTrial, double elapsedSeconds);

// Usage perturbation in 4b7530. The >= .5 branch consumes one further unit
// for every record, including zero/negative quantities, before the IP host.
std::vector<MultiPatternUse> multiPerturbMasterUses(std::span<const MultiPatternUse>, const std::function<double()> &unit);
// 4fa7a0. Descending usages >= .1 contribute floor(usage)+1 candidate copies;
// blocked identities are omitted. This queue expansion is not an IP solver.
std::vector<size_t> multiReusePatternCandidates(std::span<const MultiPatternUse>,
    const std::function<bool(size_t)> &blocked = {});
struct MultiPatternReuseHost {
    std::function<double()> unit;
    // A successful provider appends one real board and returns its reuse
    // quality. An empty optional leaves the current layout untouched.
    std::function<std::optional<double>(size_t)> apply;
    std::function<void()> removeLast;
};
struct MultiPatternReuseResult { size_t skipped = 0, attempted = 0, retained = 0, rejected = 0; };
// 4fa560: skip a candidate only for unit < .01. Keep returned layouts unless
// their quality is strictly below .99; reject by removing the appended board.
MultiPatternReuseResult multiApplyReuseCandidates(std::span<const size_t>, MultiPatternReuseHost &);

struct MultiLnsSheet {
    uint64_t identity = 0;
    // Per-placement required areas, in layout iteration order. Optional parts
    // contribute zero. The informer cache is a separate original field.
    std::vector<double> requiredAreas;
    double informerRequiredArea = 0;
    std::array<double, 2> costComponents{}, costWeights{};
    double primaryCost = 0, secondaryCost = 0;
};
double multiRequiredArea(std::span<const MultiLnsSheet>); // 4c9c00
double multiLnsSheetCost(const MultiLnsSheet &); // arithmetic in 4bb760
std::optional<size_t> multiLnsLowestRatio(std::span<const MultiLnsSheet>); // 4bb760
double multiLnsDestructionFraction(const std::function<double()> &unit); // 4baa05..4baa54
enum class MultiLnsDiagnostic { EmptyLayout, InvalidCandidate };
struct MultiLnsHost {
    std::function<std::span<const MultiLnsSheet>()> sheets;
    std::function<double()> unit;
    // 39e660(rng, 0, count-1), preserving the original integer RNG path.
    std::function<size_t(size_t)> randomIndex;
    // Callers return stock and unmet demand before removing the layout.
    std::function<void(size_t)> remove;
    std::function<void(MultiLnsDiagnostic)> diagnostic;
};
// 4bb500. The optional initial last-sheet removal precedes the area gate.
uint32_t multiLnsDestroy(MultiLnsHost &, bool preserveInitialLastSheet,
                         double fraction, double randomWeight = 25, double ratioWeight = 75);
struct MultiLnsPartialHost {
    MultiLnsHost selection;
    bool specialProblem = false, hasTechnology = true, multiheads = false;
    // 4fe160/4fd860 technology construction, strategy execution and 556b80
    // refresh are upstream providers. Called once per selected board.
    std::function<void(size_t)> modify;
};
void multiLnsPartial(MultiLnsPartialHost &, double randomWeight = 25, double ratioWeight = 75); // 4baf70
struct MultiLnsRunHost {
    MultiLnsHost destroy;
    bool preserveInitialLastSheet = false;
    std::function<bool()> hasExistingLayout;
    std::function<void()> copyExistingLayout, oneByOne;
    std::function<void(double, double)> partial;
};
struct MultiLnsRunResult { uint32_t removed = 0; bool usedExisting = false, usedPartial = false; };
// Control portion of 4ba8c0. Geometry copying, partial-technology production,
// remaining-budget accounting and OneByOne execution belong to the host.
MultiLnsRunResult multiLnsRun(MultiLnsRunHost &);

struct MultiManagedStrategy {
    uint64_t identity = 0;
    bool preferred = false;
    double estimate = 0, accumulated = 0, weight = 1;
    std::vector<double> activeStartSeconds;
};
double multiManagedPriority(const MultiManagedStrategy &, const std::function<double()> &clock); // 4c00e0
// Selection portion of 4c0cb0. Priority clocks are read for each original
// comparison and each active task. The factory itself remains a host concern.
std::optional<size_t> multiSelectManaged(std::span<const MultiManagedStrategy>, double remaining,
    bool allowTimeRejection, const std::function<double()> &clock);
}
