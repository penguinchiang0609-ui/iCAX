// Single-part process checks use the same resolved profiles, template rules
// and exact cutter evaluation as the resource-library preview.
VariantArray PunchToolLibraryArray(const ObjectMap& Object_, const std::string& Field_)
{
    const auto _Found = Object_.find(Field_);
    if (_Found == Object_.end()) return {};
    if (!_Found->second.Is<VariantArray>()) throw std::invalid_argument("工艺预览列表无效：" + Field_);
    return _Found->second.To<VariantArray>();
}

ObjectMap PunchToolLibraryReference(const ObjectMap& Payload_)
{
    VariantArray _References;
    for (const auto& _Value : PunchToolLibraryArray(Payload_, "features"))
    {
        const auto _Feature = _Value.To<ObjectMap>();
        if (_Feature.contains("toolRef")) _References.emplace_back(_Feature.at("toolRef"));
    }
    if (Payload_.contains("ends"))
        for (const auto& [_Key, _Value] : GetRequiredObject(Payload_, "ends"))
        {
            const auto _End = _Value.To<ObjectMap>();
            if (_End.contains("toolRef")) _References.emplace_back(_End.at("toolRef"));
        }
    if (_References.size() != 1 || !_References.front().Is<ObjectMap>())
        throw std::invalid_argument("管型适用检查需要一项单件工艺");
    if (Payload_.contains("partEntityId") || Payload_.contains("drawing"))
        throw std::invalid_argument("管型适用检查仅接受独立工艺预览输入");
    return _References.front().To<ObjectMap>();
}

ObjectMap CheckPunchToolLibraryPayload(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_,
    iCAX::Project::IProjectContext* ProjectContext_, iCAX::Project::ISceneContext* Scene_,
    ObjectMap Payload_)
{
    if (!Scene_) throw std::invalid_argument("单件工艺管型检查需要场景上下文");
    const auto _Reference = PunchToolLibraryReference(Payload_);
    ObjectMap _Result{{"schema", std::string("icax.punch-tool-applicability")},
        {"schemaVersion", 1ull}, {"toolId", GetRequiredText(_Reference, "id", 80)},
        {"applicable", false}, {"reason", std::string()}};
    try
    {
        Payload_.erase("diagnosticBooleanPreview");
        Payload_["toolsOnly"] = true;
        auto _PreviewRequest = Request_;
        const auto _Text = iCAX::Data::VariantSerializer::Serialize(Variant(Payload_));
        _PreviewRequest.Payload.assign(_Text.begin(), _Text.end());
        const auto _PreviewResult = PreviewEditableDrawingTools(_PreviewRequest,
            ApplicationContext_, ProductContext_, ProjectContext_, Scene_, false);
        auto _Preview = iCAX::Data::VariantSerializer::Deserialize(
            std::string(_PreviewResult.Payload.begin(), _PreviewResult.Payload.end())).To<ObjectMap>();
        const auto _Complete = _Preview.find("previewToolsComplete");
        const bool _Applicable = _Complete != _Preview.end() && _Complete->second.Is<bool>()
            && _Complete->second.To<bool>() && GetString(_Preview, "resultError").empty()
            && !PunchToolLibraryArray(_Preview, "toolPreviews").empty();
        _Result["applicable"] = _Applicable;
        _Result["reason"] = _Applicable ? std::string() : GetString(_Preview,
            "resultError", "当前管型或工艺参数不能生成有效工艺作用体");
        _Result["preview"] = std::move(_Preview);
    }
    catch (const std::exception& _Error)
    {
        _Result["reason"] = std::string(_Error.what());
    }
    return _Result;
}

iCAX::Interaction::CInvocationResult HandleCheckPunchToolApplicability(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_,
    iCAX::Project::IProjectContext* ProjectContext_, iCAX::Project::ISceneContext* Scene_)
{
    return MakeResponse(CheckPunchToolLibraryPayload(Request_, ApplicationContext_,
        ProductContext_, ProjectContext_, Scene_, DecodeObjectPayload(Request_)));
}

iCAX::Interaction::CInvocationResult HandleGetPunchToolProfileRecommendation(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_,
    iCAX::Project::IProjectContext* ProjectContext_, iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("单件工艺建议管型需要场景上下文");
    auto _Payload = DecodeObjectPayload(Request_);
    const auto _Reference = PunchToolLibraryReference(_Payload);
    const auto _ID = GetRequiredText(_Reference, "id", 80);
    ObjectMap _Result{{"schema", std::string("icax.punch-tool-profile-recommendation")},
        {"schemaVersion", 1ull}, {"toolId", _ID}, {"available", false},
        {"reason", std::string("此工艺未声明建议管型")}};
    const auto _CatalogueResult = HandleGetPunchTools(Request_, ApplicationContext_,
        ProductContext_, ProjectContext_, Scene_);
    const auto _Catalogue = iCAX::Data::VariantSerializer::Deserialize(
        std::string(_CatalogueResult.Payload.begin(), _CatalogueResult.Payload.end())).To<ObjectMap>();
    const auto _Scope = GetString(_Reference, "libraryScope", GetString(_Reference, "scope", "system"));
    for (const auto& _Value : PunchToolLibraryArray(_Catalogue, "tools"))
    {
        const auto _Tool = _Value.To<ObjectMap>();
        if (GetString(_Tool, "id") != _ID || GetString(_Tool, "libraryScope", "system") != _Scope) continue;
        if (!_Tool.contains("preview")) break;
        const auto _Declared = GetRequiredObject(_Tool, "preview");
        if (!_Declared.contains("profileRef")) break;
        ObjectMap _Profiles;
        const auto _Resolve = [&](const ObjectMap& Choice_, const std::string& Role_) {
            const auto _ProfileRef = GetRequiredObject(Choice_, "profileRef");
            const auto _Values = Choice_.contains("parameters")
                ? GetRequiredObject(Choice_, "parameters") : ObjectMap();
            const auto _Profile = ResolveProfileSnapshot(ApplicationContext_, *GetUserDataStore(ProductContext_),
                ParseProfileReference(ObjectMap{{"profileRef", _ProfileRef}}), _Values);
            const auto _Normalized = _Profile.contains("parameters")
                ? GetRequiredObject(_Profile, "parameters") : _Values;
            _Profiles[Role_] = ObjectMap{{"profileRef", _ProfileRef}, {"parameters", _Normalized}};
            return ObjectMap{{"profileRef", _ProfileRef}, {"parameters", _Normalized}, {"profile", _Profile}};
        };
        try
        {
            const auto _Main = _Resolve(_Declared, "main");
            const bool _HasSection = _Declared.contains("section");
            const auto _Section = _HasSection
                ? _Resolve(GetRequiredObject(_Declared, "section"), "branch") : ObjectMap();
            // Try changing just one input role before replacing both. Every
            // candidate retains the caller's process parameters and placement.
            const std::vector<std::pair<bool, bool>> _Candidates = _HasSection
                ? std::vector<std::pair<bool, bool>>{{false, true}, {true, false}, {true, true}}
                : std::vector<std::pair<bool, bool>>{{true, false}};
            for (const auto& [_ChangeMain, _ChangeBranch] : _Candidates)
            {
                auto _Candidate = _Payload;
                ObjectMap _ChangedProfiles;
                if (_ChangeMain)
                {
                    _Candidate["profileRef"] = _Main.at("profileRef");
                    _Candidate["parameters"] = _Main.at("parameters");
                    _ChangedProfiles["main"] = _Profiles.at("main");
                    // Part processes without a partner tube carry the target
                    // snapshot as their section input, just as the UI builds it.
                    if (GetString(_Tool, "target") == "part" && !_HasSection)
                    {
                        auto _Features = PunchToolLibraryArray(_Candidate, "features");
                        for (auto& _FeatureValue : _Features)
                        {
                            auto _Feature = _FeatureValue.To<ObjectMap>();
                            if (_Feature.contains("toolRef"))
                                _Feature["section"] = ObjectMap{{"profile", _Main.at("profile")}};
                            _FeatureValue = std::move(_Feature);
                        }
                        _Candidate["features"] = std::move(_Features);
                    }
                }
                if (_ChangeBranch)
                {
                    _ChangedProfiles["branch"] = _Profiles.at("branch");
                    auto _Features = PunchToolLibraryArray(_Candidate, "features");
                    for (auto& _FeatureValue : _Features)
                    {
                        auto _Feature = _FeatureValue.To<ObjectMap>();
                        if (_Feature.contains("toolRef")) _Feature["section"] = _Section;
                        _FeatureValue = std::move(_Feature);
                    }
                    _Candidate["features"] = std::move(_Features);
                    auto _Ends = GetRequiredObject(_Candidate, "ends");
                    for (auto& [_Key, _EndValue] : _Ends)
                    {
                        auto _End = _EndValue.To<ObjectMap>();
                        if (_End.contains("toolRef")) _End["section"] = _Section;
                        _EndValue = std::move(_End);
                    }
                    _Candidate["ends"] = std::move(_Ends);
                }
                const auto _Check = CheckPunchToolLibraryPayload(Request_, ApplicationContext_,
                    ProductContext_, ProjectContext_, Scene_, _Candidate);
                const bool _Available = _Check.at("applicable").To<bool>();
                _Result["available"] = _Available;
                _Result["reason"] = _Check.at("reason");
                if (_Available)
                {
                    _Result["profiles"] = std::move(_ChangedProfiles);
                    _Result["previewPayload"] = std::move(_Candidate);
                    _Result["preview"] = _Check.at("preview");
                    break;
                }
            }
        }
        catch (const std::exception& _Error) { _Result["reason"] = std::string(_Error.what()); }
        break;
    }
    return MakeResponse(std::move(_Result));
}
