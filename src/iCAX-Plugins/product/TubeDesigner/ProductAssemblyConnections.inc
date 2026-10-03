// Read only: these relationships are declarations in the committed product
// model. This endpoint does not apply a second cutting or joining operation.
struct SAssemblyManufacturingSourceMapping final
{
    std::string StockKey;
    VariantArray Spans;
};

// A finished member is a logical straight tube. Several such members can be
// manufactured from one continuous bent stock, and a closure seam can split a
// single member across two spans of that same stock. Never infer this relation
// from the stock's synthetic SourceMemberID or its name.
std::map<std::string, SAssemblyManufacturingSourceMapping>
AssemblyManufacturingSourceMappings(
    const iCAX::TemplateRuntime::SNeutralModel& ManufacturingModel_,
    const std::set<std::string>& DisplayedKeys_)
{
    const auto& _Output = ManufacturingOutput(ManufacturingModel_);
    std::map<std::string, SAssemblyManufacturingSourceMapping> _Mappings;
    std::set<std::string> _StockKeys;
    const auto _FiniteNumber = [](const ObjectMap& Object_, const std::string& Key_) {
        const auto _Found = Object_.find(Key_);
        if (_Found == Object_.end())
            throw std::runtime_error("制造构件映射缺少数值: " + Key_);
        const double _Value = ToDouble(_Found->second, Key_);
        if (!std::isfinite(_Value))
            throw std::runtime_error("制造构件映射包含非有限数值: " + Key_);
        return _Value;
    };
    const auto _Vector = [](const ObjectMap& Object_, const std::string& Key_) {
        const auto _Found = Object_.find(Key_);
        if (_Found == Object_.end() || !_Found->second.Is<VariantArray>())
            throw std::runtime_error("制造构件映射缺少三维坐标: " + Key_);
        const auto _Values = _Found->second.To<VariantArray>();
        if (_Values.size() != 3)
            throw std::runtime_error("制造构件映射的三维坐标无效: " + Key_);
        const gp_Vec _Value(ToDouble(_Values[0], Key_),
            ToDouble(_Values[1], Key_), ToDouble(_Values[2], Key_));
        if (!std::isfinite(_Value.X()) || !std::isfinite(_Value.Y())
            || !std::isfinite(_Value.Z()))
            throw std::runtime_error("制造构件映射包含非有限坐标: " + Key_);
        return _Value;
    };
    for (const auto& _StockKey : _Output.ItemKeys)
    {
        if (_StockKey.empty() || !_StockKeys.insert(_StockKey).second)
            throw std::runtime_error("制造输出包含重复或无效的下料构件身份");
        const auto& _Stock = FindModelItem(ManufacturingModel_, _StockKey);
        const auto _Declaration = _Stock.Properties.find("manufacturing.sourceMembers");
        if (_Declaration == _Stock.Properties.end())
            throw std::runtime_error("制造下料件必须声明成品构件映射: " + _StockKey);
        if (!_Declaration->second.Is<VariantArray>()
            || _Declaration->second.To<VariantArray>().empty())
            throw std::runtime_error("制造下料件缺少有效的成品构件映射: " + _StockKey);
        const auto _Sources = _Declaration->second.To<VariantArray>();
        const auto _PartKind = GetRequiredText(_Stock.Properties, "manufacturing.partKind", 32);
        if (_PartKind != "tube" && _PartKind != "plate" && _PartKind != "glass" && _PartKind != "accessory")
            throw std::runtime_error("制造构件类型不支持: " + _PartKind);
        if (_PartKind != "tube")
        {
            if (_Sources.size() != 1 || !_Sources.front().Is<ObjectMap>())
                throw std::runtime_error("非管件必须声明唯一的成品身份映射: " + _StockKey);
            const auto _Source = _Sources.front().To<ObjectMap>();
            const auto _SourceKey = GetRequiredText(_Source, "itemKey");
            if (_Source.size() != 2 || GetString(_Source, "mappingKind") != "identity"
                || _SourceKey != _StockKey || !DisplayedKeys_.contains(_SourceKey))
                throw std::runtime_error("非管件成品身份映射无效: " + _StockKey);
            if (!_Mappings.emplace(_SourceKey,
                SAssemblyManufacturingSourceMapping{_StockKey, {}}).second)
                throw std::runtime_error("成品构件存在重复的制造映射: " + _SourceKey);
            continue;
        }
        const double _StockLength = _FiniteNumber(_Stock.Properties,
            _Stock.Properties.contains("tubeDesigner.assemblyProcessStockLength")
                ? "tubeDesigner.assemblyProcessStockLength" : "length");
        if (_StockLength <= 0.0)
            throw std::runtime_error("制造构件映射的下料长度无效: " + _StockKey);
        for (const auto& _SourceValue : _Sources)
        {
            if (!_SourceValue.Is<ObjectMap>())
                throw std::runtime_error("制造下料件的成品构件映射必须为对象");
            const auto _Source = _SourceValue.To<ObjectMap>();
            if (_Source.contains("mappingKind"))
                throw std::runtime_error("管件必须声明真实展开区间，不能使用非管件身份映射");
            const auto _SourceKey = GetRequiredText(_Source, "itemKey");
            if (!DisplayedKeys_.contains(_SourceKey))
                throw std::runtime_error("制造映射引用不存在的成品构件: " + _SourceKey);
            const auto _SpanValues = _Source.find("spans");
            if (_SpanValues == _Source.end() || !_SpanValues->second.Is<VariantArray>()
                || _SpanValues->second.To<VariantArray>().empty())
                throw std::runtime_error("制造映射缺少成品构件的展开区间: " + _SourceKey);
            const auto _Spans = _SpanValues->second.To<VariantArray>();
            std::vector<std::pair<double, double>> _Intervals;
            for (const auto& _SpanValue : _Spans)
            {
                if (!_SpanValue.Is<ObjectMap>())
                    throw std::runtime_error("制造构件展开区间必须为对象");
                const auto _Span = _SpanValue.To<ObjectMap>();
                const auto _Start = _Vector(_Span, "start");
                const auto _End = _Vector(_Span, "end");
                const double _Length = (_End - _Start).Magnitude();
                const double _StockStart = _FiniteNumber(_Span, "stockStart");
                const double _StartReserve = _FiniteNumber(_Span, "startReserve");
                const double _EndReserve = _FiniteNumber(_Span, "endReserve");
                const double _StockEnd = _StockStart + _StartReserve + _Length + _EndReserve;
                if (_Length <= 1.0e-8 || !std::isfinite(_StockEnd)
                    || _StockStart < 0.0 || _StartReserve < 0.0 || _EndReserve < 0.0
                    || _StockEnd > _StockLength + 0.001)
                    throw std::runtime_error("制造构件展开区间超出真实下料长度: " + _SourceKey);
                const auto _Placement = GetRequiredObject(_Span, "placement");
                (void)_Vector(_Placement, "origin");
                const auto _X = _Vector(_Placement, "xAxis");
                const auto _Y = _Vector(_Placement, "yAxis");
                const auto _Z = _Vector(_Placement, "zAxis");
                if (std::abs(_X.Magnitude() - 1.0) > 1.0e-6
                    || std::abs(_Y.Magnitude() - 1.0) > 1.0e-6
                    || std::abs(_Z.Magnitude() - 1.0) > 1.0e-6
                    || std::abs(_X.Dot(_Y)) > 1.0e-6
                    || std::abs(_X.Dot(_Z)) > 1.0e-6
                    || std::abs(_Y.Dot(_Z)) > 1.0e-6
                    || std::abs(_X.Crossed(_Y).Dot(_Z) - 1.0) > 1.0e-6)
                    throw std::runtime_error("制造构件展开区间的坐标系无效: " + _SourceKey);
                for (const auto& [_PreviousStart, _PreviousEnd] : _Intervals)
                    if (std::min(_PreviousEnd, _StockEnd)
                        - std::max(_PreviousStart, _StockStart) > 1.0e-6)
                        throw std::runtime_error("同一成品构件的展开区间重复或重叠: " + _SourceKey);
                _Intervals.emplace_back(_StockStart, _StockEnd);
            }
            if (!_Mappings.emplace(_SourceKey,
                SAssemblyManufacturingSourceMapping{_StockKey, _Spans}).second)
                throw std::runtime_error("成品构件存在重复的制造映射: " + _SourceKey);
        }
    }
    return _Mappings;
}

void ValidateProductStockSourceMappings(const iCAX::TemplateRuntime::SNeutralModel& Manufacturing_,
    const iCAX::TemplateRuntime::SNeutralModel& Design_)
{
    if (OptionalPropertyUInt64(Manufacturing_.Extensions, "tubeDesigner.productStockMappingVersion") != 1)
        throw std::runtime_error("产品制造输出必须声明当前成品构件映射协议");
    const auto* _Output = FindOutputSet(Design_, "result");
    if (!_Output) throw std::runtime_error("成品模型缺少逻辑构件输出");
    const std::set<std::string> _Keys(_Output->ItemKeys.begin(), _Output->ItemKeys.end());
    const auto _Mappings = AssemblyManufacturingSourceMappings(Manufacturing_, _Keys);
    if (_Mappings.size() != _Keys.size()) throw std::runtime_error("产品制造映射未完整覆盖成品构件");
    for (const auto& [_SourceKey, _Mapping] : _Mappings)
    {
        const auto& _Source = FindModelItem(Design_, _SourceKey);
        const auto& _Stock = FindModelItem(Manufacturing_, _Mapping.StockKey);
        if (GetRequiredText(_Source.Properties, "manufacturing.partKind", 32)
            != GetRequiredText(_Stock.Properties, "manufacturing.partKind", 32))
            throw std::runtime_error("产品制造映射与成品构件类型不一致: " + _SourceKey);
    }
}

VariantArray ProductAssemblyStockRows(iCAX::Project::ISceneContext& Scene_,
    const iCAX::Data::uuid& ProductID_, const iCAX::Data::uuid& RunID_,
    const iCAX::TemplateRuntime::SNeutralModel& Manufacturing_)
{
    struct SSavedStock { std::string ID, Status; double Length = 0; ObjectMap Properties; };
    std::map<std::string, SSavedStock> _Saved;
    for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(Scene_.Database()))
        if (_Part->GetProductID() == ProductID_ && _Part->GetGenerationRunID() == RunID_)
            _Saved[_Part->GetStableKey()] = {UuidToString(_Entity->GetID()), "persisted", _Part->GetLength(), _Part->GetItemProperties()};
    for (const auto& _Prepared : CopyTransientDisassembly(Scene_))
        if (_Prepared.ProductID == ProductID_ && _Prepared.GenerationRunID == RunID_)
            for (const auto& _Part : _Prepared.Parts)
                if (!_Saved.contains(_Part.StableKey))
                    _Saved[_Part.StableKey] = {UuidToString(_Part.PartID), "transient", _Part.Length, _Part.ItemProperties};
    ObjectMap _Names;
    for (const auto& [_Entity, _Member] : Collect<CAssemblyMemberComponent>(Scene_.Database()))
        if (_Member->GetProductID() == ProductID_) _Names[_Member->GetStableKey()] = _Member->GetName();
    ObjectMap _Plan;
    if (const auto _Native = Manufacturing_.Extensions.find("tubeDesigner.assemblyProcessNative");
        _Native != Manufacturing_.Extensions.end()) _Plan = GetRequiredObject(_Native->second.To<ObjectMap>(), "plan");
    else if (const auto _Planned = Manufacturing_.Extensions.find("tubeDesigner.assemblyProcessPlan");
        _Planned != Manufacturing_.Extensions.end()) _Plan = _Planned->second.To<ObjectMap>();
    VariantArray _Rows;
    for (const auto& _Key : ManufacturingOutput(Manufacturing_).ItemKeys) {
        const auto& _Item = FindModelItem(Manufacturing_, _Key);
        const auto _Found = _Saved.find(_Key);
        const auto _Properties = _Found == _Saved.end() ? _Item.Properties : _Found->second.Properties;
        VariantArray _Sources;
        if (const auto _Mapping = _Properties.find("manufacturing.sourceMembers"); _Mapping != _Properties.end())
            for (const auto& _Value : _Mapping->second.To<VariantArray>()) {
                auto _Source = _Value.To<ObjectMap>();
                const auto _MemberKey = GetRequiredText(_Source, "itemKey");
                _Source["memberEntityId"] = UuidToString(MakeStableEntityID(ProductID_, "member", _MemberKey));
                _Source["name"] = _Names.contains(_MemberKey) ? _Names.at(_MemberKey) : Variant(_MemberKey);
                _Sources.emplace_back(std::move(_Source));
            }
        else
            throw std::runtime_error("制造下料件未声明成品构件映射: " + _Key);
        const auto _StockID = GetString(_Properties, "tubeDesigner.assemblyProcessStockId");
        VariantArray _Operations;
        if (const auto _It = _Plan.find("operations"); _It != _Plan.end())
            for (const auto& _Value : _It->second.To<VariantArray>()) {
                const auto _Operation = _Value.To<ObjectMap>();
                if (GetString(_Operation, "stockId") == _StockID) _Operations.emplace_back(_Operation);
            }
        _Rows.emplace_back(ObjectMap{{"itemKey", _Key}, {"stockId", _StockID},
            {"stockEntityId", _Found == _Saved.end() ? std::string{} : _Found->second.ID},
            {"name", _Item.DisplayName.Resolve("zh-CN")},
            {"partLength", _Found == _Saved.end() ? GetDouble(_Properties, "length", 0) : _Found->second.Length},
            {"rawStockLength", GetDouble(_Properties, "manufacturing.rawStockLength",
                GetDouble(_Properties, "tubeDesigner.assemblyProcessStockLength", GetDouble(_Properties, "length", 0)))},
            {"partMappingStatus", _Found == _Saved.end() ? std::string("planned") : _Found->second.Status},
            {"sourceMembers", std::move(_Sources)}, {"operations", std::move(_Operations)}, {"connections", VariantArray{}}});
    }
    return _Rows;
}

VariantArray ProductAssemblyMemberStockRefs(const VariantArray& Stocks_, const std::string& MemberKey_)
{
    VariantArray _Refs;
    for (const auto& _Value : Stocks_) {
        const auto _Stock = _Value.To<ObjectMap>();
        for (const auto& _SourceValue : _Stock.at("sourceMembers").To<VariantArray>()) {
            const auto _Source = _SourceValue.To<ObjectMap>();
            if (GetString(_Source, "itemKey") != MemberKey_) continue;
            ObjectMap _Reference{{"stockEntityId", _Stock.at("stockEntityId")}, {"itemKey", _Stock.at("itemKey")},
                {"name", _Stock.at("name")}, {"partLength", _Stock.at("partLength")},
                {"partMappingStatus", _Stock.at("partMappingStatus")}};
            if (const auto _Spans = _Source.find("spans"); _Spans != _Source.end())
                _Reference["spans"] = _Spans->second;
            else
                _Reference["mappingKind"] = GetRequiredText(_Source, "mappingKind");
            _Refs.emplace_back(std::move(_Reference));
        }
    }
    return _Refs;
}

iCAX::Interaction::CInvocationResult HandleGetProductAssemblyConnections(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext*,
    iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("产品装配关系需要有效的场景");
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _ProductID = ParseRequiredUuid(
        GetRequiredText(_Payload, "productEntityId"), "productEntityId");
    const auto _RequestedRunID = ParseRequiredUuid(
        GetRequiredText(_Payload, "generationRunId"), "generationRunId");
    auto& _Repository = Scene_->Database();
    const auto _Product = GetComponent<CProductInstanceComponent>(
        _Repository.GetEntity(_ProductID));
    if (!_Product) throw std::invalid_argument("产品实例不存在");
    if (_Product->GetActiveGenerationRunID() != _RequestedRunID)
        throw std::invalid_argument("装配关系请求引用了过期的产品生成版本");
    const auto _Run = GetComponent<CGenerationRunComponent>(
        _Repository.GetEntity(_RequestedRunID));
    if (!_Run || _Run->GetProductID() != _ProductID
        || _Run->GetStatus() != "Succeeded" || _Run->GetNeutralModel().empty())
        throw std::invalid_argument("产品当前生成版本没有有效的已提交模型");
    const auto _Model = ParseGenerationDisplay(*_Run);
    ValidateGenerationModelIdentity(_Model, *_Run);
    if (_Model.TemplateID != _Product->GetTemplateID()
        || _Model.TemplateVersion != _Product->GetTemplateVersion())
        throw std::runtime_error("产品与已提交模型的模板身份不一致");
    const auto* _DisplayOutput = FindOutputSet(_Model, "result");
    if (!_DisplayOutput || _DisplayOutput->ItemKeys.empty())
        throw std::runtime_error("已提交产品没有可验证的显示构件");
    const std::set<std::string> _DisplayedKeys(
        _DisplayOutput->ItemKeys.begin(), _DisplayOutput->ItemKeys.end());
    std::map<std::string, std::pair<iCAX::Data::uuid,
        std::shared_ptr<CAssemblyMemberComponent>>> _Members;
    for (const auto& [_Entity, _Member] : Collect<CAssemblyMemberComponent>(_Repository))
    {
        if (_Member->GetProductID() != _ProductID) continue;
        const auto& _Key = _Member->GetStableKey();
        if (_Key.empty() || !_DisplayedKeys.contains(_Key)
            || _Entity->GetID() != MakeStableEntityID(_ProductID, "member", _Key)
            || !_Members.emplace(_Key, std::make_pair(_Entity->GetID(), _Member)).second)
            throw std::runtime_error("产品构件身份与已提交模型不一致");
    }
    struct SPartIdentity final
    {
        iCAX::Data::uuid ID;
        iCAX::Data::uuid MemberID;
        std::string Presence;
    };
    std::map<std::string, SPartIdentity> _Parts;
    for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_Repository))
    {
        if (_Part->GetProductID() != _ProductID
            || _Part->GetGenerationRunID() != _RequestedRunID) continue;
        const auto& _Key = _Part->GetStableKey();
        if (_Key.empty() || !_Parts.emplace(_Key, SPartIdentity{
            _Entity->GetID(), _Part->GetSourceMemberID(), "persisted" }).second)
            throw std::runtime_error("当前拆单包含重复或无效的零件身份");
    }
    for (const auto& _Prepared : CopyTransientDisassembly(*Scene_))
    {
        if (_Prepared.ProductID != _ProductID
            || _Prepared.GenerationRunID != _RequestedRunID) continue;
        for (const auto& _Part : _Prepared.Parts)
        {
            if (_Part.StableKey.empty())
                throw std::runtime_error("当前拆单包含无效的零件身份");
            const auto _Existing = _Parts.find(_Part.StableKey);
            if (_Existing != _Parts.end())
            {
                if (_Existing->second.ID != _Part.PartID
                    || _Existing->second.MemberID != _Part.MemberID)
                    throw std::runtime_error("当前拆单的持久与临时零件身份冲突");
                continue;
            }
            _Parts.emplace(_Part.StableKey, SPartIdentity{
                _Part.PartID, _Part.MemberID, "transient" });
        }
    }
    std::optional<std::set<std::string>> _ManufacturingKeys;
    std::map<std::string, SAssemblyManufacturingSourceMapping> _SourceMappings;
    auto _ManufacturingDocument = !_Run->GetManufacturingModel().empty()
        ? _Run->GetManufacturingModel() : _Run->GetAssemblyManufacturingModel();
    std::string _PlanningError;
    // Geometry-node inspection never invokes manufacturing. A mapping is
    // available only after the user has committed a manufacturing snapshot.
    VariantArray _Stocks;
    if (!_ManufacturingDocument.empty())
    {
        const auto _ManufacturingModel =
            iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(
                Variant(_ManufacturingDocument));
        ValidateGenerationModelIdentity(_ManufacturingModel, *_Run);
        ValidateProductStockSourceMappings(_ManufacturingModel, _Model);
        const auto& _Output = ManufacturingOutput(_ManufacturingModel);
        _ManufacturingKeys.emplace(_Output.ItemKeys.begin(), _Output.ItemKeys.end());
        _SourceMappings = AssemblyManufacturingSourceMappings(
            _ManufacturingModel, _DisplayedKeys);
        _Stocks = ProductAssemblyStockRows(*Scene_, _ProductID, _RequestedRunID, _ManufacturingModel);
        for (const auto& [_Key, _Part] : _Parts)
            if (!_ManufacturingKeys->contains(_Key))
                throw std::runtime_error("当前拆单零件不属于已提交的制造输出: " + _Key);
    }

    auto _RelationshipRecords = _Model.Relationships;
    const auto _SharedDesign = _Run->GetNeutralModel();
    if (GetString(_SharedDesign, "schema") == "icax.display-model"
        && GetUInt64(_SharedDesign, "schemaVersion", 0) == iCAX::TemplateRuntime::kDisplayModelSchemaVersion)
    {
        auto _Declaration = _Run->GetManufacturingDefinition();
        if (_Declaration.empty())
        {
            auto _Parameters = ManufacturingParameterValues(*_Run, _ManufacturingDocument);
            _Parameters["templateId"] = _Run->GetTemplateID();
            _Parameters["templateVersion"] = _Run->GetTemplateVersion();
            const auto _Frozen = GenerationComponentSnapshots(*_Run);
            const auto _DesignParameters = GeneratedParameterValues(*_Run);
            // Read connection declarations without executing or committing any
            // machining, changing display data, or creating a stock snapshot.
            _Declaration = EvaluateNeutralTemplate(ApplicationContext_, _Parameters, "manufacturing",
                _Run->GetPackageDigest(), nullptr, &_Frozen, &_SharedDesign, &_DesignParameters, false).ScriptDocument;
        }
        if (GetUInt64(_Declaration, "schemaVersion", 0) == iCAX::TemplateRuntime::kManufacturingModelSchemaVersion)
        {
            const auto _Composed = iCAX::TemplateRuntime::CTemplateCodec::ComposeManufacturingModel(
                Variant(_Declaration), Variant(_SharedDesign));
            auto _ConnectionModel = iCAX::TemplateRuntime::CTemplateCodec::AdaptDisplayModel(Variant(_SharedDesign));
            _ConnectionModel["relationships"] = _Declaration.at("connections");
            _RelationshipRecords = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(Variant(_ConnectionModel)).Relationships;
        }
    }
    VariantArray _Connections;
    std::set<std::string> _RelationshipKeys;
    for (const auto& _Relationship : _RelationshipRecords)
    {
        if (_Relationship.Key.empty() || _Relationship.Kind.empty()
            || !_RelationshipKeys.insert(_Relationship.Key).second
            || _Relationship.ItemKeys.size() < 2)
            throw std::runtime_error("已提交产品包含无效的装配关系");
        VariantArray _Participants;
        std::set<std::string> _ParticipantKeys;
        for (const auto& _Key : _Relationship.ItemKeys)
        {
            const auto _Member = _Members.find(_Key);
            if (!_DisplayedKeys.contains(_Key) || _Member == _Members.end()
                || !_ParticipantKeys.insert(_Key).second)
                throw std::runtime_error("已提交装配关系引用不存在的当前产品构件: " + _Key);
            ObjectMap _Participant{
                {"itemKey", _Key},
                {"memberEntityId", UuidToString(_Member->second.first)},
                {"name", _Member->second.second->GetName()},
                {"role", _Member->second.second->GetRole()},
                {"manufacturingMappingStatus", std::string(
                    _ManufacturingKeys && !_SourceMappings.contains(_Key)
                        ? "not-output" : "not-present")}
            };
            const auto _Mapping = _SourceMappings.find(_Key);
            const auto _StockKey = _Mapping != _SourceMappings.end()
                ? _Mapping->second.StockKey : std::string{};
            if (_Mapping != _SourceMappings.end())
            {
                _Participant["stockKey"] = _StockKey;
                _Participant["manufacturingSpans"] = _Mapping->second.Spans;
            }
            if (const auto _Part = _Mapping == _SourceMappings.end()
                ? _Parts.end() : _Parts.find(_StockKey); _Part != _Parts.end())
            {
                _Participant["stockKey"] = _StockKey;
                if (!_Participant.contains("manufacturingSpans"))
                    _Participant["manufacturingSpans"] = VariantArray{};
                _Participant["manufacturingPartEntityId"] =
                    UuidToString(_Part->second.ID);
                _Participant["manufacturingMappingStatus"] = _Part->second.Presence;
            }
            const auto _Refs = ProductAssemblyMemberStockRefs(_Stocks, _Key);
            _Participant["stockRefs"] = _Refs;
            if (!_Refs.empty()) {
                const auto _Ref = _Refs.front().To<ObjectMap>();
                const auto _Stock = std::find_if(_Stocks.begin(), _Stocks.end(), [&](const auto& _Value) {
                    return GetString(_Value.template To<ObjectMap>(), "itemKey") == GetString(_Ref, "itemKey"); });
                const bool _Shared = _Stock != _Stocks.end()
                    && _Stock->To<ObjectMap>().at("sourceMembers").To<VariantArray>().size() > 1;
                _Participant["manufacturingMappingStatus"] = _Shared && !GetString(_Ref, "stockEntityId").empty()
                    ? std::string("shared-stock") : GetString(_Ref, "partMappingStatus");
            }
            _Participants.emplace_back(std::move(_Participant));
        }
        _Connections.emplace_back(ObjectMap{
            {"key", _Relationship.Key},
            {"kind", _Relationship.Kind},
            {"properties", _Relationship.Properties},
            {"nodeGeometry", AssemblyVerifyCommittedNodeGeometry(_Model,_Relationship)},
            {"origin", std::string("template-declared")},
            {"participants", std::move(_Participants)}
        });
    }
    const auto _RuntimeState = ProductRuntimeParameterState(
        ApplicationContext_, *_Product, _Run.get());
    return MakeResponse(Variant(ObjectMap{
        {"productEntityId", UuidToString(_ProductID)},
        {"generationRunId", UuidToString(_RequestedRunID)},
        {"templateId", _Run->GetTemplateID()},
        {"templateVersion", _Run->GetTemplateVersion()},
        {"packageDigest", _Run->GetPackageDigest()},
        {"modelOutdated", _RuntimeState.ModelOutdated},
        {"partsOutdated", _RuntimeState.PartsOutdated},
        {"source", std::string("committed-product-model")},
        {"manufacturingStocks", std::move(_Stocks)}, {"manufacturingPlanningError", _PlanningError},
        {"connections", std::move(_Connections)}
    }));
}
