// Read-only audit of finished manufacturing parts in the committed product
// coordinate system. Adjacent members are deliberately excluded here: their
// feature contact, intended gap, and fit tolerances need process-specific
// measurements, not a global Boolean or minimum solid distance.
struct SAssemblyWholeAuditPart final
{
    std::string ItemKey;
    TopoDS_Shape FinishedPart;
    ObjectMap VerifiedMapping;
};

std::set<std::pair<std::string,std::string>> AssemblyWholeAdjacentPairs(
    const VariantArray& Connections_, const std::set<std::string>& AuditedPartKeys_)
{
    std::set<std::pair<std::string,std::string>> _Pairs;
    for (const auto& _Value : Connections_)
    {
        if (!_Value.Is<ObjectMap>())
            throw std::invalid_argument("Whole-product connection is not an object");
        const auto _Connection = _Value.To<ObjectMap>();
        const auto _Found = _Connection.find("participants");
        if (_Found == _Connection.end() || !_Found->second.Is<VariantArray>())
            throw std::invalid_argument("Whole-product connection lacks participants");
        std::set<std::string> _Keys;
        for (const auto& _ParticipantValue : _Found->second.To<VariantArray>())
        {
            if (!_ParticipantValue.Is<ObjectMap>())
                throw std::invalid_argument("Whole-product participant is not an object");
            const auto _Participant = _ParticipantValue.To<ObjectMap>();
            const auto _Key = _Participant.find("itemKey");
            if (_Key == _Participant.end() || !_Key->second.Is<std::string>()
                || _Key->second.To<std::string>().empty())
                throw std::invalid_argument("Whole-product participant lacks itemKey");
            const auto _Text = _Key->second.To<std::string>();
            if (AuditedPartKeys_.contains(_Text)) _Keys.insert(_Text);
        }
        for (auto _First = _Keys.begin(); _First != _Keys.end(); ++_First)
            for (auto _Second = std::next(_First); _Second != _Keys.end(); ++_Second)
                _Pairs.emplace(*_First,*_Second);
    }
    return _Pairs;
}

ObjectMap AuditAssemblyFinishedProductSolids(
    const std::vector<SAssemblyWholeAuditPart>& Parts_,
    const std::set<std::pair<std::string,std::string>>& AdjacentPairs_)
{
    const auto _Unsupported = [](const std::string& Reason_) {
        return ObjectMap{{"status",std::string("unsupported")},
            {"scope",std::string("finished-product-non-neighbor-solids")},
            {"reason",Reason_},{"interfaceFitCertified",false}};
    };
    constexpr std::size_t _MaximumParts = 128;
    if (Parts_.empty() || Parts_.size() > _MaximumParts)
        return _Unsupported("Whole-product solid audit requires 1 to 128 manufacturing parts");
    try
    {
        std::set<std::string> _Keys;
        for (const auto& _Part : Parts_)
            if (_Part.ItemKey.empty() || !_Keys.insert(_Part.ItemKey).second)
                return _Unsupported("Whole-product part identities are missing or duplicated");
        for (const auto& _Pair : AdjacentPairs_)
            if (_Pair.first.empty() || _Pair.second.empty()
                || _Pair.first >= _Pair.second
                || !_Keys.contains(_Pair.first) || !_Keys.contains(_Pair.second))
                return _Unsupported("Whole-product adjacency refers to unknown or unordered parts");

        return iCAX::Tasks::Run([&]() -> ObjectMap {
            struct SPlacedPart final
            {
                std::string Key;
                TopoDS_Shape Shape;
                Bnd_Box Bounds;
                double Volume = 0.0;
            };
            std::vector<SPlacedPart> _Placed;
            _Placed.reserve(Parts_.size());
            for (const auto& _Part : Parts_)
            {
                const auto _PartToSource = AssemblyPairVerifiedPartToSource(_Part.VerifiedMapping);
                if (_Part.FinishedPart.IsNull()
                    || !BRepCheck_Analyzer(_Part.FinishedPart).IsValid())
                    return _Unsupported("Finished manufacturing part is not a valid BRep: " + _Part.ItemKey);
                unsigned int _SolidCount = 0;
                for (TopExp_Explorer _Solid(_Part.FinishedPart,TopAbs_SOLID);
                    _Solid.More(); _Solid.Next()) ++_SolidCount;
                if (_SolidCount != 1)
                    return _Unsupported("Finished manufacturing part is not one connected solid: "
                        + _Part.ItemKey);
                auto _Shape = BRepBuilderAPI_Transform(
                    _Part.FinishedPart,_PartToSource,true).Shape();
                if (_Shape.IsNull() || !BRepCheck_Analyzer(_Shape).IsValid())
                    return _Unsupported("Finished part could not be placed in product coordinates: "
                        + _Part.ItemKey);
                GProp_GProps _Properties;
                BRepGProp::VolumeProperties(_Shape,_Properties);
                const double _Volume = std::abs(_Properties.Mass());
                if (!std::isfinite(_Volume) || _Volume <= 1.0e-6)
                    return _Unsupported("Finished manufacturing part has no material: " + _Part.ItemKey);
                Bnd_Box _Bounds;
                BRepBndLib::AddOptimal(_Shape,_Bounds,false,false);
                if (_Bounds.IsVoid())
                    return _Unsupported("Finished manufacturing part has no finite bounds: "
                        + _Part.ItemKey);
                double _XMin = 0.0, _YMin = 0.0, _ZMin = 0.0;
                double _XMax = 0.0, _YMax = 0.0, _ZMax = 0.0;
                _Bounds.Get(_XMin,_YMin,_ZMin,_XMax,_YMax,_ZMax);
                if (!std::isfinite(_XMin) || !std::isfinite(_YMin)
                    || !std::isfinite(_ZMin) || !std::isfinite(_XMax)
                    || !std::isfinite(_YMax) || !std::isfinite(_ZMax))
                    return _Unsupported("Finished manufacturing part has unbounded geometry: "
                        + _Part.ItemKey);
                _Placed.push_back({_Part.ItemKey,std::move(_Shape),std::move(_Bounds),_Volume});
            }

            std::uint64_t _Checked = 0, _Excluded = 0, _BroadPhase = 0;
            VariantArray _Conflicts;
            for (std::size_t _First = 0; _First < _Placed.size(); ++_First)
                for (std::size_t _Second = _First + 1; _Second < _Placed.size(); ++_Second)
                {
                    const auto& _A = _Placed[_First];
                    const auto& _B = _Placed[_Second];
                    const auto _Pair = std::minmax(_A.Key,_B.Key);
                    if (AdjacentPairs_.contains({_Pair.first,_Pair.second}))
                    {
                        ++_Excluded;
                        continue;
                    }
                    ++_Checked;
                    if (_A.Bounds.IsOut(_B.Bounds)) continue;
                    ++_BroadPhase;
                    BRepAlgoAPI_Common _Common(_A.Shape,_B.Shape);
                    _Common.Build();
                    if (!_Common.IsDone() || _Common.Shape().IsNull()
                        || !BRepCheck_Analyzer(_Common.Shape()).IsValid())
                        return _Unsupported("Whole-product intersection could not be measured: "
                            + _A.Key + " / " + _B.Key);
                    GProp_GProps _Properties;
                    BRepGProp::VolumeProperties(_Common.Shape(),_Properties);
                    const double _Overlap = std::abs(_Properties.Mass());
                    const double _Tolerance = std::max(1.0e-5,
                        std::min(_A.Volume,_B.Volume) * 1.0e-8);
                    if (!std::isfinite(_Overlap)
                        || _Overlap > std::min(_A.Volume,_B.Volume) + _Tolerance)
                        return _Unsupported("Whole-product intersection volume is invalid: "
                            + _A.Key + " / " + _B.Key);
                    if (_Overlap > _Tolerance)
                        _Conflicts.emplace_back(ObjectMap{{"firstItemKey",_A.Key},
                            {"secondItemKey",_B.Key},{"intersectionVolumeMm3",_Overlap}});
                }
            return ObjectMap{{"status",std::string(_Conflicts.empty() ? "measured" : "conflict")},
                {"scope",std::string("finished-product-non-neighbor-solids")},
                {"partCount",static_cast<std::uint64_t>(_Placed.size())},
                {"checkedNonNeighborPairCount",_Checked},
                {"excludedAdjacentPairCount",_Excluded},
                {"broadPhaseCandidateCount",_BroadPhase},
                {"conflicts",std::move(_Conflicts)},{"interfaceFitCertified",false}};
        },iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    }
    catch (const std::exception& Error_)
    {
        return _Unsupported(Error_.what());
    }
    catch (...)
    {
        return _Unsupported("Whole-product OpenCascade measurement failed");
    }
}
