// Include after ProductAssemblyFinalNodeGeometry.inc and
// ProductAssemblyWholeResourceAudit.inc. Cache this read-only diagnostic by the
// committed run, binding revision, product nodes and immutable part versions.
std::mutex gAssemblyFinalNodeAuditMutex;
std::map<std::string,ObjectMap> gAssemblyFinalNodeAuditCache;

ObjectMap AuditAssemblyFinalNodeResources(
    iCAX::Project::ISceneContext& Scene_, const std::string& ProductID_,
    const std::string& GenerationRunID_, const std::string& SourceDigest_,
    const std::string& BindingsRevision_,
    const std::vector<SAssemblyWholeAuditResourcePart>& Parts_,
    const VariantArray& Connections_,
    const std::set<std::string>& BoundConnectionKeys_)
{
    const auto _Unsupported = [](const std::string& Reason_) {
        return ObjectMap{{"status",std::string("unsupported")},
            {"scope",std::string("finished-product-bound-nodes")},
            {"reason",Reason_},{"interfaceFitCertified",false}};
    };
    try
    {
        const auto _Nodes = AssemblyFinalNodeDeclarations(Connections_,BoundConnectionKeys_);
        VariantArray _Resources;
        _Resources.reserve(Parts_.size());
        for (const auto& _Part : Parts_)
            _Resources.emplace_back(ObjectMap{{"itemKey",_Part.ItemKey},
                {"resourceURL",_Part.ResourceURL},
                {"resourceVersion",_Part.ResourceVersion},
                {"mapping",_Part.VerifiedMapping}});
        VariantArray _BoundKeys;
        for (const auto& _Key : BoundConnectionKeys_)
            _BoundKeys.emplace_back(_Key);
        const ObjectMap _Identity{{"sceneId",UuidToString(Scene_.GetSceneID())},
            {"productId",ProductID_},{"generationRunId",GenerationRunID_},
            {"sourceDigest",SourceDigest_},{"bindingsRevision",BindingsRevision_},
            {"resources",std::move(_Resources)},{"connections",Connections_},
            {"boundConnectionKeys",std::move(_BoundKeys)}};
        const auto _CacheKey = ContentDigest("assembly-final-node-audit-v1",
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Identity)));
        {
            const std::lock_guard _Lock(gAssemblyFinalNodeAuditMutex);
            if (const auto _Cached = gAssemblyFinalNodeAuditCache.find(_CacheKey);
                _Cached != gAssemblyFinalNodeAuditCache.end()) return _Cached->second;
        }
        std::vector<SAssemblyWholeAuditPart> _Shapes;
        _Shapes.reserve(Parts_.size());
        for (const auto& _Part : Parts_)
        {
            if (_Part.ItemKey.empty() || _Part.ResourceURL.empty()
                || _Part.ResourceVersion == 0)
                return _Unsupported("Final-node part resource identity is missing");
            const auto _Resource = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                _Part.ResourceURL,_Part.ResourceVersion);
            if (!_Resource)
                return _Unsupported("Final-node BRep resource is unavailable: "
                    + _Part.ItemKey);
            const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(*_Resource);
            if (!_Built.bOK || _Built.Shape.IsNull())
                return _Unsupported("Final-node BRep cannot be rebuilt: "
                    + _Part.ItemKey);
            _Shapes.push_back({_Part.ItemKey,_Built.Shape,_Part.VerifiedMapping});
        }
        auto _Result = AuditAssemblyFinalNodes(_Shapes,_Nodes);
        {
            const std::lock_guard _Lock(gAssemblyFinalNodeAuditMutex);
            if (gAssemblyFinalNodeAuditCache.size() >= 32)
                gAssemblyFinalNodeAuditCache.erase(gAssemblyFinalNodeAuditCache.begin());
            gAssemblyFinalNodeAuditCache.insert_or_assign(_CacheKey,_Result);
        }
        return _Result;
    }
    catch (const std::exception& Error_)
    {
        return _Unsupported(Error_.what());
    }
    catch (...)
    {
        return _Unsupported("Final-node BRep resource audit failed");
    }
}
