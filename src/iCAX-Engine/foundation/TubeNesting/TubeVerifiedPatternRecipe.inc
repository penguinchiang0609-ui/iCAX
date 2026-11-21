// Complete ordinary-stock route combinations warm-start the same demand and
// objective. These carry no physical IDs, mutable inventory, or proof labels.
[[nodiscard]] std::optional<WorkingPlan> RestoreVerifiedTubePatternRecipe(
    const std::vector<std::size_t>& GroupInstances_, const std::vector<Instance>& Instances_,
    const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
    const PairTableSolverSettings& Settings_, TubePatternCacheSession& Session_,
    const std::chrono::steady_clock::time_point Deadline_, ResultMetrics* Metrics_)
{
    if (!Settings_.ReuseVerifiedPatterns || Settings_.PatternMasterTimeBudgetMilliseconds == 0
        || GroupInstances_.empty() || Stocks_.empty()
        || std::ranges::any_of(Stocks_, [](const auto& Stock_) { return IsContinuation(Stock_); })) return {};
    auto _Context = TubePatternCacheContext(Table_, Stocks_, Deadline_);
    if (!_Context || !VerifiedTubePatternCache().HasContext(*_Context)
        || !PrepareTubePatternCacheFingerprint(Session_, Table_, Deadline_)) return {};
    const auto _Cached = VerifiedTubePatternCache().Find({Session_.HashA, Session_.HashB, *_Context});
    if (!_Cached) return {};
    std::vector<std::size_t> _Demand(Table_.Types.size(), 0);
    std::vector<std::vector<std::size_t>> _ByType(Table_.Types.size());
    for (const auto _Instance : GroupInstances_)
    {
        if (std::chrono::steady_clock::now() >= Deadline_ || _Instance >= Instances_.size()) return {};
        const auto _Type = Instances_[_Instance].Type;
        if (_Type >= _Demand.size()) return {};
        ++_Demand[_Type]; _ByType[_Type].push_back(_Instance);
    }
    const TubePatternRecipe* _Recipe = nullptr;
    for (const auto& _Candidate : _Cached->Recipes)
        if (_Candidate.Objective == Settings_.ObjectivePolicy && _Candidate.Demand == _Demand)
        {
            if (_Candidate.Patterns.empty() || _Candidate.Patterns.size() != _Candidate.Uses.size()) continue;
            std::vector<std::size_t> _AvailableUses(Stocks_.size(), 0);
            bool _Fits = true;
            for (std::size_t _Index = 0; _Index < _Candidate.Patterns.size(); ++_Index)
            {
                const auto _Stock = _Candidate.Patterns[_Index].Stock;
                const auto _Use = _Candidate.Uses[_Index];
                if (_Stock >= Stocks_.size() || _Use == 0
                    || _Use > GroupInstances_.size() - _AvailableUses[_Stock])
                { _Fits = false; break; }
                _AvailableUses[_Stock] += _Use;
                if (Stocks_[_Stock].Quantity && _AvailableUses[_Stock] > Stocks_[_Stock].Quantity)
                { _Fits = false; break; }
            }
            // Availability is part of recipe replacement identity, but a route
            // combination may be reused under any current inventory it fits.
            if (_Fits && (!_Recipe || CompareScore(_Candidate.Value, _Recipe->Value,
                Settings_.ObjectivePolicy) < 0)) _Recipe = &_Candidate;
        }
    if (!_Recipe || _Recipe->Patterns.empty() || _Recipe->Patterns.size() != _Recipe->Uses.size()) return {};
    if (Metrics_) { ++Metrics_->PatternCacheHits; ++Metrics_->PatternCacheRecipeHits; }
    TubeMasterPool _Pool(Instances_, Table_, Stocks_, _ByType, 2048);
    std::vector<std::size_t> _Indices;
    _Indices.reserve(_Recipe->Patterns.size());
    std::size_t _BarCount = 0;
    std::vector<std::size_t> _StockUses(Stocks_.size(), 0);
    for (std::size_t _PatternIndex = 0; _PatternIndex < _Recipe->Patterns.size(); ++_PatternIndex)
    {
        if (std::chrono::steady_clock::now() >= Deadline_) return {};
        const auto& _Pattern = _Recipe->Patterns[_PatternIndex];
        const auto _Use = _Recipe->Uses[_PatternIndex];
        if (_Pattern.Stock >= Stocks_.size() || _Use == 0 || _Use > GroupInstances_.size() - _BarCount)
            return {};
        _BarCount += _Use;
        auto& _StockUse = _StockUses[_Pattern.Stock];
        if (_Use > GroupInstances_.size() - _StockUse) return {};
        _StockUse += _Use;
        if (Stocks_[_Pattern.Stock].Quantity && _StockUse > Stocks_[_Pattern.Stock].Quantity) return {};
        const auto _Index = _Pool.Add(_Pattern.Stock, _Pattern.Types);
        if (!_Index) return {};
        _Indices.push_back(*_Index);
    }
    WorkingPlan _Plan;
    _Plan.reserve(_BarCount);
    std::vector<std::size_t> _Taken(_Demand.size(), 0);
    for (std::size_t _PatternIndex = 0; _PatternIndex < _Indices.size(); ++_PatternIndex)
    {
        const auto& _Pattern = _Pool.Patterns[_Indices[_PatternIndex]];
        for (std::size_t _Copy = 0; _Copy < _Recipe->Uses[_PatternIndex]; ++_Copy)
        {
            if (std::chrono::steady_clock::now() >= Deadline_) return {};
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
            _Plan.push_back(std::move(_Bar));
        }
    }
    TubeMasterBudget _Budget{Deadline_, 0, 0};
    Score _Actual;
    if (!ValidateTubeMasterPlan(_Plan, GroupInstances_, Instances_, Table_, Stocks_, _Actual, &_Budget)) return {};
    if (Metrics_) ++Metrics_->PatternCacheRecipesRestored;
    return _Plan;
}

void PublishVerifiedTubePatternRecipe(const WorkingPlan& Plan_,
    const std::vector<std::size_t>& GroupInstances_, const std::vector<Instance>& Instances_,
    const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
    const PairTableSolverSettings& Settings_, TubePatternCacheSession& Session_,
    const std::chrono::steady_clock::time_point Deadline_, ResultMetrics* Metrics_)
{
    if (!Settings_.ReuseVerifiedPatterns || Settings_.PatternMasterTimeBudgetMilliseconds == 0
        || GroupInstances_.empty() || Plan_.empty() || Stocks_.empty()
        || std::ranges::any_of(Stocks_, [](const auto& Stock_) { return IsContinuation(Stock_); })) return;
    TubeMasterBudget _Budget{Deadline_, 0, 0};
    Score _Actual;
    if (!ValidateTubeMasterPlan(Plan_, GroupInstances_, Instances_, Table_, Stocks_, _Actual, &_Budget)) return;
    TubePatternRecipe _Recipe;
    _Recipe.Demand.assign(Table_.Types.size(), 0);
    for (const auto& _Stock : Stocks_) _Recipe.Availability.push_back(_Stock.Quantity);
    _Recipe.Objective = Settings_.ObjectivePolicy;
    for (const auto _Instance : GroupInstances_) ++_Recipe.Demand[Instances_[_Instance].Type];
    std::map<std::string, std::size_t> _Indices;
    std::size_t _Cells = 0;
    for (const auto& _Bar : Plan_)
    {
        if (std::chrono::steady_clock::now() >= Deadline_ || _Bar.Placements.size() > 4096) return;
        TubeMasterPattern _Pattern;
        _Pattern.Stock = _Bar.Stock;
        std::map<std::size_t, std::size_t> _Counts;
        std::vector<std::size_t> _Order;
        _Order.reserve(_Bar.Placements.size());
        for (const auto& _Placement : _Bar.Placements)
        {
            const auto _Type = Instances_[_Placement.Instance].Type;
            _Pattern.Types.push_back(_Type); _Order.push_back(_Placement.Instance); ++_Counts[_Type];
        }
        const auto _Directions = OptimizeDirections(_Order, 0, _Order.size(), Instances_, Table_, Stocks_[_Bar.Stock]);
        if (!_Directions.Feasible || _Directions.Occupied > UsableLength(Stocks_[_Bar.Stock])) return;
        _Pattern.Directions = _Directions.Directions;
        _Pattern.Occupied = _Directions.Occupied;
        _Pattern.Counts.assign(_Counts.begin(), _Counts.end());
        auto _Key = TubePatternCountKey(_Pattern);
        const auto _Found = _Indices.find(_Key);
        if (_Found != _Indices.end())
        {
            ++_Recipe.Uses[_Found->second];
            if (_Pattern.Occupied < _Recipe.Patterns[_Found->second].Occupied)
                _Recipe.Patterns[_Found->second] = std::move(_Pattern);
        }
        else
        {
            if (_Recipe.Patterns.size() >= 2048 || _Pattern.Types.size() > 200000 - _Cells) return;
            _Cells += _Pattern.Types.size();
            _Indices.emplace(std::move(_Key), _Recipe.Patterns.size());
            _Recipe.Patterns.push_back(std::move(_Pattern));
            _Recipe.Uses.push_back(1);
        }
    }
    for (std::size_t _Index = 0; _Index < _Recipe.Patterns.size(); ++_Index)
    {
        const auto& _Pattern = _Recipe.Patterns[_Index];
        const auto& _Stock = Stocks_[_Pattern.Stock];
        const auto _Use = _Recipe.Uses[_Index];
        _Recipe.Value.Cost += StockCost(_Stock) * static_cast<std::int64_t>(_Use);
        _Recipe.Value.NominalLength += ChargedNominalLength(_Stock) * static_cast<Length>(_Use);
        _Recipe.Value.BarCount += _Use;
        _Recipe.Value.OccupiedLength += _Pattern.Occupied * static_cast<Length>(_Use);
    }
    if (TubePatternRecipeBytes(_Recipe) > 4U * 1024U * 1024U) return;
    auto _Context = TubePatternCacheContext(Table_, Stocks_, Deadline_);
    if (!_Context || !PrepareTubePatternCacheFingerprint(Session_, Table_, Deadline_)) return;
    VerifiedTubePatternCache().Store({Session_.HashA, Session_.HashB, *_Context},
        _Recipe.Patterns, Stocks_, Deadline_, Metrics_, &_Recipe);
}
