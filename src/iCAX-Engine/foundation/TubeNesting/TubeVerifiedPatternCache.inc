// Process-local candidates, never a cache of a complete demand/inventory solve.
// Included after TubeMasterPattern / TubeMasterBudget are defined. Fingerprints
// avoid retaining the dense pair table. Imported routes are ALWAYS revalidated
// by TubeMasterPool::Add, including after a hypothetical fingerprint collision.
struct TubePatternCacheSession final
{
    std::uint64_t HashA = 1469598103934665603ULL;
    std::uint64_t HashB = 0x9e3779b97f4a7c15ULL;
    std::size_t Entry = 0, Pose = 0;
    bool Started = false, Ready = false;
};

struct TubePatternCacheKey final
{
    std::uint64_t HashA = 0, HashB = 0;
    std::string Context;
    bool operator==(const TubePatternCacheKey&) const = default;
};

[[nodiscard]] bool PrepareTubePatternCacheFingerprint(TubePatternCacheSession& Session_,
    const PairTableSnapshot& Table_, const std::chrono::steady_clock::time_point Deadline_)
{
    if (Session_.Ready) return true;
    const auto _Word = [&](const std::uint64_t Value_) {
        Session_.HashA = (Session_.HashA ^ Value_) * 1099511628211ULL;
        Session_.HashB = std::rotl(Session_.HashB + Value_ + 0x517cc1b727220a95ULL, 23)
            * 0x9ddfea08eb382d69ULL;
    };
    if (!Session_.Started)
    {
        _Word(Table_.Entries.size()); _Word(Table_.Poses.size());
        Session_.Started = true;
    }
    for (; Session_.Entry < Table_.Entries.size(); ++Session_.Entry)
    {
        if ((Session_.Entry & 1023U) == 0 && std::chrono::steady_clock::now() >= Deadline_) return false;
        const auto& _Entry = Table_.Entries[Session_.Entry];
        _Word(static_cast<std::uint64_t>(_Entry.NetSaving));
        _Word(static_cast<std::uint64_t>(_Entry.PoseMetadataIndex)
            | (static_cast<std::uint64_t>(_Entry.Status) << 32));
    }
    for (; Session_.Pose < Table_.Poses.size(); ++Session_.Pose)
    {
        if ((Session_.Pose & 1023U) == 0 && std::chrono::steady_clock::now() >= Deadline_) return false;
        const auto& _Pose = Table_.Poses[Session_.Pose];
        _Word(std::bit_cast<std::uint64_t>(_Pose.RelativeRotationRadians));
        _Word(static_cast<std::uint64_t>(_Pose.RequiredSeparation));
        _Word(std::bit_cast<std::uint64_t>(_Pose.ContinuousSeparationUpper));
        _Word(std::bit_cast<std::uint64_t>(_Pose.ContinuousSeparationLower));
        _Word(std::bit_cast<std::uint64_t>(_Pose.OptimalityGap));
        _Word(static_cast<std::uint64_t>(_Pose.PoseStatus)
            | (static_cast<std::uint64_t>(_Pose.SourceQuality) << 32));
    }
    Session_.Ready = true;
    return true;
}

[[nodiscard]] std::optional<std::string> TubePatternCacheContext(
    const PairTableSnapshot& Table_, const std::vector<StockType>& Stocks_,
    const std::chrono::steady_clock::time_point Deadline_)
{
    constexpr std::size_t _MaximumContextBytes = 1024U * 1024U;
    std::string _Context;
    const auto _Word = [&](const std::uint64_t Value_) {
        for (unsigned _Byte = 0; _Byte < 8; ++_Byte)
            _Context.push_back(static_cast<char>((Value_ >> (_Byte * 8)) & 0xffU));
    };
    const auto _Text = [&](const std::string& Value_) {
        if (Value_.size() > _MaximumContextBytes - (std::min)(_Context.size(), _MaximumContextBytes))
            return false;
        _Word(Value_.size()); _Context += Value_;
        return _Context.size() <= _MaximumContextBytes;
    };
    _Word(1); // Change this schema when the candidate/context contract changes.
    for (const auto* _TextValue : {&Table_.Schema, &Table_.Version, &Table_.LengthUnit,
        &Table_.TypeDefinitionHash, &Table_.ProcessConfigurationHash})
        if (!_Text(*_TextValue)) return {};
    _Word(Table_.Types.size());
    for (const auto& _Type : Table_.Types)
    {
        if (std::chrono::steady_clock::now() >= Deadline_ || !_Text(_Type.ID)
            || !_Text(_Type.CompatibilityKey) || !_Text(_Type.OrderKey)) return {};
        _Word(static_cast<std::uint64_t>(_Type.MaximumLength));
        _Word(static_cast<std::uint64_t>(_Type.MaterialLength));
        _Word(_Type.DirectionAllowed[0]); _Word(_Type.DirectionAllowed[1]);
        // Quantity intentionally excluded: it is checked on every import.
    }
    _Word(Stocks_.size());
    for (const auto& _Stock : Stocks_)
    {
        if (std::chrono::steady_clock::now() >= Deadline_ || !_Text(_Stock.ID)
            || !_Text(_Stock.CompatibilityKey) || !_Text(_Stock.SourceTypeID)
            || !_Text(_Stock.FixedInstanceID) || !_Text(_Stock.InitialPredecessorTypeID)) return {};
        for (const auto _Value : {static_cast<std::uint64_t>(_Stock.Kind),
            static_cast<std::uint64_t>(_Stock.TotalLength), static_cast<std::uint64_t>(_Stock.FrontMargin),
            static_cast<std::uint64_t>(_Stock.TailDeadZone), static_cast<std::uint64_t>(_Stock.CostUnits),
            static_cast<std::uint64_t>(_Stock.MaxParts), static_cast<std::uint64_t>(_Stock.MaxDistinctOrders),
            static_cast<std::uint64_t>(_Stock.MinReusableRemainder),
            static_cast<std::uint64_t>(_Stock.InitialProcessedEnd),
            static_cast<std::uint64_t>(_Stock.InitialPredecessorDirection),
            std::bit_cast<std::uint64_t>(_Stock.InitialPredecessorRotationRadians)}) _Word(_Value);
        // Stock quantity is an availability constraint of the current master.
    }
    if (_Context.size() > _MaximumContextBytes) return {};
    return _Context;
}

[[nodiscard]] std::string TubePatternCountKey(const TubeMasterPattern& Pattern_)
{
    std::string _Key;
    const auto _Word = [&](const std::size_t Value_) {
        for (unsigned _Byte = 0; _Byte < 8; ++_Byte)
            _Key.push_back(static_cast<char>((static_cast<std::uint64_t>(Value_) >> (_Byte * 8)) & 0xffU));
    };
    _Word(Pattern_.Stock);
    for (const auto& [_Type, _Count] : Pattern_.Counts) { _Word(_Type); _Word(_Count); }
    return _Key;
}

struct TubePatternRecipe final
{
    std::vector<std::size_t> Demand;
    std::vector<std::size_t> Availability;
    PairObjectivePolicy Objective = PairObjectivePolicy::MinPurchasedLength;
    std::vector<TubeMasterPattern> Patterns;
    std::vector<std::size_t> Uses;
    Score Value;
};

[[nodiscard]] std::size_t TubePatternRecipeBytes(const TubePatternRecipe& Recipe_)
{
    std::size_t _Bytes = (Recipe_.Demand.capacity() + Recipe_.Availability.capacity()) * sizeof(std::size_t)
        + Recipe_.Uses.capacity() * sizeof(std::size_t)
        + Recipe_.Patterns.capacity() * sizeof(TubeMasterPattern);
    for (const auto& _Pattern : Recipe_.Patterns)
        _Bytes += _Pattern.Types.capacity() * sizeof(std::size_t)
            + _Pattern.Directions.capacity() * sizeof(Direction)
            + _Pattern.Counts.capacity() * sizeof(std::pair<std::size_t, std::size_t>);
    return _Bytes;
}

[[nodiscard]] std::size_t TubePatternRecipeCells(const TubePatternRecipe& Recipe_)
{
    std::size_t _Cells = 0;
    for (const auto& _Pattern : Recipe_.Patterns) _Cells += _Pattern.Types.size();
    return _Cells;
}

struct TubePatternCacheSnapshot final
{
    TubePatternCacheKey Key;
    std::vector<TubeMasterPattern> Patterns;
    std::vector<TubePatternRecipe> Recipes;
    std::size_t Bytes = 0;
};

class TubeVerifiedPatternCache final
{
    static constexpr std::size_t kMaximumContexts = 8;
    static constexpr std::size_t kMaximumBytes = 16U * 1024U * 1024U;
    static constexpr std::size_t kMaximumSnapshotBytes = 4U * 1024U * 1024U;
    static constexpr std::size_t kMaximumPatterns = 2048;
    static constexpr std::size_t kMaximumCells = 200000;
    std::mutex m_Mutex;
    std::list<std::shared_ptr<const TubePatternCacheSnapshot>> m_Entries;
    std::size_t m_Bytes = 0;

public:
    [[nodiscard]] bool HasContext(const std::string& Context_)
    {
        std::unique_lock _Lock(m_Mutex, std::try_to_lock);
        if (!_Lock.owns_lock()) return false;
        return std::ranges::any_of(m_Entries, [&](const auto& Entry_) { return Entry_->Key.Context == Context_; });
    }

    [[nodiscard]] std::shared_ptr<const TubePatternCacheSnapshot> Find(const TubePatternCacheKey& Key_)
    {
        std::unique_lock _Lock(m_Mutex, std::try_to_lock);
        if (!_Lock.owns_lock()) return {};
        for (auto _It = m_Entries.begin(); _It != m_Entries.end(); ++_It)
            if ((*_It)->Key == Key_)
            {
                auto _Snapshot = *_It;
                m_Entries.splice(m_Entries.begin(), m_Entries, _It);
                return _Snapshot;
            }
        return {};
    }

    void Store(const TubePatternCacheKey& Key_, const std::vector<TubeMasterPattern>& Patterns_,
        const std::vector<StockType>& Stocks_, const std::chrono::steady_clock::time_point Deadline_,
        ResultMetrics* Metrics_, const TubePatternRecipe* Recipe_ = nullptr)
    {
        auto _Old = Find(Key_);
        std::vector<const TubePatternRecipe*> _Recipes;
        bool _RecipeChanged = false;
        const TubePatternRecipe* _ExistingRecipe = nullptr;
        if (_Old && Recipe_)
            for (const auto& _PreviousRecipe : _Old->Recipes)
                if (_PreviousRecipe.Objective == Recipe_->Objective && _PreviousRecipe.Demand == Recipe_->Demand
                    && _PreviousRecipe.Availability == Recipe_->Availability)
                { _ExistingRecipe = &_PreviousRecipe; break; }
        if (Recipe_ && (!_ExistingRecipe
            || CompareScore(Recipe_->Value, _ExistingRecipe->Value, Recipe_->Objective) < 0))
        {
            _Recipes.push_back(Recipe_);
            _RecipeChanged = true;
        }
        if (_Old)
            for (const auto& _PreviousRecipe : _Old->Recipes)
            {
                if (_RecipeChanged && &_PreviousRecipe == _ExistingRecipe) continue;
                if (_Recipes.size() == 2) break;
                _Recipes.push_back(&_PreviousRecipe);
            }
        // Build the replacement entirely outside the lock. Readers retain an
        // immutable shared snapshot even when an entry is replaced/evicted.
        std::map<std::string, const TubeMasterPattern*> _Merged;
        std::map<std::string, const TubeMasterPattern*> _Previous;
        const auto _Merge = [&](const std::vector<TubeMasterPattern>& Source_, const bool Previous_) {
            for (const auto& _Pattern : Source_)
            {
                if (std::chrono::steady_clock::now() >= Deadline_) return false;
                if (_Pattern.Stock >= Stocks_.size() || _Pattern.Types.empty()
                    || _Pattern.Types.size() > 4096 || _Pattern.Counts.empty()) continue;
                auto _Key = TubePatternCountKey(_Pattern);
                if (Previous_) _Previous.emplace(_Key, &_Pattern);
                const auto _Found = _Merged.find(_Key);
                if (_Found == _Merged.end()) _Merged.emplace(std::move(_Key), &_Pattern);
                else if (_Pattern.Occupied < _Found->second->Occupied) _Found->second = &_Pattern;
            }
            return true;
        };
        if (_Old && !_Merge(_Old->Patterns, true)) return;
        if (!_Merge(Patterns_, false)) return;
        std::vector<const TubeMasterPattern*> _Candidates;
        _Candidates.reserve(_Merged.size());
        for (const auto& [_Key, _Pattern] : _Merged) _Candidates.push_back(_Pattern);
        std::stable_sort(_Candidates.begin(), _Candidates.end(), [&](const auto* A_, const auto* B_) {
            if ((A_->Types.size() > 1) != (B_->Types.size() > 1)) return A_->Types.size() > 1;
            const auto _FillA = static_cast<long double>(A_->Occupied)
                / (std::max)(Length{1}, UsableLength(Stocks_[A_->Stock]));
            const auto _FillB = static_cast<long double>(B_->Occupied)
                / (std::max)(Length{1}, UsableLength(Stocks_[B_->Stock]));
            if (_FillA != _FillB) return _FillA > _FillB;
            return A_->Types.size() > B_->Types.size();
        });
        if (std::chrono::steady_clock::now() >= Deadline_) return;
        auto _Next = std::make_shared<TubePatternCacheSnapshot>();
        _Next->Key = Key_;
        _Next->Recipes.reserve(_Recipes.size());
        _Next->Bytes = sizeof(TubePatternCacheSnapshot) + _Next->Key.Context.capacity()
            + _Next->Recipes.capacity() * sizeof(TubePatternRecipe);
        std::size_t _Cells = 0;
        bool _StoredRecipe = false;
        for (const auto* _Recipe : _Recipes)
        {
            if (std::chrono::steady_clock::now() >= Deadline_) return;
            const auto _RecipeCells = TubePatternRecipeCells(*_Recipe);
            const auto _RecipeBytes = TubePatternRecipeBytes(*_Recipe);
            if (_RecipeCells > kMaximumCells - _Cells || _RecipeBytes > kMaximumSnapshotBytes
                - (std::min)(_Next->Bytes, kMaximumSnapshotBytes))
            {
                // Preserve the previous snapshot if a complete recipe cannot
                // fit; never silently drop its incumbent to admit singletons.
                return;
            }
            _Next->Recipes.push_back(*_Recipe);
            _Cells += _RecipeCells;
            _Next->Bytes += TubePatternRecipeBytes(_Next->Recipes.back());
            if (_Recipe == Recipe_) _StoredRecipe = true;
        }
        _RecipeChanged = _RecipeChanged && _StoredRecipe;
        const auto _MetadataSlots = (kMaximumSnapshotBytes - (std::min)(
            _Next->Bytes, kMaximumSnapshotBytes)) / sizeof(TubeMasterPattern);
        _Next->Patterns.reserve((std::min)({_Candidates.size(), kMaximumPatterns, _MetadataSlots}));
        _Next->Bytes += _Next->Patterns.capacity() * sizeof(TubeMasterPattern);
        std::uint64_t _Stored = 0;
        for (const auto* _Pattern : _Candidates)
        {
            if (std::chrono::steady_clock::now() >= Deadline_) return;
            if (_Next->Patterns.size() >= _Next->Patterns.capacity()) break;
            const auto _Bytes = _Pattern->Types.size() * sizeof(std::size_t)
                + _Pattern->Directions.size() * sizeof(Direction)
                + _Pattern->Counts.size() * sizeof(std::pair<std::size_t, std::size_t>);
            if (_Pattern->Types.size() > kMaximumCells - _Cells
                || _Bytes > kMaximumSnapshotBytes - (std::min)(_Next->Bytes, kMaximumSnapshotBytes)) continue;
            _Next->Patterns.push_back(*_Pattern);
            const auto& _Copy = _Next->Patterns.back();
            _Next->Bytes += _Copy.Types.capacity() * sizeof(std::size_t)
                + _Copy.Directions.capacity() * sizeof(Direction)
                + _Copy.Counts.capacity() * sizeof(std::pair<std::size_t, std::size_t>);
            _Cells += _Copy.Types.size();
            const auto _Before = _Previous.find(TubePatternCountKey(_Copy));
            if (_Before == _Previous.end() || _Copy.Occupied < _Before->second->Occupied) ++_Stored;
        }
        if ((_Next->Patterns.empty() && _Next->Recipes.empty()) || (_Stored == 0 && !_RecipeChanged)
            || _Next->Bytes > kMaximumSnapshotBytes
            || std::chrono::steady_clock::now() >= Deadline_) return;
        std::unique_lock _Lock(m_Mutex, std::try_to_lock);
        if (!_Lock.owns_lock()) return;
        auto _Current = std::find_if(m_Entries.begin(), m_Entries.end(),
            [&](const auto& Entry_) { return Entry_->Key == Key_; });
        if ((_Current == m_Entries.end()) != !_Old
            || (_Current != m_Entries.end() && *_Current != _Old)) return;
        if (_Current != m_Entries.end())
        {
            m_Bytes -= (*_Current)->Bytes;
            m_Entries.erase(_Current);
        }
        while (!m_Entries.empty()
            && (m_Entries.size() >= kMaximumContexts || m_Bytes > kMaximumBytes - _Next->Bytes))
        {
            m_Bytes -= m_Entries.back()->Bytes;
            m_Entries.pop_back();
            if (Metrics_) ++Metrics_->PatternCacheEvictions;
        }
        m_Bytes += _Next->Bytes;
        m_Entries.push_front(std::move(_Next));
        if (Metrics_) Metrics_->PatternCacheStored += _Stored;
    }
};

TubeVerifiedPatternCache& VerifiedTubePatternCache()
{
    static TubeVerifiedPatternCache _Cache;
    return _Cache;
}
