// Included inside PairTableNesting.cpp's anonymous namespace after the common
// direction/rotation/score helpers and the optional CP-SAT implementation.
// Patterns are real, verified single-bar routes. Only their multiplicities,
// rather than N individual circuit slots, enter the restricted master model.
struct TubeMasterPattern final
{
    std::size_t Stock = 0;
    std::vector<std::size_t> Types;
    std::vector<Direction> Directions;
    std::vector<std::pair<std::size_t, std::size_t>> Counts;
    Length Occupied = 0;
};

struct TubeMasterBudget final
{
    std::chrono::steady_clock::time_point Deadline;
    std::uint64_t EvaluationLimit = 0;
    std::uint64_t Evaluations = 0;

    [[nodiscard]] bool Expired() const
    {
        return std::chrono::steady_clock::now() >= Deadline;
    }
    [[nodiscard]] bool Evaluate()
    {
        if (Evaluations >= EvaluationLimit || ((Evaluations & 63U) == 0 && Expired()))
            return false;
        ++Evaluations;
        return true;
    }
};

#include "TubeVerifiedPatternCache.inc"

class TubeMasterPool final
{
    const std::vector<Instance>& m_Instances;
    const PairTableSnapshot& m_Table;
    const std::vector<StockType>& m_Stocks;
    const std::vector<std::vector<std::size_t>>& m_ByType;
    std::size_t m_MaximumPatterns;
    std::size_t m_MaximumCells;
    std::size_t m_Cells = 0;
    std::map<std::string, std::size_t> m_Keys;

public:
    std::vector<TubeMasterPattern> Patterns;
    TubeMasterPool(const std::vector<Instance>& Instances_, const PairTableSnapshot& Table_,
        const std::vector<StockType>& Stocks_,
        const std::vector<std::vector<std::size_t>>& ByType_, const std::size_t MaximumPatterns_)
        : m_Instances(Instances_), m_Table(Table_), m_Stocks(Stocks_), m_ByType(ByType_),
          m_MaximumPatterns(MaximumPatterns_),
          m_MaximumCells((std::min)(std::size_t{1'000'000}, MaximumPatterns_ * std::size_t{1024}))
    {
    }

    [[nodiscard]] std::optional<std::size_t> Add(
        const std::size_t Stock_, const std::vector<std::size_t>& Types_, bool* Changed_ = nullptr)
    {
        if (Changed_) *Changed_ = false;
        if (Stock_ >= m_Stocks.size() || Types_.empty()) return {};
        const auto& _Stock = m_Stocks[Stock_];
        if (IsContinuation(_Stock) || (_Stock.MaxParts && Types_.size() > _Stock.MaxParts))
            return {};
        std::map<std::size_t, std::size_t> _Counts;
        std::set<std::string> _Orders;
        std::vector<std::size_t> _Order;
        _Order.reserve(Types_.size());
        for (const auto _Type : Types_)
        {
            if (_Type >= m_Table.Types.size()
                || m_Table.Types[_Type].CompatibilityKey != _Stock.CompatibilityKey)
                return {};
            const auto _Ordinal = _Counts[_Type]++;
            if (_Ordinal >= m_ByType[_Type].size()) return {};
            _Order.push_back(m_ByType[_Type][_Ordinal]);
            if (!m_Table.Types[_Type].OrderKey.empty())
                _Orders.insert(m_Table.Types[_Type].OrderKey);
        }
        if (_Stock.MaxDistinctOrders && _Orders.size() > _Stock.MaxDistinctOrders) return {};
        std::string _Key;
        const auto _Word = [&](const std::size_t Value_) {
            for (unsigned _Byte = 0; _Byte < 8; ++_Byte)
                _Key.push_back(static_cast<char>((static_cast<std::uint64_t>(Value_)
                    >> (_Byte * 8)) & 0xffU));
        };
        _Word(Stock_);
        for (const auto& [_Type, _Count] : _Counts) { _Word(_Type); _Word(_Count); }
        const auto _Found = m_Keys.find(_Key);
        if (_Found == m_Keys.end() && (Patterns.size() >= m_MaximumPatterns
            || Types_.size() > m_MaximumCells - m_Cells)) return {};

        const auto _Directions = OptimizeDirections(
            _Order, 0, _Order.size(), m_Instances, m_Table, _Stock);
        if (!_Directions.Feasible || _Directions.Occupied < 0
            || _Directions.Occupied > UsableLength(_Stock)) return {};
        WorkingBar _Bar;
        _Bar.Stock = Stock_;
        for (std::size_t _Index = 0; _Index < _Order.size(); ++_Index)
            _Bar.Placements.push_back({_Order[_Index], _Directions.Directions[_Index], 0.0});
        if (!PopulateRotations(_Bar, m_Instances, m_Table, _Stock)) return {};
        if (_Found != m_Keys.end())
        {
            auto& _Pattern = Patterns[_Found->second];
            // For an ordinary independent bar, equal type counts and equal
            // stock have identical procurement objectives. Lower occupation
            // dominates every other route with those same counts.
            if (_Directions.Occupied < _Pattern.Occupied)
            {
                _Pattern.Types = Types_;
                _Pattern.Directions = _Directions.Directions;
                _Pattern.Occupied = _Directions.Occupied;
                if (Changed_) *Changed_ = true;
            }
            return _Found->second;
        }
        const auto _Index = Patterns.size();
        m_Keys.emplace(std::move(_Key), _Index);
        TubeMasterPattern _Pattern;
        _Pattern.Stock = Stock_;
        _Pattern.Types = Types_;
        _Pattern.Directions = _Directions.Directions;
        _Pattern.Counts.assign(_Counts.begin(), _Counts.end());
        _Pattern.Occupied = _Directions.Occupied;
        m_Cells += Types_.size();
        Patterns.push_back(std::move(_Pattern));
        if (Changed_) *Changed_ = true;
        return _Index;
    }
};

[[nodiscard]] std::vector<std::size_t> ConstructTubeMasterPattern(
    const std::vector<std::size_t>& Remaining_, const std::vector<std::size_t>& TypeOrder_,
    const PairTableSnapshot& Table_, const StockType& Stock_, const std::size_t Seed_,
    const PairTableSolverSettings& Settings_, TubeMasterBudget& Budget_)
{
    std::vector<std::size_t> _Types;
    if (TypeOrder_.empty() || UsableLength(Stock_) <= 0) return _Types;
    std::vector<std::size_t> _Used(Table_.Types.size(), 0);
    std::set<std::string> _Orders;
    std::array<Length, 2> _Cost{kInfinity, kInfinity};
    const auto _Limit = Stock_.MaxParts
        ? (std::min)(Stock_.MaxParts, std::size_t{512}) : std::size_t{512};
    const auto _Width = (std::clamp)(
        (std::min)(Settings_.CandidateWidth, std::size_t{64}) * 4,
        std::size_t{32}, std::size_t{256});
    while (_Types.size() < _Limit && !Budget_.Expired())
    {
        std::optional<std::size_t> _BestType;
        std::array<Length, 2> _BestCost{kInfinity, kInfinity};
        long double _BestRank = -(std::numeric_limits<long double>::infinity)();
        const Length _PriorCost = _Types.empty() ? 0 : (std::min)(_Cost[0], _Cost[1]);
        std::size_t _Feasible = 0;
        const auto _Begin = _Types.empty() ? Seed_ % TypeOrder_.size()
            : (Seed_ * 17 + _Types.size() * 11) % TypeOrder_.size();
        for (std::size_t _Offset = 0; _Offset < TypeOrder_.size(); ++_Offset)
        {
            if (!Budget_.Evaluate()) break;
            const auto _Type = TypeOrder_[(_Begin + _Offset) % TypeOrder_.size()];
            const auto& _Definition = Table_.Types[_Type];
            if (Remaining_[_Type] <= _Used[_Type]
                || _Definition.CompatibilityKey != Stock_.CompatibilityKey) continue;
            if (Stock_.MaxDistinctOrders && !_Definition.OrderKey.empty()
                && !_Orders.contains(_Definition.OrderKey)
                && _Orders.size() >= Stock_.MaxDistinctOrders) continue;
            std::array<Length, 2> _Next{kInfinity, kInfinity};
            for (std::size_t _Direction = 0; _Direction < 2; ++_Direction)
            {
                if (!_Definition.DirectionAllowed[_Direction]) continue;
                if (_Types.empty()) _Next[_Direction] = _Definition.MaximumLength;
                else
                    for (std::size_t _Prior = 0; _Prior < 2; ++_Prior)
                    {
                        if (_Cost[_Prior] >= kInfinity) continue;
                        const auto& _Entry = Table_.At(_Types.back(), static_cast<Direction>(_Prior),
                            _Type, static_cast<Direction>(_Direction));
                        Length _Candidate = 0;
                        if (!IsEntryUsable(_Entry) || !TryAdd(_Cost[_Prior],
                            _Definition.MaximumLength - _Entry.NetSaving, _Candidate)) continue;
                        _Next[_Direction] = (std::min)(_Next[_Direction], _Candidate);
                    }
            }
            const auto _Occupied = (std::min)(_Next[0], _Next[1]);
            if (_Occupied >= kInfinity || _Occupied > UsableLength(Stock_)) continue;
            const auto _Increment = _Occupied - _PriorCost;
            long double _Rank = static_cast<long double>(_Definition.MaximumLength);
            if (!_Types.empty())
                switch (Seed_ % 4)
                {
                case 1: _Rank /= static_cast<long double>((std::max)(Length{1}, _Increment)); break;
                case 2: _Rank = static_cast<long double>(_Occupied); break;
                case 3: _Rank -= static_cast<long double>(_Increment); break;
                default: break;
                }
            if (!_BestType || _Rank > _BestRank
                || (_Rank == _BestRank && _Type < *_BestType))
            {
                _BestType = _Type;
                _BestCost = _Next;
                _BestRank = _Rank;
            }
            if (_Types.empty() || ++_Feasible >= _Width) break;
        }
        if (!_BestType) break;
        _Types.push_back(*_BestType);
        ++_Used[*_BestType];
        _Cost = _BestCost;
        if (!Table_.Types[*_BestType].OrderKey.empty())
            _Orders.insert(Table_.Types[*_BestType].OrderKey);
    }
    return _Types;
}

#include "TubeCachedPatternConstruction.inc"

[[nodiscard]] bool ValidateTubeMasterPlan(
    const WorkingPlan& Plan_, const std::vector<std::size_t>& GroupInstances_,
    const std::vector<Instance>& Instances_, const PairTableSnapshot& Table_,
    const std::vector<StockType>& Stocks_, Score& Value_, const TubeMasterBudget* Budget_ = nullptr)
{
    Value_ = {};
    std::vector<bool> _Expected(Instances_.size(), false), _Seen(Instances_.size(), false);
    for (const auto _Instance : GroupInstances_)
    {
        if (Budget_ && Budget_->Expired()) return false;
        if (_Instance >= Instances_.size() || _Expected[_Instance]) return false;
        _Expected[_Instance] = true;
    }
    std::vector<std::size_t> _StockUses(Stocks_.size(), 0);
    std::size_t _Placed = 0;
    for (const auto& _Bar : Plan_)
    {
        if (_Bar.Stock >= Stocks_.size() || _Bar.Placements.empty()) return false;
        const auto& _Stock = Stocks_[_Bar.Stock];
        if (IsContinuation(_Stock) || (_Stock.MaxParts && _Bar.Placements.size() > _Stock.MaxParts)
            || (_Stock.Quantity && ++_StockUses[_Bar.Stock] > _Stock.Quantity)) return false;
        std::set<std::string> _Orders;
        Length _Occupied = 0;
        double _Roll = 0.0;
        for (std::size_t _Index = 0; _Index < _Bar.Placements.size(); ++_Index)
        {
            if (Budget_ && ((_Placed & 1023U) == 0) && Budget_->Expired()) return false;
            const auto& _Placement = _Bar.Placements[_Index];
            const auto _Direction = static_cast<std::size_t>(_Placement.PartDirection);
            if (_Placement.Instance >= Instances_.size() || !_Expected[_Placement.Instance]
                || _Seen[_Placement.Instance] || _Direction > 1) return false;
            const auto _Type = Instances_[_Placement.Instance].Type;
            if (_Type >= Table_.Types.size()) return false;
            const auto& _Definition = Table_.Types[_Type];
            if (!_Definition.DirectionAllowed[_Direction]
                || _Definition.CompatibilityKey != _Stock.CompatibilityKey) return false;
            Length _Increment = _Definition.MaximumLength;
            if (_Index)
            {
                const auto& _Previous = _Bar.Placements[_Index - 1];
                const auto& _Entry = Table_.At(Instances_[_Previous.Instance].Type,
                    _Previous.PartDirection, _Type, _Placement.PartDirection);
                if (!IsEntryUsable(_Entry) || _Entry.PoseMetadataIndex >= Table_.Poses.size()
                    || !TryAdd(_Increment, -_Entry.NetSaving, _Increment)) return false;
                _Roll = std::fmod(_Roll
                    + Table_.Poses[_Entry.PoseMetadataIndex].RelativeRotationRadians,
                    2.0 * std::numbers::pi_v<double>);
                if (_Roll < 0.0) _Roll += 2.0 * std::numbers::pi_v<double>;
            }
            if (_Increment < 0 || !TryAdd(_Occupied, _Increment, _Occupied)
                || _Occupied > UsableLength(_Stock) || !std::isfinite(_Placement.RotationRadians))
                return false;
            const auto _AngleError = std::remainder(
                _Placement.RotationRadians - _Roll, 2.0 * std::numbers::pi_v<double>);
            if (std::abs(_AngleError) > 1.0e-9) return false;
            if (!_Definition.OrderKey.empty()) _Orders.insert(_Definition.OrderKey);
            _Seen[_Placement.Instance] = true;
            ++_Placed;
        }
        if (_Stock.MaxDistinctOrders && _Orders.size() > _Stock.MaxDistinctOrders) return false;
        Value_ = AddStockScore(Value_, _Stock, _Occupied);
    }
    return _Placed == GroupInstances_.size();
}

#include "TubeVerifiedPatternRecipe.inc"

[[nodiscard]] bool ImprovePatternMaster(
    WorkingPlan& Plan_, const std::vector<std::size_t>& GroupInstances_,
    const std::vector<Instance>& Instances_, const PairTableSnapshot& Table_,
    const std::vector<StockType>& Stocks_, const PairTableSolverSettings& Settings_,
    const std::uint64_t TimeBudgetMilliseconds_, std::uint64_t& Nodes_, std::string& Diagnostic_,
    TubePatternCacheSession* CacheSession_ = nullptr, ResultMetrics* CacheMetrics_ = nullptr)
{
    Diagnostic_.clear();
    if (GroupInstances_.empty() || Stocks_.empty() || TimeBudgetMilliseconds_ == 0
        || Settings_.PatternMasterTimeBudgetMilliseconds == 0
        || Settings_.PatternMasterMaximumPatterns == 0) return false;
    if (std::ranges::any_of(Stocks_, [](const auto& Stock_) { return IsContinuation(Stock_); }))
    {
        Diagnostic_ = "pattern master skipped: fixed-prefix stocks retain the original solver";
        return false;
    }
    const auto _Started = std::chrono::steady_clock::now();
    const auto _BudgetMilliseconds = (std::min)({TimeBudgetMilliseconds_,
        Settings_.PatternMasterTimeBudgetMilliseconds, std::uint64_t{60'000}});
    TubeMasterBudget _Budget{_Started + std::chrono::milliseconds(_BudgetMilliseconds),
        Settings_.EvaluationLimit ? (std::min)(Settings_.EvaluationLimit, std::uint64_t{2'000'000})
            : std::uint64_t{2'000'000}, 0};
    std::vector<std::vector<std::size_t>> _ByType(Table_.Types.size());
    std::vector<std::size_t> _Demand(Table_.Types.size(), 0), _TypeOrder;
    for (const auto _Instance : GroupInstances_)
    {
        if (_Budget.Expired()) return false;
        if (_Instance >= Instances_.size() || Instances_[_Instance].Type >= Table_.Types.size()) return false;
        const auto _Type = Instances_[_Instance].Type;
        _ByType[_Type].push_back(_Instance);
        ++_Demand[_Type];
    }
    for (std::size_t _Type = 0; _Type < _Demand.size(); ++_Type)
        if (_Demand[_Type]) _TypeOrder.push_back(_Type);
    std::stable_sort(_TypeOrder.begin(), _TypeOrder.end(), [&](const auto A_, const auto B_)
    {
        return std::tie(Table_.Types[A_].MaximumLength, Table_.Types[A_].ID)
            > std::tie(Table_.Types[B_].MaximumLength, Table_.Types[B_].ID);
    });
    Score _Before;
    const auto _ValidationStarted = std::chrono::steady_clock::now();
    if (!Plan_.empty() && !ValidateTubeMasterPlan(
        Plan_, GroupInstances_, Instances_, Table_, Stocks_, _Before, &_Budget))
    {
        Diagnostic_ = "pattern master skipped: incumbent failed independent validation";
        return false;
    }
    if (!Plan_.empty()) _Before = MeasurePlanScore(Plan_, Instances_, Table_, Stocks_);
    const auto _ValidationMilliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _ValidationStarted).count());
    const auto _MaximumPatterns = (std::min)(Settings_.PatternMasterMaximumPatterns, std::size_t{8192});
    TubeMasterPool _Pool(Instances_, Table_, Stocks_, _ByType, _MaximumPatterns);
    std::vector<std::vector<std::size_t>> _Selections;
    std::vector<std::size_t> _Incumbent;
    for (const auto& _Bar : Plan_)
    {
        if (_Budget.Expired()) return false;
        std::vector<std::size_t> _Types;
        for (const auto& _Placement : _Bar.Placements)
            _Types.push_back(Instances_[_Placement.Instance].Type);
        const auto _Index = _Pool.Add(_Bar.Stock, _Types);
        if (!_Index)
        {
            Diagnostic_ = "pattern master skipped: incumbent patterns exceed the bounded pool";
            return false;
        }
        _Incumbent.resize(_Pool.Patterns.size(), 0);
        ++_Incumbent[*_Index];
    }
    if (!Plan_.empty()) _Selections.push_back(_Incumbent);

    // Reserve work proportional to the actual group and measured validation,
    // rather than a fixed 100 ms guess. All optional generation and solving
    // share the earlier deadline; only restoration may use the final reserve.
    const auto _ScaleReserve = std::uint64_t{2}
        + static_cast<std::uint64_t>((GroupInstances_.size() + 999) / 1000)
        + static_cast<std::uint64_t>((Instances_.size() + 99999) / 100000);
    const auto _ReserveCeiling = (std::max)(std::uint64_t{1}, _BudgetMilliseconds / 2);
    const auto _InitialReserve = (std::min)(_ReserveCeiling,
        (std::max)(std::uint64_t{5}, _ScaleReserve + _ValidationMilliseconds * 2));
    const auto _WorkDeadline = _Budget.Deadline - std::chrono::milliseconds(_InitialReserve);
    const auto _ConstructionDeadline = (std::min)(_WorkDeadline,
        _Started + std::chrono::milliseconds((std::max)(std::uint64_t{1}, _BudgetMilliseconds * 2 / 5)));
    TubePatternCacheSession _LocalCacheSession;
    auto& _CacheSession = CacheSession_ ? *CacheSession_ : _LocalCacheSession;
    std::optional<std::string> _CacheContext;
    std::vector<std::size_t> _CachedPatternIndices;
    if (Settings_.ReuseVerifiedPatterns)
    {
        _CacheContext = TubePatternCacheContext(Table_, Stocks_, _ConstructionDeadline);
        // A cold context does not scan the dense table before construction.
        // Partial fingerprint work can continue in later rounds of this solve.
        const auto _HashStarted = std::chrono::steady_clock::now();
        const auto _HashDeadline = _HashStarted + (_ConstructionDeadline - _HashStarted) / 4;
        if (_CacheContext && (VerifiedTubePatternCache().HasContext(*_CacheContext)
            || _CacheSession.Started) && PrepareTubePatternCacheFingerprint(
                _CacheSession, Table_, _HashDeadline))
        {
            const TubePatternCacheKey _Key{_CacheSession.HashA, _CacheSession.HashB, *_CacheContext};
            if (const auto _Cached = VerifiedTubePatternCache().Find(_Key))
            {
                if (CacheMetrics_) ++CacheMetrics_->PatternCacheHits;
                const auto _ImportLimit = (std::min)(std::size_t{256},
                    (_MaximumPatterns - _Pool.Patterns.size()) / 4);
                std::size_t _Imported = 0;
                for (const auto& _Pattern : _Cached->Patterns)
                {
                    if (std::chrono::steady_clock::now() >= _ConstructionDeadline
                        || _Imported >= _ImportLimit) break;
                    if (std::ranges::any_of(_Pattern.Counts, [&](const auto& Count_) {
                        return Count_.first >= _Demand.size() || Count_.second > _Demand[Count_.first];
                    })) continue;
                    bool _Changed = false;
                    if (const auto _Index = _Pool.Add(_Pattern.Stock, _Pattern.Types, &_Changed))
                    {
                        _CachedPatternIndices.push_back(*_Index);
                        ++_Imported;
                        if (_Changed && CacheMetrics_) ++CacheMetrics_->PatternCacheImported;
                    }
                }
                std::sort(_CachedPatternIndices.begin(), _CachedPatternIndices.end());
                _CachedPatternIndices.erase(std::unique(_CachedPatternIndices.begin(),
                    _CachedPatternIndices.end()), _CachedPatternIndices.end());
            }
        }
    }
    TubeMasterBudget _ConstructionBudget = _Budget;
    _ConstructionBudget.Deadline = _ConstructionDeadline;
    if (!_CachedPatternIndices.empty() && !_ConstructionBudget.Expired())
    {
        auto _CachedBudget = _ConstructionBudget;
        const auto _Now = std::chrono::steady_clock::now();
        _CachedBudget.Deadline = _Now + (_ConstructionDeadline - _Now) / 4;
        if (auto _Selection = ConstructCachedTubeMasterPlan(_Pool, _CachedPatternIndices,
            _Demand, _TypeOrder, Table_, Stocks_, Settings_, _CachedBudget))
        {
            _Selections.push_back(std::move(*_Selection));
            if (CacheMetrics_) ++CacheMetrics_->PatternCacheConstructedPlans;
        }
        _ConstructionBudget.Evaluations = _CachedBudget.Evaluations;
    }
    const auto _Runs = (std::min)(Settings_.PatternMasterConstructionRuns, std::size_t{64});
    std::size_t _Generated = 0, _Reused = 0;
    for (std::size_t _Run = 0; _Run < _Runs && !_Budget.Expired()
        && std::chrono::steady_clock::now() < _ConstructionDeadline; ++_Run)
    {
        auto _Remaining = _Demand;
        std::vector<std::size_t> _StockRemaining, _StockOrder(Stocks_.size()), _Selection;
        for (const auto& _Stock : Stocks_) _StockRemaining.push_back(_Stock.Quantity);
        std::iota(_StockOrder.begin(), _StockOrder.end(), 0);
        std::stable_sort(_StockOrder.begin(), _StockOrder.end(), [&](const auto A_, const auto B_)
        {
            if (_Run % 3 == 0) return UsableLength(Stocks_[A_]) < UsableLength(Stocks_[B_]);
            if (_Run % 3 == 1) return UsableLength(Stocks_[A_]) > UsableLength(Stocks_[B_]);
            const auto _CostA = Settings_.ObjectivePolicy == PairObjectivePolicy::MinPurchaseCost
                ? StockCost(Stocks_[A_]) : ChargedNominalLength(Stocks_[A_]);
            const auto _CostB = Settings_.ObjectivePolicy == PairObjectivePolicy::MinPurchaseCost
                ? StockCost(Stocks_[B_]) : ChargedNominalLength(Stocks_[B_]);
            return static_cast<long double>(_CostA) / (std::max)(Length{1}, UsableLength(Stocks_[A_]))
                < static_cast<long double>(_CostB) / (std::max)(Length{1}, UsableLength(Stocks_[B_]));
        });
        std::size_t _Unplaced = GroupInstances_.size(), _Bars = 0;
        while (_Unplaced && !_Budget.Expired()
            && std::chrono::steady_clock::now() < _ConstructionDeadline)
        {
            bool _Added = false;
            for (const auto _Stock : _StockOrder)
            {
                if (std::chrono::steady_clock::now() >= _ConstructionDeadline) break;
                if (Stocks_[_Stock].Quantity && !_StockRemaining[_Stock]) continue;
                const auto _Types = ConstructTubeMasterPattern(
                    _Remaining, _TypeOrder, Table_, Stocks_[_Stock], _Run + _Bars
                        + static_cast<std::size_t>(Settings_.PatternMasterSearchRound) * 131U,
                    Settings_, _ConstructionBudget);
                if (_Types.empty()) continue;
                const auto _Index = _Pool.Add(_Stock, _Types);
                if (!_Index) continue;
                ++_Generated;
                const auto& _Pattern = _Pool.Patterns[*_Index];
                std::size_t _Repeat = _Unplaced;
                for (const auto& [_Type, _Count] : _Pattern.Counts)
                    _Repeat = (std::min)(_Repeat, _Remaining[_Type] / _Count);
                if (Stocks_[_Stock].Quantity) _Repeat = (std::min)(_Repeat, _StockRemaining[_Stock]);
                if (_Repeat == 0) continue;
                _Selection.resize(_Pool.Patterns.size(), 0);
                _Selection[*_Index] += _Repeat;
                for (const auto& [_Type, _Count] : _Pattern.Counts) _Remaining[_Type] -= _Count * _Repeat;
                _Unplaced -= _Pattern.Types.size() * _Repeat;
                if (Stocks_[_Stock].Quantity) _StockRemaining[_Stock] -= _Repeat;
                _Reused += _Repeat - 1;
                _Bars += _Repeat;
                _Added = true;
                break;
            }
            if (!_Added) break;
        }
        if (_Unplaced == 0) _Selections.push_back(std::move(_Selection));
    }

    // Singletons allow exact residual coverage in the restricted pool. Try one
    // fitting stock per type before optional additional stock alternatives.
    const auto _SingletonDeadline = (std::min)(_WorkDeadline,
        _Started + std::chrono::milliseconds(_BudgetMilliseconds / 2));
    for (const auto _Type : _TypeOrder)
    {
        if (std::chrono::steady_clock::now() >= _SingletonDeadline) break;
        for (std::size_t _Stock = 0; _Stock < Stocks_.size(); ++_Stock)
        {
            if (std::chrono::steady_clock::now() >= _SingletonDeadline) break;
            if (_Pool.Add(_Stock, {_Type})) break;
        }
    }
    // Cache only routes already accepted by Add. No demand solution, physical
    // instance, or restricted-pool proof is retained. Optional writes get a
    // bounded slice and never consume restoration time reserved for the best.
    if (_CacheContext && std::chrono::steady_clock::now() < _WorkDeadline)
    {
        const auto _StoreDeadline = (std::min)(_WorkDeadline, std::chrono::steady_clock::now()
            + std::chrono::milliseconds((std::max)(std::uint64_t{1}, _BudgetMilliseconds / 10)));
        if (PrepareTubePatternCacheFingerprint(_CacheSession, Table_, _StoreDeadline))
            VerifiedTubePatternCache().Store(
                {_CacheSession.HashA, _CacheSession.HashB, *_CacheContext},
                _Pool.Patterns, Stocks_, _StoreDeadline, CacheMetrics_);
    }
    if (_Pool.Patterns.empty()) return false;
    const auto _SelectionScore = [&](const std::vector<std::size_t>& Selection_) {
        Score _Value;
        for (std::size_t _Index = 0; _Index < Selection_.size(); ++_Index)
        {
            if (Selection_[_Index] == 0) continue;
            const auto& _Pattern = _Pool.Patterns[_Index];
            const auto& _Stock = Stocks_[_Pattern.Stock];
            _Value.Cost += StockCost(_Stock) * static_cast<std::int64_t>(Selection_[_Index]);
            _Value.NominalLength += ChargedNominalLength(_Stock) * static_cast<Length>(Selection_[_Index]);
            _Value.BarCount += Selection_[_Index];
            _Value.OccupiedLength += _Pattern.Occupied * static_cast<Length>(Selection_[_Index]);
        }
        return _Value;
    };
    std::vector<std::size_t> _BestSelection;
    Score _BestValue;
    bool _HasSelection = false, _RestrictedOptimal = false;
    for (auto& _Selection : _Selections)
    {
        _Selection.resize(_Pool.Patterns.size(), 0);
        const auto _Value = _SelectionScore(_Selection);
        if (!_HasSelection || CompareScore(_Value, _BestValue, Settings_.ObjectivePolicy) < 0)
        {
            _BestValue = _Value;
            _BestSelection = _Selection;
            _HasSelection = true;
        }
    }
    std::string _Backend = "verified-construction-fallback";
    const auto _Materialize = [&](const std::vector<std::size_t>& Selection_,
        const Score& Expected_) -> std::optional<WorkingPlan> {
        if (_Budget.Expired()) return {};
        std::vector<std::size_t> _Taken(_ByType.size(), 0);
        WorkingPlan _Candidate;
        _Candidate.reserve(Expected_.BarCount);
        for (std::size_t _Index = 0; _Index < Selection_.size(); ++_Index)
        {
            const auto& _Pattern = _Pool.Patterns[_Index];
            for (std::size_t _Copy = 0; _Copy < Selection_[_Index]; ++_Copy)
            {
                if ((_Candidate.size() & 1023U) == 0 && _Budget.Expired()) return {};
                WorkingBar _Bar;
                _Bar.Stock = _Pattern.Stock;
                for (std::size_t _Position = 0; _Position < _Pattern.Types.size(); ++_Position)
                {
                    const auto _Type = _Pattern.Types[_Position];
                    if (_Taken[_Type] >= _ByType[_Type].size()) return {};
                    _Bar.Placements.push_back({_ByType[_Type][_Taken[_Type]++],
                        _Pattern.Directions[_Position], 0.0});
                }
                if (!PopulateRotations(_Bar, Instances_, Table_, Stocks_[_Bar.Stock])) return {};
                _Candidate.push_back(std::move(_Bar));
            }
        }
        Score _Actual;
        if (!ValidateTubeMasterPlan(_Candidate, GroupInstances_, Instances_, Table_, Stocks_, _Actual, &_Budget)
            || CompareScore(_Actual, Expected_, Settings_.ObjectivePolicy) != 0) return {};
        // Validation has completed in full. A deadline observed just after it
        // must not discard this already verified solution before a cheap move.
        return std::optional<WorkingPlan>(std::move(_Candidate));
    };
    std::optional<WorkingPlan> _Fallback;
    Score _FallbackValue;
    const auto _MaterializationStarted = std::chrono::steady_clock::now();
    if (_HasSelection && (Plan_.empty()
        || CompareScore(_BestValue, _Before, Settings_.ObjectivePolicy) < 0))
    {
        _Fallback = _Materialize(_BestSelection, _BestValue);
        if (_Fallback) _FallbackValue = _BestValue;
    }
    const auto _MaterializationMilliseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _MaterializationStarted).count());
    const auto _RestoreReserve = (std::min)(_ReserveCeiling,
        (std::max)(_InitialReserve, _ScaleReserve + _MaterializationMilliseconds * 3));
    auto _SolveDeadline = (std::min)(_WorkDeadline,
        _Budget.Deadline - std::chrono::milliseconds(_RestoreReserve));

#if ICAX_TUBE_NESTING_WITH_ORTOOLS
    if (Settings_.Constraint != ConstraintMode::Disabled
        && std::chrono::steady_clock::now() < _SolveDeadline)
    {
        using namespace operations_research::sat;
        using operations_research::Domain;
        _Backend = "CP-SAT-count-master";
        std::vector<std::size_t> _MaximumUses(_Pool.Patterns.size(), GroupInstances_.size());
        long double _Magnitude = 0.0L;
        for (std::size_t _Index = 0; _Index < _Pool.Patterns.size(); ++_Index)
        {
            const auto& _Pattern = _Pool.Patterns[_Index];
            const auto& _Stock = Stocks_[_Pattern.Stock];
            auto& _Maximum = _MaximumUses[_Index];
            for (const auto& [_Type, _Count] : _Pattern.Counts)
                _Maximum = (std::min)(_Maximum, _Demand[_Type] / _Count);
            if (_Stock.Quantity) _Maximum = (std::min)(_Maximum, _Stock.Quantity);
            _Magnitude += static_cast<long double>(_Maximum)
                * (static_cast<long double>(StockCost(_Stock))
                    + static_cast<long double>(ChargedNominalLength(_Stock))
                    + static_cast<long double>(_Pattern.Occupied) + 1.0L);
        }
        if (_Magnitude > static_cast<long double>((std::numeric_limits<std::int64_t>::max)()) / 8.0L)
            _Backend += "-coefficient-bound-fallback";
        else
        {
            CpModelBuilder _Model;
            std::vector<IntVar> _Uses;
            _Uses.reserve(_Pool.Patterns.size());
            std::vector<LinearExpr> _Coverage(_Demand.size()), _StockUse(Stocks_.size());
            LinearExpr _Cost, _Nominal, _Bars, _Occupied;
            bool _ModelComplete = true;
            for (std::size_t _Index = 0; _Index < _Pool.Patterns.size(); ++_Index)
            {
                if ((_Index & 63U) == 0 && std::chrono::steady_clock::now() >= _SolveDeadline)
                { _ModelComplete = false; break; }
                const auto& _Pattern = _Pool.Patterns[_Index];
                const auto& _Stock = Stocks_[_Pattern.Stock];
                const auto _Use = _Model.NewIntVar(Domain(0,
                    static_cast<std::int64_t>(_MaximumUses[_Index])));
                _Uses.push_back(_Use);
                for (const auto& [_Type, _Count] : _Pattern.Counts)
                    _Coverage[_Type] += LinearExpr::Term(_Use, static_cast<std::int64_t>(_Count));
                _StockUse[_Pattern.Stock] += _Use;
                _Cost += LinearExpr::Term(_Use, StockCost(_Stock));
                _Nominal += LinearExpr::Term(_Use, ChargedNominalLength(_Stock));
                _Bars += _Use;
                _Occupied += LinearExpr::Term(_Use, _Pattern.Occupied);
                if (_HasSelection) _Model.AddHint(_Use, static_cast<std::int64_t>(_BestSelection[_Index]));
            }
            if (_ModelComplete)
            {
                for (const auto _Type : _TypeOrder)
                    _Model.AddEquality(_Coverage[_Type], static_cast<std::int64_t>(_Demand[_Type]));
                for (std::size_t _Stock = 0; _Stock < Stocks_.size(); ++_Stock)
                    if (Stocks_[_Stock].Quantity)
                        _Model.AddLessOrEqual(_StockUse[_Stock],
                            static_cast<std::int64_t>(Stocks_[_Stock].Quantity));
                std::vector<LinearExpr> _Objectives;
                if (Settings_.ObjectivePolicy == PairObjectivePolicy::MinPurchaseCost)
                    _Objectives = {_Cost, _Nominal, _Bars, _Occupied};
                else _Objectives = {_Nominal, _Bars, _Occupied};
                if (_HasSelection)
                    _Model.AddLessOrEqual(_Objectives.front(),
                        Settings_.ObjectivePolicy == PairObjectivePolicy::MinPurchaseCost
                            ? _BestValue.Cost : _BestValue.NominalLength);
                SatParameters _Parameters;
                _Parameters.set_num_search_workers(static_cast<int>((std::clamp)(
                    Settings_.ConstraintWorkers ? Settings_.ConstraintWorkers : std::size_t{4},
                    std::size_t{1}, std::size_t{8})));
                _Parameters.set_random_seed(static_cast<std::int32_t>(Settings_.RandomSeed ^ 0x504F4F4CU));
                _Parameters.set_log_search_progress(false);
                std::size_t _ProvenObjectives = 0;
                for (const auto& _Objective : _Objectives)
                {
                    const auto _RemainingMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                        _SolveDeadline - std::chrono::steady_clock::now()).count();
                    if (_RemainingMilliseconds <= 0) break;
                    _Parameters.set_max_time_in_seconds(static_cast<double>(_RemainingMilliseconds) / 1000.0);
                    _Model.ClearObjective();
                    _Model.Minimize(_Objective);
                    const auto _Response = SolveWithParameters(_Model.Build(), _Parameters);
                    Nodes_ += static_cast<std::uint64_t>((std::max)(std::int64_t{0}, _Response.num_branches()));
                    if (_Response.status() != CpSolverStatus::OPTIMAL
                        && _Response.status() != CpSolverStatus::FEASIBLE)
                    {
                        if (_Response.status() == CpSolverStatus::MODEL_INVALID) _Backend += "-invalid-model";
                        break;
                    }
                    std::vector<std::size_t> _Selection;
                    _Selection.reserve(_Uses.size());
                    for (const auto _Use : _Uses)
                        _Selection.push_back(static_cast<std::size_t>(SolutionIntegerValue(_Response, _Use)));
                    const auto _Value = _SelectionScore(_Selection);
                    if (!_HasSelection || CompareScore(_Value, _BestValue, Settings_.ObjectivePolicy) < 0)
                    {
                        _BestValue = _Value;
                        _BestSelection = std::move(_Selection);
                        _HasSelection = true;
                        // Checkpoint a new complete CP incumbent before a later
                        // lexicographic phase can consume its restoration time.
                        if ((Plan_.empty() || CompareScore(_BestValue, _Before,
                                Settings_.ObjectivePolicy) < 0)
                            && std::chrono::steady_clock::now() < _SolveDeadline)
                        {
                            const auto _CheckpointStarted = std::chrono::steady_clock::now();
                            auto _Checkpoint = _Materialize(_BestSelection, _BestValue);
                            if (_Checkpoint)
                            {
                                _Fallback = std::move(_Checkpoint);
                                _FallbackValue = _BestValue;
                            }
                            const auto _CheckpointMilliseconds = static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - _CheckpointStarted).count());
                            const auto _NextReserve = (std::min)(_ReserveCeiling,
                                (std::max)(_RestoreReserve, _ScaleReserve + _CheckpointMilliseconds * 3));
                            _SolveDeadline = (std::min)(_SolveDeadline,
                                _Budget.Deadline - std::chrono::milliseconds(_NextReserve));
                        }
                    }
                    if (_Response.status() != CpSolverStatus::OPTIMAL) break;
                    ++_ProvenObjectives;
                    _Model.AddEquality(_Objective, SolutionIntegerValue(_Response, _Objective));
                    _Model.ClearHints();
                    for (std::size_t _Index = 0; _Index < _Uses.size(); ++_Index)
                        _Model.AddHint(_Uses[_Index], static_cast<std::int64_t>(_BestSelection[_Index]));
                }
                _RestrictedOptimal = _ProvenObjectives == _Objectives.size();
            }
        }
    }
    else
#endif
    if (std::chrono::steady_clock::now() < _SolveDeadline)
    {
        // The Debug backend and ConstraintMode::Disabled never invoke CP-SAT.
        // Enumerate type-count combinations only when the bounded pool is small;
        // large pools retain complete verified construction candidates.
        if (_Pool.Patterns.size() <= 256 && _TypeOrder.size() <= 24 && GroupInstances_.size() <= 512)
        {
            _Backend = "bounded-count-enumeration";
            std::vector<std::vector<std::size_t>> _Containing(_Demand.size());
            for (std::size_t _Index = 0; _Index < _Pool.Patterns.size(); ++_Index)
                for (const auto& [_Type, _Count] : _Pool.Patterns[_Index].Counts)
                { (void)_Count; _Containing[_Type].push_back(_Index); }
            auto _Remaining = _Demand;
            std::vector<std::size_t> _StockUsed(Stocks_.size(), 0), _Selection(_Pool.Patterns.size(), 0);
            std::uint64_t _Visited = 0;
            const auto _NodeLimit = Settings_.ExactNodeLimit
                ? (std::min)(Settings_.ExactNodeLimit, std::uint64_t{50'000}) : std::uint64_t{50'000};
            bool _Complete = true;
            const auto _Fits = [&](const std::size_t Index_) {
                const auto& _Pattern = _Pool.Patterns[Index_];
                if (Stocks_[_Pattern.Stock].Quantity
                    && _StockUsed[_Pattern.Stock] >= Stocks_[_Pattern.Stock].Quantity) return false;
                for (const auto& [_Type, _Count] : _Pattern.Counts)
                    if (_Count > _Remaining[_Type]) return false;
                return true;
            };
            std::function<void(std::size_t, std::size_t, Score)> _Search =
                [&](const std::size_t Unplaced_, const std::size_t Depth_, const Score Value_) {
                if (std::chrono::steady_clock::now() >= _SolveDeadline
                    || _Visited >= _NodeLimit || Depth_ >= 128)
                { _Complete = false; return; }
                ++_Visited;
                if (Unplaced_ == 0)
                {
                    if (!_HasSelection || CompareScore(Value_, _BestValue, Settings_.ObjectivePolicy) < 0)
                    {
                        _BestValue = Value_;
                        _BestSelection = _Selection;
                        _HasSelection = true;
                    }
                    return;
                }
                if (_HasSelection && CompareScore(Value_, _BestValue, Settings_.ObjectivePolicy) >= 0)
                    return;
                std::optional<std::size_t> _Pivot;
                std::size_t _Fewest = (std::numeric_limits<std::size_t>::max)();
                for (const auto _Type : _TypeOrder)
                {
                    if (!_Remaining[_Type]) continue;
                    std::size_t _Options = 0;
                    for (const auto _Index : _Containing[_Type]) _Options += _Fits(_Index);
                    if (_Options == 0) return;
                    if (_Options < _Fewest) { _Fewest = _Options; _Pivot = _Type; }
                }
                if (!_Pivot) return;
                for (const auto _Index : _Containing[*_Pivot])
                {
                    if (!_Fits(_Index)) continue;
                    const auto& _Pattern = _Pool.Patterns[_Index];
                    const auto& _Stock = Stocks_[_Pattern.Stock];
                    std::size_t _Maximum = Unplaced_;
                    for (const auto& [_Type, _Count] : _Pattern.Counts)
                        _Maximum = (std::min)(_Maximum, _Remaining[_Type] / _Count);
                    if (_Stock.Quantity) _Maximum = (std::min)(_Maximum,
                        _Stock.Quantity - _StockUsed[_Pattern.Stock]);
                    for (std::size_t _Copies = _Maximum; _Copies > 0; --_Copies)
                    {
                        for (const auto& [_Type, _Count] : _Pattern.Counts) _Remaining[_Type] -= _Count * _Copies;
                        _StockUsed[_Pattern.Stock] += _Copies;
                        _Selection[_Index] += _Copies;
                        Score _Next = Value_;
                        _Next.Cost += StockCost(_Stock) * static_cast<std::int64_t>(_Copies);
                        _Next.NominalLength += ChargedNominalLength(_Stock) * static_cast<Length>(_Copies);
                        _Next.BarCount += _Copies;
                        _Next.OccupiedLength += _Pattern.Occupied * static_cast<Length>(_Copies);
                        _Search(Unplaced_ - _Pattern.Types.size() * _Copies, Depth_ + 1, _Next);
                        _Selection[_Index] -= _Copies;
                        _StockUsed[_Pattern.Stock] -= _Copies;
                        for (const auto& [_Type, _Count] : _Pattern.Counts) _Remaining[_Type] += _Count * _Copies;
                        if (!_Complete) break;
                    }
                    if (!_Complete) break;
                }
            };
            _Search(GroupInstances_.size(), 0, {});
            Nodes_ += _Visited;
            _RestrictedOptimal = _Complete && _HasSelection;
        }
        else _Backend = "verified-construction-fallback";
    }

    const auto _Describe = [&]() {
        return "pattern master: patterns=" + std::to_string(_Pool.Patterns.size())
            + ", generated=" + std::to_string(_Generated) + ", reused-bars=" + std::to_string(_Reused)
            + ", backend=" + _Backend + ", scope=restricted-pattern-pool"
            + (_RestrictedOptimal ? ", pool-optimal=true" : ", pool-optimal=false");
    };
    const auto _PublishFallback = [&]() {
        if (!_Fallback) { Diagnostic_ = _Describe(); return false; }
        // A later, unmaterialized CP incumbent cannot transfer its pool proof
        // to the older fallback which is actually being returned.
        if (_HasSelection && CompareScore(_BestValue, _FallbackValue, Settings_.ObjectivePolicy) < 0)
            _RestrictedOptimal = false;
        Plan_ = std::move(*_Fallback);
        Diagnostic_ = _Describe() + ", prevalidated-fallback=true, accepted=true";
        return true;
    };
    if (!_HasSelection || _Budget.Expired()
        || (!Plan_.empty() && CompareScore(_BestValue, _Before, Settings_.ObjectivePolicy) >= 0)
        || (_Fallback && CompareScore(_BestValue, _FallbackValue, Settings_.ObjectivePolicy) >= 0))
        return _PublishFallback();
    auto _Candidate = _Materialize(_BestSelection, _BestValue);
    if (!_Candidate)
    {
        const auto _Accepted = _PublishFallback();
        Diagnostic_ += ", reconstruction-rejected=true";
        return _Accepted;
    }
    Plan_ = std::move(*_Candidate);
    Diagnostic_ = _Describe() + ", accepted=true";
    return true;
}
