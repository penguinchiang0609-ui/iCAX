// The manufacturing-plan read path may be polled by the UI. Cache this
// diagnostic against the exact generation, binding revision and immutable
// manufacturing resource versions; never use it as a production-ready flag.
struct SAssemblyWholeAuditResourcePart final
{
    std::string ItemKey;
    std::string ResourceURL;
    std::uint64_t ResourceVersion = 0;
    ObjectMap VerifiedMapping;
};

std::mutex gAssemblyWholeAuditMutex;
std::map<std::string,ObjectMap> gAssemblyWholeAuditCache;

ObjectMap AuditAssemblyFinishedProductResources(
    iCAX::Project::ISceneContext& Scene_, const std::string& ProductID_,
    const std::string& GenerationRunID_, const std::string& SourceDigest_,
    const std::string& BindingsRevision_,
    const std::vector<SAssemblyWholeAuditResourcePart>& Parts_,
    const VariantArray& Connections_)
{
    const auto _Unsupported = [](const std::string& Reason_) {
        return ObjectMap{{"status",std::string("unsupported")},
            {"scope",std::string("finished-product-non-neighbor-solids")},
            {"reason",Reason_},{"interfaceFitCertified",false}};
    };
    if (Parts_.empty())
        return _Unsupported("No assembly manufacturing parts are available for the audit");
    try
    {
        VariantArray _Resources;
        _Resources.reserve(Parts_.size());
        for (const auto& _Part : Parts_)
            _Resources.emplace_back(ObjectMap{{"itemKey",_Part.ItemKey},
                {"resourceURL",_Part.ResourceURL},
                {"resourceVersion",_Part.ResourceVersion},
                {"mapping",_Part.VerifiedMapping}});
        const ObjectMap _Identity{{"sceneId",UuidToString(Scene_.GetSceneID())},
            {"productId",ProductID_},{"generationRunId",GenerationRunID_},
            {"sourceDigest",SourceDigest_},{"bindingsRevision",BindingsRevision_},
            {"resources",std::move(_Resources)}};
        const auto _CacheKey = ContentDigest("assembly-whole-solid-audit-v1",
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Identity)));
        {
            const std::lock_guard _Lock(gAssemblyWholeAuditMutex);
            if (const auto _Cached = gAssemblyWholeAuditCache.find(_CacheKey);
                _Cached != gAssemblyWholeAuditCache.end()) return _Cached->second;
        }
        std::vector<SAssemblyWholeAuditPart> _Shapes;
        _Shapes.reserve(Parts_.size());
        std::set<std::string> _Keys;
        for (const auto& _Part : Parts_)
        {
            if (_Part.ItemKey.empty() || !_Keys.insert(_Part.ItemKey).second
                || _Part.ResourceURL.empty() || _Part.ResourceVersion == 0)
                return _Unsupported("Finished product audit has missing or duplicate part resources");
            const auto _Resource = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                _Part.ResourceURL,_Part.ResourceVersion);
            if (!_Resource)
                return _Unsupported("Finished manufacturing BRep resource is unavailable: "
                    + _Part.ItemKey);
            const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(*_Resource);
            if (!_Built.bOK || _Built.Shape.IsNull())
                return _Unsupported("Finished manufacturing BRep cannot be rebuilt: "
                    + _Part.ItemKey);
            _Shapes.push_back({_Part.ItemKey,_Built.Shape,_Part.VerifiedMapping});
        }
        const auto _Adjacency = AssemblyWholeAdjacentPairs(Connections_,_Keys);
        auto _Result = AuditAssemblyFinishedProductSolids(_Shapes,_Adjacency);
        {
            const std::lock_guard _Lock(gAssemblyWholeAuditMutex);
            if (gAssemblyWholeAuditCache.size() >= 32)
                gAssemblyWholeAuditCache.erase(gAssemblyWholeAuditCache.begin());
            gAssemblyWholeAuditCache.insert_or_assign(_CacheKey,_Result);
        }
        return _Result;
    }
    catch (const std::exception& Error_)
    {
        return _Unsupported(Error_.what());
    }
    catch (...)
    {
        return _Unsupported("Finished manufacturing resource audit failed");
    }
}
