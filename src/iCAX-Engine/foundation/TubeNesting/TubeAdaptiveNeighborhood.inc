// Included inside the solver's namespace after WorkingPlan/Instance are known.
// The selector only reads a plan. Stock consumption, locked prefixes, model
// construction and accepting a candidate remain the owning solver's duties.

struct AdaptiveNeighborhoodState final
{
    std::uint64_t Round = 0;
    std::size_t Stagnation = 0;
    std::size_t TimeoutStreak = 0;
    std::unordered_map<std::uint64_t, std::uint64_t> BarVisits;
    double ModelTermsPerMillisecond = 256.0;
    std::uint64_t LastBudgetMilliseconds = 0;
    std::uint64_t LastEstimatedModelTerms = 0;
    bool LastUsedConstraintModel = false;
};

struct AdaptiveNeighborhoodSelection final
{
    // For CP these are complete released bars. For OversizedFallback the first
    // is the oversized seed and the rest are at most 16 complementary probes;
    // they must not be released together into the CP model.
    std::vector<std::size_t> BarIndices;
    // Local positions in BarIndices.front().Placements, never locked-prefix
    // positions. The owner must recheck every proposed fallback move in full.
    std::vector<std::size_t> FallbackProbePositions;
    std::size_t PartLimit = 0;
    std::size_t PartCount = 0;
    std::uint64_t BudgetMilliseconds = 0;
    std::uint64_t EstimatedModelTerms = 0;
    std::uint64_t SelectionMilliseconds = 0;
    bool UseConstraintModel = false;
    bool OversizedFallback = false;
    std::string Diagnostic;
};

[[nodiscard]] static std::uint64_t AdaptiveNeighborhoodModelTerms(
    const std::size_t Count_, const std::size_t StockCount_,
    const std::size_t ContinuedStockCount_)
{
    const long double _N = static_cast<long double>(Count_);
    // Every route slot has directed arcs with up to four pose choices plus
    // its arc variable. Continued stock adds stock/head/direction conjunctions.
    const long double _Terms = 5.0L * _N * _N * _N + 12.0L * _N * _N
        + static_cast<long double>(StockCount_) * _N
        + 2.0L * static_cast<long double>(ContinuedStockCount_) * _N * _N;
    return _Terms >= static_cast<long double>((std::numeric_limits<std::uint64_t>::max)())
        ? (std::numeric_limits<std::uint64_t>::max)()
        : static_cast<std::uint64_t>(_Terms);
}

static void RecordAdaptiveNeighborhoodOutcome(AdaptiveNeighborhoodState& State_,
    const bool Improved_, const bool TimedOut_, const std::uint64_t ElapsedMilliseconds_)
{
    if (Improved_) State_.Stagnation = 0;
    else if (State_.Stagnation < 1000000) ++State_.Stagnation;
    if (TimedOut_ && !Improved_)
    {
        if (State_.TimeoutStreak < 1000000) ++State_.TimeoutStreak;
    }
    else State_.TimeoutStreak = 0;
    // An actual wall-clock overrun is observable evidence that the current
    // model-volume allowance is too generous on this runtime. Search time by
    // itself is not treated as a linear model-building throughput benchmark.
    if (State_.LastUsedConstraintModel && State_.LastBudgetMilliseconds > 0
        && ElapsedMilliseconds_ > State_.LastBudgetMilliseconds)
    {
        const double _Ratio = static_cast<double>(State_.LastBudgetMilliseconds)
            / static_cast<double>(ElapsedMilliseconds_);
        State_.ModelTermsPerMillisecond = (std::max)(16.0,
            State_.ModelTermsPerMillisecond * (std::max)(0.25, _Ratio));
    }
}

[[nodiscard]] static std::uint64_t AdaptiveMixWord(std::uint64_t Value_)
{
    Value_ += 0x9e3779b97f4a7c15ULL;
    Value_ = (Value_ ^ (Value_ >> 30)) * 0xbf58476d1ce4e5b9ULL;
    Value_ = (Value_ ^ (Value_ >> 27)) * 0x94d049bb133111ebULL;
    return Value_ ^ (Value_ >> 31);
}

struct AdaptiveBarSummary final
{
    std::uint64_t Key = 0;
    std::uint64_t Visits = 0;
    std::size_t Index = 0;
    std::size_t Count = 0;
    std::array<std::size_t, 6> Types{};
    std::size_t TypeCount = 0;
    long double Capacity = 0.0L;
    long double Occupied = 0.0L;
    long double Slack = 0.0L;
    long double MinimumPart = 0.0L;
    bool Valid = false;
};

[[nodiscard]] static bool AdaptiveEntryUsable(const PairEntry& Entry_)
{
    return Entry_.Status == PairEntryStatus::Feasible || Entry_.Status == PairEntryStatus::Certified;
}

template<class TPlan, class TInstances>
[[nodiscard]] static AdaptiveNeighborhoodSelection SelectAdaptiveNeighborhood(
    const TPlan& Plan_, const TInstances& Instances_, const PairTableSnapshot& Table_,
    const std::vector<StockType>& Stocks_, const PairTableSolverSettings& Settings_,
    std::mt19937& Random_, AdaptiveNeighborhoodState& State_,
    const std::uint64_t RemainingMilliseconds_ = (std::numeric_limits<std::uint64_t>::max)())
{
    const auto _Started = std::chrono::steady_clock::now();
    AdaptiveNeighborhoodSelection _Result;
    const auto _Elapsed = [&]() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _Started).count());
    };
    const auto _Finish = [&]() {
        _Result.SelectionMilliseconds = _Elapsed();
        if (_Result.SelectionMilliseconds >= RemainingMilliseconds_)
        {
            _Result.BarIndices.clear();
            _Result.FallbackProbePositions.clear();
            _Result.UseConstraintModel = _Result.OversizedFallback = false;
            _Result.BudgetMilliseconds = 0;
            _Result.Diagnostic = "adaptive neighborhood selection exhausted the remaining wall-clock budget";
        }
        else _Result.BudgetMilliseconds = (std::min)(_Result.BudgetMilliseconds,
            RemainingMilliseconds_ - _Result.SelectionMilliseconds);
        if (Settings_.AdaptiveConstraintNeighborhoods && _Result.UseConstraintModel
            && static_cast<long double>(_Result.EstimatedModelTerms) > (std::min)(120000.0L,
                static_cast<long double>(_Result.BudgetMilliseconds) * State_.ModelTermsPerMillisecond))
        {
            // Selection itself may consume the last few global milliseconds.
            // Do not build a model sized for the larger pre-selection budget.
            _Result.BarIndices.clear();
            _Result.UseConstraintModel = false;
            _Result.PartCount = 0;
            _Result.Diagnostic = "remaining wall-clock budget cannot hold the selected neighborhood model";
        }
        State_.LastUsedConstraintModel = _Result.UseConstraintModel;
        State_.LastBudgetMilliseconds = _Result.BudgetMilliseconds;
        State_.LastEstimatedModelTerms = _Result.EstimatedModelTerms;
        return _Result;
    };
    if (Plan_.empty() || Settings_.ConstraintNeighborhoodPartLimit < 2
        || Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds == 0 || RemainingMilliseconds_ == 0)
        return _Finish();
    if (++State_.Round == 0) { State_.Round = 1; State_.BarVisits.clear(); }
    _Result.PartLimit = Settings_.ConstraintNeighborhoodPartLimit;
    _Result.BudgetMilliseconds = (std::min)(RemainingMilliseconds_,
        Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds);
    const std::size_t _Continued = static_cast<std::size_t>(std::count_if(Stocks_.begin(), Stocks_.end(),
        [](const auto& Stock_) { return !Stock_.FixedInstanceID.empty(); }));
    if (!Settings_.AdaptiveConstraintNeighborhoods)
    {
        // Keep exactly the previous random-number consumption and whole-bar
        // selection policy for the controlled A/B switch.
        std::vector<std::size_t> _Order(Plan_.size());
        std::iota(_Order.begin(), _Order.end(), 0);
        std::shuffle(_Order.begin(), _Order.end(), Random_);
        for (const auto _Bar : _Order)
        {
            const auto _Count = Plan_[_Bar].Placements.size();
            if (_Count == 0 || _Count > _Result.PartLimit
                || (_Result.PartCount && _Count > _Result.PartLimit - _Result.PartCount)) continue;
            _Result.BarIndices.push_back(_Bar);
            _Result.PartCount += _Count;
            if (_Result.PartCount >= _Result.PartLimit) break;
        }
        _Result.UseConstraintModel = _Result.PartCount >= 2;
        _Result.EstimatedModelTerms = AdaptiveNeighborhoodModelTerms(_Result.PartCount, Stocks_.size(), _Continued);
        return _Finish();
    }

    const std::size_t _Growth = (std::min)(std::size_t{8}, State_.Stagnation / 2);
    const std::size_t _Expansion = _Growth > State_.TimeoutStreak / 2 ? _Growth - State_.TimeoutStreak / 2 : 0;
    const auto _MaximumParts = (std::min)(std::size_t{48},
        Settings_.ConstraintNeighborhoodMaximumPartLimit);
    const auto _MaximumBudget = (std::max)(Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds,
        Settings_.ConstraintNeighborhoodMaximumTimeBudgetMilliseconds);
    std::uint64_t _RequestedBudget = Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds;
    for (std::size_t _I = 0; _I < _Growth / 2; ++_I)
    {
        if (Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds > _MaximumBudget - _RequestedBudget)
        { _RequestedBudget = _MaximumBudget; break; }
        _RequestedBudget += Settings_.ConstraintNeighborhoodTimeBudgetMilliseconds;
    }
    _Result.BudgetMilliseconds = (std::min)(RemainingMilliseconds_, _RequestedBudget);
    const std::uint64_t _TermLimit = static_cast<std::uint64_t>((std::min)(120000.0L,
        static_cast<long double>(_Result.BudgetMilliseconds) * State_.ModelTermsPerMillisecond));
    std::size_t _DimensionLimit = _MaximumParts;
    while (_DimensionLimit >= 2
        && AdaptiveNeighborhoodModelTerms(_DimensionLimit, Stocks_.size(), _Continued) > _TermLimit)
        --_DimensionLimit;
    const std::size_t _Base = (std::min)(Settings_.ConstraintNeighborhoodPartLimit, _MaximumParts);
    const auto _Expanded = _Base > _MaximumParts - (std::min)(_MaximumParts, _Expansion * 3)
        ? _MaximumParts : _Base + _Expansion * 3;
    _Result.PartLimit = (std::min)(_DimensionLimit, _Expanded);

    std::vector<AdaptiveBarSummary> _Bars;
    _Bars.reserve(Plan_.size());
    std::unordered_set<std::uint64_t> _CurrentKeys;
    long double _MaximumCapacity = 1.0L;
    for (std::size_t _Index = 0; _Index < Plan_.size(); ++_Index)
    {
        if ((_Index & 255) == 0 && _Elapsed() >= RemainingMilliseconds_) return _Finish();
        const auto& _Bar = Plan_[_Index];
        AdaptiveBarSummary _Info;
        _Info.Index = _Index;
        _Info.Count = _Bar.Placements.size();
        if (_Info.Count == 0 || _Bar.Stock >= Stocks_.size()) { _Bars.push_back(_Info); continue; }
        const auto& _Stock = Stocks_[_Bar.Stock];
        _Info.Capacity = static_cast<long double>(_Stock.TotalLength) - _Stock.TailDeadZone
            - (_Stock.FixedInstanceID.empty() ? _Stock.FrontMargin : _Stock.InitialProcessedEnd);
        _Info.MinimumPart = static_cast<long double>((std::numeric_limits<Length>::max)());
        std::uint64_t _Sum = 0, _Xor = 0;
        _Info.Valid = _Info.Capacity > 0;
        for (std::size_t _Position = 0; _Position < _Info.Count && _Info.Valid; ++_Position)
        {
            if ((_Position & 1023) == 0 && _Elapsed() >= RemainingMilliseconds_) return _Finish();
            const auto& _Placement = _Bar.Placements[_Position];
            if (_Placement.Instance >= Instances_.size()
                || Instances_[_Placement.Instance].Type >= Table_.Types.size()) { _Info.Valid = false; break; }
            const auto _Type = Instances_[_Placement.Instance].Type;
            const auto& _Definition = Table_.Types[_Type];
            const auto _Direction = static_cast<std::size_t>(_Placement.PartDirection);
            if (_Direction >= 2 || !_Definition.DirectionAllowed[_Direction]) { _Info.Valid = false; break; }
            _Info.Occupied += _Definition.MaximumLength;
            _Info.MinimumPart = (std::min)(_Info.MinimumPart, static_cast<long double>(_Definition.MaximumLength));
            if (_Position > 0)
            {
                const auto& _Previous = _Bar.Placements[_Position - 1];
                const auto& _Entry = Table_.At(Instances_[_Previous.Instance].Type, _Previous.PartDirection,
                    _Type, _Placement.PartDirection);
                if (!AdaptiveEntryUsable(_Entry)) { _Info.Valid = false; break; }
                _Info.Occupied -= _Entry.NetSaving;
            }
            else if (!_Stock.FixedInstanceID.empty() && _Stock.InitialPredecessorType < Table_.Types.size())
            {
                const auto& _Entry = Table_.At(_Stock.InitialPredecessorType,
                    _Stock.InitialPredecessorDirection, _Type, _Placement.PartDirection);
                if (!AdaptiveEntryUsable(_Entry)) { _Info.Valid = false; break; }
                _Info.Occupied -= _Entry.NetSaving;
            }
            const auto _Word = AdaptiveMixWord(static_cast<std::uint64_t>(_Placement.Instance));
            _Sum += _Word; _Xor ^= _Word;
        }
        if (_Info.Valid)
            for (const auto _Position : std::array<std::size_t, 6>{0, _Info.Count - 1,
                    _Info.Count / 4, _Info.Count / 2, (_Info.Count / 4) * 3,
                    static_cast<std::size_t>((State_.Round + _Index) % _Info.Count)})
            {
                const auto _Type = Instances_[_Bar.Placements[_Position].Instance].Type;
                if (std::find(_Info.Types.begin(), _Info.Types.begin() + _Info.TypeCount, _Type)
                    == _Info.Types.begin() + _Info.TypeCount) _Info.Types[_Info.TypeCount++] = _Type;
            }
        _Info.Key = AdaptiveMixWord(_Sum) ^ _Xor ^ AdaptiveMixWord(_Bar.Stock) ^ AdaptiveMixWord(_Info.Count);
        _Info.Visits = State_.BarVisits[_Info.Key];
        _Info.Slack = (std::max)(0.0L, _Info.Capacity - _Info.Occupied);
        _MaximumCapacity = (std::max)(_MaximumCapacity, _Info.Capacity);
        _CurrentKeys.insert(_Info.Key);
        _Bars.push_back(_Info);
    }
    if (State_.BarVisits.size() > Plan_.size() * 4 + 128)
        std::erase_if(State_.BarVisits, [&](const auto& Entry_) { return !_CurrentKeys.contains(Entry_.first); });
    std::vector<std::size_t> _Seeds;
    std::uint64_t _MinimumVisits = (std::numeric_limits<std::uint64_t>::max)();
    const auto _WasteScore = [&](const AdaptiveBarSummary& Bar_) {
        return 4.0L * Bar_.Slack / Bar_.Capacity + Bar_.Slack / _MaximumCapacity;
    };
    for (const auto& _Bar : _Bars)
        if (_Bar.Valid)
        {
            if (_Bar.Visits < _MinimumVisits) { _MinimumVisits = _Bar.Visits; _Seeds.clear(); }
            if (_Bar.Visits == _MinimumVisits) _Seeds.push_back(_Bar.Index);
        }
    if (_Seeds.empty()) { _Result.Diagnostic = "no valid bars available for adaptive neighborhood"; return _Finish(); }
    std::size_t _Seed = _Seeds.front();
    const bool _Explore = State_.Round % 5 == 0;
    if (_Explore) _Seed = _Seeds[std::uniform_int_distribution<std::size_t>(0, _Seeds.size() - 1)(Random_)];
    else
        for (const auto _Candidate : _Seeds)
            if (_WasteScore(_Bars[_Candidate]) > _WasteScore(_Bars[_Seed])) _Seed = _Candidate;
    // A larger seed may be released before the normal growth schedule when
    // the explicit maximum and the current model-volume budget permit it.
    if (_Bars[_Seed].Count > _Result.PartLimit && _Bars[_Seed].Count <= _DimensionLimit)
        _Result.PartLimit = _Bars[_Seed].Count;

    std::vector<std::size_t> _Candidates;
    const auto _AppendCandidate = [&](const std::size_t Index_) {
        if (Index_ != _Seed && _Bars[Index_].Valid && _Candidates.size() < 64
            && Stocks_[Plan_[Index_].Stock].CompatibilityKey == Stocks_[Plan_[_Seed].Stock].CompatibilityKey
            && std::find(_Candidates.begin(), _Candidates.end(), Index_) == _Candidates.end())
            _Candidates.push_back(Index_);
    };
    auto _WasteOrder = _Seeds;
    std::partial_sort(_WasteOrder.begin(), _WasteOrder.begin() + (std::min)(std::size_t{16}, _WasteOrder.size()),
        _WasteOrder.end(), [&](const auto A_, const auto B_) { return _WasteScore(_Bars[A_]) > _WasteScore(_Bars[B_]); });
    for (std::size_t _I = 0; _I < (std::min)(std::size_t{16}, _WasteOrder.size()); ++_I) _AppendCandidate(_WasteOrder[_I]);
    const auto _Offset = static_cast<std::size_t>(State_.Round % Plan_.size());
    for (std::size_t _I = 0; _I < (std::min)(Plan_.size(), std::size_t{32}); ++_I)
        _AppendCandidate((_Seed + _Offset + _I) % Plan_.size());
    for (std::size_t _I = 0; _I < 16 && _Candidates.size() < 64; ++_I)
        _AppendCandidate(std::uniform_int_distribution<std::size_t>(0, Plan_.size() - 1)(Random_));

    const auto _Connection = [&](const AdaptiveBarSummary& A_, const AdaptiveBarSummary& B_) {
        long double _Best = -static_cast<long double>((std::numeric_limits<Length>::max)());
        for (std::size_t _A = 0; _A < A_.TypeCount; ++_A)
            for (std::size_t _B = 0; _B < B_.TypeCount; ++_B)
                for (const bool _ReversePair : {false, true})
                    for (std::size_t _DA = 0; _DA < 2; ++_DA)
                        for (std::size_t _DB = 0; _DB < 2; ++_DB)
                        {
                            const auto _TA = _ReversePair ? B_.Types[_B] : A_.Types[_A];
                            const auto _TB = _ReversePair ? A_.Types[_A] : B_.Types[_B];
                            if (!Table_.Types[_TA].DirectionAllowed[_DA] || !Table_.Types[_TB].DirectionAllowed[_DB]) continue;
                            const auto& _Entry = Table_.At(_TA, static_cast<Direction>(_DA), _TB, static_cast<Direction>(_DB));
                            if (AdaptiveEntryUsable(_Entry)) _Best = (std::max)(_Best, static_cast<long double>(_Entry.NetSaving));
                        }
        return _Best;
    };
    const auto _ComplementScore = [&](const std::size_t Index_) {
        const auto& _Bar = _Bars[Index_];
        long double _Best = -1.0e30L;
        for (const auto _Selected : _Result.BarIndices)
        {
            const auto& _Other = _Bars[_Selected];
            const long double _Join = _Connection(_Other, _Bar);
            const auto _Capacity = (std::max)(_Other.Capacity, _Bar.Capacity);
            long double _Score = _Join / _MaximumCapacity + 0.1L * _WasteScore(_Bar);
            if (_Other.Occupied + _Bar.Occupied - _Join <= _Capacity) _Score += 4.0L;
            if (_Bar.MinimumPart - _Join <= _Other.Slack) _Score += 1.0L;
            _Best = (std::max)(_Best, _Score);
        }
        return _Best + 0.25L / (1.0L + _Bar.Visits);
    };
    _Result.BarIndices.push_back(_Seed);
    _Result.PartCount = _Bars[_Seed].Count;
    _Result.OversizedFallback = _Result.PartCount > _Result.PartLimit || _DimensionLimit < 2;
    if (_Result.OversizedFallback)
    {
        std::stable_sort(_Candidates.begin(), _Candidates.end(),
            [&](const auto A_, const auto B_) { return _ComplementScore(A_) > _ComplementScore(B_); });
        for (std::size_t _I = 0; _I < (std::min)(std::size_t{16}, _Candidates.size()); ++_I)
            _Result.BarIndices.push_back(_Candidates[_I]);
        const auto& _Placements = Plan_[_Seed].Placements;
        const auto _AppendProbe = [&](const std::size_t Position_) {
            if (_Result.FallbackProbePositions.size() < 16 && Position_ < _Placements.size()
                && std::find(_Result.FallbackProbePositions.begin(), _Result.FallbackProbePositions.end(), Position_)
                    == _Result.FallbackProbePositions.end()) _Result.FallbackProbePositions.push_back(Position_);
        };
        _AppendProbe(0); _AppendProbe(_Placements.size() - 1);
        for (std::size_t _I = 0; _I < 6; ++_I)
            _AppendProbe((_Placements.size() * _I / 6 + State_.Round) % _Placements.size());
        std::array<std::pair<Length, std::size_t>, 4> _Weak;
        for (auto& _Entry : _Weak) _Entry = {(std::numeric_limits<Length>::max)(), 0};
        for (std::size_t _I = 1; _I < _Placements.size(); ++_I)
        {
            if ((_I & 1023) == 0 && _Elapsed() >= RemainingMilliseconds_) return _Finish();
            const auto& _A = _Placements[_I - 1]; const auto& _B = _Placements[_I];
            const auto _Saving = Table_.At(Instances_[_A.Instance].Type, _A.PartDirection,
                Instances_[_B.Instance].Type, _B.PartDirection).NetSaving;
            if (_Saving < _Weak.back().first) { _Weak.back() = {_Saving, _I}; std::sort(_Weak.begin(), _Weak.end()); }
        }
        for (const auto& [_Saving, _Position] : _Weak)
            if (_Position) { _AppendProbe(_Position - 1); _AppendProbe(_Position); }
        ++State_.BarVisits[_Bars[_Seed].Key];
        _Result.Diagnostic = "oversized bar scheduled for bounded, fully revalidated moves; not released into CP-SAT";
        return _Finish();
    }
    while (_Result.PartCount < _Result.PartLimit)
    {
        if (_Elapsed() >= RemainingMilliseconds_) return _Finish();
        std::optional<std::size_t> _Best;
        long double _BestScore = -1.0e30L;
        for (const auto _Candidate : _Candidates)
        {
            if (std::find(_Result.BarIndices.begin(), _Result.BarIndices.end(), _Candidate) != _Result.BarIndices.end()
                || _Bars[_Candidate].Count > _Result.PartLimit - _Result.PartCount) continue;
            const auto _Terms = AdaptiveNeighborhoodModelTerms(_Result.PartCount + _Bars[_Candidate].Count,
                Stocks_.size(), _Continued);
            if (_Terms > _TermLimit) continue;
            const long double _Score = _Explore ? std::uniform_real_distribution<double>(0.0, 1.0)(Random_)
                : _ComplementScore(_Candidate);
            if (!_Best || _Score > _BestScore) { _Best = _Candidate; _BestScore = _Score; }
        }
        if (!_Best) break;
        _Result.BarIndices.push_back(*_Best);
        _Result.PartCount += _Bars[*_Best].Count;
    }
    for (const auto _Bar : _Result.BarIndices) ++State_.BarVisits[_Bars[_Bar].Key];
    _Result.UseConstraintModel = _Result.PartCount >= 2;
    _Result.EstimatedModelTerms = AdaptiveNeighborhoodModelTerms(_Result.PartCount, Stocks_.size(), _Continued);
    _Result.Diagnostic = "adaptive whole-bar neighborhood: waste/coverage seed with bounded directed-complement candidates";
    return _Finish();
}
