// Included inside NestingAdapter.cpp's anonymous namespace.  Cache immutable
// geometry/process snapshots, never quantities, stock plans or mutable search
// state.  Exact canonical keys make a hash collision unable to reuse geometry.
class CNestingPairTableCache final
{
    using Snapshot = std::shared_ptr<const PairTableSnapshot>;
    struct Entry final
    {
        std::shared_future<Snapshot> Future;
        std::uint64_t LastUse = 0;
        std::size_t Bytes = 0;
        bool Ready = false;
    };

    static constexpr std::size_t MaximumEntries = 8;
    static constexpr std::size_t MaximumBytes = 128U * 1024U * 1024U;
    std::mutex m_Mutex;
    std::map<std::string, std::shared_ptr<Entry>> m_Entries;
    std::size_t m_Bytes = 0;
    std::uint64_t m_Clock = 0;
    std::uint64_t m_Hits = 0;
    std::uint64_t m_Builds = 0;

    static std::string Signature(
        const PairTableBuildInput& Input_, const PairTableBuildSettings& Settings_)
    {
        std::string _Key = "icax.nesting-pair-cache.v1";
        const auto _Word = [&](const std::uint64_t Value_) {
            for (unsigned _Byte = 0; _Byte < 8; ++_Byte)
                _Key.push_back(static_cast<char>((Value_ >> (_Byte * 8)) & 0xffU));
        };
        const auto _Text = [&](const std::string& Value_) {
            _Word(Value_.size());
            _Key.append(Value_);
        };
        const auto _Real = [&](const double Value_) {
            _Word(std::bit_cast<std::uint64_t>(Value_));
        };
        const auto _Domain = [&](const AngleDomain& Domain_) {
            _Word(Domain_.Intervals.size());
            for (const auto& _Interval : Domain_.Intervals)
            {
                _Real(_Interval.MinimumRadians);
                _Real(_Interval.MaximumRadians);
            }
            _Word(Domain_.DiscreteRadians.size());
            for (const auto _Angle : Domain_.DiscreteRadians) _Real(_Angle);
        };
        const auto _Profile = [&](const PeriodicEndProfile& Profile_) {
            _Text(Profile_.Schema);
            _Text(Profile_.SourceHash);
            _Text(Profile_.CoordinateConvention);
            _Word(static_cast<std::uint64_t>(Profile_.SourceQuality));
            _Word(Profile_.CompleteSingleValued);
            _Word(Profile_.Levels.size());
            for (const auto& _Level : Profile_.Levels)
            {
                _Word(_Level.Order);
                _Real(_Level.Constant);
                _Real(_Level.ResidualUpper);
                _Real(_Level.FirstDerivativeUpper);
                _Real(_Level.SecondDerivativeUpper);
                _Word(_Level.Certified);
                _Word(_Level.Cosine.size());
                for (const auto _Value : _Level.Cosine) _Real(_Value);
                _Word(_Level.Sine.size());
                for (const auto _Value : _Level.Sine) _Real(_Value);
            }
        };
        _Text(Input_.Version);
        _Text(Input_.TypeDefinitionHash);
        _Text(Input_.ProcessConfigurationHash);
        _Word(Input_.Types.size());
        for (const auto& _Type : Input_.Types)
        {
            _Text(_Type.ID);
            _Text(_Type.CompatibilityKey);
            _Text(_Type.OrderKey);
            _Word(static_cast<std::uint64_t>(_Type.MaximumLength));
            _Word(static_cast<std::uint64_t>(_Type.MaterialLength));
            _Word(_Type.DirectionAllowed[0]);
            _Word(_Type.DirectionAllowed[1]);
            // Quantity changes only the downstream instance pool.
        }
        _Word(Input_.Geometry.size());
        for (const auto& _Geometry : Input_.Geometry)
        {
            _Profile(_Geometry.Left);
            _Profile(_Geometry.Right);
            _Domain(_Geometry.RelativeRotationDomain);
        }
        _Word(Input_.DirectedPairDomains.size());
        for (const auto& _Value : Input_.DirectedPairDomains) _Domain(_Value);
        _Word(Input_.OrientedPairDomains.size());
        for (const auto& _Value : Input_.OrientedPairDomains) _Domain(_Value);
        // The usual process-loss matrix is constant. Lossless run encoding
        // avoids storing another 32 MiB key for a 1000-type request.
        _Word(Input_.PairProcessLosses.size());
        for (std::size_t _Begin = 0; _Begin < Input_.PairProcessLosses.size();)
        {
            std::size_t _End = _Begin + 1;
            while (_End < Input_.PairProcessLosses.size()
                && Input_.PairProcessLosses[_End] == Input_.PairProcessLosses[_Begin]) ++_End;
            _Word(_End - _Begin);
            _Word(static_cast<std::uint64_t>(Input_.PairProcessLosses[_Begin]));
            _Begin = _End;
        }
        _Real(Settings_.Pose.Tolerance);
        _Real(Settings_.Pose.FixedPhaseTolerance);
        _Word(Settings_.Pose.MaximumPhaseIntervals);
        _Word(Settings_.Pose.MaximumAxialIntervals);
        _Word(Settings_.Pose.InitialContactCount);
        _Word(Settings_.RequireCertifiedProfiles);
        return _Key;
    }

    static std::size_t SnapshotBytes(const PairTableSnapshot& Table_)
    {
        std::size_t _Bytes = sizeof(Table_)
            + Table_.Entries.capacity() * sizeof(PairEntry)
            + Table_.Poses.capacity() * sizeof(PairPoseMetadata)
            + Table_.Types.capacity() * sizeof(PairPartType);
        _Bytes += Table_.Schema.capacity() + Table_.Version.capacity()
            + Table_.LengthUnit.capacity() + Table_.TypeDefinitionHash.capacity()
            + Table_.ProcessConfigurationHash.capacity();
        for (const auto& _Type : Table_.Types)
            _Bytes += _Type.ID.capacity() + _Type.CompatibilityKey.capacity() + _Type.OrderKey.capacity();
        return _Bytes;
    }

    void Erase(const std::map<std::string, std::shared_ptr<Entry>>::iterator Item_)
    {
        m_Bytes -= Item_->second->Bytes;
        m_Entries.erase(Item_);
    }

    bool EvictOldest(const std::shared_ptr<Entry>& Keep_ = {})
    {
        auto _Oldest = m_Entries.end();
        for (auto _Item = m_Entries.begin(); _Item != m_Entries.end(); ++_Item)
            if (_Item->second->Ready && _Item->second != Keep_
                && (_Oldest == m_Entries.end()
                    || _Item->second->LastUse < _Oldest->second->LastUse)) _Oldest = _Item;
        if (_Oldest == m_Entries.end()) return false;
        Erase(_Oldest);
        return true;
    }

public:
    SNestingPairTableCacheStatistics Statistics()
    {
        std::lock_guard _Lock(m_Mutex);
        return { m_Hits, m_Builds, m_Entries.size(), m_Bytes };
    }

    PairTableSnapshot Get(const PairTableBuildInput& Input_,
        const PairTableBuildSettings& Settings_, bool& CacheHit_)
    {
        const auto _Key = Signature(Input_, Settings_);
        std::shared_ptr<Entry> _Entry;
        std::shared_ptr<std::promise<Snapshot>> _Promise;
        {
            std::lock_guard _Lock(m_Mutex);
            const auto _Found = m_Entries.find(_Key);
            CacheHit_ = _Found != m_Entries.end();
            if (CacheHit_)
            {
                ++m_Hits;
                _Entry = _Found->second;
                _Entry->LastUse = ++m_Clock;
            }
            else
            {
                ++m_Builds;
                while (m_Entries.size() >= MaximumEntries && EvictOldest()) {}
                const auto _PendingBytes = _Key.capacity() + sizeof(Entry) + 128U;
                while (_PendingBytes <= MaximumBytes
                    && m_Bytes > MaximumBytes - _PendingBytes && EvictOldest()) {}
                // Do not grow without bound when all slots are being built.
                if (m_Entries.size() < MaximumEntries && _PendingBytes <= MaximumBytes
                    && m_Bytes <= MaximumBytes - _PendingBytes)
                {
                    _Promise = std::make_shared<std::promise<Snapshot>>();
                    _Entry = std::make_shared<Entry>();
                    _Entry->Future = _Promise->get_future().share();
                    _Entry->LastUse = ++m_Clock;
                    _Entry->Bytes = _PendingBytes;
                    m_Entries.emplace(_Key, _Entry);
                    m_Bytes += _PendingBytes;
                }
            }
        }
        if (CacheHit_)
        {
            // Wait only for this exact key, outside the global cache lock.
            auto _Table = *_Entry->Future.get();
            for (std::size_t _Index = 0; _Index < _Table.Types.size(); ++_Index)
                _Table.Types[_Index].Quantity = Input_.Types[_Index].Quantity;
            return _Table;
        }
        if (!_Promise) return BuildPairTable(Input_, Settings_);
        try
        {
            auto _Table = BuildPairTable(Input_, Settings_);
            auto _Cached = std::make_shared<PairTableSnapshot>(_Table);
            for (auto& _Type : _Cached->Types) _Type.Quantity = 0;
            const bool _Valid = ValidatePairTable(_Table);
            const auto _SnapshotBytes = SnapshotBytes(*_Cached);
            {
                std::lock_guard _Lock(m_Mutex);
                const auto _Found = m_Entries.find(_Key);
                if (!_Valid || _SnapshotBytes > MaximumBytes - _Entry->Bytes)
                {
                    if (_Found != m_Entries.end() && _Found->second == _Entry) Erase(_Found);
                    _Promise->set_value(std::move(_Cached));
                }
                else
                {
                    // Publish before allowing eviction. A matching concurrent
                    // request always sees either this completed snapshot or
                    // the same pending future, never an evictable half-publish.
                    _Promise->set_value(std::move(_Cached));
                    _Entry->Bytes += _SnapshotBytes;
                    _Entry->Ready = true;
                    m_Bytes += _SnapshotBytes;
                    while (m_Bytes > MaximumBytes && EvictOldest(_Entry)) {}
                    // Other in-flight keys cannot be evicted. Do not retain
                    // this completed table if they consume the remaining cap.
                    if (m_Bytes > MaximumBytes && _Found != m_Entries.end()) Erase(_Found);
                }
            }
            return _Table;
        }
        catch (...)
        {
            const auto _Error = std::current_exception();
            {
                std::lock_guard _Lock(m_Mutex);
                const auto _Found = m_Entries.find(_Key);
                if (_Found != m_Entries.end() && _Found->second == _Entry) Erase(_Found);
            }
            _Promise->set_exception(_Error);
            std::rethrow_exception(_Error);
        }
    }
};

CNestingPairTableCache& NestingPairTableCache()
{
    static CNestingPairTableCache _Cache;
    return _Cache;
}

PairTableSnapshot GetNestingPairTable(const PairTableBuildInput& Input_,
    const PairTableBuildSettings& Settings_)
{
    bool _CacheHit = false;
    return NestingPairTableCache().Get(Input_, Settings_, _CacheHit);
}
