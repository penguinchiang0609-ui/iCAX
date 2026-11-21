#pragma once
#include "recovered_strategy.hpp"
namespace study::recovered {
struct StrategyEntry { int id=0, calibration=1; double weight=0, budget=0; };
// Input order is the original preset map traversal order. TotalWeight is the
// stored upstream field, not a newly computed normalization.
std::vector<double> strategyWeights(std::span<const StrategyEntry>); // 44b7e0
void addStrategyBudgets(std::span<StrategyEntry>,int workers,double time,double totalWeight); // 452c30
struct ScheduleState { int32_t trials=0, trialLimit=-1, poolLimit=0; double poolGrowth=0; };
// Caller serializes state and RNG. No result pool, workers or geometry here.
std::optional<size_t> nextStrategy(Random &,ScheduleState &,std::span<const double>); // 453e00
struct StrategyResult { double metric=0; int sourceId=0; };
struct ResultWeights {
    int32_t equalPrefix=0, selectedCount=0;
    std::map<int,double> weights;
};
// Nonempty, already ordered candidate pool. Reject zero integer denominator
// instead of reproducing the original hardware divide exception.
ResultWeights selectResultWeights(std::span<const StrategyResult>,int32_t trials,double config198,double config190); // 5a3dc0
}
