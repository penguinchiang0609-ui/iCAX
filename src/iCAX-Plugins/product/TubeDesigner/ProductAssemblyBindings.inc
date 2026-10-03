// Product process endpoints. Examples never supply manufacturing geometry.
struct SAssemblyBindingContext final
{
    iCAX::Data::uuid ProductID, RunID;
    std::shared_ptr<CProductInstanceComponent> Product;
    std::shared_ptr<CGenerationRunComponent> Run;
};

VariantArray AssemblyArray(const ObjectMap& Object_, const char* Key_)
{
    const auto _It = Object_.find(Key_);
    if (_It == Object_.end() || !_It->second.Is<VariantArray>())
        throw std::invalid_argument(std::string("装配加工缺少有效的 ") + Key_);
    return _It->second.To<VariantArray>();
}

ObjectMap AssemblyResponseObject(const iCAX::Interaction::CInvocationResult& Response_)
{
    return iCAX::Data::VariantSerializer::Deserialize(
        std::string(Response_.Payload.begin(), Response_.Payload.end())).To<ObjectMap>();
}

SAssemblyBindingContext AssemblyBindingContext(iCAX::Project::ISceneContext& Scene_,
    const ObjectMap& Payload_)
{
    SAssemblyBindingContext _Context;
    _Context.ProductID = ParseRequiredUuid(GetRequiredText(Payload_, "productEntityId"), "productEntityId");
    _Context.RunID = ParseRequiredUuid(GetRequiredText(Payload_, "generationRunId"), "generationRunId");
    _Context.Product = GetComponent<CProductInstanceComponent>(Scene_.Database().GetEntity(_Context.ProductID));
    _Context.Run = GetComponent<CGenerationRunComponent>(Scene_.Database().GetEntity(_Context.RunID));
    if (!_Context.Product || _Context.Product->GetActiveGenerationRunID() != _Context.RunID
        || !_Context.Run || _Context.Run->GetProductID() != _Context.ProductID
        || _Context.Run->GetStatus() != "Succeeded" || _Context.Run->GetNeutralModel().empty())
        throw std::invalid_argument("产品不存在或生成版本已变化，请重新读取产品工艺");
    const auto _Model = ParseGenerationDisplay(*_Context.Run);
    ValidateGenerationModelIdentity(_Model, *_Context.Run);
    if (_Model.TemplateID != _Context.Product->GetTemplateID()
        || _Model.TemplateVersion != _Context.Product->GetTemplateVersion())
        throw std::invalid_argument("产品与已提交模型的身份不一致");
    return _Context;
}

struct SAssemblyBindingCandidate final
{
    iCAX::Data::uuid SceneID, ProductID, RunID;
    std::string SourceDigest, BindingsRevision, ReplaceBindingID;
    ObjectMap Binding;
    std::chrono::steady_clock::time_point CreatedAt;
};
std::mutex gAssemblyBindingCandidateMutex;
std::map<std::string, SAssemblyBindingCandidate> gAssemblyBindingCandidates;

// This is a read-only product-wide view of the operations already committed at
// individual connections.  It does not infer a joint, alter stock, or certify
// that the assembled members fit together.
ObjectMap AssemblyProductManufacturingPlan(iCAX::Project::ISceneContext& Scene_,
    const SAssemblyBindingContext& Context_, const ObjectMap& Connections_,
    const VariantArray& Members_, const VariantArray& Bindings_)
{
    struct SMemberPlan final
    {
        ObjectMap Row;
        VariantArray Connections, Operations;
    };
    std::map<std::string, SMemberPlan> _Members;
    std::map<std::string,ObjectMap> _PartProperties;
    std::map<std::string,std::pair<std::string,std::uint64_t>> _PartResources;
    const auto _EnsureMember = [&](const ObjectMap& Source_) -> SMemberPlan& {
        const auto _Key = GetRequiredText(Source_, "itemKey");
        const auto _ID = GetRequiredText(Source_, "memberEntityId");
        auto [_It, _Inserted] = _Members.try_emplace(_Key);
        if (_Inserted)
            _It->second.Row = ObjectMap{{"itemKey",_Key},{"memberEntityId",_ID},
                {"name",GetString(Source_,"name",_Key)},
                {"partMappingStatus",std::string("not-present")}};
        else if (GetString(_It->second.Row,"memberEntityId") != _ID)
            throw std::runtime_error("产品下料计划中的构件身份不一致");
        if (const auto _Length = Source_.find("length"); _Length != Source_.end())
            _It->second.Row["length"] = _Length->second;
        if (GetString(Source_,"manufacturingMappingStatus") == "not-output"
            && !_It->second.Row.contains("partEntityId"))
            _It->second.Row["partMappingStatus"] = std::string("not-output");
        return _It->second;
    };
    for (const auto& _Value : Members_)
        _EnsureMember(_Value.To<ObjectMap>());

    // Current manufacturing parts may be persisted or still transient.  The
    // latter is the latest disassembly if both records identify the same part.
    const auto _SetPart = [&](const std::string& Key_, const iCAX::Data::uuid& MemberID_,
        const iCAX::Data::uuid& PartID_, const double Length_,
        const std::string& PartNumber_, const ObjectMap& Properties_,
        const std::string& ResourceURL_, const std::uint64_t ResourceVersion_,
        const char* Status_) {
        const auto _Member = _Members.find(Key_);
        if (_Member == _Members.end()) return;
        auto& _Row = _Member->second.Row;
        if (GetString(_Row,"memberEntityId") != UuidToString(MemberID_))
            throw std::runtime_error("产品下料件与构件身份不一致");
        if (_Row.contains("partEntityId") && GetString(_Row,"partEntityId") != UuidToString(PartID_))
            throw std::runtime_error("产品下料件的持久与临时身份不一致");
        _Row["partEntityId"] = UuidToString(PartID_);
        _Row["partLength"] = Length_;
        _Row["partNumber"] = PartNumber_;
        _Row["partMappingStatus"] = std::string(Status_);
        _PartProperties[Key_] = Properties_;
        _PartResources[Key_] = {ResourceURL_,ResourceVersion_};
    };
    for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(Scene_.Database()))
        if (_Part->GetProductID() == Context_.ProductID
            && _Part->GetGenerationRunID() == Context_.RunID)
            _SetPart(_Part->GetStableKey(),_Part->GetSourceMemberID(),_Entity->GetID(),
                _Part->GetLength(),_Part->GetPartNumber(),_Part->GetItemProperties(),
                _Part->GetManufacturingGeometryResourceID(),
                _Part->GetManufacturingGeometryResourceVersion(),"persisted");
    for (const auto& _Prepared : CopyTransientDisassembly(Scene_))
        if (_Prepared.ProductID == Context_.ProductID
            && _Prepared.GenerationRunID == Context_.RunID)
            for (const auto& _Part : _Prepared.Parts)
                _SetPart(_Part.StableKey,_Part.MemberID,_Part.PartID,
                    _Part.Length,_Part.PartNumber,_Part.ItemProperties,
                    _Part.ManufacturingResource.URL,_Part.ManufacturingResource.nVersion,
                    "transient");

    std::map<std::string,std::vector<ObjectMap>> _BindingsByConnection;
    std::uint64_t _UnmatchedBindings = 0;
    for (const auto& _Value : Bindings_)
    {
        const auto _Binding = _Value.To<ObjectMap>();
        const auto _Key = GetRequiredText(_Binding,"connectionKey");
        _BindingsByConnection[_Key].push_back(_Binding);
    }
    std::uint64_t _Selected = 0, _Unassigned = 0, _UnverifiedNodeGeometry = 0;
    auto _ConnectionRows = AssemblyArray(Connections_,"connections");
    std::set<std::string> _VisitedConnections;
    std::set<std::string> _AppliedConnectionKeys;
    for (auto& _Value : _ConnectionRows)
    {
        const auto _Connection = _Value.To<ObjectMap>();
        const auto _Key = GetRequiredText(_Connection,"key");
        _VisitedConnections.insert(_Key);
        const auto _Found = _BindingsByConnection.find(_Key);
        const auto* _Binding = _Found != _BindingsByConnection.end() && _Found->second.size() == 1
            ? &_Found->second.front() : nullptr;
        const bool _Applied = _Binding && GetString(*_Binding,"status") == "applied";
        const std::string _Status = _Found != _BindingsByConnection.end() && _Found->second.size() > 1
            ? "conflict" : !_Binding ? "unassigned" : _Applied ? "applied" : "outdated";
        if (_Applied)
        {
            ++_Selected;
            _AppliedConnectionKeys.insert(_Key);
            if (const auto _Contacts = _Binding->find("nodeContactRequirements");
                _Contacts != _Binding->end())
            {
                auto _WithContacts = _Connection;
                _WithContacts["nodeContactRequirements"] = _Contacts->second;
                _Value = std::move(_WithContacts);
            }
        }
        else ++_Unassigned;
        const auto _Properties = GetRequiredObject(_Connection,"properties");
        const auto _NodeGeometry = GetRequiredObject(_Connection,"nodeGeometry");
        const auto _NodeGeometryStatus = GetString(_NodeGeometry,"nodeGeometryStatus");
        if (_NodeGeometryStatus != "verified") ++_UnverifiedNodeGeometry;
        ObjectMap _ConnectionRow{{"key",_Key},{"kind",GetString(_Connection,"kind")},
            {"topology",GetString(_Properties,"topology")},{"status",_Status},
            {"nodeGeometryStatus",_NodeGeometryStatus}};
        if (_Binding)
        {
            _ConnectionRow["bindingId"] = GetString(*_Binding,"bindingId");
            _ConnectionRow["templateId"] = GetString(*_Binding,"templateId");
        }
        std::set<std::string> _ParticipantKeys;
        for (const auto& _ParticipantValue : AssemblyArray(_Connection,"participants"))
        {
            const auto _Participant = _ParticipantValue.To<ObjectMap>();
            auto& _Member = _EnsureMember(_Participant);
            _ParticipantKeys.insert(GetRequiredText(_Participant,"itemKey"));
            _Member.Connections.emplace_back(_ConnectionRow);
        }
        if (!_Applied) continue;
        const auto _Workflow = _Binding->find("workflow");
        if (_Workflow == _Binding->end() || !_Workflow->second.Is<ObjectMap>())
            throw std::runtime_error("已应用的装配工艺缺少工序清单");
        for (const auto& _OperationValue : AssemblyArray(_Workflow->second.To<ObjectMap>(),"partOperations"))
        {
            const auto _Operation = _OperationValue.To<ObjectMap>();
            const auto _ItemKey = GetRequiredText(_Operation,"itemKey");
            if (!_ParticipantKeys.contains(_ItemKey))
                throw std::runtime_error("装配工序引用了连接外的构件");
            ObjectMap _Row{{"connectionKey",_Key},
                {"processId",GetString(_Operation,"processId")},
                {"label",GetString(_Operation,"label")}};
            if (const auto _Placement = _Operation.find("placement"); _Placement != _Operation.end())
                _Row["placement"] = _Placement->second;
            _Members.at(_ItemKey).Operations.emplace_back(std::move(_Row));
        }
    }
    std::map<std::string,ObjectMap> _CoordinateMappings;
    if (!_PartProperties.empty())
    {
        try
        {
            const auto _ManufacturingDocument = !Context_.Run->GetManufacturingModel().empty()
                ? Context_.Run->GetManufacturingModel()
                : Context_.Run->GetAssemblyManufacturingModel();
            if (_ManufacturingDocument.empty())
                throw std::runtime_error("当前生成版本没有已提交的制造文档");
            const auto _ManufacturingModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
                Variant(_ManufacturingDocument));
            ValidateGenerationModelIdentity(_ManufacturingModel,*Context_.Run);
            const auto _DisplayModel = ParseGenerationDisplay(*Context_.Run);
            const auto* _Output = FindOutputSet(_ManufacturingModel,"result");
            if (!_Output) throw std::runtime_error("制造文档没有实际下料输出");
            const std::set<std::string> _ManufacturingKeys(
                _Output->ItemKeys.begin(),_Output->ItemKeys.end());
            const auto _Geometry = EvaluateOutputGeometry(_ManufacturingModel,*_Output);
            for (const auto& [_Key,_Properties] : _PartProperties)
            {
                if (!_ManufacturingKeys.contains(_Key))
                    throw std::runtime_error("真实下料件不属于当前制造文档的输出");
                const auto& _Item = FindModelItem(_ManufacturingModel,_Key);
                const auto& _DisplayItem = FindModelItem(_DisplayModel,_Key);
                const auto _Representation = _Item.Representations.find(_Output->Purpose);
                if (_Representation == _Item.Representations.end())
                    throw std::runtime_error("制造文档的构件缺少实体表示");
                _CoordinateMappings[_Key] = AssemblySourceToPartMapping(
                    _Geometry.At(_Representation->second),_Item.Properties,
                    _DisplayItem.Properties,_Properties,
                    GetDouble(_Members.at(_Key).Row,"partLength",0.0));
            }
        }
        catch (const std::exception& Error_)
        {
            for (const auto& [_Key,_Properties] : _PartProperties)
                _CoordinateMappings[_Key] = ObjectMap{{"status",std::string("unsupported")},
                    {"reason",std::string(Error_.what())}};
        }
    }
    VariantArray _Rows;
    std::uint64_t _MissingParts = 0, _VerifiedMappings = 0, _UnsupportedMappings = 0;
    for (auto& [_Key, _Member] : _Members)
    {
        if (!_Member.Row.contains("partEntityId")) ++_MissingParts;
        const auto _Mapping = _CoordinateMappings.find(_Key);
        _Member.Row["coordinateMapping"] = _Mapping != _CoordinateMappings.end()
            ? _Mapping->second : ObjectMap{{"status",std::string("unsupported")},
                {"reason",std::string("当前构件没有真实下料件")}};
        if (_Mapping != _CoordinateMappings.end()
            && GetString(_Mapping->second,"status") == "verified") ++_VerifiedMappings;
        else ++_UnsupportedMappings;
        _Member.Row["connections"] = std::move(_Member.Connections);
        _Member.Row["operations"] = std::move(_Member.Operations);
        _Rows.emplace_back(std::move(_Member.Row));
    }
    for (const auto& [_Key, _Matches] : _BindingsByConnection)
        if (!_VisitedConnections.contains(_Key)) _UnmatchedBindings += _Matches.size();
    const std::string _Reason = _ConnectionRows.empty()
        ? "产品没有已提交的装配连接节点，不能生成最终下料方案"
        : _Unassigned > 0 ? "仍有未选工艺、过期或冲突的连接节点"
        : _UnmatchedBindings > 0 ? "存在不属于当前产品连接的工艺绑定"
        : _UnverifiedNodeGeometry > 0 ? "部分成品节点的中心线或锚点未通过几何核对"
        : _MissingParts > 0 ? "部分构件缺少当前真实下料件"
        : _UnsupportedMappings > 0 ? "部分构件无法验证成品与下料件的坐标对应关系"
        : "连接工艺已选择，但整件装配配合尚未验证";
    ObjectMap _WholeGeometryAudit{{"status",std::string("unsupported")},
        {"scope",std::string("finished-product-non-neighbor-solids")},
        {"reason",std::string("等待全部连接工艺和已验证的下料件")},
        {"interfaceFitCertified",false}};
    ObjectMap _FinalNodeAudit{{"status",std::string("unsupported")},
        {"scope",std::string("finished-product-bound-nodes")},
        {"reason",std::string("等待全部连接工艺和已验证的下料件")},
        {"interfaceFitCertified",false}};
    if (!_ConnectionRows.empty() && _Unassigned == 0 && _UnmatchedBindings == 0
        && _UnverifiedNodeGeometry == 0 && _MissingParts == 0
        && _UnsupportedMappings == 0)
    {
        std::vector<SAssemblyWholeAuditResourcePart> _AuditedParts;
        for (const auto& [_Key,_Properties] : _PartProperties)
        {
            if (!_Properties.contains("assemblyFrame.member")) continue;
            const auto& _Resource = _PartResources.at(_Key);
            _AuditedParts.push_back({_Key,_Resource.first,_Resource.second,
                _CoordinateMappings.at(_Key)});
        }
        // Diagnostic only. Node-specific feature fit is still unverified and
        // this result cannot change manufacturing readiness or export policy.
        _WholeGeometryAudit = AuditAssemblyFinishedProductResources(Scene_,
            UuidToString(Context_.ProductID),UuidToString(Context_.RunID),
            AssemblyBindingSourceDigest(*Context_.Product,*Context_.Run),
            AssemblyBindingsRevision(Context_.Product->GetAssemblyBindings()),
            _AuditedParts,_ConnectionRows);
        _FinalNodeAudit = AuditAssemblyFinalNodeResources(Scene_,
            UuidToString(Context_.ProductID),UuidToString(Context_.RunID),
            AssemblyBindingSourceDigest(*Context_.Product,*Context_.Run),
            AssemblyBindingsRevision(Context_.Product->GetAssemblyBindings()),
            _AuditedParts,_ConnectionRows,_AppliedConnectionKeys);
    }
    return ObjectMap{{"schema",std::string("icax.product-assembly-manufacturing-plan")},
        {"schemaVersion",1ull},{"members",std::move(_Rows)},
        {"selectedConnectionCount",_Selected},{"unassignedConnectionCount",_Unassigned},
        {"missingPartCount",_MissingParts},{"unmatchedBindingCount",_UnmatchedBindings},
        {"unverifiedNodeGeometryCount",_UnverifiedNodeGeometry},
        {"verifiedCoordinateMappingCount",_VerifiedMappings},
        {"unsupportedCoordinateMappingCount",_UnsupportedMappings},
        {"wholeGeometryAudit",std::move(_WholeGeometryAudit)},
        {"nodeFinalAudit",std::move(_FinalNodeAudit)},
        {"fitStatus",std::string("not-verified")},
        {"readiness",std::string("not-ready")},{"reason",_Reason}};
}

iCAX::Interaction::CInvocationResult HandleGetProductAssemblyBindings(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext* ProjectContext_,
    iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("产品装配工艺需要有效场景");
    const auto _Context = AssemblyBindingContext(*Scene_, DecodeObjectPayload(Request_));
    auto _Result = AssemblyResponseObject(HandleGetProductAssemblyConnections(
        Request_, Application_, ProductContext_, ProjectContext_, Scene_));
    const auto _Model = ParseGenerationDisplay(*_Context.Run);
    const auto* _Output = FindOutputSet(_Model, "result");
    if (!_Output) throw std::runtime_error("产品缺少已提交的构件清单");
    std::set<std::string> _Keys(_Output->ItemKeys.begin(), _Output->ItemKeys.end());
    VariantArray _Members, _Bindings, _TemplateSupport;
    for (const auto& [_Entity, _Member] : Collect<CAssemblyMemberComponent>(Scene_->Database()))
    {
        if (_Member->GetProductID() != _Context.ProductID || !_Keys.contains(_Member->GetStableKey())) continue;
        if (_Entity->GetID() != MakeStableEntityID(_Context.ProductID,"member",_Member->GetStableKey()))
            throw std::runtime_error("当前产品构件身份无效");
        const auto _Profile = GetComponent<CTubeProfileComponent>(_Entity);
        if (!_Profile || _Member->GetChildPartCount() != 1) continue;
        _Members.emplace_back(ObjectMap{{"memberEntityId",UuidToString(_Entity->GetID())},
            {"itemKey",_Member->GetStableKey()},{"name",_Member->GetName()},
            {"length",_Member->GetLength()},{"profile",ProfileSnapshot(*_Profile)},
            {"profileLabel",_Profile->GetSpecification()},
            {"stockRefs",ProductAssemblyMemberStockRefs(_Result.at("manufacturingStocks").To<VariantArray>(), _Member->GetStableKey())}});
    }
    const auto _SourceDigest = AssemblyBindingSourceDigest(*_Context.Product,*_Context.Run);
    std::map<std::string,std::string> _ConnectionTopologies;
    std::set<std::string> _VerifiedConnections;
    for (const auto& _Value : AssemblyArray(_Result,"connections"))
    {
        const auto _Connection = _Value.To<ObjectMap>();
        const auto _Properties = GetRequiredObject(_Connection,"properties");
        const auto _Key = GetRequiredText(_Connection,"key");
        _ConnectionTopologies.emplace(_Key,GetString(_Properties,"topology"));
        const auto _NodeGeometry = GetRequiredObject(_Connection,"nodeGeometry");
        if (GetString(_NodeGeometry,"nodeGeometryStatus") == "verified")
            _VerifiedConnections.insert(_Key);
    }
    VariantArray _SavedChecks;
    for (const auto& [_ID,_Value] : _Context.Product->GetAssemblyBindings())
    {
        const auto _Binding = _Value.To<ObjectMap>();
        const auto _Connection = _ConnectionTopologies.find(GetRequiredText(_Binding,"connectionKey"));
        _SavedChecks.emplace_back(ObjectMap{{"bindingId",_ID},
            {"templateId",GetString(_Binding,"templateId")},
            {"templateDigest",GetString(_Binding,"templateDigest")},
            {"parameters",_Binding.contains("parameters") && _Binding.at("parameters").Is<ObjectMap>()
                ? _Binding.at("parameters") : Variant(ObjectMap{})},
            {"productTopology",_Connection == _ConnectionTopologies.end()
                ? std::string() : _Connection->second}});
    }
    std::map<std::string,ObjectMap> _SavedValidation;
    if (!_SavedChecks.empty())
    {
        const auto _Validation = InvokeAssemblyTemplateRuntime(Application_,
            {{"action",std::string("validate-existing-bindings")},{"bindings",_SavedChecks}});
        for (const auto& _Value : AssemblyArray(_Validation,"checks"))
        {
            const auto _Check = _Value.To<ObjectMap>();
            _SavedValidation.emplace(GetRequiredText(_Check,"bindingId"),_Check);
        }
    }
    for (const auto& [_ID,_Value] : _Context.Product->GetAssemblyBindings())
    {
        auto _Binding = _Value.To<ObjectMap>();
        _Binding.erase("parts"); // Frozen tools stay on the host.
        _Binding["bindingId"] = _ID;
        const auto _ConnectionKey = GetRequiredText(_Binding,"connectionKey");
        const auto _Verified = _SavedValidation.find(_ID);
        const bool _Current = _VerifiedConnections.contains(_ConnectionKey)
            && GetString(_Binding,"sourceDigest") == _SourceDigest
            && _Verified != _SavedValidation.end()
            && _Verified->second.contains("current") && _Verified->second.at("current").Is<bool>()
            && _Verified->second.at("current").To<bool>();
        const bool _ConnectionOnly = GetString(_Binding,"manufacturingEffect") == "connection-only";
        _Binding["status"] = std::string(_Current ? "applied" : "outdated");
        _Binding["statusDetail"] = std::string(_Current ? (_ConnectionOnly ? "真实连接关系已记录，装配位置待核对"
                : "单件加工已应用，装配位置待核对")
                : !_VerifiedConnections.contains(_ConnectionKey)
                    ? "成品连接中心线或锚点未通过几何核对，请重新生成产品"
                : GetString(_Binding,"sourceDigest") != _SourceDigest
                    ? "产品已变化，请重新检查或移除此工艺"
                    : _Verified != _SavedValidation.end()
                        ? GetString(_Verified->second,"detail","工艺模板或连接形状无法验证，请重新检查")
                        : "工艺模板或连接形状无法验证，请重新检查");
        _Binding["canRemove"] = true;
        _Bindings.emplace_back(std::move(_Binding));
    }
    const auto _Catalogue = InvokeAssemblyTemplateRuntime(Application_, {{"action",std::string("catalogue")}});
    for (const auto& _Value : AssemblyArray(_Catalogue,"assemblies"))
    {
        const auto _Descriptor = _Value.To<ObjectMap>();
        const auto _Capability = GetRequiredObject(_Descriptor,"productBinding");
        const auto _Topologies = _Capability.find("compatibleProductTopologies");
        const bool _Supported = GetString(_Capability,"mode") == "incremental-cut"
            && _Topologies != _Capability.end() && _Topologies->second.Is<VariantArray>()
            && !_Topologies->second.To<VariantArray>().empty();
        _TemplateSupport.emplace_back(ObjectMap{{"templateId",GetString(_Descriptor,"id")},
            {"supported",_Supported},{"detail",_Supported
                ? std::string(_Capability.contains("connectionOnlyWhen")
                    ? "可记录零切削端面连接，或在选定构件上加工公母孔槽；装配位置与配合需另行核对"
                    : "在选定构件上加工孔槽或端部；装配位置与配合需另行核对")
                : GetString(_Capability,"reason", "该工艺未声明可适用的成品连接拓扑，当前仅提供示例")}});
    }
    _Result["members"] = std::move(_Members);
    _Result["bindings"] = std::move(_Bindings);
    _Result["manufacturingPlan"] = AssemblyProductManufacturingPlan(*Scene_,_Context,_Result,
        _Result.at("members").To<VariantArray>(),_Result.at("bindings").To<VariantArray>());
    auto _ManufacturingPlan = _Result.at("manufacturingPlan").To<ObjectMap>();
    auto _StockRows = _Result.at("manufacturingStocks").To<VariantArray>();
    bool _AllStocksSaved = !_StockRows.empty();
    for (auto& _StockValue : _StockRows) {
        auto _Stock = _StockValue.To<ObjectMap>();
        VariantArray _StockConnections;
        std::set<std::string> _SourceKeys;
        for (const auto& _Source : _Stock.at("sourceMembers").To<VariantArray>())
            _SourceKeys.insert(GetRequiredText(_Source.To<ObjectMap>(), "itemKey"));
        for (const auto& _ConnectionValue : _Result.at("connections").To<VariantArray>()) {
            const auto _Connection = _ConnectionValue.To<ObjectMap>();
            bool _UsesStock = false;
            for (const auto& _Participant : _Connection.at("participants").To<VariantArray>())
                if (_SourceKeys.contains(GetRequiredText(_Participant.To<ObjectMap>(), "itemKey"))) _UsesStock = true;
            if (_UsesStock) _StockConnections.emplace_back(ObjectMap{{"key", _Connection.at("key")}, {"kind", _Connection.at("kind")}});
        }
        _Stock["connections"] = std::move(_StockConnections);
        _AllStocksSaved = _AllStocksSaved && !GetString(_Stock, "stockEntityId").empty();
        _StockValue = std::move(_Stock);
    }
    _ManufacturingPlan["stocks"] = std::move(_StockRows);
    _ManufacturingPlan["stockExecutionStatus"] = GetString(_Result, "manufacturingPlanningError").empty()
        ? (_AllStocksSaved ? std::string("saved") : std::string("planned")) : std::string("not-applicable");
    const auto _ProcessDocument = !_Context.Run->GetManufacturingModel().empty()
        ? _Context.Run->GetManufacturingModel() : _Context.Run->GetAssemblyManufacturingModel();
    if (!_ProcessDocument.empty()) {
        const auto _Extensions = GetRequiredObject(_ProcessDocument, "extensions");
        if (const auto _Native = _Extensions.find("tubeDesigner.assemblyProcessNative"); _Native != _Extensions.end()) {
            const auto _Plan = GetRequiredObject(_Native->second.To<ObjectMap>(), "plan");
            _ManufacturingPlan["processPlan"] = ObjectMap{{"planDigest", _Plan.at("planDigest")},
                {"stockCount", static_cast<unsigned long long>(_Plan.at("manufacturingParts").To<VariantArray>().size())},
                {"operationCount", static_cast<unsigned long long>(_Plan.at("operations").To<VariantArray>().size())},
                {"formingCount", static_cast<unsigned long long>(_Plan.at("forming").To<VariantArray>().size())},
                {"nativeContactChecks", _Plan.contains("nativeContactChecks") ? _Plan.at("nativeContactChecks") : Variant(VariantArray{})}};
        }
    }
    _ManufacturingPlan["planningError"] = _Result.at("manufacturingPlanningError");
    _Result["manufacturingPlan"] = std::move(_ManufacturingPlan);
    _Result["sourceRevision"] = _SourceDigest;
    _Result["revision"] = AssemblyBindingsRevision(_Context.Product->GetAssemblyBindings());
    _Result["capabilities"] = ObjectMap{{"anchorKinds",VariantArray{std::string("end"),std::string("side")}},
        {"templates",std::move(_TemplateSupport)}};
    return MakeResponse(Variant(std::move(_Result)));
}

TopoDS_Shape AssemblyPreparedShape(iCAX::Project::ISceneContext& Scene_,
    const SPreparedManufacturingPart& Part_)
{
    const auto _BRep = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
        Part_.ManufacturingResource.URL, Part_.ManufacturingResource.nVersion);
    if (!_BRep) throw std::runtime_error("真实制造件几何不可用");
    const auto _Shape = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
    if (!_Shape.bOK || _Shape.Shape.IsNull()) throw std::runtime_error("真实制造件无法恢复");
    return _Shape.Shape;
}

double AssemblyRemovedVolume(const TopoDS_Shape& Base_, const TopoDS_Shape& Tool_)
{
    BRepAlgoAPI_Cut _Cut;
    NCollection_List<TopoDS_Shape> _Bases, _Tools;
    _Bases.Append(Base_); _Tools.Append(Tool_);
    _Cut.SetArguments(_Bases); _Cut.SetTools(_Tools); _Cut.SetNonDestructive(true); _Cut.Build();
    if (!_Cut.IsDone() || _Cut.Shape().IsNull()) throw std::runtime_error("无法检查新工艺与既有加工的重叠");
    return ShapeMaterialVolume(Base_) - ShapeMaterialVolume(_Cut.Shape());
}

// The template declares how its angle parameter relates to product member
// axes. This checks the committed product frame, not a rendered preview pose.
void ValidateAssemblyDeclaredAxisAngle(const ObjectMap& Plan_,
    const VariantArray& Participants_, const CGenerationRunComponent& Run_)
{
    const auto _Declaration = Plan_.find("axisAngle");
    if (_Declaration == Plan_.end()) return;
    if (!_Declaration->second.Is<ObjectMap>())
        throw std::invalid_argument("装配模板的轴线角度声明无效");
    const auto _Rule = _Declaration->second.To<ObjectMap>();
    if (_Rule.empty()) return;
    const auto _Roles = AssemblyArray(_Rule,"roles");
    const auto _Directions = AssemblyArray(_Rule,"directions");
    if (_Roles.size() != 2 || _Directions.size() != 2)
        throw std::invalid_argument("装配模板的轴线角度角色无效");
    const auto _Model = ParseGenerationDisplay(Run_);
    ValidateGenerationModelIdentity(_Model,Run_);
    std::array<gp_Dir,2> _Axes;
    for (std::size_t _Index = 0; _Index < 2; ++_Index)
    {
        if (!_Roles[_Index].Is<std::string>() || !_Directions[_Index].Is<std::string>())
            throw std::invalid_argument("装配模板的轴线角度角色无效");
        const auto _Role = _Roles[_Index].To<std::string>();
        const auto _Direction = _Directions[_Index].To<std::string>();
        ObjectMap _Selected;
        bool _Found = false;
        for (const auto& _Value : Participants_)
        {
            const auto _Candidate = _Value.To<ObjectMap>();
            if (GetString(_Candidate,"role") != _Role) continue;
            if (_Found) throw std::invalid_argument("装配角度角色对应多个真实构件");
            _Selected = _Candidate;
            _Found = true;
        }
        if (!_Found) throw std::invalid_argument("装配角度角色缺少真实构件");
        const auto& _Item = FindModelItem(_Model,GetRequiredText(_Selected,"itemKey"));
        const auto [_Start,_End] = AssemblyMemberFrame(_Item.Properties);
        if (_Start.Distance(_End) <= 1.0e-6)
            throw std::invalid_argument("产品构件中心线长度为零，无法核对角度");
        gp_Dir _Axis(gp_Vec(_Start,_End));
        if (_Direction == "awayFromAnchor")
        {
            const auto _Anchor = GetRequiredObject(_Selected,"anchor");
            if (GetString(_Anchor,"kind") != "end")
                throw std::invalid_argument("离开节点的轴线方向需要端部锚点");
            const auto _EndName = GetRequiredText(_Anchor,"end");
            if (_EndName == "end") _Axis.Reverse();
            else if (_EndName != "start")
                throw std::invalid_argument("端部锚点无法确定轴线方向");
        }
        else if (_Direction != "startToEnd")
            throw std::invalid_argument("装配模板的轴线方向声明无效");
        _Axes[_Index] = _Axis;
    }
    const auto _Measure = GetRequiredText(_Rule,"measure");
    const double _Raw = std::acos(std::clamp(_Axes[0].Dot(_Axes[1]),-1.0,1.0))
        * 180.0 / std::numbers::pi;
    if (_Measure != "supplement" && _Measure != "direct")
        throw std::invalid_argument("装配模板的轴线角度计算规则无效");
    const double _Actual = _Measure == "supplement" ? 180.0 - _Raw : _Raw;
    const auto _ExpectedRule = GetRequiredObject(_Rule,"expected");
    double _Expected = 0.0;
    if (const auto _Parameter = _ExpectedRule.find("parameter"); _Parameter != _ExpectedRule.end())
    {
        if (!_Parameter->second.Is<std::string>())
            throw std::invalid_argument("装配模板的期望角度参数名无效");
        const auto _Key = _Parameter->second.To<std::string>();
        const auto _Parameters = GetRequiredObject(Plan_,"parameters");
        const auto _Value = _Parameters.find(_Key);
        if (_Value == _Parameters.end())
            throw std::invalid_argument("装配工艺缺少声明的轴线角度参数");
        _Expected = ToDouble(_Value->second,_Key);
    }
    else if (const auto _Fixed = _ExpectedRule.find("degrees"); _Fixed != _ExpectedRule.end())
        _Expected = ToDouble(_Fixed->second,"axisAngle.degrees");
    else throw std::invalid_argument("装配模板没有可验证的期望轴线角度");
    if (!std::isfinite(_Expected) || _Expected < 0.0 || _Expected > 180.0)
        throw std::invalid_argument("装配工艺期望轴线角度无效");
    if (std::abs(_Actual - _Expected) > 0.05)
    {
        std::ostringstream _Detail;
        _Detail << "工艺轴线角度 " << _Expected << "° 与已提交产品连接的 "
            << _Actual << "° 不一致";
        throw std::invalid_argument(_Detail.str());
    }
}

std::vector<SAssemblyNodeContactRequirement> ResolveAssemblyNodeContactRequirements(
    const ObjectMap& Plan_, const VariantArray& Participants_)
{
    std::map<std::string,std::string> _Roles;
    for (const auto& _Value : Participants_)
    {
        const auto _Participant = _Value.To<ObjectMap>();
        if (!_Roles.emplace(GetRequiredText(_Participant,"role"),
            GetRequiredText(_Participant,"itemKey")).second)
            throw std::invalid_argument("节点接触检查的构件角色重复");
    }
    std::vector<SAssemblyNodeContactRequirement> _Result;
    std::set<std::pair<std::string,std::string>> _Pairs;
    std::set<std::string> _Connected;
    // Existing two-member plans retain their pair-interference audit without
    // needing the new contact declaration. Three-member plans must provide it.
    if (!Plan_.contains("nodeContacts") && Participants_.size() == 2)
        return _Result;
    for (const auto& _Value : AssemblyArray(Plan_,"nodeContacts"))
    {
        const auto _Rule = _Value.To<ObjectMap>();
        const auto _Selected = AssemblyArray(_Rule,"roles");
        if (_Selected.size() != 2 || !_Selected[0].Is<std::string>() || !_Selected[1].Is<std::string>()
            || !_Roles.contains(_Selected[0].To<std::string>()) || !_Roles.contains(_Selected[1].To<std::string>()))
            throw std::invalid_argument("模板的节点接触检查没有两个有效真实构件角色");
        const auto _First = _Roles.at(_Selected[0].To<std::string>());
        const auto _Second = _Roles.at(_Selected[1].To<std::string>());
        const auto _Pair = std::minmax(_First,_Second);
        const double _Maximum = ToDouble(_Rule.at("maximumSeparationMm"),"maximumSeparationMm");
        if (_First == _Second || !_Pairs.emplace(_Pair.first,_Pair.second).second
            || !std::isfinite(_Maximum) || _Maximum < 0.0 || _Maximum > 100000.0)
            throw std::invalid_argument("模板的节点接触检查重复或距离无效");
        _Result.push_back({_First,_Second,_Maximum});
        _Connected.insert(_First); _Connected.insert(_Second);
    }
    if (Participants_.size() == 3 && (_Result.size() < 2 || _Connected.size() != 3))
        throw std::invalid_argument("三向节点必须声明连接全部三件的真实材料接触检查");
    return _Result;
}

VariantArray AssemblyNodeContactSnapshots(
    const std::vector<SAssemblyNodeContactRequirement>& Requirements_)
{
    VariantArray _Result;
    for (const auto& _Rule : Requirements_)
        _Result.emplace_back(ObjectMap{{"firstItemKey",_Rule.FirstItemKey},
            {"secondItemKey",_Rule.SecondItemKey},{"maximumSeparationMm",_Rule.MaximumSeparationMm}});
    return _Result;
}

// A template may declare a corner entrance independently of its operation
// names. Check the declaration against replay-verified source sections and
// the actual finished branch, including the cavity's far wall. Pair distance
// remains a material-contact diagnostic, not a tab/slot clearance certificate.
void ValidateAssemblyCornerEntrance(const ObjectMap& Plan_, const VariantArray& Participants_,
    const std::vector<SAssemblyWholeAuditPart>& FinalParts_)
{
    for (const auto& _Value : AssemblyArray(Plan_,"fitChecks"))
    {
        const auto _Check = _Value.To<ObjectMap>();
        if (GetString(_Check,"kind") != "orthogonal-corner-contact") continue;
        if (Participants_.size() != 3 || GetString(_Check,"topology") != "orthogonal-corner")
            throw std::invalid_argument("三向入口检查没有完整的真实角点");
        const auto _Roles = GetRequiredObject(_Check,"roles");
        const auto _BranchRole = GetRequiredText(_Roles,"c");
        std::map<std::string,ObjectMap> _Participants;
        for (const auto& _ParticipantValue : Participants_)
        {
            const auto _Participant = _ParticipantValue.To<ObjectMap>();
            _Participants.emplace(GetRequiredText(_Participant,"role"),_Participant);
        }
        const auto _A = GetRequiredText(_Roles,"a"), _B = GetRequiredText(_Roles,"b");
        if (!_Participants.contains(_A) || !_Participants.contains(_B)
            || !_Participants.contains(_BranchRole) || _A == _B || _A == _BranchRole || _B == _BranchRole)
            throw std::invalid_argument("三向入口检查的构件角色不完整或重复");
        const auto& _Branch = _Participants.at(_BranchRole);
        const auto _BranchSection = GetRequiredObject(_Branch,"section");
        const auto _BranchFrame = GetRequiredObject(_BranchSection,"sectionFrame");
        if (GetString(_BranchFrame,"verification") != "committed-source-brep-replay")
            throw std::invalid_argument("三向支管截面未通过真实原管核验");
        const auto _ReadVector = [](const ObjectMap& Values_,const char* Key_) {
            const auto _Values = AssemblyArray(Values_,Key_);
            if (_Values.size() != 3) throw std::invalid_argument("三向入口的真实方向无效");
            return gp_Vec(ToDouble(_Values[0],Key_),ToDouble(_Values[1],Key_),ToDouble(_Values[2],Key_));
        };
        const auto _BranchNodeFrame = GetRequiredObject(_Branch,"nodeFrame");
        const auto _BranchAwayPart = _ReadVector(_BranchNodeFrame,"selfAwayPart");
        if (std::abs(std::abs(_BranchAwayPart.X()) - 1.0) > 1.0e-6)
            throw std::invalid_argument("三向支管制造轴向无法核对");
        const auto _BranchNodePart = _ReadVector(_BranchNodeFrame,"selfNodePart");
        const auto _Final = std::find_if(FinalParts_.begin(),FinalParts_.end(),[&_Branch](const auto& Part_) {
            return Part_.ItemKey == GetRequiredText(_Branch,"itemKey");
        });
        if (_Final == FinalParts_.end()) throw std::invalid_argument("三向支管缺少真实最终实体");
        const auto _Bounds = ShapeBounds(_Final->FinishedPart);
        const auto _Low = AssemblyArray(_Bounds,"min"), _High = AssemblyArray(_Bounds,"max");
        const double _BranchNear = _BranchAwayPart.X() > 0.0
            ? ToDouble(_Low[0],"min.x") - _BranchNodePart.X()
            : _BranchNodePart.X() - ToDouble(_High[0],"max.x");
        const double _Insertion = ToDouble(_Check.at("insertionMm"),"insertionMm");
        const double _Gap = ToDouble(_Check.at("gapMm"),"gapMm");
        if (!std::isfinite(_Insertion) || _Insertion < 0.0 || !std::isfinite(_Gap) || _Gap < 0.0)
            throw std::invalid_argument("三向入口深度或间隙无效");
        const auto _Receivers = AssemblyArray(_Check,"receiverRoles");
        if (_Receivers.empty() || _Receivers.size() > 2)
            throw std::invalid_argument("三向入口没有有效承接构件");
        const auto _BranchOuter = GetUInt64(_BranchFrame,"outerContourIndex",2);
        const auto _BranchContours = AssemblyArray(_BranchSection,"contours");
        if (_BranchOuter >= _BranchContours.size()) throw std::invalid_argument("三向支管外轮廓缺失");
        const auto _BranchRectangle = AssemblyConservativeSectionRectangle(_BranchContours[_BranchOuter],false);
        const auto _BranchDatum = AssemblyArray(_BranchFrame,"centerlineUV");
        if (_BranchDatum.size() != 2) throw std::invalid_argument("三向支管中心线基准缺失");
        std::set<std::string> _Seen;
        for (const auto& _ReceiverValue : _Receivers)
        {
            if (!_ReceiverValue.Is<std::string>()) throw std::invalid_argument("三向承接角色无效");
            const auto _ReceiverRole = _ReceiverValue.To<std::string>();
            if ((_ReceiverRole != _A && _ReceiverRole != _B) || !_Seen.insert(_ReceiverRole).second)
                throw std::invalid_argument("三向承接角色重复或不属于选定角点");
            const auto& _Host = _Participants.at(_ReceiverRole);
            const auto _HostSection = GetRequiredObject(_Host,"section");
            const auto _HostFrame = GetRequiredObject(_HostSection,"sectionFrame");
            if (GetString(_HostFrame,"verification") != "committed-source-brep-replay")
                throw std::invalid_argument("三向承接截面未通过真实原管核验");
            const auto _HostNodeFrame = GetRequiredObject(_Host,"nodeFrame");
            const auto _ByRole = GetRequiredObject(_HostNodeFrame,"awayPartsByRole");
            const auto _Away = _ReadVector(_ByRole,_BranchRole.c_str());
            const auto _SourceToPart = AssemblyPairRigidMatrix(_HostNodeFrame,"sourceToPart");
            const auto _HostU = _ReadVector(_HostFrame,"xAxisWorld").Transformed(_SourceToPart);
            const auto _HostV = _ReadVector(_HostFrame,"yAxisWorld").Transformed(_SourceToPart);
            const auto _HostDatum = AssemblyArray(_HostFrame,"centerlineUV");
            const auto _HostContours = AssemblyArray(_HostSection,"contours");
            const auto _OuterIndex = GetUInt64(_HostFrame,"outerContourIndex",2);
            if (_HostDatum.size() != 2 || _HostContours.size() != 2 || _OuterIndex >= 2)
                throw std::invalid_argument("三向入口需要可验证的矩形管截面");
            const auto _Outer = AssemblyConservativeSectionRectangle(_HostContours[_OuterIndex],false);
            const auto _Project = [&](const SAssemblySectionPoint2d& Point_,const gp_Vec& Axis_) {
                return (_HostU*(Point_.X()-ToDouble(_HostDatum[0],"host.u"))
                    +_HostV*(Point_.Y()-ToDouble(_HostDatum[1],"host.v"))).Dot(Axis_);
            };
            double _NearSurface = -std::numeric_limits<double>::infinity();
            for (const auto& _Point : _Outer) _NearSurface = std::max(_NearSurface,_Project(_Point,_Away));
            if (!std::isfinite(_NearSurface) || _NearSurface <= 0.0
                || std::abs(_BranchNear - (_NearSurface-_Insertion)) > 0.05)
                throw std::invalid_argument("三向支管实际端部不在声明的承接表面或插入深度");
            if (_Insertion <= 1.0e-8) continue;
            const auto _Cavity = AssemblyConservativeSectionRectangle(_HostContours[1-_OuterIndex],true);
            const auto _Across = _Away.Crossed(gp_Vec(1,0,0));
            double _FarInner = std::numeric_limits<double>::infinity();
            double _AcrossMin = _FarInner, _AcrossMax = -_FarInner;
            for (const auto& _Point : _Cavity)
            {
                _FarInner = std::min(_FarInner,_Project(_Point,_Away));
                _AcrossMin = std::min(_AcrossMin,_Project(_Point,_Across));
                _AcrossMax = std::max(_AcrossMax,_Project(_Point,_Across));
            }
            if (_BranchNear < _FarInner + _Gap - 0.05)
                throw std::invalid_argument("三向插入深度越过承接管的远侧内壁");
            const auto _BranchU = _ReadVector(_BranchFrame,"xAxisWorld").Transformed(_SourceToPart);
            const auto _BranchV = _ReadVector(_BranchFrame,"yAxisWorld").Transformed(_SourceToPart);
            for (const auto& _Point : _BranchRectangle)
            {
                const double _Position = (_BranchU*(_Point.X()-ToDouble(_BranchDatum[0],"branch.u"))
                    +_BranchV*(_Point.Y()-ToDouble(_BranchDatum[1],"branch.v"))).Dot(_Across);
                if (_Position < _AcrossMin + _Gap - 0.05 || _Position > _AcrossMax - _Gap + 0.05)
                    throw std::invalid_argument("三向支管外轮廓会破坏承接管的非入口侧壁");
            }
        }
    }
}

iCAX::Interaction::CInvocationResult HandlePreviewProductAssemblyBinding(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
    iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_ || Request_.Payload.size() > 4*1024*1024)
        throw std::invalid_argument("产品工艺检查请求无效");
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _Context = AssemblyBindingContext(*Scene_, _Payload);
    const auto _InitialSourceDigest = AssemblyBindingSourceDigest(*_Context.Product,*_Context.Run);
    const auto _InitialRevision = AssemblyBindingsRevision(_Context.Product->GetAssemblyBindings());
    if (ProductRuntimeParameterState(Application_,*_Context.Product,_Context.Run.get()).ModelOutdated)
        throw std::invalid_argument("产品模型已变化，请重新生成后检查加工方案");
    auto _Existing = _Context.Product->GetAssemblyBindings();
    const auto _ConnectionKey = GetRequiredText(_Payload,"connectionKey");
    const auto _ReplaceID = GetString(_Payload,"replaceBindingId");
    if (!_ReplaceID.empty())
    {
        const auto _Replaced = _Existing.find(_ReplaceID);
        if (_Replaced == _Existing.end())
            throw std::invalid_argument("要修改的工艺已不存在，请重新读取");
        (void)GetRequiredText(_Replaced->second.To<ObjectMap>(),"connectionKey");
        _Existing.erase(_Replaced);
    }
    ValidateAssemblyBindingsSource(_Existing,*_Context.Product,*_Context.Run);
    for (const auto& [_ID,_Value] : _Existing)
        if (GetString(_Value.To<ObjectMap>(),"connectionKey") == _ConnectionKey)
            throw std::invalid_argument("当前产品连接已有绑定工艺，请修改或移除原工艺");
    const ObjectMap _NoBindings;
    const auto _Prepared = PrepareNeutralModelDisassembly(Application_,*Scene_,
        _Context.ProductID,_Context.RunID,*_Context.Product,*_Context.Run,nullptr,nullptr,&_NoBindings);
    VariantArray _Participants, _SavedParticipants;
    std::map<std::string,const SPreparedManufacturingPart*> _Parts;
    for (const auto& _Part : _Prepared.Parts) _Parts.emplace(UuidToString(_Part.MemberID),&_Part);
    for (const auto& _Value : AssemblyArray(_Payload,"participants"))
    {
        if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("装配角色记录无效");
        const auto _Input = _Value.To<ObjectMap>();
        const auto _ID = GetRequiredText(_Input,"memberEntityId");
        const auto _RequestedMember = GetComponent<CAssemblyMemberComponent>(
            Scene_->Database().GetEntity(ParseRequiredUuid(_ID, "memberEntityId")));
        if (_RequestedMember)
            for (const auto& _PreparedPart : _Prepared.Parts)
                if (_PreparedPart.ItemProperties.contains("tubeDesigner.assemblyProcessStockId"))
                    if (const auto _Sources = _PreparedPart.ItemProperties.find("manufacturing.sourceMembers");
                        _Sources != _PreparedPart.ItemProperties.end())
                        for (const auto& _Source : _Sources->second.To<VariantArray>())
                            if (GetString(_Source.To<ObjectMap>(), "itemKey") == _RequestedMember->GetStableKey())
                                throw std::invalid_argument("该构件已映射到自动工艺母材，不能通过旧单构件绑定另加端切；请更新产品制造方案");
        const auto _Found = _Parts.find(_ID);
        if (_Found == _Parts.end() || _Found->second->TubeProfile.empty())
            throw std::invalid_argument("选定构件没有一对一的真实管件下料输出");
        const auto& _Part = *_Found->second;
        const auto _Member = GetComponent<CAssemblyMemberComponent>(Scene_->Database().GetEntity(_Part.MemberID));
        if (!_Member || _Member->GetProductID()!=_Context.ProductID
            || _Member->GetStableKey()!=_Part.StableKey || _Member->GetChildPartCount()!=1)
            throw std::invalid_argument("选定构件不属于当前产品的有效显示构件");
        auto _Participant = ObjectMap{{"role",GetRequiredText(_Input,"role")},
            {"memberEntityId",_ID},{"itemKey",_Part.StableKey},
            {"anchor",GetRequiredObject(_Input,"anchor")}};
        _SavedParticipants.emplace_back(_Participant);
        _Participant["section"] = _Part.TubeProfile;
        const auto _Bounds = ShapeBounds(AssemblyPreparedShape(*Scene_,_Part));
        _Participant["length"] = ToDouble(_Bounds.at("max").To<VariantArray>()[0],"max.x")
            - ToDouble(_Bounds.at("min").To<VariantArray>()[0],"min.x");
        if (_Part.ItemProperties.contains("assemblyFrame.member"))
        {
            const auto [_LogicalStart,_LogicalEnd] = AssemblyMemberFrame(_Part.ItemProperties);
            const auto _Stock = AssemblyMemberStockInterval(_Part.ItemProperties,
                _LogicalStart.Distance(_LogicalEnd));
            if (std::abs(ToDouble(_Participant.at("length"),"length")
                - (_Stock.EndStation - _Stock.StartStation)) > 0.05)
                throw std::invalid_argument("当前真实下料件长度与已提交物理原管区间不一致");
        }
        _Participants.emplace_back(std::move(_Participant));
    }
    std::string _ConnectionLabel, _ProductTopology;
    ObjectMap _ConnectionProperties;
    VariantArray _AllConnections;
    {
        // Resolve against the committed run, never against a front-end label or
        // the request's participant names. A selected relationship denotes all
        // of its members; a subset is a different, unrecorded connection.
        const auto _Connections = AssemblyResponseObject(HandleGetProductAssemblyConnections(
            Request_,Application_,nullptr,nullptr,Scene_));
        bool _Found = false;
        _AllConnections = AssemblyArray(_Connections,"connections");
        for (const auto& _Value : _AllConnections)
        {
            const auto _Connection = _Value.To<ObjectMap>();
            if (GetString(_Connection,"key") != _ConnectionKey) continue;
            _Found = true;
            const auto _NodeGeometry = GetRequiredObject(_Connection,"nodeGeometry");
            if (GetString(_NodeGeometry,"nodeGeometryStatus") != "verified")
                throw std::invalid_argument("成品连接中心线或锚点未通过几何核对: "
                    + GetString(_NodeGeometry,"detail","缺少有效的节点几何声明"));
            std::set<std::string> _Expected, _Selected;
            for (const auto& _ParticipantValue : AssemblyArray(_Connection,"participants"))
            {
                const auto _Participant = _ParticipantValue.To<ObjectMap>();
                _Expected.insert(GetRequiredText(_Participant,"memberEntityId"));
                if (!_ConnectionLabel.empty()) _ConnectionLabel += " + ";
                _ConnectionLabel += GetString(_Participant,"name","未命名构件");
            }
            for (const auto& _ParticipantValue : _SavedParticipants)
                _Selected.insert(GetRequiredText(_ParticipantValue.To<ObjectMap>(),"memberEntityId"));
            if (_Expected.size() != _SavedParticipants.size() || _Expected != _Selected)
                throw std::invalid_argument("所选产品连接与工艺角色的真实构件不一致");
            const auto _Properties = GetRequiredObject(_Connection,"properties");
            _ProductTopology = GetRequiredText(_Properties,"topology");
            _ConnectionProperties = _Properties;
            if (const auto _Declared = _Properties.find("participantAnchors");
                _Declared != _Properties.end())
            {
                if (!_Declared->second.Is<VariantArray>())
                    throw std::invalid_argument("产品连接缺少有效的构件位置声明");
                std::map<std::string,ObjectMap> _Anchors;
                for (const auto& _AnchorValue : _Declared->second.To<VariantArray>())
                {
                    if (!_AnchorValue.Is<ObjectMap>())
                        throw std::invalid_argument("产品连接的构件位置声明无效");
                    auto _Anchor = _AnchorValue.To<ObjectMap>();
                    const auto _ItemKey = GetRequiredText(_Anchor,"itemKey");
                    if (!_Anchors.emplace(_ItemKey,std::move(_Anchor)).second)
                        throw std::invalid_argument("产品连接包含重复的构件位置声明");
                }
                if (_Anchors.size() != _SavedParticipants.size())
                    throw std::invalid_argument("产品连接的构件位置声明不完整");
                for (const auto& _ParticipantValue : _SavedParticipants)
                {
                    const auto _Participant = _ParticipantValue.To<ObjectMap>();
                    const auto _Anchor = GetRequiredObject(_Participant,"anchor");
                    const auto _FoundAnchor = _Anchors.find(GetRequiredText(_Participant,"itemKey"));
                    if (_FoundAnchor == _Anchors.end())
                        throw std::invalid_argument("产品连接缺少选定构件的位置声明");
                    const auto& _Declaration = _FoundAnchor->second;
                    const auto _ExpectedAnchor = _Declaration.contains("anchor")
                        ? GetRequiredObject(_Declaration,"anchor") : _Declaration;
                    const auto _Kind = GetRequiredText(_ExpectedAnchor,"kind");
                    if (GetString(_Anchor,"kind") != _Kind)
                        throw std::invalid_argument("所选加工锚点与产品连接点类型不同，当前模板没有可验证的相对节点定位规则");
                    if (_Kind == "end")
                    {
                        if (GetString(_Anchor,"end") != GetRequiredText(_ExpectedAnchor,"end"))
                            throw std::invalid_argument("工艺选择的端部与产品连接不一致");
                        const double _ActualTrim = GetDouble(_Anchor,"trim",0.0);
                        const double _DeclaredTrim = GetDouble(_ExpectedAnchor,"trim",0.0);
                        if (!std::isfinite(_ActualTrim) || !std::isfinite(_DeclaredTrim)
                            || std::abs(_ActualTrim - _DeclaredTrim) > 0.05)
                            throw std::invalid_argument("工艺选择的端部退切量与产品连接不一致");
                        // The approach face is product geometry: a process may use it
                        // to derive a near-end side cut, but may not invent another face.
                        if (GetString(_Anchor,"approachFace")
                            != GetString(_ExpectedAnchor,"approachFace"))
                            throw std::invalid_argument("工艺选择的接近面与产品连接不一致");
                        const auto& _Part = *_Parts.at(GetRequiredText(_Participant,"memberEntityId"));
                        const auto [_LogicalStart,_LogicalEnd] = AssemblyMemberFrame(_Part.ItemProperties);
                        const double _LogicalLength = _LogicalStart.Distance(_LogicalEnd);
                        const auto _Stock = AssemblyMemberStockInterval(_Part.ItemProperties,_LogicalLength);
                        const auto _EndName = GetRequiredText(_ExpectedAnchor,"end");
                        const double _ExpectedAllowance = _EndName == "start"
                            ? -_Stock.StartStation : _Stock.EndStation - _LogicalLength;
                        if (_Stock.Declared && (!_ExpectedAnchor.contains("stockAllowance")
                            || !_Anchor.contains("stockAllowance")))
                            throw std::invalid_argument("成品端点或工艺候选缺少物理原管预留");
                        const auto _AllowanceMatches = [_ExpectedAllowance](
                            const ObjectMap& Values_) {
                            const auto _It = Values_.find("stockAllowance");
                            if (_It == Values_.end()) return true;
                            const double _Value = ToDouble(_It->second,"stockAllowance");
                            return std::isfinite(_Value) && _Value >= 0.0
                                && std::abs(_Value - _ExpectedAllowance) <= 0.05;
                        };
                        if (!_AllowanceMatches(_ExpectedAnchor)
                            || !_AllowanceMatches(_Anchor))
                            throw std::invalid_argument("端部加工预留与已提交物理原管区间不一致");
                        const auto _DeclaredContact = _ExpectedAnchor.find("contactInset");
                        const auto _SelectedContact = _Anchor.find("contactInset");
                        if ((_DeclaredContact == _ExpectedAnchor.end())
                            != (_SelectedContact == _Anchor.end()))
                            throw std::invalid_argument("工艺选择的接头近侧距离与产品声明不一致");
                        if (_DeclaredContact != _ExpectedAnchor.end())
                        {
                            const double _Expected = ToDouble(_DeclaredContact->second,"contactInset");
                            const double _Actual = ToDouble(_SelectedContact->second,"contactInset");
                            if (!std::isfinite(_Expected) || !std::isfinite(_Actual)
                                || _Expected < 0.0 || _Expected > _ExpectedAllowance + 0.05
                                || std::abs(_Expected - _Actual) > 0.05)
                                throw std::invalid_argument("接头近侧距离不是已提交截面的有效尺寸");
                        }
                    }
                    else if (_Kind == "side")
                    {
                        const auto _Station = _ExpectedAnchor.find("station");
                        const auto _DeclaredStation = _Station != _ExpectedAnchor.end()
                            ? ToDouble(_Station->second,"station")
                            : ToDouble(_Declaration.at("localAxialStation"),"localAxialStation");
                        const double _SelectedStation = _Anchor.contains("station")
                            ? ToDouble(_Anchor.at("station"),"station")
                            : std::numeric_limits<double>::quiet_NaN();
                        if (GetString(_Anchor,"face") != GetRequiredText(_ExpectedAnchor,"face")
                            || GetString(_Anchor,"reference") != "start"
                            || !std::isfinite(_SelectedStation)
                            || std::abs(_SelectedStation - _DeclaredStation) > 0.05)
                            throw std::invalid_argument("工艺选择的侧面或距起端位置与产品连接不一致");
                    }
                    else throw std::invalid_argument("产品连接声明了不支持的构件位置");
                }
            }
            break;
        }
        if (!_Found) throw std::invalid_argument("所选产品连接不属于当前已提交模型");
    }
    // An end-to-end template needs the actual directions of the members at
    // the committed product node. Derive them from the product's end anchors,
    // then rotate them into each participant's verified manufacturing frame.
    // This is transient runtime input, never a user-supplied or saved anchor.
    if (_Participants.size() == 2 || _Participants.size() == 3)
    {
        const auto _MemberCount = _Participants.size();
        std::vector<ObjectMap> _DeclaredEnds(_MemberCount);
        bool _BothEnds = true;
        const auto _Declarations = AssemblyArray(_ConnectionProperties,"participantAnchors");
        for (std::size_t _Index = 0; _Index < _MemberCount; ++_Index)
        {
            const auto _Selected = _SavedParticipants[_Index].To<ObjectMap>();
            const auto _Key = GetRequiredText(_Selected,"itemKey");
            bool _Found = false;
            for (const auto& _Value : _Declarations)
            {
                const auto _Row = _Value.To<ObjectMap>();
                if (GetString(_Row,"itemKey") != _Key) continue;
                _DeclaredEnds[_Index] = _Row.contains("anchor")
                    ? GetRequiredObject(_Row,"anchor") : _Row;
                _Found = true;
                break;
            }
            if (!_Found || GetString(_DeclaredEnds[_Index],"kind") != "end")
                _BothEnds = false;
        }
        if (_BothEnds)
        {
            const auto _NodeCoords = AssemblyArray(_ConnectionProperties,"centerlinePoint");
            if (_NodeCoords.size() != 3)
                throw std::invalid_argument("成品端对端节点缺少三维中心线位置");
            const gp_Pnt _Node(ToDouble(_NodeCoords[0],"centerlinePoint.x"),
                ToDouble(_NodeCoords[1],"centerlinePoint.y"),
                ToDouble(_NodeCoords[2],"centerlinePoint.z"));
            const auto _SourceModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
                Variant(_Prepared.ManufacturingModel));
            ValidateGenerationModelIdentity(_SourceModel,*_Context.Run);
            const auto _DisplayModel = ParseGenerationDisplay(*_Context.Run);
            const auto* _Output = FindOutputSet(_SourceModel,"result");
            if (!_Output) throw std::invalid_argument("端对端装配缺少真实制造输出");
            const auto _SourceGeometry = EvaluateOutputGeometry(_SourceModel,*_Output);
            std::vector<gp_Vec> _AwayWorld(_MemberCount);
            std::vector<gp_Trsf> _SourceToPart(_MemberCount);
            for (std::size_t _Index = 0; _Index < _MemberCount; ++_Index)
            {
                const auto _Selected = _SavedParticipants[_Index].To<ObjectMap>();
                const auto& _Part = *_Parts.at(GetRequiredText(_Selected,"memberEntityId"));
                const auto& _Item = FindModelItem(_SourceModel,_Part.StableKey);
                const auto& _DisplayItem = FindModelItem(_DisplayModel,_Part.StableKey);
                const auto _Representation = _Item.Representations.find(_Output->Purpose);
                if (_Representation == _Item.Representations.end())
                    throw std::invalid_argument("端对端构件缺少真实原管实体");
                const auto _Mapping = AssemblySourceToPartMapping(
                    _SourceGeometry.At(_Representation->second),_Item.Properties,
                    _DisplayItem.Properties,_Part.ItemProperties,
                    GetDouble(_Part.ItemProperties,"length",_Part.Length));
                if (GetString(_Mapping,"status") != "verified")
                    throw std::invalid_argument("端对端构件坐标无法验证: "
                        + GetString(_Mapping,"reason","缺少真实原管映射"));
                const auto _Matrix = AssemblyArray(_Mapping,"sourceToPart");
                if (_Matrix.size() != 16)
                    throw std::invalid_argument("端对端构件制造坐标矩阵无效");
                const auto _M = [&_Matrix](const std::size_t Index_) {
                    return ToDouble(_Matrix[Index_],"sourceToPart");
                };
                _SourceToPart[_Index].SetValues(_M(0),_M(1),_M(2),_M(3),
                    _M(4),_M(5),_M(6),_M(7),_M(8),_M(9),_M(10),_M(11));
                const auto [_Start,_End] = AssemblyMemberFrame(_DisplayItem.Properties);
                _AwayWorld[_Index] = gp_Vec(_Start,_End);
                if (GetRequiredText(_DeclaredEnds[_Index],"end") == "end")
                    _AwayWorld[_Index].Reverse();
                if (_AwayWorld[_Index].Magnitude() <= 1.0e-8)
                    throw std::invalid_argument("端对端构件中心线长度无效");
                _AwayWorld[_Index].Normalize();
            }
            for (std::size_t _Index = 0; _Index < _MemberCount; ++_Index)
            {
                auto _Self = _AwayWorld[_Index].Transformed(_SourceToPart[_Index]);
                _Self.Normalize();
                const auto _NodePart = _Node.Transformed(_SourceToPart[_Index]);
                auto _Participant = _Participants[_Index].To<ObjectMap>();
                ObjectMap _Frame{
                    {"selfAwayPart",VariantArray{_Self.X(),_Self.Y(),_Self.Z()}},
                    {"selfNodePart",VariantArray{_NodePart.X(),_NodePart.Y(),_NodePart.Z()}}};
                if (_MemberCount == 2)
                {
                    auto _Other = _AwayWorld[1-_Index].Transformed(_SourceToPart[_Index]);
                    _Other.Normalize();
                    _Frame["otherAwayPart"] = VariantArray{_Other.X(),_Other.Y(),_Other.Z()};
                }
                else
                {
                    ObjectMap _AwayByRole;
                    for (std::size_t _OtherIndex = 0; _OtherIndex < _MemberCount; ++_OtherIndex)
                    {
                        auto _Away = _AwayWorld[_OtherIndex].Transformed(_SourceToPart[_Index]);
                        _Away.Normalize();
                        const auto _Role = GetRequiredText(
                            _Participants[_OtherIndex].To<ObjectMap>(),"role");
                        if (!_AwayByRole.emplace(_Role,
                            VariantArray{_Away.X(),_Away.Y(),_Away.Z()}).second)
                            throw std::invalid_argument("节点工艺角色重复");
                    }
                    _Frame["awayPartsByRole"] = std::move(_AwayByRole);
                    _Frame["sourceToPart"] = AssemblyTransformMatrix(_SourceToPart[_Index]);
                    _Frame["partToSource"] = AssemblyTransformMatrix(_SourceToPart[_Index].Inverted());
                }
                _Participant["nodeFrame"] = std::move(_Frame);
                _Participants[_Index] = std::move(_Participant);
            }
        }
    }
    // Product anchors refer to logical nodes. Tool stations refer to the
    // physical blank, which can extend before the logical start and after the
    // logical end. Translate logical side stations before reversing the stock.
    // Keep _SavedParticipants in product coordinates for edit/reopen checks.
    for (auto& _Value : _Participants)
    {
        auto _Participant = _Value.To<ObjectMap>();
        const auto& _Part = *_Parts.at(GetRequiredText(_Participant,"memberEntityId"));
        const auto [_LogicalStart,_LogicalEnd] = AssemblyMemberFrame(_Part.ItemProperties);
        const double _LogicalLength = _LogicalStart.Distance(_LogicalEnd);
        const auto _Stock = AssemblyMemberStockInterval(_Part.ItemProperties,_LogicalLength);
        const auto _StartAtMinX = _Part.ItemProperties.find("manufacturing.sourceStartAtMinX");
        if (_StartAtMinX == _Part.ItemProperties.end())
        {
            if (_Stock.Declared)
                throw std::invalid_argument("物理原管区间缺少制造起端方向");
            continue;
        }
        if (!_StartAtMinX->second.Is<bool>())
            throw std::invalid_argument("构件的制造起端方向声明无效");
        const bool _Reverse = !_StartAtMinX->second.To<bool>();
        auto _Anchor = GetRequiredObject(_Participant,"anchor");
        const auto _Kind = GetRequiredText(_Anchor,"kind");
        if (_Reverse && std::abs(GetDouble(_Anchor,"rotation",0.0)) > 1.0e-8)
            throw std::invalid_argument("反向构件暂不支持带旋转角的加工锚点");
        if (_Kind == "end")
        {
            const auto _End = GetRequiredText(_Anchor,"end");
            if (_End != "start" && _End != "end")
                throw std::invalid_argument("构件的端部加工锚点无效");
            if (_Reverse)
                _Anchor["end"] = std::string(_End == "start" ? "end" : "start");
            // stockAllowance still belongs to the same physical end; only its
            // canonical end name changes when the member's axis is reversed.
        }
        else if (_Kind == "side")
        {
            if (GetString(_Anchor,"reference") != "start" || !_Anchor.contains("station"))
                throw std::invalid_argument("构件的侧面加工锚点无效");
            const auto _Station = ToDouble(_Anchor.at("station"),"station");
            const double _PhysicalStation = _Reverse
                ? _Stock.EndStation - _Station
                : _Station - _Stock.StartStation;
            const double _PhysicalLength = ToDouble(_Participant.at("length"),"length");
            if (!std::isfinite(_PhysicalStation) || _PhysicalStation < -0.05
                || _PhysicalStation > _PhysicalLength + 0.05)
                throw std::invalid_argument("构件侧壁节点不在真实物理原管内");
            _Anchor["station"] = _PhysicalStation;
        }
        else throw std::invalid_argument("构件的加工锚点类型无效");
        _Participant["anchor"] = std::move(_Anchor);
        _Value = std::move(_Participant);
    }
    // Supplied section boundaries may have an off-centre UV datum. Recover
    // the exact datum from each committed uncut source BRep, then express the
    // branch section and product node in the host's verified part coordinates.
    // Only transient runtime participants carry this derived tool input.
    {
        for (std::size_t _Index=0; _Index<_Participants.size(); ++_Index)
        {
            auto _Participant = _Participants[_Index].To<ObjectMap>();
            const auto _ProductAnchor = GetRequiredObject(
                _SavedParticipants[_Index].To<ObjectMap>(),"anchor");
            _Participant["productAnchor"] = _ProductAnchor;
            _Participants[_Index] = std::move(_Participant);
        }
        const auto _SourceModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            Variant(_Prepared.ManufacturingModel));
        ValidateGenerationModelIdentity(_SourceModel,*_Context.Run);
        const auto _DisplayModel = ParseGenerationDisplay(*_Context.Run);
        const auto* _Output = FindOutputSet(_SourceModel,"result");
        if (!_Output) throw std::invalid_argument("制造文档缺少真实原管输出");
        const auto _SourceGeometry = EvaluateOutputGeometry(_SourceModel,*_Output);
        std::map<std::string,ObjectMap> _VerifiedSections;
        for (auto& _Value : _Participants)
        {
            auto _Participant = _Value.To<ObjectMap>();
            auto _Section = GetRequiredObject(_Participant,"section");
            const auto& _Part = *_Parts.at(GetRequiredText(_Participant,"memberEntityId"));
            const auto& _Item = FindModelItem(_SourceModel,_Part.StableKey);
            const auto& _DisplayItem = FindModelItem(_DisplayModel,_Part.StableKey);
            const auto _MemberIt = _Item.Properties.find("assemblyFrame.member");
            const bool _HasSectionFrame = _MemberIt != _Item.Properties.end()
                && _MemberIt->second.Is<ObjectMap>()
                && _MemberIt->second.To<ObjectMap>().contains("sectionFrame");
            if (!_HasSectionFrame)
            {
                if (GetString(_Section,"geometrySource") == "providedBoundary")
                    throw std::invalid_argument("提供截面缺少真实截面基准");
                continue;
            }
            const auto _Representation = _Item.Representations.find(_Output->Purpose);
            if (_Representation == _Item.Representations.end())
                throw std::invalid_argument("制造构件缺少真实原管表示");
            auto _Frame = VerifiedAssemblySectionFrame(
                _SourceGeometry.At(_Representation->second),_Item.Properties,
                _DisplayItem.Properties,_Section);
            _VerifiedSections.emplace(GetRequiredText(_Participant,"role"),_Frame);
            _Section["sectionFrame"] = std::move(_Frame);
            _Participant["section"] = std::move(_Section);
            _Value = std::move(_Participant);
        }
        if (_Participants.size() == 2)
        {
            std::size_t _SideIndex = 2, _EndIndex = 2;
            std::size_t _SideCount = 0;
            for (std::size_t _Index=0; _Index<2; ++_Index)
            {
                const auto _Participant = _Participants[_Index].To<ObjectMap>();
                const auto _Kind = GetString(GetRequiredObject(_Participant,"anchor"),"kind");
                if (_Kind == "side") { _SideIndex = _Index; ++_SideCount; }
                if (_Kind == "end") _EndIndex = _Index;
            }
            // Any supplied section can be the profile of a tool on the other
            // member. For side/end there is one such direction; for a genuine
            // midspan crossing both members are side anchored, so carry both
            // verified transforms and let the template select sectionRole.
            std::vector<std::pair<std::size_t,std::size_t>> _SectionMappings;
            if (_SideCount == 1 && _EndIndex < 2)
                _SectionMappings.emplace_back(_SideIndex,_EndIndex);
            else if (_SideCount == 2)
            {
                _SectionMappings.emplace_back(0,1);
                _SectionMappings.emplace_back(1,0);
            }
            for (const auto& [_HostIndex,_BranchIndex] : _SectionMappings)
            {
                auto _Branch = _Participants[_BranchIndex].To<ObjectMap>();
                auto _Section = GetRequiredObject(_Branch,"section");
                if (_Section.contains("sectionFrame"))
                {
                    const auto _Host = _Participants[_HostIndex].To<ObjectMap>();
                    const auto& _Part = *_Parts.at(GetRequiredText(_Host,"memberEntityId"));
                    const auto& _Item = FindModelItem(_SourceModel,_Part.StableKey);
                    const auto& _DisplayItem = FindModelItem(_DisplayModel,_Part.StableKey);
                    const auto _Representation = _Item.Representations.find(_Output->Purpose);
                    if (_Representation == _Item.Representations.end())
                        throw std::invalid_argument("主管缺少真实制造原管");
                    const auto _Mapping = AssemblySourceToPartMapping(
                        _SourceGeometry.At(_Representation->second),_Item.Properties,
                        _DisplayItem.Properties,_Part.ItemProperties,
                        GetDouble(_Part.ItemProperties,"length",_Part.Length));
                    if (GetString(_Mapping,"status") != "verified")
                        throw std::invalid_argument("提供截面的主管坐标无法验证: "
                            + GetString(_Mapping,"reason","缺少真实原管映射"));
                    const auto _Matrix = AssemblyArray(_Mapping,"sourceToPart");
                    if (_Matrix.size()!=16)
                        throw std::invalid_argument("主管制造坐标矩阵无效");
                    const auto _M = [&_Matrix](std::size_t Index_) {
                        return ToDouble(_Matrix[Index_],"sourceToPart");
                    };
                    gp_Trsf _SourceToPart;
                    _SourceToPart.SetValues(_M(0),_M(1),_M(2),_M(3),
                        _M(4),_M(5),_M(6),_M(7),_M(8),_M(9),_M(10),_M(11));
                    auto _Frame = GetRequiredObject(_Section,"sectionFrame");
                    const auto _WorldVector = [&_Frame](const char* Key_) {
                        const auto _Values = AssemblyArray(_Frame,Key_);
                        if (_Values.size()!=3)
                            throw std::invalid_argument("提供截面的三维方向无效");
                        return gp_Vec(ToDouble(_Values[0],Key_),ToDouble(_Values[1],Key_),
                            ToDouble(_Values[2],Key_));
                    };
                    const auto _U = _WorldVector("xAxisWorld").Transformed(_SourceToPart);
                    const auto _V = _WorldVector("yAxisWorld").Transformed(_SourceToPart);
                    const auto _NodeValues = AssemblyArray(_ConnectionProperties,"centerlinePoint");
                    if (_NodeValues.size()!=3)
                        throw std::invalid_argument("提供截面的产品节点位置无效");
                    const gp_Pnt _Node(ToDouble(_NodeValues[0],"centerlinePoint.x"),
                        ToDouble(_NodeValues[1],"centerlinePoint.y"),
                        ToDouble(_NodeValues[2],"centerlinePoint.z"));
                    const auto _NodePart = _Node.Transformed(_SourceToPart);
                    const auto _HostBounds = ShapeBounds(AssemblyPreparedShape(*Scene_,_Part));
                    const auto _HostMin = AssemblyArray(_HostBounds,"min");
                    const auto _HostAnchor = GetRequiredObject(_Host,"anchor");
                    const double _StationX = ToDouble(_HostMin[0],"min.x")
                        + ToDouble(_HostAnchor.at("station"),"station");
                    if (std::abs(_NodePart.X()-_StationX)>0.05)
                        throw std::invalid_argument("支管节点与主管真实加工站位不一致");
                    _Frame["xAxisToolPart"] = VariantArray{_U.X(),_U.Y(),_U.Z()};
                    _Frame["yAxisToolPart"] = VariantArray{_V.X(),_V.Y(),_V.Z()};
                    _Frame["nodeToolPart"] = VariantArray{_NodePart.X(),_NodePart.Y(),_NodePart.Z()};
                    // The paired end-tab cutter keeps the branch's physical
                    // manufacturing Z walls. Carry that verified direction
                    // into the host's tool coordinates so its two sockets
                    // follow the same walls, including quarter-turned stock.
                    const auto& _BranchPart = *_Parts.at(GetRequiredText(_Branch,"memberEntityId"));
                    const auto& _BranchItem = FindModelItem(_SourceModel,_BranchPart.StableKey);
                    const auto& _BranchDisplayItem = FindModelItem(_DisplayModel,_BranchPart.StableKey);
                    const auto _BranchRepresentation = _BranchItem.Representations.find(_Output->Purpose);
                    if (_BranchRepresentation == _BranchItem.Representations.end())
                        throw std::invalid_argument("支管缺少真实制造原管表示");
                    const auto _BranchMapping = AssemblySourceToPartMapping(
                        _SourceGeometry.At(_BranchRepresentation->second),_BranchItem.Properties,
                        _BranchDisplayItem.Properties,_BranchPart.ItemProperties,
                        GetDouble(_BranchPart.ItemProperties,"length",_BranchPart.Length));
                    if (GetString(_BranchMapping,"status") != "verified")
                        throw std::invalid_argument("支管制造坐标无法验证: "
                            + GetString(_BranchMapping,"reason","缺少真实原管映射"));
                    const auto _BranchMatrix = AssemblyArray(_BranchMapping,"sourceToPart");
                    if (_BranchMatrix.size()!=16)
                        throw std::invalid_argument("支管制造坐标矩阵无效");
                    const auto _BM = [&_BranchMatrix](std::size_t Index_) {
                        return ToDouble(_BranchMatrix[Index_],"branchSourceToPart");
                    };
                    gp_Trsf _BranchSourceToPart;
                    _BranchSourceToPart.SetValues(_BM(0),_BM(1),_BM(2),_BM(3),
                        _BM(4),_BM(5),_BM(6),_BM(7),_BM(8),_BM(9),_BM(10),_BM(11));
                    const auto _BranchZInHost = gp_Vec(0,0,1)
                        .Transformed(_BranchSourceToPart.Inverted()).Transformed(_SourceToPart);
                    if (std::abs(_BranchZInHost.Magnitude()-1.0)>1.0e-6)
                        throw std::invalid_argument("支管制造 Z 壁方向无效");
                    _Frame["branchManufacturingZHostToolPart"] = VariantArray{
                        _BranchZInHost.X(),_BranchZInHost.Y(),_BranchZInHost.Z()};
                    const auto _HostSection = GetRequiredObject(_Host,"section");
                    const auto _HostFrame = GetRequiredObject(_HostSection,"sectionFrame");
                    const auto _HostAxis = [&_HostFrame,&_SourceToPart](const char* Key_) {
                        const auto _Values = AssemblyArray(_HostFrame,Key_);
                        if (_Values.size()!=3)
                            throw std::invalid_argument("主管截面方向无效");
                        return gp_Vec(ToDouble(_Values[0],Key_),ToDouble(_Values[1],Key_),
                            ToDouble(_Values[2],Key_)).Transformed(_SourceToPart);
                    };
                    const auto _HostU = _HostAxis("xAxisWorld");
                    const auto _HostV = _HostAxis("yAxisWorld");
                    _Frame["hostXAxisToolPart"] = VariantArray{_HostU.X(),_HostU.Y(),_HostU.Z()};
                    _Frame["hostYAxisToolPart"] = VariantArray{_HostV.X(),_HostV.Y(),_HostV.Z()};
                    if (_HostFrame.contains("faceNormalsWorld"))
                    {
                        const auto _Faces=GetRequiredObject(_HostFrame,"faceNormalsWorld");
                        const auto _Face=GetRequiredText(_HostAnchor,"face");
                        const auto _ExpectedValues=AssemblyArray(_Faces,_Face.c_str());
                        if (_ExpectedValues.size()!=3)
                            throw std::invalid_argument("已核对的产品连接面方向无效");
                        const gp_Vec _Expected(ToDouble(_ExpectedValues[0],"faceNormal.x"),
                            ToDouble(_ExpectedValues[1],"faceNormal.y"),
                            ToDouble(_ExpectedValues[2],"faceNormal.z"));
                        bool _FoundHost=false;
                        for (const auto& _RowValue : AssemblyArray(_ConnectionProperties,"participantAnchors"))
                        {
                            const auto _Row=_RowValue.To<ObjectMap>();
                            if (GetString(_Row,"itemKey")!=_Part.StableKey) continue;
                            if (_FoundHost) throw std::invalid_argument("产品连接重复声明主管位置");
                            _FoundHost=true;
                            const auto _FaceValues=AssemblyArray(_Row,"faceNormal");
                            if (_FaceValues.size()!=3)
                                throw std::invalid_argument("产品连接缺少实际接触面的三维方向");
                            const gp_Vec _Declared(ToDouble(_FaceValues[0],"faceNormal.x"),
                                ToDouble(_FaceValues[1],"faceNormal.y"),
                                ToDouble(_FaceValues[2],"faceNormal.z"));
                            if (!std::isfinite(_Declared.X()) || !std::isfinite(_Declared.Y())
                                || !std::isfinite(_Declared.Z())
                                || (_Declared-_Expected).Magnitude()>1.0e-8)
                                throw std::invalid_argument("产品连接面的法向与已提交真实截面不一致");
                            const auto _NormalPart=_Declared.Transformed(_SourceToPart);
                            _Frame["hostFaceNormalToolPart"] = VariantArray{
                                _NormalPart.X(),_NormalPart.Y(),_NormalPart.Z()};
                        }
                        if (!_FoundHost)
                            throw std::invalid_argument("产品连接缺少主管实际接触面");
                    }
                    _Section["sectionFrame"] = std::move(_Frame);
                    _Branch["section"] = std::move(_Section);
                    _Participants[_BranchIndex] = std::move(_Branch);
                }
            }
        }
    }
    ObjectMap _Runtime{{"action",std::string("resolve-bound-plan")},
        {"templateId",GetRequiredText(_Payload,"templateId")},{"participants",_Participants},
        {"productTopology",_ProductTopology},
        {"parameters",_Payload.contains("parameters") ? GetRequiredObject(_Payload,"parameters") : ObjectMap{}},
        {"processDrafts",_Payload.contains("processDrafts") ? GetRequiredObject(_Payload,"processDrafts") : ObjectMap{}}};
    auto _Plan = InvokeAssemblyTemplateRuntime(Application_,_Runtime);
    bool _TopologyCompatible = false;
    for (const auto& _Value : AssemblyArray(_Plan,"compatibleProductTopologies"))
        if (_Value.Is<std::string>() && _Value.To<std::string>() == _ProductTopology)
            _TopologyCompatible = true;
    if (!_TopologyCompatible)
        throw std::invalid_argument("装配工艺与当前产品连接的成品拓扑不兼容");
    _Plan["productEntityId"] = UuidToString(_Context.ProductID);
    _Plan["generationRunId"] = UuidToString(_Context.RunID);
    _Plan["connectionKey"] = _ConnectionKey;
    _Plan["connectionLabel"] = _ConnectionLabel;
    _Plan["canApply"] = false;
    _Plan["fitStatus"] = std::string("not-verified");
    _Plan["interfaceMeasurement"] = ObjectMap{{"status",std::string("unsupported")},
        {"reason",std::string("尚未测量候选加工后的产品节点局部实体")}};
    if (!_Plan.contains("supported") || !_Plan.at("supported").Is<bool>() || !_Plan.at("supported").To<bool>())
        return MakeResponse(Variant(std::move(_Plan)));
    ValidateAssemblyDeclaredAxisAngle(_Plan,_SavedParticipants,*_Context.Run);
    const auto _ContactRequirements = ResolveAssemblyNodeContactRequirements(_Plan,_SavedParticipants);
    const auto _ManufacturingEffect = GetRequiredText(_Plan,"manufacturingEffect");
    if (_ManufacturingEffect != "incremental-cut" && _ManufacturingEffect != "connection-only")
        throw std::invalid_argument("装配绑定返回了无效的制造效果");
    const bool _ConnectionOnly = _ManufacturingEffect == "connection-only";
    VariantArray _FrozenParts, _GeometryChanges;
    try
    {
        ValidateVerifiedSectionSingleInsert(_Plan,_Participants);
        if (_ConnectionOnly)
        {
            if (!AssemblyArray(_Plan,"parts").empty())
                throw std::invalid_argument("仅连接方案不能包含单件切削加工");
            if (!AssemblyArray(GetRequiredObject(_Plan,"workflow"),"partOperations").empty())
                throw std::invalid_argument("仅连接方案不能包含单件加工步骤");
            // The selected relationship and member identities were checked against
            // the committed generation run above. Validate each actual shape and
            // its explicit anchor before saving a binding with no cutters.
            for (const auto& _Value : _Participants)
            {
                const auto _Participant = _Value.To<ObjectMap>();
                const auto _ID = GetRequiredText(_Participant,"memberEntityId");
                const auto _Found = _Parts.find(_ID);
                if (_Found == _Parts.end() || ShapeMaterialVolume(AssemblyPreparedShape(*Scene_,*_Found->second)) <= 1e-6)
                    throw std::invalid_argument("仅连接方案缺少有效真实制造件");
                const auto _Anchor = GetRequiredObject(_Participant,"anchor");
                const auto _Kind = GetRequiredText(_Anchor,"kind");
                if (_Kind == "end" && ((GetString(_Anchor,"end") != "start"
                    && GetString(_Anchor,"end") != "end") || GetDouble(_Anchor,"trim",0) != 0))
                    throw std::invalid_argument("仅连接方案需要零余量的明确端部锚点");
                if (_Kind == "side")
                {
                    const auto _Face = GetString(_Anchor,"face");
                    const auto _Station = GetDouble(_Anchor,"station",-1);
                    if ((_Face != "top" && _Face != "bottom" && _Face != "left" && _Face != "right")
                        || _Station < 0 || _Station > GetDouble(_Participant,"length",0))
                        throw std::invalid_argument("仅连接方案缺少有效侧面锚点");
                }
                else if (_Kind != "end")
                    throw std::invalid_argument("仅连接方案的构件锚点无效");
            }
        }
        for (const auto& _Value : AssemblyArray(_Plan,"parts"))
        {
            auto _Changes = _Value.To<ObjectMap>();
            const auto _ID = GetRequiredText(_Changes,"memberEntityId");
            const auto _Found = _Parts.find(_ID);
            if (_Found == _Parts.end() || GetString(_Changes,"itemKey") != _Found->second->StableKey)
                throw std::runtime_error("工艺解析返回了不属于选定构件的加工");
            const auto& _Part = *_Found->second;
            const auto _OriginalBase = AssemblyPreparedShape(*Scene_,_Part);
            const auto _Base = ApplyFrozenAssemblyBindings(*Scene_,_Existing,_Part.StableKey,_OriginalBase);
            auto _Features = ReadPunchFeatures(_Changes,"features");
            auto _Ends = ReadPunchEnds(_Changes);
            std::vector<SPunchCut> _Cuts;
            (void)EvaluatePunch(_OriginalBase,_Part.TubeProfile,_Features,_Ends,
                Request_,Application_,*Scene_,{},false,&_Cuts);
            const auto _Bounds = ShapeBounds(_OriginalBase);
            const auto _Min = _Bounds.at("min").To<VariantArray>();
            const auto _Max = _Bounds.at("max").To<VariantArray>();
            const auto _Length = ToDouble(_Max[0],"max.x")-ToDouble(_Min[0],"min.x");
            TopoDS_Shape _Nominal;
            if (GetString(_Part.TubeProfile,"geometrySource") == "providedBoundary")
            {
                // Its exact source section frame was replay-verified above.
                // An envelope-centred generic blank loses asymmetric grooves
                // and may compare a cut against material that never existed.
                _Nominal = _OriginalBase;
            }
            else
            {
                _Nominal = BuildPunchBlank(_Part.TubeProfile,_Length);
                // Legacy stock may quarter-turn the saved profile. Preserve
                // its existing normalization path for centred tube sections.
                const auto _NominalBounds = ShapeBounds(_Nominal);
                const auto _NominalMin = _NominalBounds.at("min").To<VariantArray>();
                const auto _NominalMax = _NominalBounds.at("max").To<VariantArray>();
                const auto _Span = [](const VariantArray& low,const VariantArray& high,int axis) {
                    return ToDouble(high[axis],"max")-ToDouble(low[axis],"min");
                };
                const double _ActualY = _Span(_Min,_Max,1), _ActualZ = _Span(_Min,_Max,2);
                const double _SectionY = _Span(_NominalMin,_NominalMax,1);
                const double _SectionZ = _Span(_NominalMin,_NominalMax,2);
                const auto _Same = [](double first,double second) { return std::abs(first-second)<=0.05; };
                if (!_Same(_ActualY,_SectionY) || !_Same(_ActualZ,_SectionZ)) {
                    if (!_Same(_ActualY,_SectionZ) || !_Same(_ActualZ,_SectionY))
                        throw std::invalid_argument("真实构件横截面与保存管型不匹配，无法校验新工艺重叠");
                    gp_Trsf _QuarterTurn;
                    _QuarterTurn.SetValues(1,0,0,0, 0,0,-1,0, 0,1,0,0);
                    _Nominal = BRepBuilderAPI_Transform(_Nominal,_QuarterTurn,true).Shape();
                }
                gp_Trsf _Shift;
                _Shift.SetTranslation(gp_Vec(ToDouble(_Min[0],"min.x"),
                    (ToDouble(_Min[1],"min.y")+ToDouble(_Max[1],"max.y"))/2,
                    (ToDouble(_Min[2],"min.z")+ToDouble(_Max[2],"max.z"))/2));
                _Nominal = _Nominal.Moved(TopLoc_Location(_Shift));
            }
            iCAX::Tasks::Run([&] {
                for (const auto& _Cut : _Cuts)
                {
                    const auto _Expected = AssemblyRemovedVolume(_Nominal,_Cut.Shape);
                    const auto _Actual = AssemblyRemovedVolume(_Base,_Cut.Shape);
                    if (_Expected <= 1e-6 || _Actual <= 1e-6)
                        throw std::invalid_argument("新工艺未切到真实构件材料，或此处已经加工");
                    if (_Actual + std::max(1e-5,_Expected*1e-4) < _Expected)
                        throw std::invalid_argument("新工艺与既有孔槽或端切重叠，请调整加工位置");
                }
            },iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
            VariantArray _FeatureSnapshots;
            for (const auto& _Feature : _Features) _FeatureSnapshots.emplace_back(PunchFeatureSnapshot(_Feature));
            _Changes["features"] = std::move(_FeatureSnapshots);
            _Changes["ends"] = PunchEndsSnapshot(_Ends);
            _Changes["frozenCuts"] = FrozenAssemblyCuts(_Features,_Ends);
            const auto _Result = ApplyFrozenAssemblyBindings(*Scene_,
                ObjectMap{{"candidate",ObjectMap{{"parts",VariantArray{_Changes}}}}},_Part.StableKey,_Base);
            _FrozenParts.emplace_back(std::move(_Changes));
            _GeometryChanges.emplace_back(ObjectMap{{"memberEntityId",_ID},{"itemKey",_Part.StableKey},
                {"volumeBefore",ShapeMaterialVolume(_Base)},{"volumeAfter",ShapeMaterialVolume(_Result)}});
        }
        if (!_ConnectionOnly && _FrozenParts.empty()) throw std::invalid_argument("工艺没有生成实际加工");
    }
    catch (const std::exception& _Error)
    {
        _Plan["checks"] = VariantArray{ObjectMap{{"id",std::string(_ConnectionOnly
                ? "actual-connection-conflict" : "actual-material-conflict")},
            {"status",std::string("conflict")},{"blocking",true},{"detail",std::string(_Error.what())}}};
        _Plan["detail"] = std::string(_Error.what());
        return MakeResponse(Variant(std::move(_Plan)));
    }
    // Place both candidate-finished manufacturing BReps back into the product
    // coordinate system. This is only a node-local material penetration check;
    // contact, zero nearest distance, and template fitChecks are not a fitted
    // joint certificate.
    try
    {
        if (_Participants.size() != 2 && _Participants.size() != 3)
            throw std::invalid_argument("节点局部实体测量需要两个或三个真实构件");
        const auto _PointValue = _ConnectionProperties.find("centerlinePoint");
        if (_PointValue == _ConnectionProperties.end()
            || !_PointValue->second.Is<VariantArray>())
            throw std::invalid_argument("产品节点没有已提交的中心线位置");
        const auto _Coords = _PointValue->second.To<VariantArray>();
        if (_Coords.size() != 3)
            throw std::invalid_argument("产品节点中心线位置不是三维坐标");
        const gp_Pnt _Node(ToDouble(_Coords[0],"centerlinePoint.x"),
            ToDouble(_Coords[1],"centerlinePoint.y"),
            ToDouble(_Coords[2],"centerlinePoint.z"));
        const auto _SourceModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
            Variant(_Prepared.ManufacturingModel));
        ValidateGenerationModelIdentity(_SourceModel,*_Context.Run);
        const auto _DisplayModel = ParseGenerationDisplay(*_Context.Run);
        const auto* _Output = FindOutputSet(_SourceModel,"result");
        if (!_Output)
            throw std::invalid_argument("制造文档缺少真实下料输出");
        const auto _SourceGeometry = EvaluateOutputGeometry(_SourceModel,*_Output);
        const ObjectMap _CandidateBindings{{"candidate",ObjectMap{{"parts",_FrozenParts}}}};
        std::vector<TopoDS_Shape> _FinalShapes;
        std::vector<ObjectMap> _Mappings;
        std::vector<SAssemblyWholeAuditPart> _NodeParts;
        double _SectionExtent = 0.0;
        for (const auto& _Value : _Participants)
        {
            const auto _Participant = _Value.To<ObjectMap>();
            const auto _ID = GetRequiredText(_Participant,"memberEntityId");
            const auto& _Part = *_Parts.at(_ID);
            const auto& _Item = FindModelItem(_SourceModel,_Part.StableKey);
            const auto& _DisplayItem = FindModelItem(_DisplayModel,_Part.StableKey);
            const auto _Representation = _Item.Representations.find(_Output->Purpose);
            if (_Representation == _Item.Representations.end())
                throw std::invalid_argument("制造文档的节点构件缺少实体表示");
            const auto _Source = _SourceGeometry.At(_Representation->second);
            const auto _Base = AssemblyPreparedShape(*Scene_,_Part);
            const auto _Bounds = ShapeBounds(_Base);
            const auto _Low = _Bounds.at("min").To<VariantArray>();
            const auto _High = _Bounds.at("max").To<VariantArray>();
            _SectionExtent = std::max({_SectionExtent,
                ToDouble(_High[1],"max.y")-ToDouble(_Low[1],"min.y"),
                ToDouble(_High[2],"max.z")-ToDouble(_Low[2],"min.z")});
            const auto _Current = ApplyFrozenAssemblyBindings(*Scene_,_Existing,
                _Part.StableKey,_Base);
            const auto _Final = ApplyFrozenAssemblyBindings(*Scene_,_CandidateBindings,
                _Part.StableKey,_Current);
            const auto _Mapping = AssemblySourceToPartMapping(_Source,_Item.Properties,
                _DisplayItem.Properties,_Part.ItemProperties,
                GetDouble(_Part.ItemProperties,"length",_Part.Length));
            _FinalShapes.emplace_back(_Final);
            _Mappings.emplace_back(_Mapping);
            _NodeParts.push_back({_Part.StableKey,_Final,_Mapping});
        }
        ValidateAssemblyCornerEntrance(_Plan,_Participants,_NodeParts);
        if (_NodeParts.size() == 2)
            _Plan["interfaceMeasurement"] = MeasureAssemblyPairNearNode(
                _FinalShapes[0],_Mappings[0],_FinalShapes[1],_Mappings[1],_Node,
                std::max(1.0,2.0 * _SectionExtent));
        else
        {
            SAssemblyFinalNodeAuditNode _Declaration{_ConnectionKey,
                _NodeParts[0].ItemKey,_NodeParts[1].ItemKey,_Node,
                std::max(1.0,2.0 * _SectionExtent),{_NodeParts[2].ItemKey},_ContactRequirements};
            const auto _Audit = AuditAssemblyFinalNodes(_NodeParts,{_Declaration});
            const auto _Nodes = AssemblyArray(_Audit,"nodes");
            _Plan["interfaceMeasurement"] = _Nodes.size() == 1
                ? _Nodes.front().To<ObjectMap>() : _Audit;
        }
        try
        {
            std::vector<SAssemblyWholeAuditPart> _WholeParts;
            std::set<std::string> _WholeKeys;
            for (const auto& _Part : _Prepared.Parts)
            {
                if (!_Part.ItemProperties.contains("assemblyFrame.member")) continue;
                const auto& _Item = FindModelItem(_SourceModel,_Part.StableKey);
                const auto& _DisplayItem = FindModelItem(_DisplayModel,_Part.StableKey);
                const auto _Representation = _Item.Representations.find(_Output->Purpose);
                if (_Representation == _Item.Representations.end())
                    throw std::invalid_argument("整框构件缺少已提交的原管实体");
                const auto _Base = AssemblyPreparedShape(*Scene_,_Part);
                const auto _Current = ApplyFrozenAssemblyBindings(*Scene_,_Existing,
                    _Part.StableKey,_Base);
                const auto _Final = ApplyFrozenAssemblyBindings(*Scene_,_CandidateBindings,
                    _Part.StableKey,_Current);
                const auto _Mapping = AssemblySourceToPartMapping(
                    _SourceGeometry.At(_Representation->second),_Item.Properties,
                    _DisplayItem.Properties,_Part.ItemProperties,
                    GetDouble(_Part.ItemProperties,"length",_Part.Length));
                _WholeKeys.insert(_Part.StableKey);
                _WholeParts.push_back({_Part.StableKey,_Final,_Mapping});
            }
            _Plan["wholeGeometryAudit"] = AuditAssemblyFinishedProductSolids(_WholeParts,
                AssemblyWholeAdjacentPairs(_AllConnections,_WholeKeys));
        }
        catch (const std::exception& _Error)
        {
            _Plan["wholeGeometryAudit"] = ObjectMap{{"status",std::string("unsupported")},
                {"scope",std::string("finished-product-non-neighbor-solids")},
                {"reason",std::string(_Error.what())},{"interfaceFitCertified",false}};
        }
    }
    catch (const std::exception& _Error)
    {
        _Plan["interfaceMeasurement"] = ObjectMap{{"status",std::string("unsupported")},
            {"reason",std::string(_Error.what())}};
    }
    const auto _Interface = GetRequiredObject(_Plan,"interfaceMeasurement");
    // A three-member composite must measure every pair of its actual final
    // solids. An unsupported pair is a failed check, never a partial success.
    if (_Participants.size() == 3 && (GetString(_Interface,"status") != "measured"
        || GetUInt64(_Interface,"checkedPairCount",0) != 3))
    {
        const bool _Conflict = GetString(_Interface,"status") == "conflict";
        _Plan["checks"] = VariantArray{ObjectMap{{"id",std::string("node-material-check")},
            {"status",std::string(_Conflict ? "conflict" : "unsupported")},{"blocking",true},
            {"detail",std::string(_Conflict ? "三向节点的真实制造实体存在穿透或声明接触失效"
                : "无法完整核对三向节点的三对真实制造实体")}}};
        _Plan["detail"] = std::string(_Conflict ? "三向节点的真实制造实体存在穿透或声明接触失效"
            : "无法完整核对三向节点的三对真实制造实体");
        return MakeResponse(Variant(std::move(_Plan)));
    }
    if (_Participants.size() == 2 && GetString(_Interface,"status") == "measured")
    {
        const double _Intersection = GetDouble(_Interface,"intersectionVolumeMm3",0.0);
        const double _LocalMinimum = std::min(
            GetDouble(_Interface,"localMaterialVolumeAMm3",0.0),
            GetDouble(_Interface,"localMaterialVolumeBMm3",0.0));
        const double _BooleanTolerance = std::max(1.0e-5,_LocalMinimum * 1.0e-8);
        if (!std::isfinite(_Intersection) || _Intersection > _BooleanTolerance)
        {
            _Plan["checks"] = VariantArray{ObjectMap{{"id",std::string("node-material-overlap")},
                {"status",std::string("conflict")},{"blocking",true},
                {"detail",std::string("候选加工后的两件在产品节点附近有实体穿透")}}};
            _Plan["detail"] = std::string("候选加工后的两件在产品节点附近有实体穿透");
            return MakeResponse(Variant(std::move(_Plan)));
        }
    }
    auto _Checks = AssemblyArray(_Plan,"checks");
    for (auto& _Value : _Checks)
    {
        auto _Check = _Value.To<ObjectMap>();
        if (GetString(_Check,"id") == "native-material-check")
        {
            _Check["status"] = std::string("pass"); _Check["blocking"] = false;
            _Check["detail"] = std::string("真实构件切削有效，未与既有加工重叠");
        }
        else if (GetString(_Check,"id") == "native-connection-check" && _ConnectionOnly)
        {
            _Check["status"] = std::string("pass"); _Check["blocking"] = false;
            _Check["detail"] = std::string("真实构件身份和锚点有效；无需新增单件切削");
        }
        _Value = std::move(_Check);
    }
    _Plan["checks"] = _Checks;
    auto _Workflow = GetRequiredObject(_Plan,"workflow");
    _Workflow["checks"] = _Checks;
    _Workflow["validationStatus"] = std::string(_ConnectionOnly ? "connection-checked" : "manufacturing-checked");
    _Plan["workflow"] = _Workflow;
    _Plan["geometryChanges"] = _GeometryChanges;
    _Plan["canApply"] = true;
    _Plan["detail"] = std::string(_ConnectionOnly
        ? "无需新增单件加工；真实连接成员和锚点已核对，装配位置待核对"
        : "单件加工可执行，装配位置待核对");
    const auto _BindingID = _ReplaceID.empty() ? UuidToString(iCAX::Data::GenerateNewUUID()) : _ReplaceID;
    const auto _SourceDigest = AssemblyBindingSourceDigest(*_Context.Product,*_Context.Run);
    const auto _Revision = AssemblyBindingsRevision(_Context.Product->GetAssemblyBindings());
    if (_InitialSourceDigest != _SourceDigest || _InitialRevision != _Revision
        || _Context.Product->GetActiveGenerationRunID() != _Context.RunID)
        throw std::invalid_argument("检查期间产品或工艺已变化，请重新检查加工方案");
    ObjectMap _Binding{{"schemaVersion",1ull},{"bindingId",_BindingID},
        {"generationRunId",UuidToString(_Context.RunID)},{"sourceDigest",_SourceDigest},
        {"templateId",_Plan.at("templateId")},{"templateVersion",_Plan.at("templateVersion")},
        {"templateDigest",_Plan.at("templateDigest")},{"parameters",_Plan.at("parameters")},
        {"processDrafts",_Runtime.at("processDrafts")},{"participants",std::move(_SavedParticipants)},
        {"connectionKey",_ConnectionKey},{"connectionLabel",_ConnectionLabel},
        {"workflow",std::move(_Workflow)},{"parts",std::move(_FrozenParts)},
        {"assemblyFitStatus",std::string("not-verified")},{"manufacturingEffect",_ManufacturingEffect}};
    if (!_ContactRequirements.empty())
        _Binding["nodeContactRequirements"] = AssemblyNodeContactSnapshots(_ContactRequirements);
    const auto _Catalogue = InvokeAssemblyTemplateRuntime(Application_,{{"action",std::string("catalogue")}});
    for (const auto& _Value : AssemblyArray(_Catalogue,"assemblies"))
        if (const auto _Descriptor = _Value.To<ObjectMap>(); GetString(_Descriptor,"id")==GetString(_Binding,"templateId"))
            _Binding["templateName"] = GetString(_Descriptor,"displayName");
    const auto _Token = UuidToString(iCAX::Data::GenerateNewUUID());
    {
        const std::lock_guard _Lock(gAssemblyBindingCandidateMutex);
        const auto _Now = std::chrono::steady_clock::now();
        std::erase_if(gAssemblyBindingCandidates,[&](const auto& _Entry) {
            return _Now-_Entry.second.CreatedAt>std::chrono::minutes(10);
        });
        if (gAssemblyBindingCandidates.size()>=64) gAssemblyBindingCandidates.erase(gAssemblyBindingCandidates.begin());
        gAssemblyBindingCandidates.emplace(_Token,SAssemblyBindingCandidate{Scene_->GetSceneID(),
            _Context.ProductID,_Context.RunID,_SourceDigest,_Revision,_ReplaceID,std::move(_Binding),_Now});
    }
    _Plan["candidateToken"] = _Token;
    _Plan["revision"] = _Revision;
    return MakeResponse(Variant(std::move(_Plan)));
}

iCAX::Interaction::CInvocationResult CommitProductAssemblyBindings(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext* ProjectContext_,
    iCAX::Project::ISceneContext& Scene_, const SAssemblyBindingContext& Context_,
    const ObjectMap& Bindings_, const char* SuccessFlag_, const std::string& BindingID_)
{
    auto _Request = Request_;
    const auto _Payload = iCAX::Data::VariantSerializer::Serialize(Variant(ObjectMap{
        {"productEntityIds",VariantArray{UuidToString(Context_.ProductID)}}}));
    _Request.Payload.assign(_Payload.begin(),_Payload.end());
    const std::map<iCAX::Data::uuid,ObjectMap> _Updates{{Context_.ProductID,Bindings_}};
    auto _Result = AssemblyResponseObject(DisassembleProducts(_Request,Application_,
        ProductContext_,ProjectContext_,&Scene_,&_Updates));
    _Result[SuccessFlag_] = true;
    _Result["manufacturingUpdated"] = true;
    _Result["bindingId"] = BindingID_;
    return MakeResponse(Variant(std::move(_Result)));
}

iCAX::Interaction::CInvocationResult HandleApplyProductAssemblyBinding(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext* ProjectContext_,
    iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("产品工艺应用需要有效场景");
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _Context = AssemblyBindingContext(*Scene_,_Payload);
    const auto _Token = GetRequiredText(_Payload,"candidateToken");
    SAssemblyBindingCandidate _Candidate;
    {
        const std::lock_guard _Lock(gAssemblyBindingCandidateMutex);
        const auto _It = gAssemblyBindingCandidates.find(_Token);
        if (_It == gAssemblyBindingCandidates.end())
            throw std::invalid_argument("加工方案已失效，请重新检查后应用");
        _Candidate = _It->second;
    }
    if (_Candidate.SceneID != Scene_->GetSceneID() || _Candidate.ProductID != _Context.ProductID
        || _Candidate.RunID != _Context.RunID
        || _Candidate.SourceDigest != AssemblyBindingSourceDigest(*_Context.Product,*_Context.Run)
        || _Candidate.BindingsRevision != AssemblyBindingsRevision(_Context.Product->GetAssemblyBindings())
        || std::chrono::steady_clock::now()-_Candidate.CreatedAt > std::chrono::minutes(10)
        || ProductRuntimeParameterState(Application_,*_Context.Product,_Context.Run.get()).ModelOutdated)
        throw std::invalid_argument("产品或工艺已变化，请重新检查后应用");
    auto _Bindings = _Context.Product->GetAssemblyBindings();
    if (!_Candidate.ReplaceBindingID.empty() && _Bindings.erase(_Candidate.ReplaceBindingID) != 1)
        throw std::invalid_argument("要修改的工艺已不存在");
    const auto _ID = GetRequiredText(_Candidate.Binding,"bindingId");
    if (_Bindings.size() >= 64) throw std::invalid_argument("单个产品最多支持 64 条装配加工绑定");
    _Bindings[_ID] = _Candidate.Binding;
    auto _Result = CommitProductAssemblyBindings(Request_,Application_,ProductContext_,ProjectContext_,
        *Scene_,_Context,_Bindings,"applied",_ID);
    {
        const std::lock_guard _Lock(gAssemblyBindingCandidateMutex);
        gAssemblyBindingCandidates.erase(_Token);
    }
    return _Result;
}

iCAX::Interaction::CInvocationResult HandleRemoveProductAssemblyBinding(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext* ProjectContext_,
    iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("移除产品工艺需要有效场景");
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _Context = AssemblyBindingContext(*Scene_,_Payload);
    auto _Bindings = _Context.Product->GetAssemblyBindings();
    const auto _ID = GetRequiredText(_Payload,"bindingId");
    if (!_Bindings.contains(_ID)) throw std::invalid_argument("该工艺已不存在，请重新读取");
    const auto _SourceDigest = AssemblyBindingSourceDigest(*_Context.Product,*_Context.Run);
    const bool _Stale = std::any_of(_Bindings.begin(),_Bindings.end(),[&](const auto& _Entry) {
        return !_Entry.second.template Is<ObjectMap>()
            || GetString(_Entry.second.template To<ObjectMap>(),"sourceDigest") != _SourceDigest;
    });
    _Bindings.erase(_ID);
    if (!_Stale)
        return CommitProductAssemblyBindings(Request_,Application_,ProductContext_,ProjectContext_,
            *Scene_,_Context,_Bindings,"removed",_ID);
    // Stale locations cannot be replayed on a changed product. Removing one
    // obsolete choice must remain possible even while other stale choices exist.
    auto& _DB = Scene_->Database();
    auto _Undo = _DB.BeginUndoCommand("Remove obsolete product assembly binding");
    auto& _Transaction = _DB.BeginTransaction("Remove obsolete assembly choice");
    _Transaction.ModifyComponent(_Context.ProductID,CProductInstanceComponent::S_ClassName,
        {{CProductInstanceComponent::PropertyName_AssemblyBindings,PropertyValue(_Bindings)}});
    std::string _Error;
    if (!_DB.CommitTransaction(_Transaction,_Error)) throw std::runtime_error(_Error);
    _Undo->End();
    SyncNestingPersistence(*Scene_);
    auto _Result = BuildSnapshot(*Scene_,Application_,false);
    _Result["removed"] = true; _Result["manufacturingUpdated"] = false; _Result["bindingId"] = _ID;
    _Result["detail"] = std::string("失效工艺已移除，现有下料仍需重新生成");
    return MakeResponse(Variant(std::move(_Result)));
}
