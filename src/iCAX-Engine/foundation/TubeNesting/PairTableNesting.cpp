#include "TubeNesting.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifndef ICAX_TUBE_NESTING_WITH_ORTOOLS
#define ICAX_TUBE_NESTING_WITH_ORTOOLS 0
#endif

#if ICAX_TUBE_NESTING_WITH_ORTOOLS
#include "ortools/sat/cp_model.h"
#include "ortools/sat/cp_model_solver.h"
#include "ortools/sat/sat_parameters.pb.h"
#endif

namespace iCAX::TubeNesting
{
    namespace
    {
        constexpr Length kInfinity = (std::numeric_limits<Length>::max)() / 4;

        struct Instance final
        {
            std::size_t Type = 0;
            std::size_t Ordinal = 0;
            std::string ID;
        };

        struct WorkingPlacement final
        {
            std::size_t Instance = 0;
            Direction PartDirection = Direction::Forward;
            double RotationRadians = 0.0;
        };

        struct WorkingBar final
        {
            std::size_t Stock = 0;
            std::vector<WorkingPlacement> Placements;
        };

        using WorkingPlan = std::vector<WorkingBar>;

        struct Score final
        {
            std::int64_t Cost = 0;
            Length NominalLength = 0;
            std::size_t BarCount = 0;
            Length OccupiedLength = 0;
        };

        [[nodiscard]] bool TryAdd(const Length Left_, const Length Right_, Length& Result_)
        {
            if (Right_ > 0 && Left_ > (std::numeric_limits<Length>::max)() - Right_) return false;
            if (Right_ < 0 && Left_ < (std::numeric_limits<Length>::min)() - Right_) return false;
            Result_ = Left_ + Right_;
            return true;
        }

        [[nodiscard]] std::int64_t StockCost(const StockType& Stock_)
        {
            if (!Stock_.FixedInstanceID.empty()) return 0;
            if (Stock_.CostUnits >= 0) return Stock_.CostUnits;
            return Stock_.Kind == StockKind::Standard ? Stock_.TotalLength : 0;
        }

        [[nodiscard]] bool IsContinuation(const StockType& Stock_)
        {
            return !Stock_.FixedInstanceID.empty();
        }

        [[nodiscard]] Length ChargedNominalLength(const StockType& Stock_)
        {
            return IsContinuation(Stock_) ? 0 : Stock_.TotalLength;
        }

        [[nodiscard]] int CompareScore(
            const Score& Left_, const Score& Right_, const PairObjectivePolicy Policy_)
        {
            const auto _LeftTuple = Policy_ == PairObjectivePolicy::MinPurchaseCost
                ? std::tuple{Left_.Cost, Left_.NominalLength,
                    static_cast<std::uint64_t>(Left_.BarCount), Left_.OccupiedLength}
                : std::tuple{static_cast<std::int64_t>(Left_.NominalLength), Left_.NominalLength,
                    static_cast<std::uint64_t>(Left_.BarCount), Left_.OccupiedLength};
            const auto _RightTuple = Policy_ == PairObjectivePolicy::MinPurchaseCost
                ? std::tuple{Right_.Cost, Right_.NominalLength,
                    static_cast<std::uint64_t>(Right_.BarCount), Right_.OccupiedLength}
                : std::tuple{static_cast<std::int64_t>(Right_.NominalLength), Right_.NominalLength,
                    static_cast<std::uint64_t>(Right_.BarCount), Right_.OccupiedLength};
            if (_LeftTuple < _RightTuple) return -1;
            if (_RightTuple < _LeftTuple) return 1;
            return 0;
        }

        [[nodiscard]] Score AddStockScore(
            Score Value_, const StockType& Stock_, const Length Occupied_)
        {
            Value_.Cost += StockCost(Stock_);
            Value_.NominalLength += ChargedNominalLength(Stock_);
            ++Value_.BarCount;
            Value_.OccupiedLength += Occupied_;
            return Value_;
        }

        [[nodiscard]] bool IsEntryUsable(const PairEntry& Entry_)
        {
            return Entry_.Status == PairEntryStatus::Feasible
                || Entry_.Status == PairEntryStatus::Certified;
        }

        [[nodiscard]] Length UsableLength(const StockType& Stock_)
        {
            Length _Reserved = 0;
            const Length _Start = IsContinuation(Stock_)
                ? Stock_.InitialProcessedEnd : Stock_.FrontMargin;
            if (!TryAdd(_Start, Stock_.TailDeadZone, _Reserved)
                || _Reserved >= Stock_.TotalLength)
                return -1;
            return Stock_.TotalLength - _Reserved;
        }

        [[nodiscard]] const PairEntry* InitialPairEntry(
            const StockType& Stock_, const std::size_t Type_, const Direction Direction_,
            const PairTableSnapshot& Table_)
        {
            if (!IsContinuation(Stock_)
                || Stock_.InitialPredecessorType
                    == (std::numeric_limits<std::size_t>::max)())
                return nullptr;
            if (Stock_.InitialPredecessorType >= Table_.Types.size()) return nullptr;
            const auto& _Entry = Table_.At(
                Stock_.InitialPredecessorType, Stock_.InitialPredecessorDirection,
                Type_, Direction_);
            return IsEntryUsable(_Entry)
                && _Entry.NetSaving <= Stock_.InitialProcessedEnd
                ? &_Entry : nullptr;
        }

        [[nodiscard]] Length InitialDirectionCost(
            const StockType& Stock_, const std::size_t Type_, const Direction Direction_,
            const PairTableSnapshot& Table_)
        {
            const auto& _Type = Table_.Types[Type_];
            if (!_Type.DirectionAllowed[static_cast<std::size_t>(Direction_)])
                return kInfinity;
            if (!IsContinuation(Stock_)
                || Stock_.InitialPredecessorType
                    == (std::numeric_limits<std::size_t>::max)())
                return _Type.MaximumLength;
            const auto* _Entry = InitialPairEntry(Stock_, Type_, Direction_, Table_);
            if (!_Entry) return kInfinity;
            if (_Entry->NetSaving > Stock_.InitialProcessedEnd) return kInfinity;
            Length _Cost = 0;
            if (!TryAdd(_Type.MaximumLength, -_Entry->NetSaving, _Cost) || _Cost < 0)
                return kInfinity;
            return _Cost;
        }

        struct DirectionResult final
        {
            bool Feasible = false;
            Length Occupied = 0;
            std::vector<Direction> Directions;
        };

        [[nodiscard]] DirectionResult OptimizeDirections(
            const std::vector<std::size_t>& Order_, const std::size_t Begin_,
            const std::size_t End_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const StockType& Stock_)
        {
            DirectionResult _Result;
            if (Begin_ >= End_ || End_ > Order_.size()) return _Result;
            const std::size_t _Count = End_ - Begin_;
            std::vector<std::array<Length, 2>> _Cost(_Count, {kInfinity, kInfinity});
            std::vector<std::array<std::uint8_t, 2>> _Previous(_Count, {0, 0});
            const auto& _FirstType = Table_.Types[Instances_[Order_[Begin_]].Type];
            for (std::size_t _Direction = 0; _Direction < 2; ++_Direction)
                if (_FirstType.DirectionAllowed[_Direction])
                    _Cost[0][_Direction] = InitialDirectionCost(
                        Stock_, Instances_[Order_[Begin_]].Type,
                        static_cast<Direction>(_Direction), Table_);
            for (std::size_t _Offset = 1; _Offset < _Count; ++_Offset)
            {
                const auto _PreviousType = Instances_[Order_[Begin_ + _Offset - 1]].Type;
                const auto _Type = Instances_[Order_[Begin_ + _Offset]].Type;
                for (std::size_t _Direction = 0; _Direction < 2; ++_Direction)
                {
                    if (!Table_.Types[_Type].DirectionAllowed[_Direction]) continue;
                    for (std::size_t _Prior = 0; _Prior < 2; ++_Prior)
                    {
                        if (_Cost[_Offset - 1][_Prior] >= kInfinity) continue;
                        const auto& _Entry = Table_.At(
                            _PreviousType, static_cast<Direction>(_Prior),
                            _Type, static_cast<Direction>(_Direction));
                        if (!IsEntryUsable(_Entry)) continue;
                        Length _Candidate = 0;
                        if (!TryAdd(_Cost[_Offset - 1][_Prior],
                            Table_.Types[_Type].MaximumLength - _Entry.NetSaving, _Candidate))
                            continue;
                        if (_Candidate < _Cost[_Offset][_Direction])
                        {
                            _Cost[_Offset][_Direction] = _Candidate;
                            _Previous[_Offset][_Direction] = static_cast<std::uint8_t>(_Prior);
                        }
                    }
                }
            }
            std::size_t _Last = _Cost.back()[1] < _Cost.back()[0] ? 1 : 0;
            if (_Cost.back()[_Last] >= kInfinity) return _Result;
            _Result.Feasible = true;
            _Result.Occupied = _Cost.back()[_Last];
            _Result.Directions.resize(_Count);
            for (std::size_t _Offset = _Count; _Offset-- > 0;)
            {
                _Result.Directions[_Offset] = static_cast<Direction>(_Last);
                if (_Offset) _Last = _Previous[_Offset][_Last];
            }
            return _Result;
        }

        [[nodiscard]] bool PopulateRotations(
            WorkingBar& Bar_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const StockType& Stock_)
        {
            if (Bar_.Placements.empty()) return false;
            if (IsContinuation(Stock_)
                && Stock_.InitialPredecessorType
                    != (std::numeric_limits<std::size_t>::max)())
            {
                const auto& _First = Bar_.Placements.front();
                const auto* _Entry = InitialPairEntry(
                    Stock_, Instances_[_First.Instance].Type,
                    _First.PartDirection, Table_);
                if (!_Entry || _Entry->PoseMetadataIndex >= Table_.Poses.size()) return false;
                Bar_.Placements.front().RotationRadians = std::fmod(
                    Stock_.InitialPredecessorRotationRadians
                        + Table_.Poses[_Entry->PoseMetadataIndex].RelativeRotationRadians,
                    2.0 * std::numbers::pi_v<double>);
                if (Bar_.Placements.front().RotationRadians < 0.0)
                    Bar_.Placements.front().RotationRadians += 2.0 * std::numbers::pi_v<double>;
            }
            else
                Bar_.Placements.front().RotationRadians = 0.0;
            for (std::size_t _Index = 1; _Index < Bar_.Placements.size(); ++_Index)
            {
                const auto& _Previous = Bar_.Placements[_Index - 1];
                auto& _Current = Bar_.Placements[_Index];
                const auto& _Entry = Table_.At(
                    Instances_[_Previous.Instance].Type, _Previous.PartDirection,
                    Instances_[_Current.Instance].Type, _Current.PartDirection);
                if (!IsEntryUsable(_Entry) || _Entry.PoseMetadataIndex >= Table_.Poses.size())
                    return false;
                _Current.RotationRadians = std::fmod(
                    _Previous.RotationRadians
                        + Table_.Poses[_Entry.PoseMetadataIndex].RelativeRotationRadians,
                    2.0 * std::numbers::pi_v<double>);
                if (_Current.RotationRadians < 0.0)
                    _Current.RotationRadians += 2.0 * std::numbers::pi_v<double>;
            }
            return true;
        }

        struct SegmentOption final
        {
            std::size_t End = 0;
            std::size_t Stock = 0;
            Length Occupied = 0;
        };

        [[nodiscard]] std::vector<std::vector<SegmentOption>> BuildSegmentOptions(
            const std::vector<std::size_t>& Order_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_)
        {
            std::vector<std::vector<SegmentOption>> _Options(Order_.size());
            for (std::size_t _Begin = 0; _Begin < Order_.size(); ++_Begin)
            {
                const auto _FirstTypeIndex = Instances_[Order_[_Begin]].Type;
                const auto& _FirstType = Table_.Types[_FirstTypeIndex];
                std::vector<std::array<Length, 2>> _Costs(
                    Stocks_.size(), {kInfinity, kInfinity});
                for (std::size_t _StockIndex = 0;
                    _StockIndex < Stocks_.size(); ++_StockIndex)
                {
                    const auto& _Stock = Stocks_[_StockIndex];
                    if (_Stock.CompatibilityKey != _FirstType.CompatibilityKey) continue;
                    for (std::size_t _Direction = 0; _Direction < 2; ++_Direction)
                        _Costs[_StockIndex][_Direction] = InitialDirectionCost(
                            _Stock, _FirstTypeIndex,
                            static_cast<Direction>(_Direction), Table_);
                }
                std::set<std::string> _Orders;
                for (std::size_t _End = _Begin; _End < Order_.size(); ++_End)
                {
                    const auto _TypeIndex = Instances_[Order_[_End]].Type;
                    if (!Table_.Types[_TypeIndex].OrderKey.empty())
                        _Orders.insert(Table_.Types[_TypeIndex].OrderKey);
                    if (_End > _Begin)
                    {
                        const auto _PriorType = Instances_[Order_[_End - 1]].Type;
                        for (auto& _Cost : _Costs)
                        {
                            std::array<Length, 2> _Next{kInfinity, kInfinity};
                            for (std::size_t _Direction = 0; _Direction < 2; ++_Direction)
                            {
                                if (!Table_.Types[_TypeIndex].DirectionAllowed[_Direction]) continue;
                                for (std::size_t _Prior = 0; _Prior < 2; ++_Prior)
                                {
                                    if (_Cost[_Prior] >= kInfinity) continue;
                                    const auto& _Entry = Table_.At(
                                        _PriorType, static_cast<Direction>(_Prior),
                                        _TypeIndex, static_cast<Direction>(_Direction));
                                    if (!IsEntryUsable(_Entry)) continue;
                                    Length _Candidate = 0;
                                    if (!TryAdd(_Cost[_Prior],
                                        Table_.Types[_TypeIndex].MaximumLength - _Entry.NetSaving,
                                        _Candidate)) continue;
                                    _Next[_Direction] = (std::min)(
                                        _Next[_Direction], _Candidate);
                                }
                            }
                            _Cost = _Next;
                        }
                    }
                    bool _CanExtend = false;
                    for (std::size_t _StockIndex = 0; _StockIndex < Stocks_.size(); ++_StockIndex)
                    {
                        const auto& _Stock = Stocks_[_StockIndex];
                        if (_Stock.CompatibilityKey != _FirstType.CompatibilityKey) continue;
                        const Length _Occupied = (std::min)(
                            _Costs[_StockIndex][0], _Costs[_StockIndex][1]);
                        if (_Occupied >= kInfinity) continue;
                        if (_Stock.MaxParts && _End - _Begin + 1 > _Stock.MaxParts) continue;
                        if (_Stock.MaxDistinctOrders && _Orders.size() > _Stock.MaxDistinctOrders) continue;
                        if (_Occupied <= UsableLength(_Stock))
                        {
                            _Options[_Begin].push_back({_End + 1, _StockIndex, _Occupied});
                            _CanExtend = true;
                        }
                    }
                    // Every additional part has a non-negative incremental
                    // length.  Once no stock can hold this prefix, a longer
                    // prefix from the same begin position cannot recover.
                    if (!_CanExtend) break;
                }
            }
            return _Options;
        }

        struct Label final
        {
            Score Value;
            std::vector<std::uint32_t> FiniteUsed;
            std::size_t PreviousPrefix = 0;
            std::size_t PreviousLabel = 0;
            std::size_t Stock = 0;
            std::size_t SegmentBegin = 0;
        };

        [[nodiscard]] bool Dominates(
            const Label& Left_, const Label& Right_, const PairObjectivePolicy Policy_)
        {
            if (CompareScore(Left_.Value, Right_.Value, Policy_) > 0) return false;
            for (std::size_t _Index = 0; _Index < Left_.FiniteUsed.size(); ++_Index)
                if (Left_.FiniteUsed[_Index] > Right_.FiniteUsed[_Index]) return false;
            return true;
        }

        struct DecodeResult final
        {
            std::optional<WorkingPlan> Plan;
            Score Value;
            bool Exact = false;
            bool StateLimit = false;
        };

        [[nodiscard]] DecodeResult DecodeFixedOrder(
            const std::vector<std::size_t>& Order_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
            const PairTableSolverSettings& Settings_)
        {
            DecodeResult _Result;
            if (Order_.empty())
            {
                _Result.Plan = WorkingPlan{};
                _Result.Exact = true;
                return _Result;
            }
            const auto _Options = BuildSegmentOptions(Order_, Instances_, Table_, Stocks_);
            std::vector<int> _FiniteDimension(Stocks_.size(), -1);
            std::vector<std::size_t> _FiniteStocks;
            for (std::size_t _Stock = 0; _Stock < Stocks_.size(); ++_Stock)
                if (Stocks_[_Stock].Quantity > 0)
                {
                    _FiniteDimension[_Stock] = static_cast<int>(_FiniteStocks.size());
                    _FiniteStocks.push_back(_Stock);
                }
            std::vector<std::vector<Label>> _Labels(Order_.size() + 1);
            _Labels[0].push_back(Label{{}, std::vector<std::uint32_t>(_FiniteStocks.size(), 0)});
            std::size_t _LabelCount = 1;
            for (std::size_t _Prefix = 0; _Prefix < Order_.size(); ++_Prefix)
            {
                if (_Labels[_Prefix].empty()) continue;
                for (std::size_t _LabelIndex = 0;
                    _LabelIndex < _Labels[_Prefix].size(); ++_LabelIndex)
                {
                    const auto _Current = _Labels[_Prefix][_LabelIndex];
                    for (const auto& _Option : _Options[_Prefix])
                    {
                        Label _Candidate;
                        _Candidate.Value = AddStockScore(
                            _Current.Value, Stocks_[_Option.Stock], _Option.Occupied);
                        _Candidate.FiniteUsed = _Current.FiniteUsed;
                        const int _Dimension = _FiniteDimension[_Option.Stock];
                        if (_Dimension >= 0)
                        {
                            auto& _Used = _Candidate.FiniteUsed[static_cast<std::size_t>(_Dimension)];
                            if (++_Used > Stocks_[_Option.Stock].Quantity) continue;
                        }
                        _Candidate.PreviousPrefix = _Prefix;
                        _Candidate.PreviousLabel = _LabelIndex;
                        _Candidate.Stock = _Option.Stock;
                        _Candidate.SegmentBegin = _Prefix;
                        auto& _Destination = _Labels[_Option.End];
                        if (_FiniteStocks.empty())
                        {
                            if (_Destination.empty())
                            {
                                _Destination.push_back(std::move(_Candidate));
                                ++_LabelCount;
                            }
                            else if (CompareScore(_Candidate.Value, _Destination.front().Value,
                                Settings_.ObjectivePolicy) < 0)
                            {
                                _Destination.front() = std::move(_Candidate);
                            }
                            continue;
                        }
                        if (std::ranges::any_of(_Destination, [&](const Label& Existing_)
                            { return Dominates(Existing_, _Candidate, Settings_.ObjectivePolicy); }))
                            continue;
                        std::erase_if(_Destination, [&](const Label& Existing_)
                            { return Dominates(_Candidate, Existing_, Settings_.ObjectivePolicy); });
                        _Destination.push_back(std::move(_Candidate));
                        if (++_LabelCount > Settings_.FiniteInventoryLabelLimit)
                        {
                            _Result.StateLimit = true;
                            return _Result;
                        }
                    }
                }
            }
            if (_Labels.back().empty()) return _Result;
            std::size_t _Best = 0;
            for (std::size_t _Index = 1; _Index < _Labels.back().size(); ++_Index)
                if (CompareScore(_Labels.back()[_Index].Value,
                    _Labels.back()[_Best].Value, Settings_.ObjectivePolicy) < 0)
                    _Best = _Index;
            _Result.Value = _Labels.back()[_Best].Value;
            WorkingPlan _Plan;
            std::size_t _Prefix = Order_.size();
            while (_Prefix > 0)
            {
                const auto& _Label = _Labels[_Prefix][_Best];
                const auto _Directions = OptimizeDirections(
                    Order_, _Label.SegmentBegin, _Prefix, Instances_, Table_,
                    Stocks_[_Label.Stock]);
                if (!_Directions.Feasible) return {};
                WorkingBar _Bar;
                _Bar.Stock = _Label.Stock;
                for (std::size_t _Index = _Label.SegmentBegin; _Index < _Prefix; ++_Index)
                    _Bar.Placements.push_back({Order_[_Index],
                        _Directions.Directions[_Index - _Label.SegmentBegin], 0.0});
                if (!PopulateRotations(
                    _Bar, Instances_, Table_, Stocks_[_Label.Stock])) return {};
                _Plan.push_back(std::move(_Bar));
                const auto _PreviousPrefix = _Label.PreviousPrefix;
                _Best = _Label.PreviousLabel;
                _Prefix = _PreviousPrefix;
            }
            std::reverse(_Plan.begin(), _Plan.end());
            _Result.Plan = std::move(_Plan);
            _Result.Exact = true;
            return _Result;
        }

        [[nodiscard]] Score MeasurePlanScore(
            const WorkingPlan& Plan_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_)
        {
            Score _Result;
            for (const auto& _Bar : Plan_)
            {
                std::vector<std::size_t> _Order;
                _Order.reserve(_Bar.Placements.size());
                for (const auto& _Placement : _Bar.Placements) _Order.push_back(_Placement.Instance);
                const auto _Direction = OptimizeDirections(
                    _Order, 0, _Order.size(), Instances_, Table_, Stocks_[_Bar.Stock]);
                if (!_Direction.Feasible) return {0, kInfinity, 0, kInfinity};
                _Result = AddStockScore(_Result, Stocks_[_Bar.Stock], _Direction.Occupied);
            }
            return _Result;
        }

#if ICAX_TUBE_NESTING_WITH_ORTOOLS
        struct ConstraintSolveResult final
        {
            std::optional<WorkingPlan> Plan;
            Score Value;
            bool Optimal = false;
            bool Infeasible = false;
            bool TimeLimit = false;
            bool ModelInvalid = false;
            std::uint64_t Branches = 0;
            std::string Diagnostic;
        };

        struct ConstraintSlotVariables final
        {
            operations_research::sat::BoolVar Used;
            operations_research::sat::BoolVar Empty;
            std::vector<operations_research::sat::BoolVar> Stock;
            std::vector<std::array<operations_research::sat::BoolVar, 2>> Direction;
            std::vector<operations_research::sat::BoolVar> Present;
            std::vector<operations_research::sat::BoolVar> Loop;
            std::vector<operations_research::sat::BoolVar> Start;
            std::vector<operations_research::sat::BoolVar> End;
            std::vector<std::vector<std::optional<operations_research::sat::BoolVar>>> Arc;
            operations_research::sat::LinearExpr Occupied;
        };

        [[nodiscard]] ConstraintSolveResult SolveConstraintModel(
            const std::vector<std::size_t>& GroupInstances_,
            const std::vector<Instance>& Instances_, const PairTableSnapshot& Table_,
            const std::vector<StockType>& Stocks_, const PairTableSolverSettings& Settings_,
            const std::uint64_t TimeBudgetMilliseconds_, const WorkingPlan* HintPlan_,
            const std::vector<std::int64_t>* StockAvailabilityOverride_ = nullptr)
        {
            using namespace operations_research::sat;
            ConstraintSolveResult _Result;
            const std::size_t _Count = GroupInstances_.size();
            if (_Count == 0)
            {
                _Result.Plan = WorkingPlan{};
                _Result.Optimal = true;
                return _Result;
            }
            if (_Count > static_cast<std::size_t>((std::numeric_limits<int>::max)() - 1)
                || Stocks_.empty()
                || (StockAvailabilityOverride_
                    && StockAvailabilityOverride_->size() != Stocks_.size()))
            {
                _Result.ModelInvalid = true;
                _Result.Diagnostic = "CP-SAT model dimensions exceed the supported integer index range";
                return _Result;
            }

            long double _PartMagnitude = 0.0L;
            for (const auto _Instance : GroupInstances_)
                _PartMagnitude += static_cast<long double>(
                    Table_.Types[Instances_[_Instance].Type].MaximumLength);
            long double _MaximumSavingMagnitude = 0.0L;
            for (const auto _Tail : GroupInstances_)
                for (const auto _Head : GroupInstances_)
                {
                    if (_Tail == _Head) continue;
                    const auto _TailType = Instances_[_Tail].Type;
                    const auto _HeadType = Instances_[_Head].Type;
                    for (std::size_t _TailDirection = 0; _TailDirection < 2; ++_TailDirection)
                        for (std::size_t _HeadDirection = 0; _HeadDirection < 2; ++_HeadDirection)
                        {
                            const auto& _Entry = Table_.At(
                                _TailType, static_cast<Direction>(_TailDirection),
                                _HeadType, static_cast<Direction>(_HeadDirection));
                            if (!IsEntryUsable(_Entry)) continue;
                            _MaximumSavingMagnitude = (std::max)(
                                _MaximumSavingMagnitude,
                                std::abs(static_cast<long double>(_Entry.NetSaving)));
                        }
                }
            for (const auto& _Stock : Stocks_)
            {
                if (!IsContinuation(_Stock)
                    || _Stock.InitialPredecessorType
                        == (std::numeric_limits<std::size_t>::max)())
                    continue;
                for (const auto _Head : GroupInstances_)
                {
                    const auto _HeadType = Instances_[_Head].Type;
                    for (const auto _Direction : {Direction::Forward, Direction::Reverse})
                    {
                        const auto* _Entry = InitialPairEntry(
                            _Stock, _HeadType, _Direction, Table_);
                        if (_Entry)
                            _MaximumSavingMagnitude = (std::max)(
                                _MaximumSavingMagnitude,
                                std::abs(static_cast<long double>(_Entry->NetSaving)));
                    }
                }
            }
            // CP-SAT checks every linear expression in int64 arithmetic.  Use
            // a deliberately loose sum-of-absolute-coefficients bound, not
            // merely the largest feasible path value, so malformed negative
            // savings cannot overflow presolve or the aggregate objective.
            long double _Magnitude = static_cast<long double>(_Count) * (
                _PartMagnitude
                + static_cast<long double>(_Count) * static_cast<long double>(_Count)
                    * _MaximumSavingMagnitude);
            for (const auto& _Stock : Stocks_)
                _Magnitude += static_cast<long double>(_Count)
                    * ((std::max)(
                        std::abs(static_cast<long double>(_Stock.TotalLength)),
                        std::abs(static_cast<long double>(StockCost(_Stock)))));
            if (_Magnitude > static_cast<long double>((std::numeric_limits<std::int64_t>::max)()) / 8.0L)
            {
                _Result.ModelInvalid = true;
                _Result.Diagnostic = "CP-SAT model coefficients exceed the safe int64 range";
                return _Result;
            }

            CpModelBuilder _Model;
            std::vector<ConstraintSlotVariables> _Slots(_Count);
            std::vector<std::vector<BoolVar>> _InstanceAssignments(_Count);
            std::vector<std::string> _OrderKeys;
            std::unordered_map<std::string, std::size_t> _OrderIndex;
            for (const auto _Instance : GroupInstances_)
            {
                const auto& _Key = Table_.Types[Instances_[_Instance].Type].OrderKey;
                if (_Key.empty()) continue;
                if (_OrderIndex.emplace(_Key, _OrderKeys.size()).second)
                    _OrderKeys.push_back(_Key);
            }

            LinearExpr _TotalCost;
            LinearExpr _TotalNominalLength;
            LinearExpr _TotalBars;
            LinearExpr _TotalOccupied;

            for (std::size_t _SlotIndex = 0; _SlotIndex < _Count; ++_SlotIndex)
            {
                auto& _Slot = _Slots[_SlotIndex];
                _Slot.Used = _Model.NewBoolVar();
                _Slot.Empty = _Model.NewBoolVar();
                _Model.AddEquality(_Slot.Used + _Slot.Empty, 1);

                _Slot.Stock.resize(Stocks_.size());
                for (std::size_t _StockIndex = 0; _StockIndex < Stocks_.size(); ++_StockIndex)
                {
                    _Slot.Stock[_StockIndex] = _Model.NewBoolVar();
                    _TotalCost += LinearExpr::Term(
                        _Slot.Stock[_StockIndex], StockCost(Stocks_[_StockIndex]));
                    _TotalNominalLength += LinearExpr::Term(
                        _Slot.Stock[_StockIndex],
                        ChargedNominalLength(Stocks_[_StockIndex]));
                }
                _Model.AddEquality(LinearExpr::Sum(_Slot.Stock), _Slot.Used);
                _TotalBars += _Slot.Used;

                _Slot.Direction.resize(_Count);
                _Slot.Present.resize(_Count);
                _Slot.Loop.resize(_Count);
                _Slot.Start.resize(_Count);
                _Slot.End.resize(_Count);
                _Slot.Arc.resize(_Count,
                    std::vector<std::optional<BoolVar>>(_Count));
                auto _Circuit = _Model.AddCircuitConstraint();
                _Circuit.AddArc(0, 0, _Slot.Empty);

                LinearExpr _PartCount;
                std::vector<std::vector<BoolVar>> _PartsByOrder(_OrderKeys.size());
                for (std::size_t _Part = 0; _Part < _Count; ++_Part)
                {
                    const auto _Type = Instances_[GroupInstances_[_Part]].Type;
                    const auto& _Definition = Table_.Types[_Type];
                    _Slot.Present[_Part] = _Model.NewBoolVar();
                    _Slot.Loop[_Part] = _Model.NewBoolVar();
                    _Slot.Start[_Part] = _Model.NewBoolVar();
                    _Slot.End[_Part] = _Model.NewBoolVar();
                    for (std::size_t _Direction = 0; _Direction < 2; ++_Direction)
                    {
                        auto& _Variable = _Slot.Direction[_Part][_Direction];
                        _Variable = _Model.NewBoolVar();
                        if (!_Definition.DirectionAllowed[_Direction])
                            _Model.FixVariable(_Variable, false);
                        _InstanceAssignments[_Part].push_back(_Variable);
                    }
                    _Model.AddEquality(
                        _Slot.Direction[_Part][0] + _Slot.Direction[_Part][1],
                        _Slot.Present[_Part]);
                    _Model.AddLessOrEqual(_Slot.Present[_Part], _Slot.Used);
                    _Model.AddEquality(_Slot.Present[_Part] + _Slot.Loop[_Part], 1);
                    _Circuit.AddArc(static_cast<int>(_Part + 1),
                        static_cast<int>(_Part + 1), _Slot.Loop[_Part]);
                    _Circuit.AddArc(0, static_cast<int>(_Part + 1), _Slot.Start[_Part]);
                    _Circuit.AddArc(static_cast<int>(_Part + 1), 0, _Slot.End[_Part]);
                    _PartCount += _Slot.Present[_Part];
                    _Slot.Occupied += LinearExpr::Term(
                        _Slot.Present[_Part], _Definition.MaximumLength);
                    if (!_Definition.OrderKey.empty())
                        _PartsByOrder[_OrderIndex.at(_Definition.OrderKey)].push_back(
                            _Slot.Present[_Part]);
                }

                // A continued stock starts behind an immutable predecessor.
                // Couple the selected stock, route head and head direction so
                // only that stock's directed start saving is subtracted.
                for (std::size_t _StockIndex = 0;
                    _StockIndex < Stocks_.size(); ++_StockIndex)
                {
                    const auto& _Stock = Stocks_[_StockIndex];
                    if (!IsContinuation(_Stock)
                        || _Stock.InitialPredecessorType
                            == (std::numeric_limits<std::size_t>::max)())
                        continue;
                    std::vector<BoolVar> _Choices;
                    for (std::size_t _Part = 0; _Part < _Count; ++_Part)
                    {
                        const auto _Type = Instances_[GroupInstances_[_Part]].Type;
                        for (std::size_t _Direction = 0; _Direction < 2; ++_Direction)
                        {
                            const auto* _Entry = InitialPairEntry(
                                _Stock, _Type, static_cast<Direction>(_Direction), Table_);
                            if (!_Entry) continue;
                            const auto _Choice = _Model.NewBoolVar();
                            _Model.AddLessOrEqual(_Choice, _Slot.Stock[_StockIndex]);
                            _Model.AddLessOrEqual(_Choice, _Slot.Start[_Part]);
                            _Model.AddLessOrEqual(
                                _Choice, _Slot.Direction[_Part][_Direction]);
                            _Model.AddGreaterOrEqual(
                                _Choice,
                                _Slot.Stock[_StockIndex] + _Slot.Start[_Part]
                                    + _Slot.Direction[_Part][_Direction] - 2);
                            _Slot.Occupied += LinearExpr::Term(
                                _Choice, -_Entry->NetSaving);
                            _Choices.push_back(_Choice);
                        }
                    }
                    if (_Choices.empty())
                        _Model.FixVariable(_Slot.Stock[_StockIndex], false);
                    else
                        _Model.AddEquality(
                            LinearExpr::Sum(_Choices), _Slot.Stock[_StockIndex]);
                }

                for (std::size_t _Tail = 0; _Tail < _Count; ++_Tail)
                {
                    const auto _TailType = Instances_[GroupInstances_[_Tail]].Type;
                    for (std::size_t _Head = 0; _Head < _Count; ++_Head)
                    {
                        if (_Tail == _Head) continue;
                        const auto _HeadType = Instances_[GroupInstances_[_Head]].Type;
                        std::vector<std::pair<BoolVar, Length>> _Choices;
                        for (std::size_t _TailDirection = 0; _TailDirection < 2; ++_TailDirection)
                            for (std::size_t _HeadDirection = 0; _HeadDirection < 2; ++_HeadDirection)
                            {
                                if (!Table_.Types[_TailType].DirectionAllowed[_TailDirection]
                                    || !Table_.Types[_HeadType].DirectionAllowed[_HeadDirection])
                                    continue;
                                const auto& _Entry = Table_.At(
                                    _TailType, static_cast<Direction>(_TailDirection),
                                    _HeadType, static_cast<Direction>(_HeadDirection));
                                if (!IsEntryUsable(_Entry)) continue;
                                const auto _Choice = _Model.NewBoolVar();
                                _Model.AddLessOrEqual(
                                    _Choice, _Slot.Direction[_Tail][_TailDirection]);
                                _Model.AddLessOrEqual(
                                    _Choice, _Slot.Direction[_Head][_HeadDirection]);
                                _Choices.emplace_back(_Choice, _Entry.NetSaving);
                            }
                        if (_Choices.empty()) continue;
                        const auto _Arc = _Model.NewBoolVar();
                        _Slot.Arc[_Tail][_Head] = _Arc;
                        _Circuit.AddArc(static_cast<int>(_Tail + 1),
                            static_cast<int>(_Head + 1), _Arc);
                        std::vector<BoolVar> _ChoiceVariables;
                        _ChoiceVariables.reserve(_Choices.size());
                        for (const auto& [_Choice, _Saving] : _Choices)
                        {
                            _ChoiceVariables.push_back(_Choice);
                            _Slot.Occupied += LinearExpr::Term(_Choice, -_Saving);
                        }
                        _Model.AddEquality(LinearExpr::Sum(_ChoiceVariables), _Arc);
                    }
                }

                LinearExpr _Capacity;
                LinearExpr _MaximumParts;
                LinearExpr _MaximumOrders;
                for (std::size_t _StockIndex = 0; _StockIndex < Stocks_.size(); ++_StockIndex)
                {
                    const auto& _Stock = Stocks_[_StockIndex];
                    _Capacity += LinearExpr::Term(
                        _Slot.Stock[_StockIndex], UsableLength(_Stock));
                    _MaximumParts += LinearExpr::Term(_Slot.Stock[_StockIndex],
                        static_cast<std::int64_t>(_Stock.MaxParts
                            ? (std::min)(_Stock.MaxParts, _Count) : _Count));
                    _MaximumOrders += LinearExpr::Term(_Slot.Stock[_StockIndex],
                        static_cast<std::int64_t>(_Stock.MaxDistinctOrders
                            ? (std::min)(_Stock.MaxDistinctOrders, _OrderKeys.size())
                            : _OrderKeys.size()));
                }
                _Model.AddGreaterOrEqual(_Slot.Occupied, 0);
                _Model.AddLessOrEqual(_Slot.Occupied, _Capacity);
                _Model.AddLessOrEqual(_PartCount, _MaximumParts);

                std::vector<BoolVar> _OrderPresent(_OrderKeys.size());
                for (std::size_t _Order = 0; _Order < _OrderKeys.size(); ++_Order)
                {
                    _OrderPresent[_Order] = _Model.NewBoolVar();
                    for (const auto _Part : _PartsByOrder[_Order])
                        _Model.AddLessOrEqual(_Part, _OrderPresent[_Order]);
                    _Model.AddLessOrEqual(
                        _OrderPresent[_Order], LinearExpr::Sum(_PartsByOrder[_Order]));
                }
                if (!_OrderPresent.empty())
                    _Model.AddLessOrEqual(LinearExpr::Sum(_OrderPresent), _MaximumOrders);
                _TotalOccupied += _Slot.Occupied;
            }

            for (const auto& _Assignments : _InstanceAssignments)
                _Model.AddExactlyOne(_Assignments);
            for (std::size_t _Stock = 0; _Stock < Stocks_.size(); ++_Stock)
            {
                const auto _Availability = StockAvailabilityOverride_
                    ? (*StockAvailabilityOverride_)[_Stock]
                    : (Stocks_[_Stock].Quantity == 0 ? std::int64_t{-1}
                        : static_cast<std::int64_t>(Stocks_[_Stock].Quantity));
                if (_Availability < 0) continue;
                std::vector<BoolVar> _Uses;
                _Uses.reserve(_Count);
                for (const auto& _Slot : _Slots) _Uses.push_back(_Slot.Stock[_Stock]);
                _Model.AddLessOrEqual(LinearExpr::Sum(_Uses),
                    _Availability);
            }
            for (std::size_t _Slot = 1; _Slot < _Count; ++_Slot)
                _Model.AddLessOrEqual(_Slots[_Slot].Used, _Slots[_Slot - 1].Used);

            if (HintPlan_)
            {
                std::unordered_map<std::size_t, std::size_t> _LocalIndex;
                for (std::size_t _Part = 0; _Part < _Count; ++_Part)
                    _LocalIndex.emplace(GroupInstances_[_Part], _Part);
                for (std::size_t _SlotIndex = 0; _SlotIndex < _Count; ++_SlotIndex)
                {
                    auto& _Slot = _Slots[_SlotIndex];
                    if (_SlotIndex >= HintPlan_->size())
                    {
                        _Model.AddHint(_Slot.Used, false);
                        _Model.AddHint(_Slot.Empty, true);
                        continue;
                    }
                    const auto& _Bar = (*HintPlan_)[_SlotIndex];
                    if (_Bar.Stock >= Stocks_.size() || _Bar.Placements.empty()) continue;
                    _Model.AddHint(_Slot.Used, true);
                    _Model.AddHint(_Slot.Empty, false);
                    _Model.AddHint(_Slot.Stock[_Bar.Stock], true);
                    for (std::size_t _Position = 0; _Position < _Bar.Placements.size(); ++_Position)
                    {
                        const auto _Found = _LocalIndex.find(_Bar.Placements[_Position].Instance);
                        if (_Found == _LocalIndex.end()) continue;
                        const auto _Part = _Found->second;
                        const auto _Direction = static_cast<std::size_t>(
                            _Bar.Placements[_Position].PartDirection);
                        _Model.AddHint(_Slot.Present[_Part], true);
                        _Model.AddHint(_Slot.Loop[_Part], false);
                        _Model.AddHint(_Slot.Direction[_Part][_Direction], true);
                        if (_Position == 0) _Model.AddHint(_Slot.Start[_Part], true);
                        if (_Position + 1 == _Bar.Placements.size())
                            _Model.AddHint(_Slot.End[_Part], true);
                        if (_Position + 1 < _Bar.Placements.size())
                        {
                            const auto _NextFound = _LocalIndex.find(
                                _Bar.Placements[_Position + 1].Instance);
                            if (_NextFound != _LocalIndex.end())
                            {
                                const auto& _Arc = _Slot.Arc[_Part][_NextFound->second];
                                if (_Arc) _Model.AddHint(*_Arc, true);
                            }
                        }
                    }
                }
            }

            std::vector<LinearExpr> _Objectives;
            if (Settings_.ObjectivePolicy == PairObjectivePolicy::MinPurchaseCost)
                _Objectives = {_TotalCost, _TotalNominalLength, _TotalBars, _TotalOccupied};
            else
                _Objectives = {_TotalNominalLength, _TotalBars, _TotalOccupied};

            SatParameters _Parameters;
            const auto _Hardware = (std::max)(1u, std::thread::hardware_concurrency());
            const auto _Workers = Settings_.ConstraintWorkers == 0
                ? (std::min)(std::size_t{8}, static_cast<std::size_t>(_Hardware))
                : Settings_.ConstraintWorkers;
            _Parameters.set_num_search_workers(static_cast<int>((std::max)(std::size_t{1}, _Workers)));
            _Parameters.set_random_seed(static_cast<std::int32_t>(Settings_.RandomSeed));
            _Parameters.set_log_search_progress(false);
            const auto _Started = std::chrono::steady_clock::now();
            CpSolverResponse _BestResponse;
            bool _HasResponse = false;
            bool _AllOptimal = true;
            for (const auto& _Objective : _Objectives)
            {
                if (TimeBudgetMilliseconds_ > 0)
                {
                    const auto _Elapsed = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - _Started).count());
                    if (_Elapsed >= TimeBudgetMilliseconds_)
                    {
                        _Result.TimeLimit = true;
                        _AllOptimal = false;
                        break;
                    }
                    _Parameters.set_max_time_in_seconds(
                        static_cast<double>(TimeBudgetMilliseconds_ - _Elapsed) / 1000.0);
                }
                _Model.ClearObjective();
                _Model.Minimize(_Objective);
                const auto _Response = SolveWithParameters(_Model.Build(), _Parameters);
                _Result.Branches += static_cast<std::uint64_t>((std::max)(
                    std::int64_t{0}, _Response.num_branches()));
                if (_Response.status() == CpSolverStatus::MODEL_INVALID)
                {
                    _Result.ModelInvalid = true;
                    _Result.Diagnostic = "CP-SAT rejected the complete tube-nesting model";
                    return _Result;
                }
                if (_Response.status() == CpSolverStatus::INFEASIBLE)
                {
                    if (!_HasResponse) _Result.Infeasible = true;
                    return _Result;
                }
                if (_Response.status() != CpSolverStatus::OPTIMAL
                    && _Response.status() != CpSolverStatus::FEASIBLE)
                {
                    _Result.TimeLimit = true;
                    _AllOptimal = false;
                    break;
                }
                _BestResponse = _Response;
                _HasResponse = true;
                if (_Response.status() != CpSolverStatus::OPTIMAL)
                {
                    _Result.TimeLimit = true;
                    _AllOptimal = false;
                    break;
                }
                _Model.AddEquality(_Objective,
                    SolutionIntegerValue(_Response, _Objective));
            }
            if (!_HasResponse) return _Result;

            WorkingPlan _Plan;
            std::vector<bool> _Placed(_Count, false);
            for (std::size_t _SlotIndex = 0; _SlotIndex < _Count; ++_SlotIndex)
            {
                const auto& _Slot = _Slots[_SlotIndex];
                if (!SolutionBooleanValue(_BestResponse, _Slot.Used)) continue;
                WorkingBar _Bar;
                _Bar.Stock = Stocks_.size();
                for (std::size_t _Stock = 0; _Stock < Stocks_.size(); ++_Stock)
                    if (SolutionBooleanValue(_BestResponse, _Slot.Stock[_Stock]))
                    {
                        _Bar.Stock = _Stock;
                        break;
                    }
                std::optional<std::size_t> _Current;
                for (std::size_t _Part = 0; _Part < _Count; ++_Part)
                    if (SolutionBooleanValue(_BestResponse, _Slot.Start[_Part]))
                    {
                        _Current = _Part;
                        break;
                    }
                std::size_t _Guard = 0;
                while (_Current && _Guard++ < _Count)
                {
                    const auto _Part = *_Current;
                    if (_Placed[_Part]) break;
                    _Placed[_Part] = true;
                    const auto _Direction = SolutionBooleanValue(
                        _BestResponse, _Slot.Direction[_Part][1])
                        ? Direction::Reverse : Direction::Forward;
                    _Bar.Placements.push_back({GroupInstances_[_Part], _Direction, 0.0});
                    if (SolutionBooleanValue(_BestResponse, _Slot.End[_Part])) break;
                    _Current.reset();
                    for (std::size_t _Next = 0; _Next < _Count; ++_Next)
                    {
                        const auto& _Arc = _Slot.Arc[_Part][_Next];
                        if (_Arc && SolutionBooleanValue(_BestResponse, *_Arc))
                        {
                            _Current = _Next;
                            break;
                        }
                    }
                }
                if (_Bar.Stock >= Stocks_.size() || _Bar.Placements.empty()
                    || !PopulateRotations(
                        _Bar, Instances_, Table_, Stocks_[_Bar.Stock]))
                {
                    _Result.ModelInvalid = true;
                    _Result.Diagnostic = "CP-SAT returned a solution that could not be reconstructed";
                    return _Result;
                }
                _Plan.push_back(std::move(_Bar));
            }
            if (std::ranges::any_of(_Placed, [](const bool Value_) { return !Value_; }))
            {
                _Result.ModelInvalid = true;
                _Result.Diagnostic = "CP-SAT solution omitted one or more nesting instances";
                return _Result;
            }
            _Result.Value = MeasurePlanScore(_Plan, Instances_, Table_, Stocks_);
            _Result.Plan = std::move(_Plan);
            _Result.Optimal = _AllOptimal;
            return _Result;
        }

        [[nodiscard]] bool ImproveConstraintNeighborhood(
            WorkingPlan& Plan_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
            const PairTableSolverSettings& Settings_, std::mt19937& Random_,
            std::uint64_t& Branches_, bool& TimeLimit_, std::string& Diagnostic_)
        {
            if (Plan_.empty() || Settings_.ConstraintNeighborhoodPartLimit < 2) return false;
            std::vector<std::size_t> _BarIndices(Plan_.size());
            std::iota(_BarIndices.begin(), _BarIndices.end(), 0);
            std::shuffle(_BarIndices.begin(), _BarIndices.end(), Random_);
            std::vector<bool> _Released(Plan_.size(), false);
            std::vector<std::size_t> _Instances;
            for (const auto _BarIndex : _BarIndices)
            {
                const auto _Size = Plan_[_BarIndex].Placements.size();
                if (_Size == 0 || _Size > Settings_.ConstraintNeighborhoodPartLimit) continue;
                if (!_Instances.empty()
                    && _Instances.size() + _Size > Settings_.ConstraintNeighborhoodPartLimit)
                    continue;
                _Released[_BarIndex] = true;
                for (const auto& _Placement : Plan_[_BarIndex].Placements)
                    _Instances.push_back(_Placement.Instance);
                if (_Instances.size() >= Settings_.ConstraintNeighborhoodPartLimit) break;
            }
            if (_Instances.size() < 2) return false;

            std::vector<std::int64_t> _RemainingAvailability;
            _RemainingAvailability.reserve(Stocks_.size());
            for (const auto& _Stock : Stocks_)
                _RemainingAvailability.push_back(_Stock.Quantity == 0
                    ? std::int64_t{-1} : static_cast<std::int64_t>(_Stock.Quantity));
            WorkingPlan _Fixed;
            WorkingPlan _Hint;
            for (std::size_t _Bar = 0; _Bar < Plan_.size(); ++_Bar)
            {
                if (_Released[_Bar])
                {
                    _Hint.push_back(Plan_[_Bar]);
                    continue;
                }
                _Fixed.push_back(Plan_[_Bar]);
                const auto _Stock = Plan_[_Bar].Stock;
                if (_Stock >= _RemainingAvailability.size()) return false;
                if (_RemainingAvailability[_Stock] >= 0)
                {
                    if (_RemainingAvailability[_Stock] == 0) return false;
                    --_RemainingAvailability[_Stock];
                }
            }
            const auto _Solved = SolveConstraintModel(
                _Instances, Instances_, Table_, Stocks_, Settings_,
                Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds, &_Hint,
                &_RemainingAvailability);
            Branches_ += _Solved.Branches;
            TimeLimit_ = TimeLimit_ || _Solved.TimeLimit;
            if (!_Solved.Diagnostic.empty()) Diagnostic_ = _Solved.Diagnostic;
            if (!_Solved.Plan) return false;
            WorkingPlan _Candidate = std::move(_Fixed);
            _Candidate.insert(_Candidate.end(),
                _Solved.Plan->begin(), _Solved.Plan->end());
            const auto _Before = MeasurePlanScore(Plan_, Instances_, Table_, Stocks_);
            const auto _After = MeasurePlanScore(_Candidate, Instances_, Table_, Stocks_);
            if (CompareScore(_After, _Before, Settings_.ObjectivePolicy) >= 0) return false;
            Plan_ = std::move(_Candidate);
            return true;
        }
#endif

        [[nodiscard]] Length BestConnection(
            const std::size_t TypeA_, const std::size_t TypeB_,
            const PairTableSnapshot& Table_)
        {
            Length _Best = (std::numeric_limits<Length>::min)();
            for (std::size_t _A = 0; _A < 2; ++_A)
                for (std::size_t _B = 0; _B < 2; ++_B)
                {
                    const auto& _Entry = Table_.At(TypeA_, static_cast<Direction>(_A),
                        TypeB_, static_cast<Direction>(_B));
                    if (IsEntryUsable(_Entry)) _Best = (std::max)(_Best, _Entry.NetSaving);
                }
            return _Best;
        }

        [[nodiscard]] std::vector<std::size_t> ChainOrder(
            const std::vector<std::size_t>& GroupInstances_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_)
        {
            if (GroupInstances_.empty()) return {};
            std::vector<std::vector<std::size_t>> _ByType(Table_.Types.size());
            for (const auto _Instance : GroupInstances_)
                _ByType[Instances_[_Instance].Type].push_back(_Instance);
            std::size_t _CurrentType = Instances_[*std::max_element(
                GroupInstances_.begin(), GroupInstances_.end(), [&](const auto Left_, const auto Right_)
                {
                    return Table_.Types[Instances_[Left_].Type].MaximumLength
                        < Table_.Types[Instances_[Right_].Type].MaximumLength;
                })].Type;
            std::vector<std::size_t> _Order;
            _Order.reserve(GroupInstances_.size());
            while (_Order.size() < GroupInstances_.size())
            {
                if (!_ByType[_CurrentType].empty())
                {
                    _Order.push_back(_ByType[_CurrentType].back());
                    _ByType[_CurrentType].pop_back();
                }
                if (_Order.size() == GroupInstances_.size()) break;
                std::optional<std::size_t> _Next;
                Length _Best = (std::numeric_limits<Length>::min)();
                for (std::size_t _Type = 0; _Type < _ByType.size(); ++_Type)
                {
                    if (_ByType[_Type].empty()) continue;
                    const Length _Saving = BestConnection(_CurrentType, _Type, Table_);
                    if (!_Next || _Saving > _Best
                        || (_Saving == _Best && Table_.Types[_Type].MaximumLength
                            > Table_.Types[*_Next].MaximumLength))
                    {
                        _Next = _Type;
                        _Best = _Saving;
                    }
                }
                if (!_Next) break;
                _CurrentType = *_Next;
            }
            return _Order;
        }

        [[nodiscard]] Length LocalEdgeScore(
            const std::vector<std::size_t>& Order_, const std::size_t Edge_,
            const std::vector<Instance>& Instances_, const PairTableSnapshot& Table_)
        {
            if (Edge_ + 1 >= Order_.size()) return 0;
            const Length _Value = BestConnection(
                Instances_[Order_[Edge_]].Type, Instances_[Order_[Edge_ + 1]].Type, Table_);
            return _Value == (std::numeric_limits<Length>::min)() ? -kInfinity : _Value;
        }

        void ImproveOrderProxy(
            std::vector<std::size_t>& Order_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, std::mt19937& Random_,
            const PairTableSolverSettings& Settings_)
        {
            if (Order_.size() < 3) return;
            const std::size_t _Attempts = (std::min)(
                Settings_.EvaluationLimit > 0 ? static_cast<std::size_t>(Settings_.EvaluationLimit)
                    : Order_.size() * Settings_.CandidateWidth,
                Order_.size() * (std::max)(std::size_t{1}, Settings_.CandidateWidth));
            std::uniform_int_distribution<std::size_t> _Position(0, Order_.size() - 1);
            for (std::size_t _Attempt = 0; _Attempt < _Attempts; ++_Attempt)
            {
                const std::size_t _Left = _Position(Random_);
                const std::size_t _Right = _Position(Random_);
                if (_Left == _Right) continue;
                std::array<std::size_t, 4> _Edges{
                    _Left ? _Left - 1 : _Left, _Left,
                    _Right ? _Right - 1 : _Right, _Right
                };
                std::sort(_Edges.begin(), _Edges.end());
                const auto _End = std::unique(_Edges.begin(), _Edges.end());
                Length _Before = 0;
                for (auto _It = _Edges.begin(); _It != _End; ++_It)
                    _Before += LocalEdgeScore(Order_, *_It, Instances_, Table_);
                std::swap(Order_[_Left], Order_[_Right]);
                Length _After = 0;
                for (auto _It = _Edges.begin(); _It != _End; ++_It)
                    _After += LocalEdgeScore(Order_, *_It, Instances_, Table_);
                if (_After < _Before) std::swap(Order_[_Left], Order_[_Right]);
            }
        }

        [[nodiscard]] long double SelectedEdgeScore(
            const std::vector<std::size_t>& Order_,
            const std::vector<std::size_t>& Positions_,
            const std::vector<Instance>& Instances_, const PairTableSnapshot& Table_)
        {
            std::vector<std::size_t> _Edges;
            _Edges.reserve(Positions_.size() * 2);
            for (const auto _Position : Positions_)
            {
                if (_Position > 0) _Edges.push_back(_Position - 1);
                if (_Position + 1 < Order_.size()) _Edges.push_back(_Position);
            }
            std::sort(_Edges.begin(), _Edges.end());
            _Edges.erase(std::unique(_Edges.begin(), _Edges.end()), _Edges.end());
            long double _Score = 0.0L;
            for (const auto _Edge : _Edges)
            {
                const auto _Saving = LocalEdgeScore(Order_, _Edge, Instances_, Table_);
                if (_Saving <= -kInfinity) return -(std::numeric_limits<long double>::infinity)();
                _Score += static_cast<long double>(_Saving);
            }
            return _Score;
        }

        /**
         * Exactly reorder a small, randomly selected set of positions against
         * the pair-table edge proxy.  The expensive finite-stock decoder is
         * invoked only once for the best permutation, not once per leaf.
         */
        [[nodiscard]] bool ReorderLargeNeighborhood(
            std::vector<std::size_t>& Order_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, std::mt19937& Random_,
            const PairTableSolverSettings& Settings_, std::uint64_t& Evaluations_)
        {
            if (Order_.size() < 4 || Evaluations_ >= Settings_.EvaluationLimit) return false;
            const std::size_t _Requested = Settings_.ExactPartLimit > 0
                ? Settings_.ExactPartLimit : std::size_t{6};
            const std::size_t _Width = (std::min)(
                Order_.size(), (std::clamp)(_Requested, std::size_t{4}, std::size_t{8}));
            std::vector<std::size_t> _Positions(Order_.size());
            std::iota(_Positions.begin(), _Positions.end(), 0);
            std::shuffle(_Positions.begin(), _Positions.end(), Random_);
            _Positions.resize(_Width);
            std::sort(_Positions.begin(), _Positions.end());

            std::vector<std::size_t> _Selected;
            _Selected.reserve(_Width);
            for (const auto _Position : _Positions) _Selected.push_back(Order_[_Position]);
            auto _Candidate = Order_;
            auto _Best = Order_;
            long double _BestScore = SelectedEdgeScore(
                Order_, _Positions, Instances_, Table_);
            bool _Complete = true;
            std::vector<bool> _Used(_Width, false);
            std::function<void(std::size_t)> _Search = [&](const std::size_t Depth_)
            {
                if (!_Complete) return;
                if (Depth_ == _Width)
                {
                    if (++Evaluations_ > Settings_.EvaluationLimit)
                    {
                        _Complete = false;
                        return;
                    }
                    const auto _Score = SelectedEdgeScore(
                        _Candidate, _Positions, Instances_, Table_);
                    if (_Score > _BestScore)
                    {
                        _BestScore = _Score;
                        _Best = _Candidate;
                    }
                    return;
                }
                std::set<std::size_t> _TriedTypes;
                for (std::size_t _Index = 0; _Index < _Width; ++_Index)
                {
                    if (_Used[_Index]) continue;
                    const auto _Type = Instances_[_Selected[_Index]].Type;
                    if (!_TriedTypes.insert(_Type).second) continue;
                    _Used[_Index] = true;
                    _Candidate[_Positions[Depth_]] = _Selected[_Index];
                    _Search(Depth_ + 1);
                    _Used[_Index] = false;
                    if (!_Complete) break;
                }
            };
            _Search(0);
            if (_Best == Order_) return false;
            Order_ = std::move(_Best);
            return true;
        }

        struct SearchClock final
        {
            std::chrono::steady_clock::time_point Started = std::chrono::steady_clock::now();
            std::uint64_t LimitMilliseconds = 0;
            [[nodiscard]] bool Expired() const
            {
                return LimitMilliseconds > 0
                    && std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - Started).count()
                        >= static_cast<std::int64_t>(LimitMilliseconds);
            }
        };

        struct GroupSolve final
        {
            std::optional<WorkingPlan> Plan;
            Score Value;
            bool GlobalExact = false;
            bool ProvenInfeasible = false;
            bool FixedOrderExact = false;
            bool StateLimit = false;
            bool TimeLimit = false;
            bool UsedLargeNeighborhood = false;
            bool UsedConstraintBackend = false;
            std::uint64_t Nodes = 0;
            std::string ConstraintDiagnostic;
        };

        [[nodiscard]] GroupSolve SolveGroup(
            const std::vector<std::size_t>& GroupInstances_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
            const PairTableSolverSettings& Settings_, std::mt19937& Random_, SearchClock& Clock_)
        {
            GroupSolve _Output;
            std::vector<std::vector<std::size_t>> _Orders;
            auto _Descending = GroupInstances_;
            std::stable_sort(_Descending.begin(), _Descending.end(), [&](const auto Left_, const auto Right_)
            {
                const auto& _Left = Table_.Types[Instances_[Left_].Type];
                const auto& _Right = Table_.Types[Instances_[Right_].Type];
                return std::tie(_Left.MaximumLength, _Left.ID)
                    > std::tie(_Right.MaximumLength, _Right.ID);
            });
            _Orders.push_back(_Descending);
            _Orders.push_back(ChainOrder(GroupInstances_, Instances_, Table_));
            for (std::size_t _Run = 2; _Run < (std::max)(std::size_t{2}, Settings_.ConstructionRuns); ++_Run)
            {
                auto _Order = _Descending;
                std::shuffle(_Order.begin(), _Order.end(), Random_);
                _Orders.push_back(std::move(_Order));
            }
            std::vector<std::size_t> _BestOrder;
            for (auto& _Order : _Orders)
            {
                if (Clock_.Expired()) { _Output.TimeLimit = true; break; }
                const auto _Decoded = DecodeFixedOrder(
                    _Order, Instances_, Table_, Stocks_, Settings_);
                _Output.StateLimit = _Output.StateLimit || _Decoded.StateLimit;
                if (!_Decoded.Plan) continue;
                if (!_Output.Plan || CompareScore(
                    _Decoded.Value, _Output.Value, Settings_.ObjectivePolicy) < 0)
                {
                    _Output.Plan = *_Decoded.Plan;
                    _Output.Value = _Decoded.Value;
                    _Output.FixedOrderExact = _Decoded.Exact;
                    _BestOrder = _Order;
                }
            }
            if (_Output.Plan && !_BestOrder.empty())
            {
                for (std::size_t _Pass = 0; _Pass < Settings_.LocalSearchPasses; ++_Pass)
                {
                    if (Clock_.Expired()) { _Output.TimeLimit = true; break; }
                    auto _CandidateOrder = _BestOrder;
                    ImproveOrderProxy(_CandidateOrder, Instances_, Table_, Random_, Settings_);
                    const auto _Candidate = DecodeFixedOrder(
                        _CandidateOrder, Instances_, Table_, Stocks_, Settings_);
                    _Output.StateLimit = _Output.StateLimit || _Candidate.StateLimit;
                    if (_Candidate.Plan && CompareScore(
                        _Candidate.Value, _Output.Value, Settings_.ObjectivePolicy) < 0)
                    {
                        _Output.Plan = *_Candidate.Plan;
                        _Output.Value = _Candidate.Value;
                        _Output.FixedOrderExact = _Candidate.Exact;
                        _BestOrder = std::move(_CandidateOrder);
                    }
                }
            }

#if ICAX_TUBE_NESTING_WITH_ORTOOLS
            if (Settings_.Constraint != ConstraintMode::Disabled
                && GroupInstances_.size() <= Settings_.ExactPartLimit && !Clock_.Expired())
            {
                const WorkingPlan* _Hint = _Output.Plan ? &*_Output.Plan : nullptr;
                const auto _Constraint = SolveConstraintModel(
                    GroupInstances_, Instances_, Table_, Stocks_, Settings_,
                    Settings_.ConstraintTimeBudgetMilliseconds, _Hint);
                _Output.UsedConstraintBackend = true;
                _Output.Nodes += _Constraint.Branches;
                if (!_Constraint.Diagnostic.empty())
                    _Output.ConstraintDiagnostic = _Constraint.Diagnostic;
                if (_Constraint.Plan)
                {
                    // Re-run the inexpensive exact direction/split decoder on
                    // the order returned by CP-SAT.  A merely FEASIBLE CP
                    // response must not be mislabeled as a fixed-order proof,
                    // and the materialized directions must match the score used
                    // to compare it with the incumbent.
                    std::vector<std::size_t> _ConstraintOrder;
                    _ConstraintOrder.reserve(GroupInstances_.size());
                    for (const auto& _Bar : *_Constraint.Plan)
                        for (const auto& _Placement : _Bar.Placements)
                            _ConstraintOrder.push_back(_Placement.Instance);
                    const auto _DecodedConstraint = DecodeFixedOrder(
                        _ConstraintOrder, Instances_, Table_, Stocks_, Settings_);
                    _Output.StateLimit = _Output.StateLimit
                        || _DecodedConstraint.StateLimit;
                    const auto& _CandidatePlan = _DecodedConstraint.Plan
                        ? *_DecodedConstraint.Plan : *_Constraint.Plan;
                    const auto& _CandidateValue = _DecodedConstraint.Plan
                        ? _DecodedConstraint.Value : _Constraint.Value;
                    const bool _CandidateFixedExact = _DecodedConstraint.Plan
                        ? _DecodedConstraint.Exact : _Constraint.Optimal;
                    if (!_Output.Plan || CompareScore(
                        _CandidateValue, _Output.Value,
                        Settings_.ObjectivePolicy) <= 0)
                    {
                        _Output.Plan = _CandidatePlan;
                        _Output.Value = _CandidateValue;
                        _Output.FixedOrderExact = _CandidateFixedExact;
                    }
                }
                _Output.GlobalExact = _Constraint.Optimal && _Constraint.Plan.has_value();
                _Output.ProvenInfeasible = _Constraint.Infeasible;
                _Output.TimeLimit = _Output.TimeLimit || _Constraint.TimeLimit;
            }
#endif

            if (Settings_.Constraint == ConstraintMode::FullExact
                && GroupInstances_.size() > Settings_.ExactPartLimit)
            {
                _Output.StateLimit = true;
                _Output.ConstraintDiagnostic =
                    "full_exact group exceeds ExactPartLimit; incumbent is feasible but not proven optimal";
            }

            if (_Output.Plan && !_BestOrder.empty()
                && Settings_.Constraint == ConstraintMode::HybridLargeNeighborhood
                && GroupInstances_.size() > Settings_.ExactPartLimit)
            {
#if ICAX_TUBE_NESTING_WITH_ORTOOLS
                for (std::size_t _Iteration = 0;
                    _Iteration < Settings_.LargeNeighborhoodIterations; ++_Iteration)
                {
                    if (Clock_.Expired())
                    {
                        _Output.TimeLimit = true;
                        break;
                    }
                    bool _NeighborhoodTimeLimit = false;
                    std::string _Diagnostic;
                    _Output.UsedConstraintBackend = true;
                    _Output.UsedLargeNeighborhood = true;
                    if (ImproveConstraintNeighborhood(
                        *_Output.Plan, Instances_, Table_, Stocks_, Settings_, Random_,
                        _Output.Nodes, _NeighborhoodTimeLimit, _Diagnostic))
                    {
                        _Output.Value = MeasurePlanScore(
                            *_Output.Plan, Instances_, Table_, Stocks_);
                        _Output.FixedOrderExact = false;
                        _BestOrder.clear();
                        for (const auto& _Bar : *_Output.Plan)
                            for (const auto& _Placement : _Bar.Placements)
                                _BestOrder.push_back(_Placement.Instance);
                    }
                    if (!_Diagnostic.empty()) _Output.ConstraintDiagnostic = _Diagnostic;
                }
#else
                std::uint64_t _Evaluations = 0;
                for (std::size_t _Iteration = 0;
                    _Iteration < Settings_.LargeNeighborhoodIterations; ++_Iteration)
                {
                    if (Clock_.Expired())
                    {
                        _Output.TimeLimit = true;
                        break;
                    }
                    auto _CandidateOrder = _BestOrder;
                    if (!ReorderLargeNeighborhood(
                        _CandidateOrder, Instances_, Table_, Random_, Settings_, _Evaluations))
                    {
                        if (_Evaluations >= Settings_.EvaluationLimit) break;
                        continue;
                    }
                    _Output.UsedLargeNeighborhood = true;
                    const auto _Candidate = DecodeFixedOrder(
                        _CandidateOrder, Instances_, Table_, Stocks_, Settings_);
                    _Output.StateLimit = _Output.StateLimit || _Candidate.StateLimit;
                    if (_Candidate.Plan && CompareScore(
                        _Candidate.Value, _Output.Value, Settings_.ObjectivePolicy) < 0)
                    {
                        _Output.Plan = *_Candidate.Plan;
                        _Output.Value = _Candidate.Value;
                        _Output.FixedOrderExact = _Candidate.Exact;
                        _BestOrder = std::move(_CandidateOrder);
                    }
                }
                _Output.Nodes += _Evaluations;
#endif
            }

            if (Settings_.Constraint != ConstraintMode::Disabled
                && !_Output.GlobalExact
                && !_Output.ProvenInfeasible
                && GroupInstances_.size() <= Settings_.ExactPartLimit && !Clock_.Expired())
            {
                std::vector<std::size_t> _Permutation(GroupInstances_.size());
                std::vector<bool> _Used(GroupInstances_.size(), false);
                bool _Complete = true;
                std::function<void(std::size_t)> _Search = [&](const std::size_t Depth_)
                {
                    if (!_Complete) return;
                    if (Clock_.Expired())
                    {
                        _Output.TimeLimit = true;
                        _Complete = false;
                        return;
                    }
                    if (++_Output.Nodes > Settings_.ExactNodeLimit)
                    {
                        _Output.StateLimit = true;
                        _Complete = false;
                        return;
                    }
                    if (Depth_ == _Permutation.size())
                    {
                        const auto _Decoded = DecodeFixedOrder(
                            _Permutation, Instances_, Table_, Stocks_, Settings_);
                        if (_Decoded.StateLimit)
                        {
                            _Output.StateLimit = true;
                            _Complete = false;
                            return;
                        }
                        if (_Decoded.Plan && (!_Output.Plan || CompareScore(
                            _Decoded.Value, _Output.Value, Settings_.ObjectivePolicy) < 0))
                        {
                            _Output.Plan = *_Decoded.Plan;
                            _Output.Value = _Decoded.Value;
                            _Output.FixedOrderExact = true;
                        }
                        return;
                    }
                    std::unordered_set<std::size_t> _TriedTypes;
                    for (std::size_t _Index = 0; _Index < GroupInstances_.size(); ++_Index)
                    {
                        if (_Used[_Index]) continue;
                        const auto _Type = Instances_[GroupInstances_[_Index]].Type;
                        if (!_TriedTypes.insert(_Type).second) continue;
                        _Used[_Index] = true;
                        _Permutation[Depth_] = GroupInstances_[_Index];
                        _Search(Depth_ + 1);
                        _Used[_Index] = false;
                        if (!_Complete) break;
                    }
                };
                _Search(0);
                _Output.GlobalExact = _Complete && _Output.Plan.has_value();
                _Output.ProvenInfeasible = _Complete && !_Output.Plan.has_value();
            }
            return _Output;
        }

        void AppendPlan(
            SolveResult& Result_, const WorkingPlan& Plan_, const std::vector<Instance>& Instances_,
            const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
            std::map<std::string, std::size_t>& StockOrdinals_)
        {
            for (const auto& _Bar : Plan_)
            {
                if (_Bar.Placements.empty()) continue;
                const auto& _Stock = Stocks_[_Bar.Stock];
                StockPlan _Output;
                _Output.StockTypeID = _Stock.SourceTypeID.empty()
                    ? _Stock.ID : _Stock.SourceTypeID;
                _Output.StockInstanceID = IsContinuation(_Stock)
                    ? _Stock.FixedInstanceID
                    : _Output.StockTypeID + "#"
                        + std::to_string(++StockOrdinals_[_Output.StockTypeID]);
                _Output.CompatibilityKey = _Stock.CompatibilityKey;
                _Output.Kind = _Stock.Kind;
                _Output.Continued = IsContinuation(_Stock);
                _Output.TotalLength = _Stock.TotalLength;
                _Output.FrontMargin = _Stock.FrontMargin;
                _Output.TailDeadZone = _Stock.TailDeadZone;
                _Output.InitialProcessedEnd = IsContinuation(_Stock)
                    ? _Stock.InitialProcessedEnd : _Stock.FrontMargin;
                Length _Start = _Output.InitialProcessedEnd;
                Length _Material = 0;
                std::size_t _OrderChanges = 0;
                for (std::size_t _Index = 0; _Index < _Bar.Placements.size(); ++_Index)
                {
                    const auto& _Working = _Bar.Placements[_Index];
                    const auto& _Instance = Instances_[_Working.Instance];
                    const auto& _Type = Table_.Types[_Instance.Type];
                    Length _Saving = 0;
                    std::uint32_t _PoseIndex = 0;
                    if (!_Index && IsContinuation(_Stock)
                        && _Stock.InitialPredecessorType
                            != (std::numeric_limits<std::size_t>::max)())
                    {
                        const auto* _Entry = InitialPairEntry(
                            _Stock, _Instance.Type, _Working.PartDirection, Table_);
                        if (!_Entry || _Entry->NetSaving > _Stock.InitialProcessedEnd)
                            throw std::logic_error(
                                "continued stock plan contains an invalid start edge");
                        _Saving = _Entry->NetSaving;
                        _PoseIndex = _Entry->PoseMetadataIndex;
                        _Start = _Stock.InitialProcessedEnd - _Saving;
                    }
                    else if (_Index)
                    {
                        const auto& _Prior = _Bar.Placements[_Index - 1];
                        const auto& _PriorInstance = Instances_[_Prior.Instance];
                        const auto& _Entry = Table_.At(
                            _PriorInstance.Type, _Prior.PartDirection,
                            _Instance.Type, _Working.PartDirection);
                        _Saving = _Entry.NetSaving;
                        _PoseIndex = _Entry.PoseMetadataIndex;
                        const auto& _PriorType = Table_.Types[_PriorInstance.Type];
                        _Start = _Output.Placements.back().Start
                            + _PriorType.MaximumLength - _Saving;
                        if (!_PriorType.OrderKey.empty() && !_Type.OrderKey.empty()
                            && _PriorType.OrderKey != _Type.OrderKey)
                            ++_OrderChanges;
                    }
                    const Length _End = _Start + _Type.MaximumLength;
                    _Material += _Type.MaterialLength > 0
                        ? _Type.MaterialLength : _Type.MaximumLength;
                    _Output.Placements.push_back(Placement{
                        _Type.ID, _Instance.ID,
                        _Working.PartDirection == Direction::Forward ? "forward" : "reverse",
                        _Type.OrderKey, _Start, _End,
                        (_Index || IsContinuation(_Stock)) ? -_Saving : 0,
                        _Working.PartDirection == Direction::Reverse,
                        _Working.RotationRadians, 0, false,
                        _Working.PartDirection, _Saving, _PoseIndex
                    });
                }
                _Output.ProcessedEnd = _Output.Placements.back().End;
                _Output.RemainingLength = _Stock.TotalLength - _Output.ProcessedEnd;
                _Output.OccupiedLength = _Output.ProcessedEnd
                    - _Output.InitialProcessedEnd;
                _Output.PartLength = _Material;
                _Output.GapLength = _Output.OccupiedLength - _Material;
                // Pair savings do not prove that two contours are a common
                // cut.  Count every internal interface as two cuts and leave
                // CommonCutCount at zero until that fact is carried explicitly
                // by the immutable pair table.
                _Output.CutCount = _Output.Placements.size()
                    + (_Output.Placements.size() - 1)
                    + (!IsContinuation(_Stock) && _Stock.FrontMargin > 0 ? 1U : 0U);
                _Output.UnrecoverableWaste = (IsContinuation(_Stock) ? 0 : _Stock.FrontMargin)
                    + (std::max)(Length{0}, _Output.GapLength);
                if (_Stock.MinReusableRemainder > 0
                    && _Output.RemainingLength >= _Stock.MinReusableRemainder)
                {
                    _Output.ReusableRemainderLength = _Output.RemainingLength;
                }
                else
                    _Output.UnrecoverableWaste += _Output.RemainingLength;
                Result_.Metrics.StockCost += StockCost(_Stock);
                Result_.Metrics.TotalStockLength += _Stock.TotalLength;
                Result_.Metrics.PartLength += _Output.PartLength;
                Result_.Metrics.GapLength += _Output.GapLength;
                Result_.Metrics.UnrecoverableWaste += _Output.UnrecoverableWaste;
                Result_.Metrics.ReusableRemainderLength += _Output.ReusableRemainderLength;
                Result_.Metrics.CutCount += _Output.CutCount;
                Result_.Metrics.CommonCutCount += _Output.CommonCutCount;
                Result_.Metrics.OrderChangeCount += _OrderChanges;
                if (!IsContinuation(_Stock))
                {
                    Result_.Metrics.OpenedStockCount++;
                    if (_Stock.Kind == StockKind::Standard)
                        Result_.Metrics.StandardStockCount++;
                }
                if (_Output.ReusableRemainderLength > 0) Result_.Metrics.ReusableRemainderCount++;
                Result_.Stocks.push_back(std::move(_Output));
            }
        }

        [[nodiscard]] std::vector<std::string> ValidateStocks(
            const std::vector<StockType>& Stocks_, const PairTableSnapshot& Table_)
        {
            std::vector<std::string> _Diagnostics;
            std::set<std::string> _IDs, _InstanceIDs;
            for (const auto& _Stock : Stocks_)
            {
                if (_Stock.ID.empty() || _Stock.CompatibilityKey.empty()
                    || !_IDs.insert(_Stock.ID).second)
                    _Diagnostics.emplace_back("stock IDs and compatibility keys must be non-empty and unique");
                if (_Stock.TotalLength <= 0 || _Stock.FrontMargin < 0
                    || _Stock.TailDeadZone < 0 || _Stock.MinReusableRemainder < 0
                    || UsableLength(_Stock) < 0)
                    _Diagnostics.emplace_back("stock contains invalid lengths or margins");
                if (_Stock.CostUnits < -1)
                    _Diagnostics.emplace_back("stock cost must be non-negative or -1 for its default");
                if (_Stock.Quantity > static_cast<std::size_t>(
                    (std::numeric_limits<std::int64_t>::max)()))
                    _Diagnostics.emplace_back(
                        "finite stock quantity exceeds the backend integer range");
                if (IsContinuation(_Stock))
                {
                    if (!_InstanceIDs.insert(_Stock.FixedInstanceID).second)
                        _Diagnostics.emplace_back(
                            "continued stock instance IDs must be unique");
                    if (_Stock.Quantity != 1)
                        _Diagnostics.emplace_back(
                            "continued stock must have finite quantity one");
                    Length _ReservedEnd = 0;
                    if (_Stock.InitialProcessedEnd < 0
                        || !TryAdd(_Stock.InitialProcessedEnd,
                            _Stock.TailDeadZone, _ReservedEnd)
                        || _ReservedEnd >= _Stock.TotalLength)
                        _Diagnostics.emplace_back(
                            "continued stock has no usable open tail");
                    if (!std::isfinite(_Stock.InitialPredecessorRotationRadians))
                        _Diagnostics.emplace_back(
                            "continued stock predecessor rotation must be finite");
                    if (!_Stock.InitialPredecessorTypeID.empty())
                    {
                        if (_Stock.InitialPredecessorType >= Table_.Types.size())
                            _Diagnostics.emplace_back(
                                "continued stock predecessor is missing from the pair table");
                        else
                        {
                            const auto& _Predecessor =
                                Table_.Types[_Stock.InitialPredecessorType];
                            if (_Predecessor.ID != _Stock.InitialPredecessorTypeID
                                || _Predecessor.CompatibilityKey
                                    != _Stock.CompatibilityKey)
                                _Diagnostics.emplace_back(
                                    "continued stock predecessor is incompatible");
                            if (!_Predecessor.DirectionAllowed[static_cast<std::size_t>(
                                _Stock.InitialPredecessorDirection)])
                                _Diagnostics.emplace_back(
                                    "continued stock predecessor direction is forbidden");
                        }
                    }
                }
                else if (_Stock.InitialProcessedEnd != 0
                    || !_Stock.InitialPredecessorTypeID.empty()
                    || _Stock.InitialPredecessorType
                        != (std::numeric_limits<std::size_t>::max)())
                    _Diagnostics.emplace_back(
                        "stock fixed-prefix fields require a fixed instance ID");
            }
            return _Diagnostics;
        }
    }

    SolveResult PairTableSolver::Solve(
        const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
        const PairTableSolverSettings& Settings_) const
    {
        SolveResult _Result;
        _Result.PairTableVersion = Table_.Version;
        ValidatePairTable(Table_, &_Result.Diagnostics);
        std::vector<StockType> _ResolvedStocks = Stocks_;
        std::unordered_map<std::string, std::size_t> _TypeIndices;
        for (std::size_t _Type = 0; _Type < Table_.Types.size(); ++_Type)
            _TypeIndices.emplace(Table_.Types[_Type].ID, _Type);
        for (auto& _Stock : _ResolvedStocks)
        {
            _Stock.InitialPredecessorType =
                (std::numeric_limits<std::size_t>::max)();
            if (_Stock.InitialPredecessorTypeID.empty()) continue;
            const auto _Found = _TypeIndices.find(_Stock.InitialPredecessorTypeID);
            if (_Found != _TypeIndices.end())
                _Stock.InitialPredecessorType = _Found->second;
        }
        const auto _StockDiagnostics = ValidateStocks(_ResolvedStocks, Table_);
        _Result.Diagnostics.insert(_Result.Diagnostics.end(),
            _StockDiagnostics.begin(), _StockDiagnostics.end());
        if (Settings_.CandidateWidth == 0 || Settings_.FiniteInventoryLabelLimit == 0)
            _Result.Diagnostics.emplace_back("pair-table solver settings contain a zero search limit");
        if (Settings_.Constraint != ConstraintMode::Disabled
            && (Settings_.ExactNodeLimit == 0 || Settings_.ExactPartLimit == 0
                || Settings_.ConstraintTimeBudgetMilliseconds == 0))
            _Result.Diagnostics.emplace_back("constraint solving requires non-zero exact and time limits");
        if (Settings_.ConstraintWorkers
            > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            _Result.Diagnostics.emplace_back("constraint worker count exceeds the backend integer range");
        if (Settings_.Constraint == ConstraintMode::HybridLargeNeighborhood
            && Settings_.LargeNeighborhoodIterations > 0
            && (Settings_.ConstraintNeighborhoodPartLimit < 2
                || Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds == 0))
            _Result.Diagnostics.emplace_back("constraint neighborhoods require at least two parts and a time budget");
        if (!_Result.Diagnostics.empty())
        {
            _Result.Status = SolveStatus::InvalidInput;
            _Result.Termination = TerminationReason::InvalidInput;
            return _Result;
        }
        if (Table_.Types.empty())
        {
            _Result.Status = SolveStatus::Optimal;
            _Result.ExactSearchCompleted = true;
            _Result.ProvenScope = ProofScope::FullIntegerModel;
            return _Result;
        }

        long double _InstanceCountBound = 0.0L;
        long double _PartLengthBound = 0.0L;
        for (const auto& _Type : Table_.Types)
        {
            _InstanceCountBound += static_cast<long double>(_Type.Quantity);
            _PartLengthBound += static_cast<long double>(_Type.Quantity)
                * static_cast<long double>(_Type.MaximumLength);
        }
        long double _MaximumExtraSeparation = 0.0L;
        for (const auto& _Entry : Table_.Entries)
            if (IsEntryUsable(_Entry) && _Entry.NetSaving < 0)
                _MaximumExtraSeparation = (std::max)(_MaximumExtraSeparation,
                    -static_cast<long double>(_Entry.NetSaving));
        long double _MaximumStockLength = 0.0L;
        long double _MaximumStockCost = 0.0L;
        for (const auto& _Stock : _ResolvedStocks)
        {
            _MaximumStockLength = (std::max)(_MaximumStockLength,
                static_cast<long double>(_Stock.TotalLength));
            _MaximumStockCost = (std::max)(_MaximumStockCost,
                static_cast<long double>(StockCost(_Stock)));
        }
        const long double _SafeInteger = static_cast<long double>(
            (std::numeric_limits<std::int64_t>::max)()) / 16.0L;
        if (_PartLengthBound + _InstanceCountBound * _MaximumExtraSeparation > _SafeInteger
            || _InstanceCountBound * _MaximumStockLength > _SafeInteger
            || _InstanceCountBound * _MaximumStockCost > _SafeInteger)
        {
            _Result.Status = SolveStatus::InvalidInput;
            _Result.Termination = TerminationReason::InvalidInput;
            _Result.Diagnostics.emplace_back(
                "aggregate nesting lengths or costs exceed the safe int64 range");
            return _Result;
        }

        std::vector<Instance> _Instances;
        std::map<std::string, std::vector<std::size_t>> _Groups;
        constexpr std::size_t _MaximumInstances = 1'000'000;
        std::size_t _TotalInstances = 0;
        for (const auto& _Definition : Table_.Types)
        {
            if (_Definition.Quantity > _MaximumInstances - _TotalInstances)
            {
                _Result.Status = SolveStatus::InvalidInput;
                _Result.Termination = TerminationReason::InvalidInput;
                _Result.Diagnostics.emplace_back(
                    "pair-table demand exceeds the one-million-instance safety limit");
                return _Result;
            }
            _TotalInstances += _Definition.Quantity;
        }
        _Instances.reserve(_TotalInstances);
        for (std::size_t _Type = 0; _Type < Table_.Types.size(); ++_Type)
        {
            const auto& _Definition = Table_.Types[_Type];
            for (std::size_t _Ordinal = 1; _Ordinal <= _Definition.Quantity; ++_Ordinal)
            {
                std::ostringstream _ID;
                _ID << _Definition.ID << '#';
                _ID.width(4);
                _ID.fill('0');
                _ID << _Ordinal;
                const auto _Index = _Instances.size();
                _Instances.push_back({_Type, _Ordinal, _ID.str()});
                _Groups[_Definition.CompatibilityKey].push_back(_Index);
            }
        }

        SearchClock _Clock;
        _Clock.LimitMilliseconds = Settings_.TimeBudgetMilliseconds;
        std::mt19937 _Random(Settings_.RandomSeed);
        bool _AllFeasible = true;
        bool _AllGlobalExact = true;
        bool _AllFixedOrderExact = true;
        bool _AnyProvenInfeasible = false;
        bool _AnyUnprovenFailure = false;
        bool _UsedLargeNeighborhood = false;
        bool _UsedConstraintBackend = false;
        std::map<std::string, std::size_t> _StockOrdinals;
        for (const auto& [_Compatibility, _GroupInstances] : _Groups)
        {
            std::vector<StockType> _GroupStocks;
            std::ranges::copy_if(_ResolvedStocks, std::back_inserter(_GroupStocks), [&](const auto& Stock_)
            {
                return Stock_.CompatibilityKey == _Compatibility;
            });
            if (_GroupStocks.empty())
            {
                _AllFeasible = false;
                _AllGlobalExact = false;
                _AnyProvenInfeasible = true;
                for (const auto _Instance : _GroupInstances)
                    _Result.UnplacedInstances.push_back(_Instances[_Instance].ID);
                _Result.Diagnostics.push_back(
                    "compatibility group '" + _Compatibility + "' has no stock");
                continue;
            }
            auto _Solved = SolveGroup(
                _GroupInstances, _Instances, Table_, _GroupStocks,
                Settings_, _Random, _Clock);
            _Result.Metrics.SearchNodes += _Solved.Nodes;
            _UsedLargeNeighborhood = _UsedLargeNeighborhood || _Solved.UsedLargeNeighborhood;
            _UsedConstraintBackend = _UsedConstraintBackend || _Solved.UsedConstraintBackend;
            if (!_Solved.ConstraintDiagnostic.empty())
                _Result.Diagnostics.push_back(
                    "compatibility group '" + _Compatibility + "': "
                    + _Solved.ConstraintDiagnostic);
            if (!_Solved.Plan)
            {
                _AllFeasible = false;
                _AllGlobalExact = false;
                _AnyProvenInfeasible = _AnyProvenInfeasible || _Solved.ProvenInfeasible;
                _AnyUnprovenFailure = _AnyUnprovenFailure || !_Solved.ProvenInfeasible;
                for (const auto _Instance : _GroupInstances)
                    _Result.UnplacedInstances.push_back(_Instances[_Instance].ID);
                _Result.Diagnostics.push_back(
                    "no complete plan was found for compatibility group '" + _Compatibility + "'");
                if (_Solved.StateLimit) _Result.Termination = TerminationReason::StateLimit;
                if (_Solved.TimeLimit) _Result.Termination = TerminationReason::TimeLimit;
                continue;
            }
            AppendPlan(_Result, *_Solved.Plan, _Instances, Table_, _GroupStocks, _StockOrdinals);
            _AllGlobalExact = _AllGlobalExact && _Solved.GlobalExact;
            _AllFixedOrderExact = _AllFixedOrderExact && _Solved.FixedOrderExact;
            if (_Solved.StateLimit) _Result.Termination = TerminationReason::StateLimit;
            if (_Solved.TimeLimit) _Result.Termination = TerminationReason::TimeLimit;
        }

        if (_Result.Metrics.TotalStockLength > 0)
            _Result.Metrics.GrossUtilization = static_cast<double>(_Result.Metrics.PartLength)
                / static_cast<double>(_Result.Metrics.TotalStockLength);
        const Length _Consumed = _Result.Metrics.TotalStockLength
            - _Result.Metrics.ReusableRemainderLength;
        if (_Consumed > 0)
            _Result.Metrics.NetMaterialYield = static_cast<double>(_Result.Metrics.PartLength)
                / static_cast<double>(_Consumed);
        _Result.FixedOrderSplitOptimal = _AllFeasible && _AllFixedOrderExact;
        _Result.ExactSearchCompleted = (_AllFeasible && _AllGlobalExact)
            || _AnyProvenInfeasible;
        _Result.ProvenScope = _Result.ExactSearchCompleted
            ? ProofScope::FullIntegerModel
            : (_Result.FixedOrderSplitOptimal ? ProofScope::FixedOrder : ProofScope::None);
        _Result.Status = _AnyProvenInfeasible ? SolveStatus::Infeasible
            : (_AnyUnprovenFailure ? SolveStatus::NoSolutionFound
                : (_Result.ExactSearchCompleted ? SolveStatus::Optimal : SolveStatus::Feasible));
        if (_UsedLargeNeighborhood)
            _Result.Diagnostics.emplace_back(
                "dynamic large-neighborhood optimization used immutable pair-table costs");
        if (_UsedConstraintBackend)
            _Result.Diagnostics.emplace_back(
                "OR-Tools CP-SAT optimized the complete small model or released stock neighborhoods");
#if !ICAX_TUBE_NESTING_WITH_ORTOOLS
        if (Settings_.Constraint != ConstraintMode::Disabled)
            _Result.Diagnostics.emplace_back(
                "OR-Tools was unavailable at build time; exact enumeration and table-only search were used");
#endif
        if (_Result.Termination == TerminationReason::Completed && _Clock.Expired())
            _Result.Termination = TerminationReason::TimeLimit;
        return _Result;
    }
}
