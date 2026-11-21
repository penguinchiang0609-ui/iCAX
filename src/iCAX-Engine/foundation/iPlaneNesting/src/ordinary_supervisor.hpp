#pragma once
#include "ordinary_runtime.hpp"
#include "recovered_history.hpp"
#include "recovered_worker.hpp"
#include "recovered_engine.hpp"
#include "recovered_chris6.hpp"
#include "ordinary_predefined.hpp"
#include "recovered_multiheads.hpp"

namespace study {
struct SupervisorReference : recovered::StructuringElement {
    Polygon polygon;
};
struct SupervisorProgram {
    uint64_t initialPreset = 4011001844ULL, reversePreset = 20222, fastPreset = 1000;
    recovered::ManagerFactoryConfig factory;
    double globalSeedOffset = 27, keptScale198 = 250, inverseKept190 = 4;
    int32_t maximumCandidates = 30, selectionTrialLimit = -1, optimizationTrialLimit = -1;
    double selectionPoolGrowth = 2, optimizationPoolGrowth = 0;
    std::array<double, 3> phaseFractions{.25, .25, .5};
    recovered::SingleEngineBudgetInput engineBudget;
    double strategyFraction = NAN;
    int32_t problemScale = -1;
    double technologyMemoryLimitD0 = 8000;
    double technologyMinimum40 = 0;
    // Negative entries use the original selection-count/rectangle/head gate.
    std::array<int32_t, 3> constructionPhases{-1, -1, -1};
    bool hasTiles = true;
    std::vector<std::vector<size_t>> rectangleGroups;
    std::vector<std::shared_ptr<const SupervisorReference>> historyElements;
    std::shared_ptr<const SupervisorReference> fixedReference;
    bool dynamicHistory = false;
    bool useDynamicStructuringElements = true;
    std::map<int, ReverseRunnerFactory> additionalFactories;
    std::optional<recovered::UserEvaluation> problemEvaluation;
    std::optional<OrdinaryPredefinedProgram> predefined;
    recovered::MultiheadsHardware multiheads;
    double materialUnitCost10 = 1, masterCuttingUnitCost18 = 17, additionalHeadCuttingUnitCost20 = 1;
    bool usePostOptimization = true;
    double theoreticalSecond828 = 1;
    // Unspecified means the original fast-ending gate is not enabled without
    // its concrete upstream reference-length provider.
    std::optional<double> fastEndReferenceLength;
};
struct SupervisorTrace {
    struct CandidateCost {
        int32_t sourceId = 0, trial = 0;
        int64_t rawLength = 0, correctedLength = 0;
        double informer20 = 0, benefit = 0;
        std::array<double, 3> processQuantities{};
    };
    struct Stage {
        int phase = 0;
        double seconds = 0;
        std::vector<int> strategies;
        recovered::WorkerRun worker;
    };
    std::vector<Stage> stages;
    std::map<int, size_t> managerAttempts, managerPlacements;
    std::set<int> unsupportedManagers;
    std::set<std::string> missingProviders;
    size_t trials = 0, localAccepted = 0, globalAccepted = 0, bestUpdates = 0;
    size_t warmSelections = 0, warmRetained = 0, historyChoices = 0, historyUpdates = 0;
    size_t predefinedPhases = 0, predefinedCommitted = 0;
    size_t noMapCandidateQueries = 0, noMapCommitted = 0;
    size_t postOptimizationTasks = 0, postOptimizationAccepted = 0;
    size_t postOptimizationGuides = 0;
    size_t postOptimizationPointRows = 0;
    size_t postOptimizationHeadRows = 0;
    std::map<int32_t, size_t> postOptimizationOperations;
    size_t candidateCount = 0;
    int32_t orientationPopulation = 0;
    std::vector<size_t> partOrientationCacheSizes;
    std::vector<double> shakerGainSamples;
    double medianShakerGain = 0;
    bool expectedShakerEvaluator = false;
    bool timedOut = false, fastEnded = false;
    std::vector<std::vector<std::pair<size_t,size_t>>> sisterGroups;
    size_t rectangleInferenceCalls = 0, rectangleInferredGroups = 0;
    std::vector<CandidateCost> candidateCosts;
    std::optional<CandidateCost> bestCost;
};
Output ordinaryDefault(const Input &, Budget &, SupervisorTrace * = nullptr);
Output ordinaryDefault(const Input &, Budget &, const SupervisorProgram &, SupervisorTrace * = nullptr);
// The final board already exists. The 4cba60/4cb660 polish is operation two,
// with immutable L-offcut guides. Retain the source if remnant value worsens.
Output ordinaryImproveLOffcut(const Input &,const Output &,Budget &);
double ordinaryLOffcutValue(const Input &,const Output &,Budget &);
}
