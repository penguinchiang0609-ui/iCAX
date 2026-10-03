// A process plan contains local operations on stocks, independent of any
// product catalogue shape.  Resolve again before native execution so neither
// an old asynchronous response nor a client-authored request becomes geometry.
ObjectMap AssemblyProcessProfileInput(const ObjectMap& Source_)
{
    return {{"profileRef", GetRequiredObject(Source_, "profileRef")},
        {"parameters", Source_.contains("parameters") ? GetRequiredObject(Source_, "parameters") : ObjectMap{}}};
}

ObjectMap AssemblyProcessAuthoritativeStockSection(ObjectMap Input_,
    const std::function<ObjectMap(const ObjectMap&)>& Resolve_)
{
    const auto _PartsIt = Input_.find("parts");
    if (_PartsIt == Input_.end() || !_PartsIt->second.Is<ObjectMap>()) return Input_;
    auto _Parts = _PartsIt->second.To<ObjectMap>();
    for (auto& [_Role,_Value] : _Parts) {
        if (!_Value.Is<ObjectMap>()) continue;
        auto _Part = _Value.To<ObjectMap>();
        if (!_Part.contains("profileRef")) continue;
        _Part["section"] = ObjectMap{{"profile", Resolve_(AssemblyProcessProfileInput(_Part))}};
        _Value = std::move(_Part);
    }
    Input_["parts"] = std::move(_Parts);
    return Input_;
}

ObjectMap ResolveAssemblyProcessPlanRequest(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_,
    const std::map<std::string, ObjectMap>* AuthoritativeProductProfiles_ = nullptr)
{
    if (Request_.Payload.size() > 16 * 1024 * 1024)
        throw std::invalid_argument("装配工艺计划请求过大");
    auto _Parameters = DecodeObjectPayload(Request_);
    _Parameters.erase("expectedPlanDigest");
    _Parameters.erase("previewResourceKey");
    _Parameters.erase("planId");
    _Parameters.erase("requestId");
    _Parameters.erase("expectedStoredPlanDigest");
    // Client-supplied dependency snapshots are never trusted. Resolve every
    // referenced section afresh, de-duplicate by canonical profile input, and
    // sort the dependencies so replay order cannot change this part of identity.
    std::map<std::string, ObjectMap> _ProfileInputs;
    const auto _AddProfile = [&](const ObjectMap& Source_) {
        const auto _Input = AssemblyProcessProfileInput(Source_);
        _ProfileInputs.emplace(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Input)), _Input);
    };
    if (const auto _It = _Parameters.find("stocks"); _It != _Parameters.end() && _It->second.Is<VariantArray>())
        for (const auto& _Stock : _It->second.To<VariantArray>()) _AddProfile(_Stock.To<ObjectMap>());
    if (const auto _It = _Parameters.find("instances"); _It != _Parameters.end() && _It->second.Is<VariantArray>())
        for (const auto& _Value : _It->second.To<VariantArray>())
            for (const auto& [_Role, _Part] : GetRequiredObject(
                GetRequiredObject(_Value.To<ObjectMap>(), "processInput"), "parts"))
                _AddProfile(_Part.To<ObjectMap>());
    VariantArray _Dependencies;
    std::map<std::string, ObjectMap> _ResolvedProfiles;
    for (const auto& [_Key, _Input] : _ProfileInputs) {
        const auto _ProductProfile = AuthoritativeProductProfiles_
            ? AuthoritativeProductProfiles_->find(_Key) : std::map<std::string, ObjectMap>::const_iterator{};
        const bool _ProductOwned = AuthoritativeProductProfiles_
            && _ProductProfile != AuthoritativeProductProfiles_->end();
        ObjectMap _Dependency{{"input", _Input}, {"profile", _ProductOwned
            ? _ProductProfile->second : ResolvePunchBlankProfile(_Input, ApplicationContext_, ProductContext_)}};
        _ResolvedProfiles[_Key] = GetRequiredObject(_Dependency, "profile");
        const auto _Reference = _ProductOwned ? SProfileReference{}
            : ParseProfileReference(_Input);
        if (_ProductOwned) _Dependency["authority"] = std::string("product-neutral-section");
        else if (_Reference.Scope == "user") {
            const auto _Store = GetUserDataStore(ProductContext_);
            if (const auto _Record = FindProfileRecord(*_Store, _Reference.ID))
                _Dependency["record"] = ObjectMap{{"id", _Record->RecordID},
                    {"type", _Record->RecordType}, {"revision", _Record->Revision}, {"payload", _Record->Payload}};
            const auto _Root = ResolveUserProfileRoot(ApplicationContext_);
            if (!_Root.empty()) {
                const auto _Directory = _Root / _Reference.ID;
                if (std::filesystem::is_directory(_Directory)) {
                    ObjectMap _Sources;
                    for (const auto& _File : std::filesystem::recursive_directory_iterator(_Directory)) {
                        if (!_File.is_regular_file()) continue;
                        const auto _Extension = _File.path().extension().string();
                        if (_Extension != ".py" && _Extension != ".json") continue;
                        _Sources[PathToUTF8(std::filesystem::relative(_File.path(), _Directory))] = ReadTextFile(_File.path());
                    }
                    _Dependency["packageSources"] = std::move(_Sources);
                }
            }
        } else if (_Reference.Scope == "template")
            _Dependency["packageDigest"] = LoadPythonTemplatePackage(ApplicationContext_, _Reference.TemplateID).Descriptor.PackageDigest;
        _Dependencies.emplace_back(std::move(_Dependency));
    }
    _Parameters["nativeProfileDependencies"] = std::move(_Dependencies);
    if (const auto _It = _Parameters.find("instances"); _It != _Parameters.end() && _It->second.Is<VariantArray>()) {
        auto _Instances = _It->second.To<VariantArray>();
        for (auto& _Value : _Instances) {
            auto _Instance = _Value.To<ObjectMap>();
            _Instance["processInput"] = AssemblyProcessAuthoritativeStockSection(
                GetRequiredObject(_Instance, "processInput"), [&](const ObjectMap& _Input) {
                    return _ResolvedProfiles.at(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Input)));
                });
            _Value = std::move(_Instance);
        }
        _Parameters["instances"] = std::move(_Instances);
    }
    _Parameters["action"] = std::string("process-plan");
    auto _Plan = InvokeAssemblyTemplateRuntime(ApplicationContext_, _Parameters);
    if (GetString(_Plan, "schema") != "icax.assembly-process-plan"
        || GetUInt64(_Plan, "schemaVersion", 0) != 1)
        throw std::runtime_error("装配工艺计划格式无效");
    const auto _Digest = GetRequiredText(_Plan, "planDigest", 128);
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _Expected = GetString(_Payload, "expectedPlanDigest");
    if (!_Expected.empty() && _Expected != _Digest)
        throw std::invalid_argument("装配工艺计划或资源已改变，请重新生成预览");
    return _Plan;
}

iCAX::Interaction::CInvocationResult HandleEvaluateAssemblyProcess(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext*,
    iCAX::Project::ISceneContext*)
{
    if (Request_.Payload.size() > 16 * 1024 * 1024)
        throw std::invalid_argument("装配工艺请求过大");
    auto _Parameters = DecodeObjectPayload(Request_);
    if (const auto _It = _Parameters.find("processInput"); _It != _Parameters.end() && _It->second.Is<ObjectMap>())
        _Parameters["processInput"] = AssemblyProcessAuthoritativeStockSection(
            _It->second.To<ObjectMap>(), [&](const ObjectMap& _Input) {
                return ResolvePunchBlankProfile(_Input, ApplicationContext_, ProductContext_);
            });
    _Parameters["action"] = std::string("evaluate-process");
    return MakeResponse(InvokeAssemblyTemplateRuntime(ApplicationContext_, _Parameters));
}

iCAX::Interaction::CInvocationResult HandleResolveAssemblyProcessPlan(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext*,
    iCAX::Project::ISceneContext*)
{
    return MakeResponse(ResolveAssemblyProcessPlanRequest(Request_, ApplicationContext_, ProductContext_));
}

iCAX::Interaction::CInvocationResult HandlePreviewAssemblyProcessPlan(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_,
    iCAX::Project::IProjectContext* ProjectContext_, iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_ || Request_.Payload.size() > 16 * 1024 * 1024)
        throw std::invalid_argument("装配工艺预览请求无效");
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _Prefix = GetString(_Payload, "previewResourceKey", "process-plan");
    if (_Prefix.empty() || _Prefix.size() > 64
        || !std::ranges::all_of(_Prefix, [](unsigned char Character_) {
            return (Character_ >= 'a' && Character_ <= 'z')
                || (Character_ >= 'A' && Character_ <= 'Z')
                || (Character_ >= '0' && Character_ <= '9')
                || Character_ == '-' || Character_ == '_';
        })) throw std::invalid_argument("装配工艺预览资源标识无效");

    auto _Plan = ResolveAssemblyProcessPlanRequest(Request_, ApplicationContext_, ProductContext_);
    const auto _PartsIt = _Plan.find("manufacturingParts");
    if (_PartsIt == _Plan.end() || !_PartsIt->second.Is<VariantArray>())
        throw std::runtime_error("装配工艺计划缺少下料请求");
    const auto _Parts = _PartsIt->second.To<VariantArray>();
    if (_Parts.empty() || _Parts.size() > 1000)
        throw std::invalid_argument("装配工艺计划须包含 1 至 1000 根母材");
    const auto _StocksIt = _Payload.find("stocks");
    if (_StocksIt == _Payload.end() || !_StocksIt->second.Is<VariantArray>())
        throw std::invalid_argument("装配工艺计划缺少母材");
    std::set<std::string> _Stocks;
    for (const auto& _Value : _StocksIt->second.To<VariantArray>()) {
        if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("母材须为对象");
        const auto _StockID = GetRequiredText(_Value.To<ObjectMap>(), "id", 160);
        if (!_Stocks.insert(_StockID).second) throw std::invalid_argument("母材标识重复");
    }
    // Validate every stock identity before publishing any native preview.
    std::set<std::string> _PlannedStocks;
    for (const auto& _Value : _Parts) {
        if (!_Value.Is<ObjectMap>()) throw std::runtime_error("装配下料请求须为对象");
        const auto _Part = _Value.To<ObjectMap>();
        const auto _ID = GetRequiredText(_Part, "blankId", 160);
        if (!_Stocks.contains(_ID) || !_PlannedStocks.insert(_ID).second)
            throw std::runtime_error("装配下料请求引用未知或重复的母材");
        (void)GetRequiredObject(_Part, "request");
    }
    if (_Stocks != _PlannedStocks)
        throw std::runtime_error("装配工艺计划没有完整覆盖输入母材");

    VariantArray _Results;
    for (const auto& _Value : _Parts) {
        const auto _Part = _Value.To<ObjectMap>();
        const auto _ID = GetString(_Part, "blankId");
        auto _Manufacturing = GetRequiredObject(_Part, "request");
        // A digest-dependent resource identity prevents an older response
        // from overwriting a newer plan, while identical replays reuse BReps.
        const auto _Suffix = ContentDigest(GetString(_Plan, "planDigest"), _ID).substr(8);
        _Manufacturing["previewResourceKey"] = _Prefix + "-" + _Suffix;
        auto _PartRequest = Request_;
        const auto _Json = iCAX::Data::VariantSerializer::Serialize(Variant(_Manufacturing));
        _PartRequest.Payload.assign(_Json.begin(), _Json.end());
        try {
            const auto _StockToWorld = _Part.contains("matrix") ? AssemblyPairRigidMatrix(_Part,"matrix") : gp_Trsf();
            const auto _Response = PreviewAssemblyManufacturingPartInFrame(_PartRequest,
                ApplicationContext_, ProductContext_, ProjectContext_, Scene_, _StockToWorld.Inverted());
            if (!_Response.IsOK()) throw std::runtime_error(_Response.strError);
            auto _Result = iCAX::Data::VariantSerializer::Deserialize(std::string(
                _Response.Payload.begin(), _Response.Payload.end())).To<ObjectMap>();
            _Result["blankId"] = _ID;
            _Results.emplace_back(std::move(_Result));
        } catch (const std::exception& _Error) {
            throw std::runtime_error("母材 " + _ID + " 加工失败：" + _Error.what());
        }
    }
    _Plan["nativeResults"] = std::move(_Results);
    return MakeResponse(_Plan);
}
