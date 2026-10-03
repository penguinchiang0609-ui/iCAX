// Include after ProductAssemblyPairGeometry.inc and ProductAssemblyWholeGeometry.inc.
// Recheck every bound node against the *final* manufacturing BReps,
// after all connection cutters have been accumulated on their shared blanks.
// This only measures nearby material penetration and separation. In particular,
// zero overlap does not certify contact, the requested fit gap, or assembly fit.
struct SAssemblyNodeContactRequirement final
{
    std::string FirstItemKey;
    std::string SecondItemKey;
    double MaximumSeparationMm = 0.0;
};

struct SAssemblyFinalNodeAuditNode final
{
    std::string ConnectionKey;
    std::string FirstItemKey;
    std::string SecondItemKey;
    gp_Pnt NodeInSource;
    double LocalRadiusMm = 0.0; // zero selects twice the larger tube-section extent
    std::vector<std::string> AdditionalItemKeys;
    std::vector<SAssemblyNodeContactRequirement> ContactRequirements;
};

std::vector<SAssemblyFinalNodeAuditNode> AssemblyFinalNodeDeclarations(
    const VariantArray& Connections_, const std::set<std::string>& BoundConnectionKeys_)
{
    std::vector<SAssemblyFinalNodeAuditNode> _Nodes;
    _Nodes.reserve(BoundConnectionKeys_.size());
    std::set<std::string> _FoundKeys;
    for (const auto& _Value : Connections_)
    {
        if (!_Value.Is<ObjectMap>())
            throw std::invalid_argument("Final-node connection is not an object");
        const auto _Connection = _Value.To<ObjectMap>();
        const auto _Key = AssemblyPairMappingString(_Connection,"key");
        if (_Key.empty())
            throw std::invalid_argument("Final-node connection lacks an identity");
        if (!BoundConnectionKeys_.contains(_Key)) continue;
        if (!_FoundKeys.insert(_Key).second)
            throw std::invalid_argument("Final-node bound connection is duplicated: " + _Key);
        const auto _Geometry = _Connection.find("nodeGeometry");
        if (_Geometry == _Connection.end() || !_Geometry->second.Is<ObjectMap>()
            || AssemblyPairMappingString(_Geometry->second.To<ObjectMap>(),
                "nodeGeometryStatus") != "verified")
            throw std::invalid_argument("Final-node product geometry is not verified: " + _Key);
        const auto _Properties = _Connection.find("properties");
        if (_Properties == _Connection.end() || !_Properties->second.Is<ObjectMap>())
            throw std::invalid_argument("Final-node connection lacks product properties: " + _Key);
        const auto _PropertyMap = _Properties->second.To<ObjectMap>();
        const auto _PointInProperties = _PropertyMap.find("centerlinePoint");
        if (_PointInProperties == _PropertyMap.end()
            || !_PointInProperties->second.Is<VariantArray>())
            throw std::invalid_argument("Final-node connection lacks its product point: " + _Key);
        const auto _Coordinates = _PointInProperties->second.To<VariantArray>();
        if (_Coordinates.size() != 3)
            throw std::invalid_argument("Final-node product point is not three-dimensional: " + _Key);
        const auto _Participants = _Connection.find("participants");
        if (_Participants == _Connection.end() || !_Participants->second.Is<VariantArray>())
            throw std::invalid_argument("Final-node connection lacks participants: " + _Key);
        const auto _Members = _Participants->second.To<VariantArray>();
        const bool _Corner = AssemblyPairMappingString(_PropertyMap,"topology")
            == "orthogonal-corner";
        if (_Members.size() != (_Corner ? 3u : 2u))
            throw std::invalid_argument("Final-node member count differs from its topology: " + _Key);
        for (const auto& _Member : _Members)
            if (!_Member.Is<ObjectMap>())
                throw std::invalid_argument("Final-node participant is not an object: " + _Key);
        const auto _First = AssemblyPairMappingString(_Members[0].To<ObjectMap>(),"itemKey");
        const auto _Second = AssemblyPairMappingString(_Members[1].To<ObjectMap>(),"itemKey");
        if (_First.empty() || _Second.empty() || _First == _Second)
            throw std::invalid_argument("Final-node participant identities are invalid: " + _Key);
        SAssemblyFinalNodeAuditNode _Node{_Key,_First,_Second,
            gp_Pnt(AssemblyPairMatrixNumber(_Coordinates[0]),
                AssemblyPairMatrixNumber(_Coordinates[1]),
                AssemblyPairMatrixNumber(_Coordinates[2]))};
        if (_Corner)
        {
            const auto _Third = AssemblyPairMappingString(_Members[2].To<ObjectMap>(),"itemKey");
            if (_Third.empty() || _Third == _First || _Third == _Second)
                throw std::invalid_argument("Final-node participant identities are invalid: " + _Key);
            _Node.AdditionalItemKeys.push_back(_Third);
        }
        if (const auto _Contacts = _Connection.find("nodeContactRequirements");
            _Contacts != _Connection.end())
        {
            if (!_Contacts->second.Is<VariantArray>())
                throw std::invalid_argument("Final-node contact declarations are invalid: " + _Key);
            for (const auto& _Value : _Contacts->second.To<VariantArray>())
            {
                if (!_Value.Is<ObjectMap>())
                    throw std::invalid_argument("Final-node contact declaration is not an object");
                const auto _Contact = _Value.To<ObjectMap>();
                _Node.ContactRequirements.push_back({
                    AssemblyPairMappingString(_Contact,"firstItemKey"),
                    AssemblyPairMappingString(_Contact,"secondItemKey"),
                    AssemblyPairMatrixNumber(_Contact.at("maximumSeparationMm"))});
            }
        }
        _Nodes.push_back(std::move(_Node));
    }
    if (_FoundKeys != BoundConnectionKeys_)
        throw std::invalid_argument("Final-node bound connection is missing from the product");
    return _Nodes;
}

double AssemblyFinalNodeLocalRadius(const TopoDS_Shape& First_,
    const TopoDS_Shape& Second_)
{
    // AssemblySourceToPartMapping replay-verifies longitudinal X for each
    // manufacturing blank; Y/Z are therefore its actual section envelope.
    double _Extent = 0.0;
    for (const auto& _Part : {First_,Second_})
    {
        if (_Part.IsNull()) throw std::invalid_argument("Final-node part is null");
        Bnd_Box _Bounds;
        BRepBndLib::AddOptimal(_Part,_Bounds,false,false);
        if (_Bounds.IsVoid())
            throw std::invalid_argument("Final-node part has no section bounds");
        double _X0 = 0.0, _Y0 = 0.0, _Z0 = 0.0;
        double _X1 = 0.0, _Y1 = 0.0, _Z1 = 0.0;
        _Bounds.Get(_X0,_Y0,_Z0,_X1,_Y1,_Z1);
        if (!std::isfinite(_Y0) || !std::isfinite(_Z0)
            || !std::isfinite(_Y1) || !std::isfinite(_Z1))
            throw std::invalid_argument("Final-node part section is unbounded");
        _Extent = std::max({_Extent,_Y1-_Y0,_Z1-_Z0});
    }
    if (!std::isfinite(_Extent) || _Extent <= 0.0)
        throw std::invalid_argument("Final-node part section has no extent");
    return std::max(1.0,2.0*_Extent);
}

// Keep the pair diagnostics unchanged for existing two-member nodes. A
// multi-member node carries every unordered pair so adjacency exclusions in
// the whole-product audit cannot hide a collision with a third member.
ObjectMap MeasureAssemblyFinalNodePair(const SAssemblyWholeAuditPart& First_,
    const SAssemblyWholeAuditPart& Second_, const gp_Pnt& Node_, double Radius_)
{
    if (!std::isfinite(Radius_) || Radius_ < 0.0)
        throw std::invalid_argument("Final-node local radius is invalid");
    const double _Radius = Radius_ > 0.0 ? Radius_
        : AssemblyFinalNodeLocalRadius(First_.FinishedPart,Second_.FinishedPart);
    const auto _Measurement = MeasureAssemblyPairNearNode(
        First_.FinishedPart,First_.VerifiedMapping,
        Second_.FinishedPart,Second_.VerifiedMapping,Node_,_Radius);
    if (AssemblyPairMappingString(_Measurement,"status") != "measured")
        throw std::runtime_error(AssemblyPairMappingString(_Measurement,"reason"));
    const double _FirstVolume = AssemblyPairMatrixNumber(
        _Measurement.at("localMaterialVolumeAMm3"));
    const double _SecondVolume = AssemblyPairMatrixNumber(
        _Measurement.at("localMaterialVolumeBMm3"));
    const double _Overlap = AssemblyPairMatrixNumber(_Measurement.at("intersectionVolumeMm3"));
    const double _Tolerance = std::max(1.0e-5,std::min(_FirstVolume,_SecondVolume)*1.0e-8);
    if (!std::isfinite(_Overlap) || !std::isfinite(_Tolerance))
        throw std::runtime_error("Final-node material volume is invalid");
    return ObjectMap{{"status",std::string(_Overlap > _Tolerance ? "conflict" : "measured")},
        {"firstItemKey",First_.ItemKey},{"secondItemKey",Second_.ItemKey},
        {"localRadiusMm",_Radius},{"localMaterialVolumeFirstMm3",_FirstVolume},
        {"localMaterialVolumeSecondMm3",_SecondVolume},{"intersectionVolumeMm3",_Overlap},
        {"penetrationToleranceMm3",_Tolerance},{"nearestDistanceMm",_Measurement.at("nearestDistanceMm")},
        {"interfaceFitCertified",false}};
}

ObjectMap AuditAssemblyFinalNodes(const std::vector<SAssemblyWholeAuditPart>& Parts_,
    const std::vector<SAssemblyFinalNodeAuditNode>& Nodes_)
{
    const auto _Unsupported = [](const std::string& Reason_) {
        return ObjectMap{{"status",std::string("unsupported")},
            {"scope",std::string("finished-product-bound-nodes")},
            {"reason",Reason_},{"interfaceFitCertified",false}};
    };
    if (Parts_.empty() || Parts_.size() > 128 || Nodes_.empty() || Nodes_.size() > 512)
        return _Unsupported("Final-node audit requires 1 to 128 parts and 1 to 512 connections");
    try
    {
        std::map<std::string,const SAssemblyWholeAuditPart*> _PartsByKey;
        for (const auto& _Part : Parts_)
        {
            if (_Part.ItemKey.empty()
                || !_PartsByKey.emplace(_Part.ItemKey,&_Part).second)
                return _Unsupported("Final-node parts have missing or duplicate identities");
            if (_Part.FinishedPart.IsNull()
                || !BRepCheck_Analyzer(_Part.FinishedPart).IsValid())
                return _Unsupported("Final-node finished part is invalid: " + _Part.ItemKey);
            unsigned int _SolidCount = 0;
            for (TopExp_Explorer _Solid(_Part.FinishedPart,TopAbs_SOLID);
                _Solid.More(); _Solid.Next()) ++_SolidCount;
            if (_SolidCount != 1)
                return _Unsupported("Final-node finished part is not one solid: "
                    + _Part.ItemKey);
        }
        std::set<std::string> _NodeKeys;
        VariantArray _Results;
        std::uint64_t _MeasuredCount = 0, _ConflictCount = 0, _UnsupportedCount = 0;
        for (const auto& _Node : Nodes_)
        {
            if (_Node.ConnectionKey.empty() || !_NodeKeys.insert(_Node.ConnectionKey).second)
                return _Unsupported("Final-node connections have missing or duplicate identities");
            ObjectMap _Result{{"connectionKey",_Node.ConnectionKey},
                {"firstItemKey",_Node.FirstItemKey},
                {"secondItemKey",_Node.SecondItemKey},
                {"interfaceFitCertified",false}};
            try
            {
                std::vector<std::string> _MemberKeys{_Node.FirstItemKey,_Node.SecondItemKey};
                _MemberKeys.insert(_MemberKeys.end(),_Node.AdditionalItemKeys.begin(),
                    _Node.AdditionalItemKeys.end());
                if (_MemberKeys.size() < 2 || _MemberKeys.size() > 3)
                    throw std::invalid_argument("Final-node audit requires two or three members");
                std::set<std::string> _Unique;
                VariantArray _Keys;
                for (const auto& _Key : _MemberKeys)
                {
                    if (_Key.empty() || !_Unique.insert(_Key).second || !_PartsByKey.contains(_Key))
                        throw std::invalid_argument("Final-node members have no distinct finished parts");
                    _Keys.emplace_back(_Key);
                }
                VariantArray _Pairs;
                bool _Conflict = false;
                for (std::size_t _First = 0; _First < _MemberKeys.size(); ++_First)
                    for (std::size_t _Second = _First + 1; _Second < _MemberKeys.size(); ++_Second)
                    {
                        auto _Pair = MeasureAssemblyFinalNodePair(*_PartsByKey.at(_MemberKeys[_First]),
                            *_PartsByKey.at(_MemberKeys[_Second]),_Node.NodeInSource,_Node.LocalRadiusMm);
                        _Conflict = _Conflict || AssemblyPairMappingString(_Pair,"status") == "conflict";
                        if (_MemberKeys.size() == 2)
                            for (const auto& [_Key,_Value] : _Pair) _Result[_Key] = _Value;
                        _Pairs.emplace_back(std::move(_Pair));
                    }
                _Result["status"] = std::string(_Conflict ? "conflict" : "measured");
                _Result["participantItemKeys"] = std::move(_Keys);
                _Result["checkedPairCount"] = static_cast<std::uint64_t>(_Pairs.size());
                VariantArray _Contacts;
                std::set<std::pair<std::string,std::string>> _ContactPairs;
                for (const auto& _Requirement : _Node.ContactRequirements)
                {
                    const auto _Key = std::minmax(_Requirement.FirstItemKey,_Requirement.SecondItemKey);
                    if (_Requirement.FirstItemKey == _Requirement.SecondItemKey
                        || !_Unique.contains(_Requirement.FirstItemKey)
                        || !_Unique.contains(_Requirement.SecondItemKey)
                        || !_ContactPairs.emplace(_Key.first,_Key.second).second
                        || !std::isfinite(_Requirement.MaximumSeparationMm)
                        || _Requirement.MaximumSeparationMm < 0.0)
                        throw std::invalid_argument("Final-node contact requirement is invalid");
                    for (const auto& _Value : _Pairs)
                    {
                        const auto _Pair = _Value.To<ObjectMap>();
                        const auto _First = AssemblyPairMappingString(_Pair,"firstItemKey");
                        const auto _Second = AssemblyPairMappingString(_Pair,"secondItemKey");
                        if ((_First != _Requirement.FirstItemKey || _Second != _Requirement.SecondItemKey)
                            && (_Second != _Requirement.FirstItemKey || _First != _Requirement.SecondItemKey)) continue;
                        const double _Distance = AssemblyPairMatrixNumber(_Pair.at("nearestDistanceMm"));
                        const bool _Pass = _Distance <= _Requirement.MaximumSeparationMm + 1.0e-6;
                        _Conflict = _Conflict || !_Pass;
                        _Contacts.emplace_back(ObjectMap{{"firstItemKey",_Requirement.FirstItemKey},
                            {"secondItemKey",_Requirement.SecondItemKey},{"nearestDistanceMm",_Distance},
                            {"maximumSeparationMm",_Requirement.MaximumSeparationMm},
                            {"status",std::string(_Pass ? "measured" : "conflict")},
                            {"interfaceClearanceCertified",false}});
                    }
                }
                _Result["contacts"] = std::move(_Contacts);
                _Result["status"] = std::string(_Conflict ? "conflict" : "measured");
                _Result["pairs"] = std::move(_Pairs);
                if (_Conflict) ++_ConflictCount;
                else ++_MeasuredCount;
            }
            catch (const std::exception& Error_)
            {
                _Result["status"] = std::string("unsupported");
                _Result["reason"] = std::string(Error_.what());
                ++_UnsupportedCount;
            }
            _Results.emplace_back(std::move(_Result));
        }
        const std::string _Status = _UnsupportedCount > 0 ? "unsupported"
            : _ConflictCount > 0 ? "conflict" : "measured";
        return ObjectMap{{"status",_Status},
            {"scope",std::string("finished-product-bound-nodes")},
            {"nodeCount",static_cast<std::uint64_t>(Nodes_.size())},
            {"measuredNodeCount",_MeasuredCount},
            {"conflictNodeCount",_ConflictCount},
            {"unsupportedNodeCount",_UnsupportedCount},
            {"nodes",std::move(_Results)},{"interfaceFitCertified",false}};
    }
    catch (const std::exception& Error_)
    {
        return _Unsupported(Error_.what());
    }
    catch (...)
    {
        return _Unsupported("Final-node OpenCascade audit failed");
    }
}
