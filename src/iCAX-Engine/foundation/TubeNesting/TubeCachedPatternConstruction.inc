// Included after ConstructTubeMasterPattern. Cached routes have already passed
// TubeMasterPool::Add against this request. This only proposes multiplicities;
// the shared master materializer still restores and independently checks every
// physical instance before a complete candidate may replace the incumbent.
[[nodiscard]] std::optional<std::vector<std::size_t>> ConstructCachedTubeMasterPlan(
    TubeMasterPool& Pool_, const std::vector<std::size_t>& CachedIndices_,
    const std::vector<std::size_t>& Demand_, const std::vector<std::size_t>& TypeOrder_,
    const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
    const PairTableSolverSettings& Settings_, TubeMasterBudget& Budget_)
{
    if (CachedIndices_.empty() || Budget_.Expired()) return {};
    auto _Remaining = Demand_;
    std::size_t _Unplaced = std::accumulate(_Remaining.begin(), _Remaining.end(), std::size_t{0});
    std::vector<std::size_t> _StockRemaining, _StockOrder(Stocks_.size()), _Selection;
    for (const auto& _Stock : Stocks_) _StockRemaining.push_back(_Stock.Quantity);
    std::iota(_StockOrder.begin(), _StockOrder.end(), 0);
    const auto _UnitPrice = [&](const std::size_t Stock_) {
        const auto& _Stock = Stocks_[Stock_];
        const auto _Cost = Settings_.ObjectivePolicy == PairObjectivePolicy::MinPurchaseCost
            ? StockCost(_Stock) : ChargedNominalLength(_Stock);
        return static_cast<long double>(_Cost) / (std::max)(Length{1}, UsableLength(_Stock));
    };
    std::stable_sort(_StockOrder.begin(), _StockOrder.end(), [&](const auto A_, const auto B_) {
        return _UnitPrice(A_) < _UnitPrice(B_);
    });
    bool _UsedCachedRoute = false;
    std::size_t _Bars = 0;
    while (_Unplaced && !Budget_.Expired())
    {
        std::optional<std::size_t> _Best;
        long double _BestPrice = 0.0L, _BestNominal = 0.0L, _BestCoverage = 0.0L;
        for (const auto _Index : CachedIndices_)
        {
            if (!Budget_.Evaluate()) return {};
            if (_Index >= Pool_.Patterns.size()) continue;
            const auto& _Pattern = Pool_.Patterns[_Index];
            if (_Pattern.Stock >= Stocks_.size() || _Pattern.Counts.empty()) continue;
            const auto& _Stock = Stocks_[_Pattern.Stock];
            if (_Stock.Quantity && _StockRemaining[_Pattern.Stock] == 0) continue;
            bool _Fits = true;
            long double _Coverage = 0.0L;
            for (const auto& [_Type, _Count] : _Pattern.Counts)
            {
                if (!Budget_.Evaluate()) return {};
                if (_Type >= _Remaining.size() || _Count == 0 || _Count > _Remaining[_Type])
                { _Fits = false; break; }
                _Coverage += static_cast<long double>(Table_.Types[_Type].MaximumLength) * _Count;
            }
            if (!_Fits || _Coverage <= 0.0L) continue;
            const auto _Nominal = static_cast<long double>(ChargedNominalLength(_Stock)) / _Coverage;
            const auto _Price = Settings_.ObjectivePolicy == PairObjectivePolicy::MinPurchaseCost
                ? static_cast<long double>(StockCost(_Stock)) / _Coverage : _Nominal;
            // Procurement per covered demand is only a construction heuristic.
            // The complete candidate is compared with the ordinary full score.
            if (!_Best || _Price < _BestPrice
                || (_Price == _BestPrice && (_Nominal < _BestNominal
                    || (_Nominal == _BestNominal && _Coverage > _BestCoverage))))
            {
                _Best = _Index;
                _BestPrice = _Price;
                _BestNominal = _Nominal;
                _BestCoverage = _Coverage;
            }
        }
        if (_Best) _UsedCachedRoute = true;
        if (!_Best)
        {
            // Residual demand need not be identical to an earlier order. A
            // fresh verified route can close the remaining quantity coverage.
            for (const auto _Stock : _StockOrder)
            {
                if (Budget_.Expired()) return {};
                if (Stocks_[_Stock].Quantity && _StockRemaining[_Stock] == 0) continue;
                auto _Types = ConstructTubeMasterPattern(_Remaining, TypeOrder_, Table_, Stocks_[_Stock],
                    _Bars + static_cast<std::size_t>(Settings_.PatternMasterSearchRound) * 131U,
                    Settings_, Budget_);
                if (_Types.empty()) continue;
                _Best = Pool_.Add(_Stock, _Types);
                if (_Best) break;
            }
        }
        if (!_Best) return {};
        const auto& _Pattern = Pool_.Patterns[*_Best];
        std::size_t _Repeat = _Unplaced;
        for (const auto& [_Type, _Count] : _Pattern.Counts)
            _Repeat = (std::min)(_Repeat, _Remaining[_Type] / _Count);
        if (Stocks_[_Pattern.Stock].Quantity)
            _Repeat = (std::min)(_Repeat, _StockRemaining[_Pattern.Stock]);
        if (_Repeat == 0) return {};
        _Selection.resize(Pool_.Patterns.size(), 0);
        _Selection[*_Best] += _Repeat;
        for (const auto& [_Type, _Count] : _Pattern.Counts) _Remaining[_Type] -= _Count * _Repeat;
        _Unplaced -= _Pattern.Types.size() * _Repeat;
        if (Stocks_[_Pattern.Stock].Quantity) _StockRemaining[_Pattern.Stock] -= _Repeat;
        _Bars += _Repeat;
    }
    if (_Unplaced || !_UsedCachedRoute) return {};
    return _Selection;
}
