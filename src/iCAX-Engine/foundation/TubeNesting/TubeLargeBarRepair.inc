// Bounded repair for stocks too large for the instance-level CP neighborhood.
// Only mutable placements participate; a continuation stock's locked prefix is
// represented by StockType and is rechecked by OptimizeDirections.
template<class TSelection>
bool ImproveLargeBarNeighborhood(WorkingPlan& Plan_, const TSelection& Selection_,
    const std::vector<Instance>& Instances_, const PairTableSnapshot& Table_,
    const std::vector<StockType>& Stocks_, const PairTableSolverSettings& Settings_,
    const std::uint64_t BudgetMilliseconds_)
{
    if (Selection_.BarIndices.empty() || BudgetMilliseconds_ == 0) return false;
    const auto _Seed = Selection_.BarIndices.front();
    if (_Seed >= Plan_.size() || Plan_[_Seed].Placements.empty()) return false;
    const auto _Started = std::chrono::steady_clock::now();
    const auto _Expired = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _Started).count()
                >= static_cast<std::int64_t>(BudgetMilliseconds_);
    };
    std::uint64_t _WorkRemaining = 250000;
    const auto _Rebuild = [&](WorkingBar& Bar_, Length& Occupied_) {
        Occupied_ = 0;
        if (Bar_.Stock >= Stocks_.size()) return false;
        if (Bar_.Placements.empty()) return true;
        const auto& _Stock = Stocks_[Bar_.Stock];
        if (_Stock.MaxParts && Bar_.Placements.size() > _Stock.MaxParts) return false;
        std::vector<std::size_t> _Order;
        std::set<std::string> _Orders;
        _Order.reserve(Bar_.Placements.size());
        for (const auto& _Placement : Bar_.Placements)
        {
            if (_Placement.Instance >= Instances_.size()) return false;
            const auto _Type = Instances_[_Placement.Instance].Type;
            if (_Type >= Table_.Types.size()
                || Table_.Types[_Type].CompatibilityKey != _Stock.CompatibilityKey) return false;
            if (!Table_.Types[_Type].OrderKey.empty()) _Orders.insert(Table_.Types[_Type].OrderKey);
            _Order.push_back(_Placement.Instance);
        }
        if (_Stock.MaxDistinctOrders && _Orders.size() > _Stock.MaxDistinctOrders) return false;
        const auto _Directions = OptimizeDirections(_Order, 0, _Order.size(), Instances_, Table_, _Stock);
        if (!_Directions.Feasible || _Directions.Occupied > UsableLength(_Stock)) return false;
        for (std::size_t _Index = 0; _Index < Bar_.Placements.size(); ++_Index)
            Bar_.Placements[_Index].PartDirection = _Directions.Directions[_Index];
        if (!PopulateRotations(Bar_, Instances_, Table_, _Stock)) return false;
        Occupied_ = _Directions.Occupied;
        return true;
    };
    const auto _ScorePair = [&](const WorkingBar& A_, const Length OccupiedA_,
        const WorkingBar& B_, const Length OccupiedB_) {
        Score _Value;
        if (!A_.Placements.empty()) _Value = AddStockScore(_Value, Stocks_[A_.Stock], OccupiedA_);
        if (!B_.Placements.empty()) _Value = AddStockScore(_Value, Stocks_[B_.Stock], OccupiedB_);
        return _Value;
    };
    std::vector<std::size_t> _Probes = Selection_.FallbackProbePositions;
    if (_Probes.empty()) _Probes = {0, Plan_[_Seed].Placements.size() / 2,
        Plan_[_Seed].Placements.size() - 1};
    if (_Probes.size() > 8) _Probes.resize(8);
    for (std::size_t _CandidateIndex = 1;
        _CandidateIndex < Selection_.BarIndices.size() && _CandidateIndex <= 16; ++_CandidateIndex)
    {
        const auto _Other = Selection_.BarIndices[_CandidateIndex];
        if (_Other >= Plan_.size() || _Other == _Seed || Plan_[_Other].Placements.empty()) continue;
        const auto _PairWork = static_cast<std::uint64_t>(
            Plan_[_Seed].Placements.size() + Plan_[_Other].Placements.size());
        if (_PairWork > _WorkRemaining || _Expired()) break;
        auto _OriginalA = Plan_[_Seed], _OriginalB = Plan_[_Other];
        Length _BeforeA = 0, _BeforeB = 0;
        if (!_Rebuild(_OriginalA, _BeforeA) || !_Rebuild(_OriginalB, _BeforeB)) continue;
        _WorkRemaining -= _PairWork;
        const auto _Before = _ScorePair(_OriginalA, _BeforeA, _OriginalB, _BeforeB);
        const auto _Try = [&](WorkingBar A_, WorkingBar B_) {
            if (_Expired() || _PairWork > _WorkRemaining) return false;
            _WorkRemaining -= _PairWork;
            Length _AfterA = 0, _AfterB = 0;
            if (!_Rebuild(A_, _AfterA) || !_Rebuild(B_, _AfterB)
                || CompareScore(_ScorePair(A_, _AfterA, B_, _AfterB), _Before,
                    Settings_.ObjectivePolicy) >= 0) return false;
            // Transfer and swap preserve every instance exactly once and never
            // add a stock. Hence no finite stock can be over-consumed here.
            Plan_[_Seed] = std::move(A_);
            Plan_[_Other] = std::move(B_);
            if (Plan_[_Seed].Placements.empty())
                Plan_.erase(Plan_.begin() + static_cast<std::ptrdiff_t>(_Seed));
            else if (Plan_[_Other].Placements.empty())
                Plan_.erase(Plan_.begin() + static_cast<std::ptrdiff_t>(_Other));
            return true;
        };
        const auto _SizeB = _OriginalB.Placements.size();
        std::vector<std::size_t> _Positions{0, _SizeB / 2, _SizeB};
        if (_SizeB > 2) { _Positions.push_back(_SizeB / 3); _Positions.push_back(_SizeB * 2 / 3); }
        std::sort(_Positions.begin(), _Positions.end());
        _Positions.erase(std::unique(_Positions.begin(), _Positions.end()), _Positions.end());
        for (const auto _PositionA : _Probes)
        {
            if (_PositionA >= _OriginalA.Placements.size()) continue;
            for (const auto _PositionB : _Positions)
            {
                if (_Expired() || _PairWork > _WorkRemaining) return false;
                auto _A = _OriginalA, _B = _OriginalB;
                const auto _Moved = _A.Placements[_PositionA];
                _A.Placements.erase(_A.Placements.begin() + static_cast<std::ptrdiff_t>(_PositionA));
                _B.Placements.insert(_B.Placements.begin() + static_cast<std::ptrdiff_t>(_PositionB), _Moved);
                if (_Try(std::move(_A), std::move(_B))) return true;
                if (_PositionB < _SizeB)
                {
                    _A = _OriginalA; _B = _OriginalB;
                    std::swap(_A.Placements[_PositionA], _B.Placements[_PositionB]);
                    if (_Try(std::move(_A), std::move(_B))) return true;
                    // A small donor stock may fit into this large seed. The
                    // symmetric move can empty that donor and reduce material
                    // procurement, rather than only tightening two full bars.
                    for (const bool _Append : {false, true})
                    {
                        _A = _OriginalA; _B = _OriginalB;
                        const auto _Donor = _B.Placements[_PositionB];
                        _B.Placements.erase(_B.Placements.begin() + static_cast<std::ptrdiff_t>(_PositionB));
                        const auto _Insert = _Append ? _A.Placements.size() : _PositionA;
                        _A.Placements.insert(_A.Placements.begin() + static_cast<std::ptrdiff_t>(_Insert), _Donor);
                        if (_Try(std::move(_A), std::move(_B))) return true;
                    }
                }
            }
        }
    }
    return false;
}
