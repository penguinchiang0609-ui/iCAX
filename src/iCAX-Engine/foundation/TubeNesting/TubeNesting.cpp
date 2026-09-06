#include "TubeNesting.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace iCAX::TubeNesting
{
    namespace
    {
        constexpr Length kLengthMax = (std::numeric_limits<Length>::max)();
        constexpr std::int64_t kMetricMax = (std::numeric_limits<std::int64_t>::max)();

        struct PartInstance
        {
            std::size_t DemandIndex = 0;
            std::size_t Ordinal = 0;
            std::string ID;
        };

        struct WorkingPlacement
        {
            std::size_t InstanceIndex = 0;
            std::size_t VariantIndex = 0;
        };

        struct WorkingBar
        {
            std::size_t StockIndex = 0;
            std::vector<WorkingPlacement> Placements;
        };

        using WorkingPlan = std::vector<WorkingBar>;

        struct BarMeasure
        {
            bool Feasible = false;
            Length ProcessedEnd = 0;
            Length RemainingLength = 0;
            Length ReusableRemainderLength = 0;
            Length UnrecoverableWaste = 0;
            Length PartLength = 0;
            Length GapLength = 0;
            std::size_t CutCount = 0;
            std::size_t CommonCutCount = 0;
            std::size_t OrderChangeCount = 0;
            std::vector<Length> Starts;
            std::vector<Length> Ends;
            std::vector<Length> GapsBefore;
            std::vector<bool> CommonCutWithPrevious;
        };

        struct InternalMetrics
        {
            std::int64_t StockCost = 0;
            std::size_t StandardStockCount = 0;
            std::size_t OpenedStockCount = 0;
            Length TotalStockLength = 0;
            Length PartLength = 0;
            Length GapLength = 0;
            Length UnrecoverableWaste = 0;
            Length ReusableRemainderLength = 0;
            std::size_t ReusableRemainderCount = 0;
            std::size_t CutCount = 0;
            std::size_t CommonCutCount = 0;
            std::size_t OrderChangeCount = 0;
        };

        struct GroupOutcome
        {
            std::optional<WorkingPlan> Plan;
            InternalMetrics Metrics;
            bool ExactCompleted = false;
            std::uint64_t SearchNodes = 0;
            std::vector<std::string> Diagnostics;
        };

        [[nodiscard]] bool TryAdd(const Length _Left, const Length _Right, Length& _Result)
        {
            if (_Right > 0 && _Left > kLengthMax - _Right)
            {
                return false;
            }
            if (_Right < 0 && _Left < (std::numeric_limits<Length>::min)() - _Right)
            {
                return false;
            }
            _Result = _Left + _Right;
            return true;
        }

        void SaturatingAdd(Length& _Target, const Length _Value)
        {
            Length Result = 0;
            _Target = TryAdd(_Target, _Value, Result) ? Result : kLengthMax;
        }

        [[nodiscard]] Length VariantLength(const PartDemand& _Demand, const PartVariant& _Variant)
        {
            return _Variant.AxialLength > 0 ? _Variant.AxialLength : _Demand.NominalLength;
        }

        [[nodiscard]] Length VariantMaterialLength(
            const PartDemand& _Demand, const PartVariant& _Variant)
        {
            return _Variant.MaterialLength > 0
                ? _Variant.MaterialLength
                : VariantLength(_Demand, _Variant);
        }

        [[nodiscard]] bool CanCommonCut(const EndDescriptor& _Left, const EndDescriptor& _Right)
        {
            return _Left.AllowCommonCut &&
                   _Right.AllowCommonCut &&
                   !_Left.Signature.empty() &&
                   _Left.Signature == _Right.Signature;
        }

        [[nodiscard]] bool CanTrapezoidNest(
            const EndDescriptor& _Left, const EndDescriptor& _Right)
        {
            return _Left.AllowTrapezoidNesting &&
                   _Right.AllowTrapezoidNesting &&
                   _Left.NestingProjection > 0 &&
                   _Right.NestingProjection > 0 &&
                   !_Left.NestingPlane.empty() &&
                   _Left.NestingPlane == _Right.NestingPlane;
        }

        [[nodiscard]] bool TryTransitionGap(
            const PartVariant& _Previous,
            const PartVariant& _Next,
            const SolverSettings& _Settings,
            Length& _Gap,
            bool& _CommonCut)
        {
            _CommonCut = CanCommonCut(_Previous.RightEnd, _Next.LeftEnd);
            if (_CommonCut)
            {
                _Gap = _Settings.Kerf;
            }
            else
            {
                Length Allowance = 0;
                Length TwoKerfs = 0;
                if (!TryAdd(_Previous.RightEnd.SeparationAllowance,
                            _Next.LeftEnd.SeparationAllowance,
                            Allowance) ||
                    !TryAdd(_Settings.Kerf, _Settings.Kerf, TwoKerfs) ||
                    !TryAdd(_Settings.PartGap, Allowance, _Gap) ||
                    !TryAdd(_Gap, TwoKerfs, _Gap))
                {
                    return false;
                }
            }

            if (CanTrapezoidNest(_Previous.RightEnd, _Next.LeftEnd))
            {
                const Length Overlap = (std::min)(
                    _Previous.RightEnd.NestingProjection,
                    _Next.LeftEnd.NestingProjection);
                _Gap -= Overlap;
            }
            return true;
        }

        [[nodiscard]] std::int64_t EffectiveStockCost(const StockType& _Stock)
        {
            if (_Stock.CostUnits >= 0)
            {
                return _Stock.CostUnits;
            }
            return _Stock.Kind == StockKind::Standard ? _Stock.TotalLength : 0;
        }

        [[nodiscard]] BarMeasure MeasureBar(
            const WorkingBar& _Bar,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            BarMeasure Result;
            if (_Bar.StockIndex >= _Stocks.size() || _Bar.Placements.empty())
            {
                return Result;
            }

            const StockType& Stock = _Stocks[_Bar.StockIndex];
            if (Stock.MaxParts > 0 && _Bar.Placements.size() > Stock.MaxParts)
            {
                return Result;
            }

            std::set<std::string> Orders;
            Length Cursor = Stock.FrontMargin;
            Result.Starts.reserve(_Bar.Placements.size());
            Result.Ends.reserve(_Bar.Placements.size());
            Result.GapsBefore.reserve(_Bar.Placements.size());
            Result.CommonCutWithPrevious.reserve(_Bar.Placements.size());

            for (std::size_t Index = 0; Index < _Bar.Placements.size(); ++Index)
            {
                const WorkingPlacement& Placement = _Bar.Placements[Index];
                if (Placement.InstanceIndex >= _Instances.size())
                {
                    return Result;
                }
                const PartInstance& Instance = _Instances[Placement.InstanceIndex];
                if (Instance.DemandIndex >= _Demands.size())
                {
                    return Result;
                }
                const PartDemand& Demand = _Demands[Instance.DemandIndex];
                if (Demand.CompatibilityKey != Stock.CompatibilityKey ||
                    Placement.VariantIndex >= Demand.Variants.size())
                {
                    return Result;
                }
                const PartVariant& Variant = Demand.Variants[Placement.VariantIndex];

                Length Gap = 0;
                bool bCommonCut = false;
                if (Index > 0)
                {
                    const WorkingPlacement& PreviousPlacement = _Bar.Placements[Index - 1];
                    const PartInstance& PreviousInstance = _Instances[PreviousPlacement.InstanceIndex];
                    const PartDemand& PreviousDemand = _Demands[PreviousInstance.DemandIndex];
                    const PartVariant& PreviousVariant = PreviousDemand.Variants[PreviousPlacement.VariantIndex];
                    if (!TryTransitionGap(PreviousVariant, Variant, _Settings, Gap, bCommonCut))
                    {
                        return Result;
                    }
                    Result.CommonCutCount += bCommonCut ? 1U : 0U;
                    if (!PreviousDemand.OrderKey.empty() &&
                        !Demand.OrderKey.empty() &&
                        PreviousDemand.OrderKey != Demand.OrderKey)
                    {
                        ++Result.OrderChangeCount;
                    }
                }

                if (!Demand.OrderKey.empty())
                {
                    Orders.insert(Demand.OrderKey);
                }
                if (Stock.MaxDistinctOrders > 0 && Orders.size() > Stock.MaxDistinctOrders)
                {
                    return Result;
                }

                if (!TryAdd(Cursor, Gap, Cursor))
                {
                    return Result;
                }
                Result.GapsBefore.push_back(Gap);
                Result.CommonCutWithPrevious.push_back(bCommonCut);
                Result.Starts.push_back(Cursor);

                const Length PartLength = VariantLength(Demand, Variant);
                if (PartLength <= 0 || !TryAdd(Cursor, PartLength, Cursor))
                {
                    return Result;
                }
                Result.Ends.push_back(Cursor);
                SaturatingAdd(Result.PartLength, VariantMaterialLength(Demand, Variant));
            }

            // 末件必须从余料上分离，因此始终为最后一刀预留一条锯缝。
            if (!TryAdd(Cursor, _Settings.Kerf, Cursor))
            {
                return Result;
            }
            SaturatingAdd(Result.GapLength, _Settings.Kerf);

            if (Cursor > Stock.TotalLength)
            {
                return Result;
            }
            Result.ProcessedEnd = Cursor;
            Result.GapLength = Result.ProcessedEnd - Stock.FrontMargin - Result.PartLength;
            if (Result.GapLength < 0)
            {
                return Result;
            }
            Result.RemainingLength = Stock.TotalLength - Cursor;
            if (Result.RemainingLength < Stock.TailDeadZone)
            {
                return Result;
            }

            const bool bReusable = Stock.MinReusableRemainder > 0 &&
                                   Result.RemainingLength >= Stock.MinReusableRemainder;
            Result.ReusableRemainderLength = bReusable ? Result.RemainingLength : 0;
            Result.UnrecoverableWaste = Stock.FrontMargin;
            SaturatingAdd(Result.UnrecoverableWaste, Result.GapLength);
            if (!bReusable)
            {
                SaturatingAdd(Result.UnrecoverableWaste, Result.RemainingLength);
            }

            const std::size_t NonCommonAdjacencyCount =
                (_Bar.Placements.size() - 1) - Result.CommonCutCount;
            Result.CutCount = _Bar.Placements.size() + NonCommonAdjacencyCount;
            Result.CutCount += Stock.FrontMargin > 0 ? 1U : 0U;
            Result.Feasible = true;
            return Result;
        }

        [[nodiscard]] InternalMetrics MeasurePlan(
            const WorkingPlan& _Plan,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            InternalMetrics Result;
            for (const WorkingBar& Bar : _Plan)
            {
                const StockType& Stock = _Stocks[Bar.StockIndex];
                const BarMeasure Measure = MeasureBar(Bar, _Instances, _Demands, _Stocks, _Settings);
                if (!Measure.Feasible)
                {
                    Result.StockCost = kMetricMax;
                    Result.UnrecoverableWaste = kLengthMax;
                    return Result;
                }

                SaturatingAdd(Result.StockCost, EffectiveStockCost(Stock));
                Result.StandardStockCount += Stock.Kind == StockKind::Standard ? 1U : 0U;
                ++Result.OpenedStockCount;
                SaturatingAdd(Result.TotalStockLength, Stock.TotalLength);
                SaturatingAdd(Result.PartLength, Measure.PartLength);
                SaturatingAdd(Result.GapLength, Measure.GapLength);
                SaturatingAdd(Result.UnrecoverableWaste, Measure.UnrecoverableWaste);
                SaturatingAdd(Result.ReusableRemainderLength, Measure.ReusableRemainderLength);
                Result.ReusableRemainderCount += Measure.ReusableRemainderLength > 0 ? 1U : 0U;
                Result.CutCount += Measure.CutCount;
                Result.CommonCutCount += Measure.CommonCutCount;
                Result.OrderChangeCount += Measure.OrderChangeCount;
            }
            return Result;
        }

        [[nodiscard]] std::int64_t CountAsMetric(const std::size_t _Value)
        {
            return _Value > static_cast<std::size_t>(kMetricMax)
                       ? kMetricMax
                       : static_cast<std::int64_t>(_Value);
        }

        [[nodiscard]] std::int64_t ObjectiveValue(const InternalMetrics& _Metrics, const Objective _Objective)
        {
            switch (_Objective)
            {
            case Objective::StockCost:
                return _Metrics.StockCost;
            case Objective::StandardStockCount:
                return CountAsMetric(_Metrics.StandardStockCount);
            case Objective::UnrecoverableWaste:
                return _Metrics.UnrecoverableWaste;
            case Objective::OpenedStockCount:
                return CountAsMetric(_Metrics.OpenedStockCount);
            case Objective::ReusableRemainderCount:
                return CountAsMetric(_Metrics.ReusableRemainderCount);
            case Objective::CutCount:
                return CountAsMetric(_Metrics.CutCount);
            case Objective::OrderChangeCount:
                return CountAsMetric(_Metrics.OrderChangeCount);
            }
            return 0;
        }

        [[nodiscard]] int CompareMetrics(
            const InternalMetrics& _Left,
            const InternalMetrics& _Right,
            const SolverSettings& _Settings)
        {
            for (const Objective Current : _Settings.ObjectiveOrder)
            {
                const std::int64_t LeftValue = ObjectiveValue(_Left, Current);
                const std::int64_t RightValue = ObjectiveValue(_Right, Current);
                if (LeftValue < RightValue)
                {
                    return -1;
                }
                if (LeftValue > RightValue)
                {
                    return 1;
                }
            }
            return 0;
        }

        [[nodiscard]] std::string CanonicalPlanKey(
            const WorkingPlan& _Plan,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const bool _IgnoreCopyOrdinal)
        {
            std::vector<std::string> Bars;
            Bars.reserve(_Plan.size());
            for (const WorkingBar& Bar : _Plan)
            {
                std::ostringstream Stream;
                Stream << _Stocks[Bar.StockIndex].ID << ':';
                for (const WorkingPlacement& Placement : Bar.Placements)
                {
                    const PartInstance& Instance = _Instances[Placement.InstanceIndex];
                    const PartDemand& Demand = _Demands[Instance.DemandIndex];
                    Stream << Demand.ID;
                    if (!_IgnoreCopyOrdinal)
                    {
                        Stream << '#' << Instance.Ordinal;
                    }
                    Stream << '@' << Demand.Variants[Placement.VariantIndex].ID << ',';
                }
                Bars.push_back(Stream.str());
            }
            std::sort(Bars.begin(), Bars.end());
            std::ostringstream Result;
            for (const std::string& Bar : Bars)
            {
                Result << '[' << Bar << ']';
            }
            return Result.str();
        }

        [[nodiscard]] bool BetterPlan(
            const WorkingPlan& _Left,
            const WorkingPlan& _Right,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            const InternalMetrics LeftMetrics = MeasurePlan(_Left, _Instances, _Demands, _Stocks, _Settings);
            const InternalMetrics RightMetrics = MeasurePlan(_Right, _Instances, _Demands, _Stocks, _Settings);
            const int Compared = CompareMetrics(LeftMetrics, RightMetrics, _Settings);
            if (Compared != 0)
            {
                return Compared < 0;
            }
            return CanonicalPlanKey(_Left, _Instances, _Demands, _Stocks, false) <
                   CanonicalPlanKey(_Right, _Instances, _Demands, _Stocks, false);
        }

        [[nodiscard]] std::size_t UsedStockCount(const WorkingPlan& _Plan, const std::size_t _StockIndex)
        {
            return static_cast<std::size_t>(std::count_if(
                _Plan.begin(), _Plan.end(),
                [_StockIndex](const WorkingBar& _Bar) { return _Bar.StockIndex == _StockIndex; }));
        }

        [[nodiscard]] bool CanOpenStock(
            const WorkingPlan& _Plan,
            const StockType& _Stock,
            const std::size_t _StockIndex)
        {
            return _Stock.Quantity == 0 || UsedStockCount(_Plan, _StockIndex) < _Stock.Quantity;
        }

        template <typename Visitor>
        void EnumerateInsertions(
            const WorkingPlan& _Plan,
            const std::size_t _InstanceIndex,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings,
            Visitor&& _Visitor)
        {
            const PartDemand& Demand = _Demands[_Instances[_InstanceIndex].DemandIndex];
            for (std::size_t BarIndex = 0; BarIndex < _Plan.size(); ++BarIndex)
            {
                const WorkingBar& SourceBar = _Plan[BarIndex];
                if (_Stocks[SourceBar.StockIndex].CompatibilityKey != Demand.CompatibilityKey)
                {
                    continue;
                }
                for (std::size_t Position = 0; Position <= SourceBar.Placements.size(); ++Position)
                {
                    for (std::size_t VariantIndex = 0; VariantIndex < Demand.Variants.size(); ++VariantIndex)
                    {
                        WorkingPlan Candidate = _Plan;
                        Candidate[BarIndex].Placements.insert(
                            Candidate[BarIndex].Placements.begin() + static_cast<std::ptrdiff_t>(Position),
                            WorkingPlacement{_InstanceIndex, VariantIndex});
                        if (MeasureBar(Candidate[BarIndex], _Instances, _Demands, _Stocks, _Settings).Feasible)
                        {
                            _Visitor(std::move(Candidate));
                        }
                    }
                }
            }

            for (std::size_t StockIndex = 0; StockIndex < _Stocks.size(); ++StockIndex)
            {
                const StockType& Stock = _Stocks[StockIndex];
                if (Stock.CompatibilityKey != Demand.CompatibilityKey ||
                    !CanOpenStock(_Plan, Stock, StockIndex))
                {
                    continue;
                }
                for (std::size_t VariantIndex = 0; VariantIndex < Demand.Variants.size(); ++VariantIndex)
                {
                    WorkingBar Bar{StockIndex, {{_InstanceIndex, VariantIndex}}};
                    if (!MeasureBar(Bar, _Instances, _Demands, _Stocks, _Settings).Feasible)
                    {
                        continue;
                    }
                    WorkingPlan Candidate = _Plan;
                    Candidate.push_back(std::move(Bar));
                    _Visitor(std::move(Candidate));
                }
            }
        }

        [[nodiscard]] std::optional<WorkingPlan> BestInsertion(
            const WorkingPlan& _Plan,
            const std::size_t _InstanceIndex,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            std::optional<WorkingPlan> Best;
            EnumerateInsertions(
                _Plan, _InstanceIndex, _Instances, _Demands, _Stocks, _Settings,
                [&](WorkingPlan&& _Candidate)
                {
                    if (!Best || BetterPlan(_Candidate, *Best, _Instances, _Demands, _Stocks, _Settings))
                    {
                        Best = std::move(_Candidate);
                    }
                });
            return Best;
        }

        [[nodiscard]] std::optional<WorkingPlan> ConstructPlan(
            const std::vector<std::size_t>& _Order,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            WorkingPlan Result;
            for (const std::size_t InstanceIndex : _Order)
            {
                std::optional<WorkingPlan> Next = BestInsertion(
                    Result, InstanceIndex, _Instances, _Demands, _Stocks, _Settings);
                if (!Next)
                {
                    return std::nullopt;
                }
                Result = std::move(*Next);
            }
            return Result;
        }

        // When every adjacency consumes the same amount of material, a bar is a
        // one-dimensional cutting pattern.  Solving the whole pattern at once is
        // materially stronger than choosing the cheapest insertion for one part
        // at a time, especially when several standard stock lengths are offered.
        struct UniformPatternModel
        {
            Length TransitionGap = 0;
            bool CommonTransition = false;
            std::vector<Length> EffectiveLengths;
        };

        [[nodiscard]] std::optional<UniformPatternModel> BuildUniformPatternModel(
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const SolverSettings& _Settings)
        {
            UniformPatternModel Result;
            Result.EffectiveLengths.resize(_Instances.size());

            std::vector<std::size_t> DemandIndices;
            for (const PartInstance& Instance : _Instances)
            {
                const PartDemand& Demand = _Demands[Instance.DemandIndex];
                if (Demand.Variants.size() != 1)
                {
                    return std::nullopt;
                }
                if (std::find(DemandIndices.begin(), DemandIndices.end(), Instance.DemandIndex) ==
                    DemandIndices.end())
                {
                    DemandIndices.push_back(Instance.DemandIndex);
                }
            }

            bool bHasTransition = false;
            for (const std::size_t PreviousIndex : DemandIndices)
            {
                for (const std::size_t NextIndex : DemandIndices)
                {
                    const PartVariant& Previous = _Demands[PreviousIndex].Variants.front();
                    const PartVariant& Next = _Demands[NextIndex].Variants.front();
                    Length Gap = 0;
                    bool bCommon = false;
                    if (!TryTransitionGap(Previous, Next, _Settings, Gap, bCommon))
                    {
                        return std::nullopt;
                    }
                    if (!bHasTransition)
                    {
                        Result.TransitionGap = Gap;
                        Result.CommonTransition = bCommon;
                        bHasTransition = true;
                    }
                    else if (Result.TransitionGap != Gap || Result.CommonTransition != bCommon)
                    {
                        return std::nullopt;
                    }
                }
            }

            for (std::size_t InstanceIndex = 0; InstanceIndex < _Instances.size(); ++InstanceIndex)
            {
                const PartDemand& Demand = _Demands[_Instances[InstanceIndex].DemandIndex];
                if (!TryAdd(
                        VariantLength(Demand, Demand.Variants.front()),
                        Result.TransitionGap,
                        Result.EffectiveLengths[InstanceIndex]))
                {
                    return std::nullopt;
                }
            }
            return Result;
        }

        [[nodiscard]] std::optional<Length> UniformPatternCapacity(
            const StockType& _Stock,
            const UniformPatternModel& _Model,
            const SolverSettings& _Settings)
        {
            if (_Stock.MaxParts > 0 || _Stock.MaxDistinctOrders > 0)
            {
                return std::nullopt;
            }

            Length Reserved = 0;
            if (!TryAdd(_Stock.FrontMargin, _Stock.TailDeadZone, Reserved) ||
                Reserved >= _Stock.TotalLength)
            {
                return std::nullopt;
            }
            Length Capacity = _Stock.TotalLength - Reserved;
            if (Capacity < _Settings.Kerf)
            {
                return std::nullopt;
            }
            Capacity -= _Settings.Kerf;
            if (!TryAdd(Capacity, _Model.TransitionGap, Capacity))
            {
                return std::nullopt;
            }
            return Capacity;
        }

        struct UniformQualityLowerBound
        {
            std::int64_t StockCost = 0;
            std::size_t OpenedStockCount = 0;
        };

        [[nodiscard]] std::optional<UniformQualityLowerBound> CalculateUniformQualityLowerBound(
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            const std::optional<UniformPatternModel> Model =
                BuildUniformPatternModel(_Instances, _Demands, _Settings);
            if (!Model || _Instances.empty())
            {
                return std::nullopt;
            }

            Length TotalLoad = 0;
            for (const Length Effective : Model->EffectiveLengths)
            {
                if (!TryAdd(TotalLoad, Effective, TotalLoad))
                {
                    return std::nullopt;
                }
            }

            const std::string& CompatibilityKey =
                _Demands[_Instances.front().DemandIndex].CompatibilityKey;
            Length MaximumCapacity = 0;
            long double BestCostPerCapacity = (std::numeric_limits<long double>::max)();
            for (const StockType& Stock : _Stocks)
            {
                if (Stock.CompatibilityKey != CompatibilityKey)
                {
                    continue;
                }
                const std::optional<Length> Capacity =
                    UniformPatternCapacity(Stock, *Model, _Settings);
                if (!Capacity || *Capacity <= 0)
                {
                    continue;
                }
                MaximumCapacity = (std::max)(MaximumCapacity, *Capacity);
                const long double Ratio = static_cast<long double>(EffectiveStockCost(Stock)) /
                                          static_cast<long double>(*Capacity);
                BestCostPerCapacity = (std::min)(BestCostPerCapacity, Ratio);
            }
            if (MaximumCapacity <= 0 ||
                BestCostPerCapacity == (std::numeric_limits<long double>::max)())
            {
                return std::nullopt;
            }

            UniformQualityLowerBound Result;
            Result.OpenedStockCount = static_cast<std::size_t>(TotalLoad / MaximumCapacity);
            Result.OpenedStockCount += TotalLoad % MaximumCapacity == 0 ? 0U : 1U;
            const long double FractionalCost =
                static_cast<long double>(TotalLoad) * BestCostPerCapacity;
            if (FractionalCost > static_cast<long double>(kMetricMax))
            {
                Result.StockCost = kMetricMax;
            }
            else
            {
                Result.StockCost = static_cast<std::int64_t>(
                    std::ceil((std::max)(0.0L, FractionalCost - 1e-12L)));
            }
            return Result;
        }

        [[nodiscard]] std::vector<std::size_t> GreedyUniformPattern(
            const std::vector<std::size_t>& _Remaining,
            const Length _Capacity,
            const UniformPatternModel& _Model)
        {
            std::vector<std::size_t> Order = _Remaining;
            std::sort(Order.begin(), Order.end(), [&](const std::size_t _Left, const std::size_t _Right)
            {
                return std::tie(_Model.EffectiveLengths[_Left], _Left) >
                       std::tie(_Model.EffectiveLengths[_Right], _Right);
            });

            Length Used = 0;
            std::vector<std::size_t> Result;
            for (const std::size_t InstanceIndex : Order)
            {
                Length Candidate = 0;
                if (TryAdd(Used, _Model.EffectiveLengths[InstanceIndex], Candidate) &&
                    Candidate <= _Capacity)
                {
                    Used = Candidate;
                    Result.push_back(InstanceIndex);
                }
            }
            return Result;
        }

        [[nodiscard]] std::vector<std::size_t> BestUniformPattern(
            const std::vector<std::size_t>& _Remaining,
            const Length _Capacity,
            const UniformPatternModel& _Model)
        {
            if (_Remaining.empty() || _Capacity <= 0)
            {
                return {};
            }

            Length Scale = 0;
            for (const std::size_t InstanceIndex : _Remaining)
            {
                const Length Effective = _Model.EffectiveLengths[InstanceIndex];
                if (Effective > 0 && Effective <= _Capacity)
                {
                    Scale = Scale == 0 ? Effective : std::gcd(Scale, Effective);
                }
            }
            if (Scale <= 0)
            {
                return {};
            }

            const Length ScaledCapacityValue = _Capacity / Scale;
            constexpr std::uint64_t kMaximumKnapsackWordWork = 25'000'000;
            constexpr Length kMaximumScaledCapacity = 5'000'000;
            const std::uint64_t WordCount =
                static_cast<std::uint64_t>(ScaledCapacityValue / 64 + 1);
            if (ScaledCapacityValue > kMaximumScaledCapacity ||
                WordCount * _Remaining.size() > kMaximumKnapsackWordWork)
            {
                return GreedyUniformPattern(_Remaining, _Capacity, _Model);
            }

            const std::size_t Capacity = static_cast<std::size_t>(ScaledCapacityValue);
            const std::size_t BitWordCount = Capacity / 64 + 1;
            const std::size_t NoParent = (std::numeric_limits<std::size_t>::max)();
            std::vector<std::uint64_t> Reachable(BitWordCount, 0);
            std::vector<std::size_t> PreviousSum(Capacity + 1, NoParent);
            std::vector<std::size_t> ParentInstance(Capacity + 1, NoParent);
            Reachable[0] = 1U;

            for (const std::size_t InstanceIndex : _Remaining)
            {
                const Length Effective = _Model.EffectiveLengths[InstanceIndex];
                if (Effective <= 0 || Effective > _Capacity)
                {
                    continue;
                }
                const std::size_t Weight = static_cast<std::size_t>(Effective / Scale);
                const std::size_t WordShift = Weight / 64;
                const unsigned BitShift = static_cast<unsigned>(Weight % 64);
                for (std::size_t TargetWord = BitWordCount; TargetWord-- > WordShift;)
                {
                    std::uint64_t Shifted = Reachable[TargetWord - WordShift] << BitShift;
                    if (BitShift != 0 && TargetWord > WordShift)
                    {
                        Shifted |= Reachable[TargetWord - WordShift - 1] >> (64U - BitShift);
                    }
                    if (TargetWord + 1 == BitWordCount && Capacity % 64 != 63)
                    {
                        Shifted &= (std::uint64_t{1} << (Capacity % 64 + 1)) - 1U;
                    }
                    std::uint64_t NewlyReachable = Shifted & ~Reachable[TargetWord];
                    Reachable[TargetWord] |= Shifted;
                    while (NewlyReachable != 0)
                    {
                        const unsigned Bit = std::countr_zero(NewlyReachable);
                        const std::size_t Sum = TargetWord * 64 + Bit;
                        if (Sum <= Capacity)
                        {
                            PreviousSum[Sum] = Sum - Weight;
                            ParentInstance[Sum] = InstanceIndex;
                        }
                        NewlyReachable &= NewlyReachable - 1U;
                    }
                }
            }

            std::size_t BestSum = Capacity;
            while (BestSum > 0 &&
                   (Reachable[BestSum / 64] & (std::uint64_t{1} << (BestSum % 64))) == 0)
            {
                --BestSum;
            }
            if (BestSum == 0)
            {
                return {};
            }

            std::vector<std::size_t> Result;
            for (std::size_t Sum = BestSum; Sum > 0; Sum = PreviousSum[Sum])
            {
                if (ParentInstance[Sum] == NoParent || PreviousSum[Sum] >= Sum)
                {
                    return GreedyUniformPattern(_Remaining, _Capacity, _Model);
                }
                Result.push_back(ParentInstance[Sum]);
            }
            std::reverse(Result.begin(), Result.end());
            return Result;
        }

        struct UniformBarChoice
        {
            WorkingBar Bar;
            BarMeasure Measure;
            long double FillRatio = 0.0L;
            long double CostPerPartLength = 0.0L;
            Length FreeUsableLength = 0;
        };

        [[nodiscard]] bool BetterUniformBarChoice(
            const UniformBarChoice& _Left,
            const UniformBarChoice& _Right,
            const std::size_t _Strategy,
            const std::vector<StockType>& _Stocks)
        {
            if (_Strategy == 0 && _Left.FillRatio != _Right.FillRatio)
            {
                return _Left.FillRatio > _Right.FillRatio;
            }
            if (_Strategy == 1 && _Left.FreeUsableLength != _Right.FreeUsableLength)
            {
                return _Left.FreeUsableLength < _Right.FreeUsableLength;
            }
            if (_Strategy == 2 && _Left.CostPerPartLength != _Right.CostPerPartLength)
            {
                return _Left.CostPerPartLength < _Right.CostPerPartLength;
            }
            if (_Strategy == 3 && _Left.Bar.Placements.size() != _Right.Bar.Placements.size())
            {
                return _Left.Bar.Placements.size() > _Right.Bar.Placements.size();
            }
            if (_Left.FillRatio != _Right.FillRatio)
            {
                return _Left.FillRatio > _Right.FillRatio;
            }
            if (_Left.Measure.PartLength != _Right.Measure.PartLength)
            {
                return _Left.Measure.PartLength > _Right.Measure.PartLength;
            }
            const StockType& LeftStock = _Stocks[_Left.Bar.StockIndex];
            const StockType& RightStock = _Stocks[_Right.Bar.StockIndex];
            return std::tie(LeftStock.ID, _Left.Bar.StockIndex) <
                   std::tie(RightStock.ID, _Right.Bar.StockIndex);
        }

        [[nodiscard]] std::optional<WorkingPlan> ConstructUniformPatternPlan(
            const std::vector<std::size_t>& _Order,
            const UniformPatternModel& _Model,
            const std::size_t _Strategy,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            WorkingPlan Result;
            std::vector<std::size_t> Remaining = _Order;
            while (!Remaining.empty())
            {
                std::optional<UniformBarChoice> Best;
                for (std::size_t StockIndex = 0; StockIndex < _Stocks.size(); ++StockIndex)
                {
                    const StockType& Stock = _Stocks[StockIndex];
                    const PartDemand& FirstDemand =
                        _Demands[_Instances[Remaining.front()].DemandIndex];
                    if (Stock.CompatibilityKey != FirstDemand.CompatibilityKey ||
                        !CanOpenStock(Result, Stock, StockIndex))
                    {
                        continue;
                    }
                    const std::optional<Length> Capacity =
                        UniformPatternCapacity(Stock, _Model, _Settings);
                    if (!Capacity)
                    {
                        continue;
                    }

                    const std::vector<std::size_t> Selected =
                        BestUniformPattern(Remaining, *Capacity, _Model);
                    if (Selected.empty())
                    {
                        continue;
                    }

                    WorkingBar Bar;
                    Bar.StockIndex = StockIndex;
                    for (const std::size_t InstanceIndex : Selected)
                    {
                        Bar.Placements.push_back(WorkingPlacement{InstanceIndex, 0});
                    }
                    const BarMeasure Measure =
                        MeasureBar(Bar, _Instances, _Demands, _Stocks, _Settings);
                    if (!Measure.Feasible)
                    {
                        continue;
                    }

                    const Length UsableLength =
                        Stock.TotalLength - Stock.FrontMargin - Stock.TailDeadZone;
                    const Length FreeUsableLength = Measure.RemainingLength - Stock.TailDeadZone;
                    const long double FillRatio = UsableLength > 0
                        ? 1.0L - static_cast<long double>(FreeUsableLength) /
                                     static_cast<long double>(UsableLength)
                        : 0.0L;
                    const long double CostPerPartLength = Measure.PartLength > 0
                        ? static_cast<long double>(EffectiveStockCost(Stock)) /
                              static_cast<long double>(Measure.PartLength)
                        : (std::numeric_limits<long double>::max)();
                    UniformBarChoice Choice{
                        std::move(Bar), Measure, FillRatio, CostPerPartLength, FreeUsableLength};
                    if (!Best || BetterUniformBarChoice(Choice, *Best, _Strategy, _Stocks))
                    {
                        Best = std::move(Choice);
                    }
                }

                if (!Best)
                {
                    return std::nullopt;
                }

                std::vector<bool> IsSelected(_Instances.size(), false);
                for (const WorkingPlacement& Placement : Best->Bar.Placements)
                {
                    IsSelected[Placement.InstanceIndex] = true;
                }
                Remaining.erase(
                    std::remove_if(
                        Remaining.begin(), Remaining.end(),
                        [&](const std::size_t _InstanceIndex)
                        {
                            return IsSelected[_InstanceIndex];
                        }),
                    Remaining.end());
                Result.push_back(std::move(Best->Bar));
            }
            return Result;
        }

        void ImproveOrientationsAndOrder(
            WorkingPlan& _Plan,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            bool bChanged = true;
            while (bChanged)
            {
                bChanged = false;
                for (std::size_t BarIndex = 0; BarIndex < _Plan.size() && !bChanged; ++BarIndex)
                {
                    for (std::size_t Position = 0;
                         Position < _Plan[BarIndex].Placements.size() && !bChanged;
                         ++Position)
                    {
                        const WorkingPlacement Current = _Plan[BarIndex].Placements[Position];
                        const PartDemand& Demand = _Demands[_Instances[Current.InstanceIndex].DemandIndex];
                        for (std::size_t VariantIndex = 0; VariantIndex < Demand.Variants.size(); ++VariantIndex)
                        {
                            if (VariantIndex == Current.VariantIndex)
                            {
                                continue;
                            }
                            WorkingPlan Candidate = _Plan;
                            Candidate[BarIndex].Placements[Position].VariantIndex = VariantIndex;
                            if (MeasureBar(Candidate[BarIndex], _Instances, _Demands, _Stocks, _Settings).Feasible &&
                                BetterPlan(Candidate, _Plan, _Instances, _Demands, _Stocks, _Settings))
                            {
                                _Plan = std::move(Candidate);
                                bChanged = true;
                                break;
                            }
                        }
                    }
                }

                for (std::size_t BarIndex = 0; BarIndex < _Plan.size() && !bChanged; ++BarIndex)
                {
                    for (std::size_t Left = 0; Left < _Plan[BarIndex].Placements.size() && !bChanged; ++Left)
                    {
                        for (std::size_t Right = Left + 1; Right < _Plan[BarIndex].Placements.size(); ++Right)
                        {
                            WorkingPlan Candidate = _Plan;
                            std::swap(Candidate[BarIndex].Placements[Left], Candidate[BarIndex].Placements[Right]);
                            if (MeasureBar(Candidate[BarIndex], _Instances, _Demands, _Stocks, _Settings).Feasible &&
                                BetterPlan(Candidate, _Plan, _Instances, _Demands, _Stocks, _Settings))
                            {
                                _Plan = std::move(Candidate);
                                bChanged = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        void ImproveByRelocation(
            WorkingPlan& _Plan,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            for (std::size_t Pass = 0; Pass < _Settings.LocalSearchPasses; ++Pass)
            {
                bool bChanged = false;
                ImproveOrientationsAndOrder(_Plan, _Instances, _Demands, _Stocks, _Settings);

                for (std::size_t BarIndex = 0; BarIndex < _Plan.size() && !bChanged; ++BarIndex)
                {
                    for (std::size_t Position = 0;
                         Position < _Plan[BarIndex].Placements.size() && !bChanged;
                         ++Position)
                    {
                        const std::size_t InstanceIndex = _Plan[BarIndex].Placements[Position].InstanceIndex;
                        WorkingPlan Base = _Plan;
                        Base[BarIndex].Placements.erase(
                            Base[BarIndex].Placements.begin() + static_cast<std::ptrdiff_t>(Position));
                        if (Base[BarIndex].Placements.empty())
                        {
                            Base.erase(Base.begin() + static_cast<std::ptrdiff_t>(BarIndex));
                        }

                        std::optional<WorkingPlan> Candidate = BestInsertion(
                            Base, InstanceIndex, _Instances, _Demands, _Stocks, _Settings);
                        if (Candidate && BetterPlan(*Candidate, _Plan, _Instances, _Demands, _Stocks, _Settings))
                        {
                            _Plan = std::move(*Candidate);
                            bChanged = true;
                        }
                    }
                }

                if (!bChanged)
                {
                    break;
                }
            }
        }

        void ImproveByLargeNeighborhood(
            WorkingPlan& _Best,
            std::mt19937& _Random,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            if (_Best.size() < 2)
            {
                return;
            }

            for (std::size_t Iteration = 0; Iteration < _Settings.LargeNeighborhoodIterations; ++Iteration)
            {
                WorkingPlan Candidate = _Best;
                std::size_t FirstBar = 0;
                if (Iteration % 3 == 0)
                {
                    Length WorstWaste = -1;
                    for (std::size_t Index = 0; Index < Candidate.size(); ++Index)
                    {
                        const BarMeasure Measure = MeasureBar(
                            Candidate[Index], _Instances, _Demands, _Stocks, _Settings);
                        if (Measure.UnrecoverableWaste > WorstWaste)
                        {
                            WorstWaste = Measure.UnrecoverableWaste;
                            FirstBar = Index;
                        }
                    }
                }
                else
                {
                    FirstBar = std::uniform_int_distribution<std::size_t>(0, Candidate.size() - 1)(_Random);
                }

                std::vector<std::size_t> Removed;
                for (const WorkingPlacement& Placement : Candidate[FirstBar].Placements)
                {
                    Removed.push_back(Placement.InstanceIndex);
                }
                Candidate.erase(Candidate.begin() + static_cast<std::ptrdiff_t>(FirstBar));

                if (Candidate.size() > 1 && Iteration % 4 == 0)
                {
                    const std::size_t SecondBar =
                        std::uniform_int_distribution<std::size_t>(0, Candidate.size() - 1)(_Random);
                    for (const WorkingPlacement& Placement : Candidate[SecondBar].Placements)
                    {
                        Removed.push_back(Placement.InstanceIndex);
                    }
                    Candidate.erase(Candidate.begin() + static_cast<std::ptrdiff_t>(SecondBar));
                }

                if (Iteration % 2 == 0)
                {
                    std::shuffle(Removed.begin(), Removed.end(), _Random);
                }
                else
                {
                    std::sort(Removed.begin(), Removed.end(), [&](const std::size_t _Left, const std::size_t _Right)
                    {
                        const PartDemand& LeftDemand = _Demands[_Instances[_Left].DemandIndex];
                        const PartDemand& RightDemand = _Demands[_Instances[_Right].DemandIndex];
                        const Length LeftLength = VariantLength(LeftDemand, LeftDemand.Variants.front());
                        const Length RightLength = VariantLength(RightDemand, RightDemand.Variants.front());
                        return std::tie(LeftLength, LeftDemand.ID) > std::tie(RightLength, RightDemand.ID);
                    });
                }

                bool bFeasible = true;
                for (const std::size_t InstanceIndex : Removed)
                {
                    std::optional<WorkingPlan> Next = BestInsertion(
                        Candidate, InstanceIndex, _Instances, _Demands, _Stocks, _Settings);
                    if (!Next)
                    {
                        bFeasible = false;
                        break;
                    }
                    Candidate = std::move(*Next);
                }

                if (bFeasible)
                {
                    ImproveOrientationsAndOrder(Candidate, _Instances, _Demands, _Stocks, _Settings);
                    if (BetterPlan(Candidate, _Best, _Instances, _Demands, _Stocks, _Settings))
                    {
                        _Best = std::move(Candidate);
                    }
                }
            }
        }

        struct SequenceState
        {
            Length OccupiedLength = kLengthMax;
            std::uint32_t PreviousMask = 0;
            std::uint16_t PreviousGlobalVariant = (std::numeric_limits<std::uint16_t>::max)();
            std::uint16_t NonCommonAdjacencies = 0;
            std::uint16_t OrderChanges = 0;
        };

        [[nodiscard]] bool BetterSequenceState(
            const Length _OccupiedLength,
            const std::uint16_t _NonCommonAdjacencies,
            const std::uint16_t _OrderChanges,
            const SequenceState& _Current)
        {
            return std::tie(_OccupiedLength, _NonCommonAdjacencies, _OrderChanges) <
                   std::tie(
                       _Current.OccupiedLength,
                       _Current.NonCommonAdjacencies,
                       _Current.OrderChanges);
        }

        // Exactly optimises the sequence and enumerated orientation of the parts
        // already assigned to one bar. This closes an important gap between the
        // multi-stock heuristic and the exact single-stock subproblem used by
        // industrial bar-nesting methods.
        void ImproveBarByExactSequence(
            WorkingPlan& _Plan,
            const std::size_t _BarIndex,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            constexpr std::size_t kMaximumSequenceParts = 16;
            constexpr std::size_t kMaximumSequenceStates = 3'000'000;
            const WorkingBar& Source = _Plan[_BarIndex];
            const std::size_t PartCount = Source.Placements.size();
            if (PartCount < 2 || PartCount > kMaximumSequenceParts)
            {
                return;
            }

            std::vector<std::size_t> VariantOffsets(PartCount + 1, 0);
            std::vector<std::size_t> VariantOwners;
            std::vector<std::size_t> VariantIndices;
            for (std::size_t Position = 0; Position < PartCount; ++Position)
            {
                const PartDemand& Demand =
                    _Demands[_Instances[Source.Placements[Position].InstanceIndex].DemandIndex];
                VariantOffsets[Position] = VariantOwners.size();
                for (std::size_t VariantIndex = 0; VariantIndex < Demand.Variants.size(); ++VariantIndex)
                {
                    VariantOwners.push_back(Position);
                    VariantIndices.push_back(VariantIndex);
                }
            }
            VariantOffsets[PartCount] = VariantOwners.size();
            const std::size_t VariantCount = VariantOwners.size();
            const std::size_t MaskCount = std::size_t{1} << PartCount;
            if (VariantCount == 0 || VariantCount > (std::numeric_limits<std::uint16_t>::max)() ||
                MaskCount > kMaximumSequenceStates / VariantCount)
            {
                return;
            }

            std::vector<SequenceState> States(MaskCount * VariantCount);
            const auto StateAt = [&](const std::size_t _Mask, const std::size_t _GlobalVariant)
                -> SequenceState&
            {
                return States[_Mask * VariantCount + _GlobalVariant];
            };

            for (std::size_t GlobalVariant = 0; GlobalVariant < VariantCount; ++GlobalVariant)
            {
                const std::size_t Position = VariantOwners[GlobalVariant];
                const WorkingPlacement& Placement = Source.Placements[Position];
                const PartDemand& Demand = _Demands[_Instances[Placement.InstanceIndex].DemandIndex];
                SequenceState& State = StateAt(std::size_t{1} << Position, GlobalVariant);
                State.OccupiedLength = VariantLength(Demand, Demand.Variants[VariantIndices[GlobalVariant]]);
            }

            for (std::size_t Mask = 1; Mask < MaskCount; ++Mask)
            {
                for (std::size_t PreviousGlobal = 0; PreviousGlobal < VariantCount; ++PreviousGlobal)
                {
                    const std::size_t PreviousPosition = VariantOwners[PreviousGlobal];
                    if ((Mask & (std::size_t{1} << PreviousPosition)) == 0)
                    {
                        continue;
                    }
                    const SequenceState PreviousState = StateAt(Mask, PreviousGlobal);
                    if (PreviousState.OccupiedLength == kLengthMax)
                    {
                        continue;
                    }

                    const WorkingPlacement& PreviousPlacement = Source.Placements[PreviousPosition];
                    const PartDemand& PreviousDemand =
                        _Demands[_Instances[PreviousPlacement.InstanceIndex].DemandIndex];
                    const PartVariant& PreviousVariant =
                        PreviousDemand.Variants[VariantIndices[PreviousGlobal]];

                    for (std::size_t NextPosition = 0; NextPosition < PartCount; ++NextPosition)
                    {
                        const std::size_t NextBit = std::size_t{1} << NextPosition;
                        if ((Mask & NextBit) != 0)
                        {
                            continue;
                        }
                        const WorkingPlacement& NextPlacement = Source.Placements[NextPosition];
                        const PartDemand& NextDemand =
                            _Demands[_Instances[NextPlacement.InstanceIndex].DemandIndex];
                        for (std::size_t NextGlobal = VariantOffsets[NextPosition];
                             NextGlobal < VariantOffsets[NextPosition + 1];
                             ++NextGlobal)
                        {
                            const PartVariant& NextVariant =
                                NextDemand.Variants[VariantIndices[NextGlobal]];
                            Length Gap = 0;
                            bool bCommon = false;
                            if (!TryTransitionGap(
                                    PreviousVariant, NextVariant, _Settings, Gap, bCommon))
                            {
                                continue;
                            }
                            Length Occupied = 0;
                            if (!TryAdd(PreviousState.OccupiedLength, Gap, Occupied) ||
                                !TryAdd(Occupied, VariantLength(NextDemand, NextVariant), Occupied))
                            {
                                continue;
                            }
                            const std::uint16_t NonCommon = static_cast<std::uint16_t>(
                                PreviousState.NonCommonAdjacencies + (bCommon ? 0U : 1U));
                            const bool bOrderChange = !PreviousDemand.OrderKey.empty() &&
                                                      !NextDemand.OrderKey.empty() &&
                                                      PreviousDemand.OrderKey != NextDemand.OrderKey;
                            const std::uint16_t OrderChanges = static_cast<std::uint16_t>(
                                PreviousState.OrderChanges + (bOrderChange ? 1U : 0U));
                            SequenceState& NextState = StateAt(Mask | NextBit, NextGlobal);
                            if (!BetterSequenceState(Occupied, NonCommon, OrderChanges, NextState))
                            {
                                continue;
                            }
                            NextState.OccupiedLength = Occupied;
                            NextState.PreviousMask = static_cast<std::uint32_t>(Mask);
                            NextState.PreviousGlobalVariant = static_cast<std::uint16_t>(PreviousGlobal);
                            NextState.NonCommonAdjacencies = NonCommon;
                            NextState.OrderChanges = OrderChanges;
                        }
                    }
                }
            }

            const std::size_t CompleteMask = MaskCount - 1;
            WorkingPlan Best = _Plan;
            for (std::size_t LastGlobal = 0; LastGlobal < VariantCount; ++LastGlobal)
            {
                if (StateAt(CompleteMask, LastGlobal).OccupiedLength == kLengthMax)
                {
                    continue;
                }
                std::vector<WorkingPlacement> ReversedPlacements;
                ReversedPlacements.reserve(PartCount);
                std::size_t Mask = CompleteMask;
                std::size_t GlobalVariant = LastGlobal;
                while (Mask != 0)
                {
                    const std::size_t Position = VariantOwners[GlobalVariant];
                    ReversedPlacements.push_back(WorkingPlacement{
                        Source.Placements[Position].InstanceIndex,
                        VariantIndices[GlobalVariant]});
                    const SequenceState& State = StateAt(Mask, GlobalVariant);
                    Mask = State.PreviousMask;
                    if (Mask == 0)
                    {
                        break;
                    }
                    GlobalVariant = State.PreviousGlobalVariant;
                }
                std::reverse(ReversedPlacements.begin(), ReversedPlacements.end());
                if (ReversedPlacements.size() != PartCount)
                {
                    continue;
                }

                WorkingPlan Candidate = _Plan;
                Candidate[_BarIndex].Placements = std::move(ReversedPlacements);
                if (MeasureBar(
                        Candidate[_BarIndex], _Instances, _Demands, _Stocks, _Settings).Feasible &&
                    BetterPlan(Candidate, Best, _Instances, _Demands, _Stocks, _Settings))
                {
                    Best = std::move(Candidate);
                }
            }
            _Plan = std::move(Best);
        }

        void ImproveByExactBarSequencing(
            WorkingPlan& _Plan,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            for (std::size_t BarIndex = 0; BarIndex < _Plan.size(); ++BarIndex)
            {
                ImproveBarByExactSequence(
                    _Plan, BarIndex, _Instances, _Demands, _Stocks, _Settings);
            }
        }

        [[nodiscard]] bool IsMonotoneObjective(const Objective _Objective)
        {
            return _Objective == Objective::StockCost ||
                   _Objective == Objective::StandardStockCount ||
                   _Objective == Objective::OpenedStockCount ||
                   _Objective == Objective::CutCount;
        }

        [[nodiscard]] bool CannotBeatByMonotonePrefix(
            const InternalMetrics& _Current,
            const InternalMetrics& _Best,
            const SolverSettings& _Settings)
        {
            for (const Objective Current : _Settings.ObjectiveOrder)
            {
                if (!IsMonotoneObjective(Current))
                {
                    return false;
                }
                const std::int64_t CurrentValue = ObjectiveValue(_Current, Current);
                const std::int64_t BestValue = ObjectiveValue(_Best, Current);
                if (CurrentValue < BestValue)
                {
                    return false;
                }
                if (CurrentValue > BestValue)
                {
                    return true;
                }
            }
            return false;
        }

        struct ExactContext
        {
            const std::vector<PartInstance>& Instances;
            const std::vector<PartDemand>& Demands;
            const std::vector<StockType>& Stocks;
            const SolverSettings& Settings;
            std::vector<std::size_t> Order;
            std::optional<WorkingPlan> Best;
            std::vector<std::unordered_set<std::string>> Seen;
            std::uint64_t Nodes = 0;
            bool Aborted = false;

            void Search(const std::size_t _Depth, const WorkingPlan& _Current)
            {
                if (Aborted)
                {
                    return;
                }
                ++Nodes;
                if (Nodes > Settings.ExactNodeLimit)
                {
                    Aborted = true;
                    return;
                }

                if (Best)
                {
                    const InternalMetrics CurrentMetrics = MeasurePlan(
                        _Current, Instances, Demands, Stocks, Settings);
                    const InternalMetrics BestMetrics = MeasurePlan(
                        *Best, Instances, Demands, Stocks, Settings);
                    if (CannotBeatByMonotonePrefix(CurrentMetrics, BestMetrics, Settings))
                    {
                        return;
                    }
                }

                if (_Depth == Order.size())
                {
                    if (!Best || BetterPlan(_Current, *Best, Instances, Demands, Stocks, Settings))
                    {
                        Best = _Current;
                    }
                    return;
                }

                const std::string StateKey = CanonicalPlanKey(
                    _Current, Instances, Demands, Stocks, true);
                if (!Seen[_Depth].insert(StateKey).second)
                {
                    return;
                }

                std::vector<WorkingPlan> Candidates;
                std::unordered_set<std::string> CandidateKeys;
                EnumerateInsertions(
                    _Current, Order[_Depth], Instances, Demands, Stocks, Settings,
                    [&](WorkingPlan&& _Candidate)
                    {
                        const std::string Key = CanonicalPlanKey(
                            _Candidate, Instances, Demands, Stocks, true);
                        if (CandidateKeys.insert(Key).second)
                        {
                            Candidates.push_back(std::move(_Candidate));
                        }
                    });

                std::sort(Candidates.begin(), Candidates.end(), [&](const WorkingPlan& _Left, const WorkingPlan& _Right)
                {
                    return BetterPlan(_Left, _Right, Instances, Demands, Stocks, Settings);
                });
                for (const WorkingPlan& Candidate : Candidates)
                {
                    Search(_Depth + 1, Candidate);
                    if (Aborted)
                    {
                        return;
                    }
                }
            }
        };

        [[nodiscard]] GroupOutcome SolveGroup(
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings,
            std::mt19937& _Random)
        {
            GroupOutcome Result;
            std::vector<std::size_t> BaseOrder(_Instances.size());
            std::iota(BaseOrder.begin(), BaseOrder.end(), 0);
            std::sort(BaseOrder.begin(), BaseOrder.end(), [&](const std::size_t _Left, const std::size_t _Right)
            {
                const PartDemand& LeftDemand = _Demands[_Instances[_Left].DemandIndex];
                const PartDemand& RightDemand = _Demands[_Instances[_Right].DemandIndex];
                const Length LeftLength = VariantLength(LeftDemand, LeftDemand.Variants.front());
                const Length RightLength = VariantLength(RightDemand, RightDemand.Variants.front());
                return std::tie(LeftLength, LeftDemand.ID, _Instances[_Left].Ordinal) >
                       std::tie(RightLength, RightDemand.ID, _Instances[_Right].Ordinal);
            });

            if (const std::optional<UniformPatternModel> PatternModel =
                    BuildUniformPatternModel(_Instances, _Demands, _Settings))
            {
                // Different selectors deliberately explore dense fill, smallest
                // leftover, material cost efficiency, and maximum part count.
                // The complete plans are still compared by the public
                // lexicographic objective, so this only broadens the incumbent set.
                std::size_t CompatibleStockCount = 0;
                const std::string& CompatibilityKey =
                    _Demands[_Instances.front().DemandIndex].CompatibilityKey;
                for (const StockType& Stock : _Stocks)
                {
                    CompatibleStockCount +=
                        Stock.CompatibilityKey == CompatibilityKey &&
                        UniformPatternCapacity(Stock, *PatternModel, _Settings).has_value()
                            ? 1U
                            : 0U;
                }
                const std::size_t StrategyCount = CompatibleStockCount > 1 ? 4U : 1U;
                for (std::size_t Strategy = 0; Strategy < StrategyCount; ++Strategy)
                {
                    std::optional<WorkingPlan> Candidate = ConstructUniformPatternPlan(
                        BaseOrder,
                        *PatternModel,
                        Strategy,
                        _Instances,
                        _Demands,
                        _Stocks,
                        _Settings);
                    if (!Candidate)
                    {
                        continue;
                    }
                    if (!Result.Plan ||
                        BetterPlan(*Candidate, *Result.Plan, _Instances, _Demands, _Stocks, _Settings))
                    {
                        Result.Plan = std::move(*Candidate);
                    }
                }

                // A matching continuous lower bound is a real optimality
                // certificate for the common straight-cut case when every
                // remaining objective is fixed by stock count and total load.
                // Stop here instead of running the much more expensive generic
                // insertion/LNS path over an already-proven cutting pattern.
                if (Result.Plan)
                {
                    const auto Bound = CalculateUniformQualityLowerBound(
                        _Instances, _Demands, _Stocks, _Settings);
                    const auto Metrics = MeasurePlan(
                        *Result.Plan, _Instances, _Demands, _Stocks, _Settings);
                    std::optional<StockKind> Kind;
                    bool bUniformKind = true;
                    bool bNoReusableRemainderThreshold = true;
                    bool bDefaultStandardCost = true;
                    std::size_t CompatibleTypeCount = 0;
                    std::optional<std::string> OrderKey;
                    bool bUniformOrder = true;
                    for (const StockType& Stock : _Stocks)
                    {
                        if (Stock.CompatibilityKey != CompatibilityKey) continue;
                        ++CompatibleTypeCount;
                        if (Kind && *Kind != Stock.Kind) bUniformKind = false;
                        else Kind = Stock.Kind;
                        bNoReusableRemainderThreshold = bNoReusableRemainderThreshold
                            && Stock.MinReusableRemainder == 0;
                        bDefaultStandardCost = bDefaultStandardCost
                            && Stock.Kind == StockKind::Standard && Stock.CostUnits < 0;
                    }
                    for (const PartInstance& Instance : _Instances)
                    {
                        const auto& Current = _Demands[Instance.DemandIndex].OrderKey;
                        if (OrderKey && *OrderKey != Current) bUniformOrder = false;
                        else OrderKey = Current;
                    }
                    const bool bCostCertified = Bound
                        && (Metrics.StockCost == Bound->StockCost
                            || (CompatibleTypeCount == 1
                                && Metrics.OpenedStockCount == Bound->OpenedStockCount));
                    const bool bWasteFixedByCertifiedObjectives =
                        CompatibleTypeCount == 1 || bDefaultStandardCost;
                    if (Bound && bCostCertified && bWasteFixedByCertifiedObjectives && bUniformKind
                        && bNoReusableRemainderThreshold && bUniformOrder
                        && Metrics.OpenedStockCount == Bound->OpenedStockCount)
                    {
                        Result.Metrics = Metrics;
                        Result.ExactCompleted = true;
                        return Result;
                    }
                }
            }

            const std::size_t Runs = (std::max)(std::size_t{1}, _Settings.ConstructionRuns);
            for (std::size_t Run = 0; Run < Runs; ++Run)
            {
                std::vector<std::size_t> Order = BaseOrder;
                if (Run == 1)
                {
                    std::stable_sort(Order.begin(), Order.end(), [&](const std::size_t _Left, const std::size_t _Right)
                    {
                        const PartDemand& LeftDemand = _Demands[_Instances[_Left].DemandIndex];
                        const PartDemand& RightDemand = _Demands[_Instances[_Right].DemandIndex];
                        return std::tuple{LeftDemand.Variants.size(), LeftDemand.ID} <
                               std::tuple{RightDemand.Variants.size(), RightDemand.ID};
                    });
                }
                else if (Run == 2)
                {
                    std::stable_sort(Order.begin(), Order.end(), [&](const std::size_t _Left, const std::size_t _Right)
                    {
                        const PartDemand& LeftDemand = _Demands[_Instances[_Left].DemandIndex];
                        const PartDemand& RightDemand = _Demands[_Instances[_Right].DemandIndex];
                        return std::tie(LeftDemand.OrderKey, LeftDemand.ID) <
                               std::tie(RightDemand.OrderKey, RightDemand.ID);
                    });
                }
                else if (Run >= 3)
                {
                    std::shuffle(Order.begin(), Order.end(), _Random);
                }

                std::optional<WorkingPlan> Candidate = ConstructPlan(
                    Order, _Instances, _Demands, _Stocks, _Settings);
                if (!Candidate)
                {
                    continue;
                }
                ImproveByRelocation(*Candidate, _Instances, _Demands, _Stocks, _Settings);
                if (!Result.Plan || BetterPlan(*Candidate, *Result.Plan, _Instances, _Demands, _Stocks, _Settings))
                {
                    Result.Plan = std::move(*Candidate);
                }
            }

            if (Result.Plan)
            {
                ImproveByLargeNeighborhood(
                    *Result.Plan, _Random, _Instances, _Demands, _Stocks, _Settings);
                ImproveByExactBarSequencing(
                    *Result.Plan, _Instances, _Demands, _Stocks, _Settings);
            }

            const bool bRunExact = _Settings.ExactNodeLimit > 0 &&
                                   (_Instances.size() <= _Settings.ExactPartLimit || !Result.Plan);
            if (bRunExact)
            {
                ExactContext Context{
                    _Instances,
                    _Demands,
                    _Stocks,
                    _Settings,
                    BaseOrder,
                    Result.Plan,
                    std::vector<std::unordered_set<std::string>>(_Instances.size() + 1),
                };
                Context.Search(0, {});
                Result.Plan = std::move(Context.Best);
                Result.ExactCompleted = !Context.Aborted;
                Result.SearchNodes = Context.Nodes;
                if (Context.Aborted)
                {
                    Result.Diagnostics.push_back("exact search reached its node limit; returning the best incumbent");
                }
            }

            if (Result.Plan)
            {
                Result.Metrics = MeasurePlan(*Result.Plan, _Instances, _Demands, _Stocks, _Settings);
            }
            return Result;
        }

        [[nodiscard]] std::vector<std::string> ValidateAndNormalize(
            std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            std::vector<std::string> Diagnostics;
            if (_Settings.Kerf < 0 || _Settings.PartGap < 0)
            {
                Diagnostics.push_back("part gaps must be non-negative");
            }
            if (_Settings.ObjectiveOrder.empty())
            {
                Diagnostics.push_back("at least one objective is required");
            }

            std::set<std::string> IDs;
            std::size_t InstanceCount = 0;
            for (PartDemand& Demand : _Demands)
            {
                if (Demand.ID.empty())
                {
                    Diagnostics.push_back("part demand ID must not be empty");
                }
                else if (!IDs.insert(Demand.ID).second)
                {
                    Diagnostics.push_back("duplicate part demand ID: " + Demand.ID);
                }
                if (Demand.CompatibilityKey.empty())
                {
                    Diagnostics.push_back("part " + Demand.ID + " has an empty compatibility key");
                }
                if (Demand.NominalLength <= 0)
                {
                    Diagnostics.push_back("part " + Demand.ID + " has a non-positive nominal length");
                }
                if (Demand.Quantity == 0)
                {
                    Diagnostics.push_back("part " + Demand.ID + " has zero quantity");
                }
                if (InstanceCount > 1000000U - (std::min)(Demand.Quantity, std::size_t{1000000}))
                {
                    Diagnostics.push_back("expanded part quantity exceeds 1,000,000 instances");
                }
                else
                {
                    InstanceCount += Demand.Quantity;
                }
                if (Demand.Variants.empty())
                {
                    PartVariant Default;
                    Default.LeftEnd.Signature = "saw.square";
                    Default.LeftEnd.AllowCommonCut = true;
                    Default.RightEnd.Signature = "saw.square";
                    Default.RightEnd.AllowCommonCut = true;
                    Demand.Variants.push_back(std::move(Default));
                }

                std::set<std::string> VariantIDs;
                for (const PartVariant& Variant : Demand.Variants)
                {
                    if (Variant.ID.empty())
                    {
                        Diagnostics.push_back("part " + Demand.ID + " has an empty variant ID");
                    }
                    else if (!VariantIDs.insert(Variant.ID).second)
                    {
                        Diagnostics.push_back("part " + Demand.ID + " has duplicate variant ID: " + Variant.ID);
                    }
                    if (VariantLength(Demand, Variant) <= 0)
                    {
                        Diagnostics.push_back("part " + Demand.ID + " has a non-positive variant length");
                    }
                    if (Variant.MaterialLength < 0 ||
                        Variant.MaterialLength > VariantLength(Demand, Variant))
                    {
                        Diagnostics.push_back("part " + Demand.ID + " has an invalid material length");
                    }
                    if (Variant.LeftEnd.SeparationAllowance < 0 || Variant.RightEnd.SeparationAllowance < 0)
                    {
                        Diagnostics.push_back("part " + Demand.ID + " has a negative end allowance");
                    }
                    Length ProjectionSum = 0;
                    if (Variant.LeftEnd.NestingProjection < 0 ||
                        Variant.RightEnd.NestingProjection < 0 ||
                        !TryAdd(
                            Variant.LeftEnd.NestingProjection,
                            Variant.RightEnd.NestingProjection,
                            ProjectionSum) ||
                        ProjectionSum >= VariantLength(Demand, Variant))
                    {
                        Diagnostics.push_back("part " + Demand.ID + " has invalid trapezoid projections");
                    }
                    for (const EndDescriptor* End : {&Variant.LeftEnd, &Variant.RightEnd})
                    {
                        if (End->AllowTrapezoidNesting &&
                            (End->NestingProjection <= 0 || End->NestingPlane.empty()))
                        {
                            Diagnostics.push_back(
                                "part " + Demand.ID + " has an incomplete trapezoid end descriptor");
                        }
                    }
                    if (!std::isfinite(Variant.RotationRadians))
                    {
                        Diagnostics.push_back("part " + Demand.ID + " has a non-finite rotation");
                    }
                }
            }

            IDs.clear();
            for (const StockType& Stock : _Stocks)
            {
                if (Stock.ID.empty())
                {
                    Diagnostics.push_back("stock type ID must not be empty");
                }
                else if (!IDs.insert(Stock.ID).second)
                {
                    Diagnostics.push_back("duplicate stock type ID: " + Stock.ID);
                }
                if (Stock.CompatibilityKey.empty())
                {
                    Diagnostics.push_back("stock " + Stock.ID + " has an empty compatibility key");
                }
                if (Stock.TotalLength <= 0 || Stock.FrontMargin < 0 || Stock.TailDeadZone < 0 ||
                    Stock.MinReusableRemainder < 0)
                {
                    Diagnostics.push_back("stock " + Stock.ID + " has invalid lengths");
                }
                Length Reserved = 0;
                if (!TryAdd(Stock.FrontMargin, Stock.TailDeadZone, Reserved) || Reserved >= Stock.TotalLength)
                {
                    Diagnostics.push_back("stock " + Stock.ID + " has no usable interval after margins");
                }
            }
            return Diagnostics;
        }

        void AddMetrics(ResultMetrics& _Target, const InternalMetrics& _Source)
        {
            SaturatingAdd(_Target.StockCost, _Source.StockCost);
            _Target.StandardStockCount += _Source.StandardStockCount;
            _Target.OpenedStockCount += _Source.OpenedStockCount;
            SaturatingAdd(_Target.TotalStockLength, _Source.TotalStockLength);
            SaturatingAdd(_Target.PartLength, _Source.PartLength);
            SaturatingAdd(_Target.GapLength, _Source.GapLength);
            SaturatingAdd(_Target.UnrecoverableWaste, _Source.UnrecoverableWaste);
            SaturatingAdd(_Target.ReusableRemainderLength, _Source.ReusableRemainderLength);
            _Target.ReusableRemainderCount += _Source.ReusableRemainderCount;
            _Target.CutCount += _Source.CutCount;
            _Target.CommonCutCount += _Source.CommonCutCount;
            _Target.OrderChangeCount += _Source.OrderChangeCount;
        }

        void AppendPlans(
            SolveResult& _Result,
            const WorkingPlan& _Plan,
            const std::vector<PartInstance>& _Instances,
            const std::vector<PartDemand>& _Demands,
            const std::vector<StockType>& _Stocks,
            const SolverSettings& _Settings)
        {
            std::map<std::string, std::size_t> StockOrdinals;
            for (const WorkingBar& Bar : _Plan)
            {
                const StockType& Stock = _Stocks[Bar.StockIndex];
                const BarMeasure Measure = MeasureBar(Bar, _Instances, _Demands, _Stocks, _Settings);
                StockPlan Output;
                Output.StockTypeID = Stock.ID;
                Output.StockInstanceID = Stock.ID + "#" + std::to_string(++StockOrdinals[Stock.ID]);
                Output.CompatibilityKey = Stock.CompatibilityKey;
                Output.Kind = Stock.Kind;
                Output.TotalLength = Stock.TotalLength;
                Output.FrontMargin = Stock.FrontMargin;
                Output.TailDeadZone = Stock.TailDeadZone;
                Output.ProcessedEnd = Measure.ProcessedEnd;
                Output.RemainingLength = Measure.RemainingLength;
                Output.ReusableRemainderLength = Measure.ReusableRemainderLength;
                Output.UnrecoverableWaste = Measure.UnrecoverableWaste;
                Output.PartLength = Measure.PartLength;
                Output.GapLength = Measure.GapLength;
                Output.CutCount = Measure.CutCount;
                Output.CommonCutCount = Measure.CommonCutCount;

                for (std::size_t Index = 0; Index < Bar.Placements.size(); ++Index)
                {
                    const WorkingPlacement& Working = Bar.Placements[Index];
                    const PartInstance& Instance = _Instances[Working.InstanceIndex];
                    const PartDemand& Demand = _Demands[Instance.DemandIndex];
                    const PartVariant& Variant = Demand.Variants[Working.VariantIndex];
                    Output.Placements.push_back(Placement{
                        Demand.ID,
                        Instance.ID,
                        Variant.ID,
                        Demand.OrderKey,
                        Measure.Starts[Index],
                        Measure.Ends[Index],
                        Measure.GapsBefore[Index],
                        Variant.Reversed,
                        Variant.RotationRadians,
                        Measure.CommonCutWithPrevious[Index],
                    });
                }
                _Result.Stocks.push_back(std::move(Output));
            }
        }
    }

    SolveResult Solver::Solve(
        const std::vector<PartDemand>& _Demands,
        const std::vector<StockType>& _Stocks,
        const SolverSettings& _Settings) const
    {
        SolveResult Result;
        std::vector<PartDemand> Demands = _Demands;
        Result.Diagnostics = ValidateAndNormalize(Demands, _Stocks, _Settings);
        if (!Result.Diagnostics.empty())
        {
            Result.Status = SolveStatus::InvalidInput;
            return Result;
        }
        if (Demands.empty())
        {
            Result.Status = SolveStatus::Optimal;
            Result.ExactSearchCompleted = true;
            return Result;
        }

        std::map<std::string, std::vector<PartInstance>> Groups;
        for (std::size_t DemandIndex = 0; DemandIndex < Demands.size(); ++DemandIndex)
        {
            const PartDemand& Demand = Demands[DemandIndex];
            for (std::size_t Ordinal = 1; Ordinal <= Demand.Quantity; ++Ordinal)
            {
                std::ostringstream InstanceID;
                InstanceID << Demand.ID << '#';
                InstanceID.width(4);
                InstanceID.fill('0');
                InstanceID << Ordinal;
                Groups[Demand.CompatibilityKey].push_back(
                    PartInstance{DemandIndex, Ordinal, InstanceID.str()});
            }
        }

        std::mt19937 Random(_Settings.RandomSeed);
        bool bAllExact = true;
        bool bAllFeasible = true;
        bool bAllQualityBoundsAvailable = true;
        for (const auto& [CompatibilityKey, Instances] : Groups)
        {
            const std::optional<UniformQualityLowerBound> QualityBound =
                CalculateUniformQualityLowerBound(Instances, Demands, _Stocks, _Settings);
            if (QualityBound)
            {
                SaturatingAdd(Result.Metrics.StockCostLowerBound, QualityBound->StockCost);
                Result.Metrics.OpenedStockCountLowerBound += QualityBound->OpenedStockCount;
            }
            else
            {
                bAllQualityBoundsAvailable = false;
            }

            bool bEveryPartCanFit = true;
            for (std::size_t InstanceIndex = 0; InstanceIndex < Instances.size(); ++InstanceIndex)
            {
                const PartDemand& Demand = Demands[Instances[InstanceIndex].DemandIndex];
                bool bFits = false;
                for (std::size_t StockIndex = 0; StockIndex < _Stocks.size() && !bFits; ++StockIndex)
                {
                    if (_Stocks[StockIndex].CompatibilityKey != CompatibilityKey)
                    {
                        continue;
                    }
                    for (std::size_t VariantIndex = 0; VariantIndex < Demand.Variants.size(); ++VariantIndex)
                    {
                        const WorkingBar Single{StockIndex, {{InstanceIndex, VariantIndex}}};
                        if (MeasureBar(Single, Instances, Demands, _Stocks, _Settings).Feasible)
                        {
                            bFits = true;
                            break;
                        }
                    }
                }
                if (!bFits)
                {
                    bEveryPartCanFit = false;
                    Result.UnplacedInstances.push_back(Instances[InstanceIndex].ID);
                }
            }

            if (!bEveryPartCanFit)
            {
                bAllFeasible = false;
                bAllExact = false;
                Result.Diagnostics.push_back(
                    "compatibility group '" + CompatibilityKey + "' contains parts that fit no available stock");
                continue;
            }

            GroupOutcome Outcome = SolveGroup(Instances, Demands, _Stocks, _Settings, Random);
            Result.Metrics.SearchNodes += Outcome.SearchNodes;
            for (const std::string& Diagnostic : Outcome.Diagnostics)
            {
                Result.Diagnostics.push_back("group '" + CompatibilityKey + "': " + Diagnostic);
            }
            if (!Outcome.Plan)
            {
                bAllFeasible = false;
                bAllExact = bAllExact && Outcome.ExactCompleted;
                for (const PartInstance& Instance : Instances)
                {
                    Result.UnplacedInstances.push_back(Instance.ID);
                }
                Result.Diagnostics.push_back(
                    "no complete plan was found for compatibility group '" + CompatibilityKey + "'");
                continue;
            }

            AddMetrics(Result.Metrics, Outcome.Metrics);
            AppendPlans(Result, *Outcome.Plan, Instances, Demands, _Stocks, _Settings);
            bAllExact = bAllExact && Outcome.ExactCompleted;
        }

        const double TotalStockLength = static_cast<double>(Result.Metrics.TotalStockLength);
        if (TotalStockLength > 0.0)
        {
            Result.Metrics.GrossUtilization = static_cast<double>(Result.Metrics.PartLength) / TotalStockLength;
        }
        const Length ConsumedLength = Result.Metrics.TotalStockLength - Result.Metrics.ReusableRemainderLength;
        if (ConsumedLength > 0)
        {
            Result.Metrics.NetMaterialYield =
                static_cast<double>(Result.Metrics.PartLength) / static_cast<double>(ConsumedLength);
        }

        Result.ExactSearchCompleted = bAllFeasible && bAllExact;
        Result.Metrics.QualityLowerBoundAvailable = bAllFeasible && bAllQualityBoundsAvailable;
        if (Result.Metrics.QualityLowerBoundAvailable)
        {
            if (Result.Metrics.StockCostLowerBound > 0 &&
                Result.Metrics.StockCost >= Result.Metrics.StockCostLowerBound)
            {
                Result.Metrics.StockCostRelativeGap =
                    static_cast<double>(Result.Metrics.StockCost - Result.Metrics.StockCostLowerBound) /
                    static_cast<double>(Result.Metrics.StockCostLowerBound);
            }
            if (Result.Metrics.OpenedStockCountLowerBound > 0 &&
                Result.Metrics.OpenedStockCount >= Result.Metrics.OpenedStockCountLowerBound)
            {
                Result.Metrics.OpenedStockCountRelativeGap =
                    static_cast<double>(
                        Result.Metrics.OpenedStockCount -
                        Result.Metrics.OpenedStockCountLowerBound) /
                    static_cast<double>(Result.Metrics.OpenedStockCountLowerBound);
            }
        }
        Result.Status = !bAllFeasible
                            ? SolveStatus::Infeasible
                            : (Result.ExactSearchCompleted ? SolveStatus::Optimal : SolveStatus::Feasible);
        return Result;
    }
}
