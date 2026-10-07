// Assembly plans own independent nesting parts.  Only their raw source inputs
// are replayable: all stock geometry is prepared before the single DB commit.
struct SAssemblyProcessSavedPart
{
    std::shared_ptr<iCAX::Database::IEntity> Entity;
    std::shared_ptr<CManufacturingPartComponent> Part;
    ObjectMap Metadata;
};
using AssemblyProcessSavedParts = std::map<std::string, SAssemblyProcessSavedPart>;

std::map<std::string, AssemblyProcessSavedParts> ReadAssemblyProcessPlans(
    iCAX::Database::IRepository& Database_)
{
    std::map<std::string, AssemblyProcessSavedParts> _Plans;
    for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(Database_)) {
        const auto _Wizard = GetComponent<CPunchWizardComponent>(_Entity);
        if (!_Wizard) continue;
        const auto& _Definition = _Wizard->GetDefinition();
        const auto _It = _Definition.find("assemblyProcessPlan");
        if (_It == _Definition.end()) continue;
        if (!_It->second.Is<ObjectMap>())
            throw std::runtime_error("已保存的装配工艺来源格式无效");
        const auto _Metadata = _It->second.To<ObjectMap>();
        const auto _PlanID = GetRequiredText(_Metadata, "planId", 160);
        const auto _StockID = GetRequiredText(_Metadata, "stockId", 160);
        if (!IsIndependentNestingPart(*_Part)
            || !_Plans[_PlanID].emplace(_StockID, SAssemblyProcessSavedPart{
                _Entity, _Part, _Metadata}).second)
            throw std::runtime_error("已保存的装配工艺母材身份重复或无效");
    }
    return _Plans;
}

ObjectMap AssemblyProcessSavedPlan(const std::string& PlanID_, const AssemblyProcessSavedParts& Parts_)
{
    if (Parts_.empty()) throw std::runtime_error("装配工艺计划缺少母材");
    const auto& _Owner = Parts_.begin()->second;
    const auto _OwnerID = UuidToString(_Owner.Entity->GetID());
    const auto _Digest = GetRequiredText(_Owner.Metadata, "planDigest", 128);
    ObjectMap _IDs;
    for (const auto& [_StockID, _Saved] : Parts_) {
        if (GetString(_Saved.Metadata, "ownerId") != _OwnerID
            || GetString(_Saved.Metadata, "planDigest") != _Digest)
            throw std::runtime_error("已保存的装配工艺来源不完整，请检查下料零件");
        _IDs[_StockID] = UuidToString(_Saved.Entity->GetID());
    }
    return {{"planId", PlanID_}, {"planDigest", _Digest},
        {"sourceInput", GetRequiredObject(_Owner.Metadata, "sourceInput")},
        {"entityIds", _IDs}};
}

iCAX::Interaction::CInvocationResult HandleGetAssemblyProcessPlans(
    const iCAX::Interaction::CInvocation&,
    const iCAX::Application::IApplicationContext&,
    iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*,
    iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("装配工艺计划需要项目场景");
    VariantArray _Plans;
    for (const auto& [_ID, _Parts] : ReadAssemblyProcessPlans(Scene_->Database()))
        _Plans.emplace_back(AssemblyProcessSavedPlan(_ID, _Parts));
    return MakeResponse(ObjectMap{{"schema", std::string("icax.assembly-process-plans")},
        {"schemaVersion", 1ull}, {"plans", std::move(_Plans)}});
}

iCAX::Interaction::CInvocationResult HandleCommitAssemblyProcessPlan(
    const iCAX::Interaction::CInvocation& Request_,
    const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::IProjectContext*,
    iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_ || Request_.Payload.size() > 16 * 1024 * 1024)
        throw std::invalid_argument("保存装配工艺计划请求无效");
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _PlanID = GetRequiredText(_Payload, "planId", 160);
    const auto _RequestID = GetRequiredText(_Payload, "requestId", 160);
    const auto _Plan = ResolveAssemblyProcessPlanRequest(Request_, ApplicationContext_, ProductContext_);
    const auto _Digest = GetRequiredText(_Plan, "planDigest", 128);
    const auto _RequestDigest = ContentDigest(_Digest,
        iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Payload)));
    auto& _DB = Scene_->Database();
    const auto _Meta = _DB.GetMetaEntity();
    if (!_Meta) throw std::runtime_error("Missing project root");
    const auto _AllSaved = ReadAssemblyProcessPlans(_DB);
    AssemblyProcessSavedParts _Saved;
    if (const auto _Found = _AllSaved.find(_PlanID); _Found != _AllSaved.end())
        _Saved = _Found->second;
    VariantArray _Journal;
    if (!_Saved.empty()) {
        (void)AssemblyProcessSavedPlan(_PlanID, _Saved);
        const auto& _Owner = _Saved.begin()->second;
        const auto _ExistingDigest = GetString(_Owner.Metadata, "planDigest");
        const auto _ExpectedStored = GetString(_Payload, "expectedStoredPlanDigest");
        for (const auto& [_ID, _Value] : _Saved) {
            const auto _Ref = GetRequiredObject(_Value.Metadata, "currentGeometryRef");
            if (GetString(_Ref, "resourceId") != _Value.Part->GetManufacturingGeometryResourceID()
                || GetUInt64(_Ref, "resourceVersion", 0) != _Value.Part->GetManufacturingGeometryResourceVersion())
                throw std::invalid_argument("母材 " + _ID + " 已在下料向导中单独修改，不能直接覆盖；请先处理该修改");
        }
        if (const auto _It = _Owner.Metadata.find("requestJournal"); _It != _Owner.Metadata.end())
            _Journal = _It->second.To<VariantArray>();
        for (const auto& _Value : _Journal) {
            const auto _Entry = _Value.To<ObjectMap>();
            if (GetString(_Entry, "requestId") != _RequestID) continue;
            if (GetString(_Entry, "planDigest") != _Digest)
                throw std::invalid_argument("同一保存请求标识不能用于不同的装配工艺计划");
            if (GetString(_Entry, "requestDigest") != _RequestDigest)
                throw std::invalid_argument("同一保存请求标识不能用于不同的请求内容");
            return MakeResponse(ObjectMap{{"schema", std::string("icax.assembly-process-commit")},
                {"schemaVersion", 1ull}, {"planId", _PlanID}, {"planDigest", _Digest},
                {"entityIds", GetRequiredObject(_Entry, "entityIds")}, {"replayed", true},
                {"snapshot", BuildSnapshot(*Scene_, ApplicationContext_, false)}});
        }
        // A response may have been lost after the transaction succeeded. Its
        // identical request is acknowledged above before testing the old CAS.
        if (!_ExpectedStored.empty() && _ExpectedStored != _ExistingDigest)
            throw std::invalid_argument("已保存的装配工艺计划已改变，请重新载入");
    } else if (!GetString(_Payload, "expectedStoredPlanDigest").empty())
        throw std::invalid_argument("要更新的装配工艺计划已不存在");

    const auto _StocksIt = _Payload.find("stocks");
    if (_StocksIt == _Payload.end() || !_StocksIt->second.Is<VariantArray>())
        throw std::invalid_argument("装配工艺计划缺少母材");
    std::map<std::string, ObjectMap> _Stocks;
    for (const auto& _Value : _StocksIt->second.To<VariantArray>()) {
        const auto _Stock = _Value.To<ObjectMap>();
        if (!_Stocks.emplace(GetRequiredText(_Stock, "id", 160), _Stock).second)
            throw std::invalid_argument("母材标识重复");
    }
    const auto _Parts = _Plan.at("manufacturingParts").To<VariantArray>();
    if (_Stocks.empty() || _Stocks.size() > 1000 || _Parts.size() != _Stocks.size())
        throw std::invalid_argument("装配工艺计划须完整包含 1 至 1000 根母材");
    std::map<std::string, ObjectMap> _Requests;
    for (const auto& _Value : _Parts) {
        const auto _Part = _Value.To<ObjectMap>();
        const auto _ID = GetRequiredText(_Part, "blankId", 160);
        if (!_Stocks.contains(_ID) || !_Requests.emplace(_ID, _Part).second)
            throw std::runtime_error("装配下料请求引用未知或重复的母材");
    }
    const ObjectMap _Source{{"stocks", _Payload.at("stocks")},
        {"instances", _Payload.contains("instances") ? _Payload.at("instances") : Variant(VariantArray{})}};
    std::map<std::string, iCAX::Data::uuid> _EntityIDs;
    ObjectMap _EntityTexts;
    for (const auto& [_ID, _Stock] : _Stocks) {
        const auto _Found = _Saved.find(_ID);
        const auto _EntityID = _Found == _Saved.end() ? iCAX::Data::GenerateNewUUID() : _Found->second.Entity->GetID();
        _EntityIDs[_ID] = _EntityID;
        _EntityTexts[_ID] = UuidToString(_EntityID);
    }
    const auto _OwnerID = UuidToString(_EntityIDs.begin()->second);
    _Journal.emplace_back(ObjectMap{{"requestId", _RequestID}, {"planDigest", _Digest},
        {"requestDigest", _RequestDigest}, {"entityIds", _EntityTexts}});
    if (_Journal.size() > 32) _Journal.erase(_Journal.begin(), _Journal.end() - 32);
    const auto _BatchID = iCAX::Data::GenerateNewUUID();
    std::uint64_t _NextIndex = 1;
    for (const auto& [_Entity, _Part] : Collect<CManufacturingPartComponent>(_DB))
        if (IsIndependentNestingPart(*_Part) && _Part->GetPartIndex() >= _NextIndex
            && _Part->GetPartIndex() < (std::numeric_limits<std::uint64_t>::max)())
            _NextIndex = _Part->GetPartIndex() + 1;
    const auto _UserTools = ProductContext_
        ? ListPunchToolUserDataRecords(*GetUserDataStore(ProductContext_)) : VariantArray{};
    struct SPreparedStock {
        std::string ID;
        iCAX::Data::PropertySet Part, Profile;
        ObjectMap Wizard;
    };
    std::vector<SPreparedStock> _Prepared;
    for (const auto& [_StockID, _Planned] : _Requests) {
        try {
            auto _Manufacturing = GetRequiredObject(_Planned, "request");
            const auto _Length = GetDouble(_Manufacturing, "length", 0);
            if (!std::isfinite(_Length) || _Length < 1 || _Length > 100000)
                throw std::invalid_argument("母材长度须为 1 至 100000 mm");
            const auto _Profile = ResolvePunchBlankProfile(_Manufacturing, ApplicationContext_, ProductContext_);
            const auto _ResolveSection = [&](ObjectMap Item_) {
                if (!Item_.contains("section")) return Item_;
                auto _Section = GetRequiredObject(Item_, "section");
                if (!_Section.contains("profile") && _Section.contains("profileRef")) {
                    _Section["profile"] = ResolvePunchBlankProfile(_Section, ApplicationContext_, ProductContext_);
                    Item_["section"] = std::move(_Section);
                }
                return Item_;
            };
            if (_Manufacturing.contains("features")) {
                auto _Features = _Manufacturing.at("features").To<VariantArray>();
                for (auto& _Value : _Features) {
                    auto _Item = _Value.To<ObjectMap>();
                    if (_Item.contains("enabled") && _Item.at("enabled").Is<bool>() && !_Item.at("enabled").To<bool>()) continue;
                    _Value = _ResolveSection(std::move(_Item));
                }
                _Manufacturing["features"] = std::move(_Features);
            }
            if (_Manufacturing.contains("ends")) {
                auto _Ends = GetRequiredObject(_Manufacturing, "ends");
                for (auto& [_Key, _Value] : _Ends) {
                    const auto _Item = _Value.To<ObjectMap>();
                    if (GetString(_Item, "type", "keep") == "keep" && !_Item.contains("toolRef")) continue;
                    _Value = _ResolveSection(_Item);
                }
                _Manufacturing["ends"] = std::move(_Ends);
            }
            auto _Features = ReadPunchFeatures(_Manufacturing, "features");
            auto _Ends = ReadPunchEnds(_Manufacturing);
            const auto _Base = BuildPunchBlank(_Profile, _Length);
            SPunchPreviewDiagnostics _Diagnostics;
            auto _Shape = EvaluatePunch(_Base, _Profile, _Features, _Ends, Request_,
                ApplicationContext_, *Scene_, {}, false, nullptr, nullptr, &_Diagnostics, false, 0, _UserTools);
            const auto _StockToWorld = _Planned.contains("matrix") ? AssemblyPairRigidMatrix(_Planned,"matrix") : gp_Trsf();
            _Shape = ApplyAssemblyNeutralOperations(_Shape,_Manufacturing,_StockToWorld.Inverted());
            std::size_t _ProcessedSolids=0;
            for (TopExp_Explorer _It(_Shape,TopAbs_SOLID);_It.More();_It.Next()) ++_ProcessedSolids;
            if (_Shape.IsNull() || !_Diagnostics.ResultComputed || _ProcessedSolids != 1
                || !BRepCheck_Analyzer(_Shape).IsValid())
                throw std::runtime_error("加工须生成有效的单实体零件");
            const auto _EntityID = _EntityIDs.at(_StockID);
            const auto _Name = TrimText(GetString(_Stocks.at(_StockID), "name",
                GetString(_Stocks.at(_StockID), "label", _StockID)));
            if (_Name.empty() || _Name.size() > 160 || _Name.find('\0') != std::string::npos)
                throw std::invalid_argument("母材名称无效");
            const auto _Material = TrimText(GetString(_Stocks.at(_StockID), "material"));
            if (_Material.size() > 240 || _Material.find('\0') != std::string::npos)
                throw std::invalid_argument("材料名称无效");
            const auto _Quantity = ValidateInstanceQuantity(GetUInt64(_Stocks.at(_StockID), "quantity", 1));
            const auto _Prefix = "tube-designer/nesting/assembly-process/" + UuidToString(_BatchID) + "/" + UuidToString(_EntityID);
            const auto _Raw = StorePunchBRep(*Scene_, _Prefix + "/base", _Name + " 原始母材", _Base, Request_);
            const auto _Resource = StorePunchBRep(*Scene_, _Prefix + "/processed", _Name, _Shape, Request_);
            const auto _Mesh = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
                Scene_->Resources(), _Resource.URL, iCAX::Render::ERenderGeometryKind::Mesh);
            const auto _Envelope = GetDouble(ShapeBounds(_Shape), "width", 0);
            if (!std::isfinite(_Envelope) || _Envelope <= 0.02)
                throw std::invalid_argument("母材加工后的轴向包络长度无效");
            auto _Measurement = MeasureFinalPartGeometry(_Shape, _Resource.URL, _Resource.nVersion);
            _Measurement["nestingEnvelopeLength"] = _Envelope;
            _Measurement["normalizedAxis"] = std::string("+X");
            VariantArray _FeatureSnapshots, _InstanceIDs;
            for (const auto& _Feature : _Features) _FeatureSnapshots.emplace_back(PunchFeatureSnapshot(_Feature));
            for (const auto& _Value : _Source.at("instances").To<VariantArray>()) {
                const auto _Instance = _Value.To<ObjectMap>();
                bool _UsesStock = false;
                for (const auto& [_Role, _Target] : GetRequiredObject(_Instance, "targets"))
                    if ((_Target.Is<std::string>() && _Target.To<std::string>() == _StockID)
                        || (_Target.Is<ObjectMap>() && GetString(_Target.To<ObjectMap>(), "stockId") == _StockID))
                        _UsesStock = true;
                if (_UsesStock) _InstanceIDs.emplace_back(GetString(_Instance, "instanceId"));
            }
            ObjectMap _Metadata{{"schema", std::string("icax.assembly-process-source")}, {"schemaVersion", 1ull},
                {"planId", _PlanID}, {"stockId", _StockID}, {"ownerId", _OwnerID}, {"planDigest", _Digest},
                {"currentGeometryRef", ObjectMap{{"resourceId", _Resource.URL}, {"resourceVersion", _Resource.nVersion}}},
                {"instanceIds", _InstanceIDs}, {"operationIds", _Planned.at("operationIds")}};
            if (UuidToString(_EntityID) == _OwnerID) {
                _Metadata["sourceInput"] = _Source;
                _Metadata["requestJournal"] = _Journal;
            }
            ObjectMap _Wizard{{"schema", std::string("icax.tube-designer.punch-wizard")}, {"schemaVersion", 4ull},
                {"baseResourceId", _Raw.URL}, {"baseResourceVersion", _Raw.nVersion}, {"baseLength", _Length},
                {"baseBounds", ShapeBounds(_Base)},
                {"baseEndProcess", ObjectMap{{"startCut", std::string("square")}, {"endCut", std::string("square")}}},
                {"operations", PunchOperationChain(_Features, _Ends)}, {"ends", PunchEndsSnapshot(_Ends)},
                {"features", _FeatureSnapshots}, {"assemblyProcessPlan", _Metadata}};
            const auto _Reference = ProfileReferencePayload(ParseProfileReference(_Manufacturing));
            const auto _Existing = _Saved.find(_StockID);
            const auto _Index = _Existing == _Saved.end() ? _NextIndex++ : _Existing->second.Part->GetPartIndex();
            const auto _Number = _Existing == _Saved.end()
                ? MakeSafePathSegment(_Name + "-" + UuidToString(_EntityID).substr(0, 8), "process-stock")
                : _Existing->second.Part->GetPartNumber();
            bool _RequiresBending = false;
            for (const auto& _Value : _Plan.at("forming").To<VariantArray>()) {
                const auto _Forming = _Value.To<ObjectMap>();
                if (GetString(_Forming, "stockId") == _StockID) _RequiresBending = true;
            }
            ObjectMap _Properties{{"manufacturing.partKind", std::string("tube")},
                {"manufacturing.materialCategory", std::string("tube")}, {"manufacturing.sourcing", std::string("made")},
                {"manufacturing.process", std::string("assembly-process")}, {"manufacturing.requiresBending", _RequiresBending},
                {"manufacturing.material", _Material}, {"manufacturing.categoryName", std::string("装配工艺下料")},
                {"manufacturing.punchPart", true}, {"manufacturing.geometryMeasurement", _Measurement},
                {"nesting.snapshot", ObjectMap{{"name", _Name}, {"quantity", _Quantity},
                    {"source", std::string("assembly-process")}, {"profileRef", _Reference}, {"length", _Envelope}}}};
            iCAX::Data::PropertySet _PartProperties{
                {CManufacturingPartComponent::PropertyName_ProductID, PropertyValue(iCAX::Data::uuid())},
                {CManufacturingPartComponent::PropertyName_SourceMemberID, PropertyValue(iCAX::Data::uuid())},
                {CManufacturingPartComponent::PropertyName_GenerationRunID, PropertyValue(_BatchID)},
                {CManufacturingPartComponent::PropertyName_PartIndex, PropertyValue(_Index)},
                {CManufacturingPartComponent::PropertyName_StableKey, PropertyValue("assembly-process/" + UuidToString(_EntityID))},
                {CManufacturingPartComponent::PropertyName_PartNumber, PropertyValue(_Number)},
                {CManufacturingPartComponent::PropertyName_Name, PropertyValue(_Name)},
                {CManufacturingPartComponent::PropertyName_Role, PropertyValue(std::string("装配工艺下料"))},
                {CManufacturingPartComponent::PropertyName_Quantity, PropertyValue(_Quantity)},
                {CManufacturingPartComponent::PropertyName_QuantityOverride, PropertyValue(0ull)},
                {CManufacturingPartComponent::PropertyName_Length, PropertyValue(_Envelope)},
                {CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_Resource.URL)},
                {CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion, PropertyValue(_Resource.nVersion)},
                {CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID, PropertyValue(_Mesh.URL)},
                {CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion, PropertyValue(_Mesh.nVersion)},
                {CManufacturingPartComponent::PropertyName_FileName, PropertyValue(MakeStepFileName(_Number))},
                {CManufacturingPartComponent::PropertyName_Status, PropertyValue(std::string("Ready"))},
                {CManufacturingPartComponent::PropertyName_ItemProperties, PropertyValue(_Properties)}};
            _Prepared.push_back({_StockID, std::move(_PartProperties),
                TubeProfileComponentProperties(_Profile, "assembly-process"), std::move(_Wizard)});
        } catch (const std::exception& _Error) {
            throw std::runtime_error("母材 " + _StockID + " 保存预备失败：" + _Error.what());
        }
    }
    // Publishing resources above may leave harmless unreferenced versions on
    // failure.  No entity or nesting task is touched until all stocks succeed.
    auto _Task = GetComponent<CTubeDesignerRootComponent>(_Meta)
        ? GetComponent<CTubeDesignerRootComponent>(_Meta)->GetNestingTask() : ObjectMap{};
    VariantArray _References;
    if (const auto _It = _Task.find("parts"); _It != _Task.end() && _It->second.Is<VariantArray>())
        for (const auto& _Reference : _It->second.To<VariantArray>()) {
            if (!_Reference.Is<ObjectMap>()) throw std::runtime_error("既有下料选择格式无效");
            const auto _ID = GetString(_Reference.To<ObjectMap>(), "partEntityId");
            bool _Owned = false;
            for (const auto& [_StockID, _SavedPart] : _Saved)
                if (UuidToString(_SavedPart.Entity->GetID()) == _ID) _Owned = true;
            if (!_Owned) _References.emplace_back(_Reference);
        }
    for (const auto& [_ID, _Entity] : _EntityIDs)
        _References.emplace_back(ObjectMap{{"partEntityId", UuidToString(_Entity)}, {"generationRunId", UuidToString(_BatchID)}});
    _Task["parts"] = std::move(_References);
    _Task["request"] = ObjectMap{};
    _Task["result"] = ObjectMap{};
    _Task["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
    auto _Undo = _DB.BeginUndoCommand("Commit assembly process plan");
    auto& _Transaction = _DB.BeginTransaction("Commit assembly process plan");
    bool _Committing = false;
    try {
        for (const auto& _Part : _Prepared) {
            const auto _ID = _EntityIDs.at(_Part.ID);
            const auto _Found = _Saved.find(_Part.ID);
            const auto _Entity = _Found == _Saved.end() ? std::shared_ptr<iCAX::Database::IEntity>{} : _Found->second.Entity;
            if (!_Entity) _Transaction.CreateEntity(_ID);
            QueueUpsertComponent(_Transaction, _Entity, _ID, CManufacturingPartComponent::S_ClassName, _Part.Part);
            QueueUpsertComponent(_Transaction, _Entity, _ID, CTubeProfileComponent::S_ClassName, _Part.Profile);
            QueueUpsertComponent(_Transaction, _Entity, _ID, CPunchWizardComponent::S_ClassName,
                {{CPunchWizardComponent::PropertyName_Definition, PropertyValue(_Part.Wizard)}});
        }
        for (const auto& [_ID, _Part] : _Saved)
            if (!_EntityIDs.contains(_ID)) _Transaction.DisposeEntity(_Part.Entity->GetID());
        QueueUpsertComponent(_Transaction, _Meta, _Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
            {{CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task)}});
        std::string _Error;
        _Committing = true;
        if (!_DB.CommitTransaction(_Transaction, _Error))
            throw std::runtime_error(_Error.empty() ? "保存装配工艺计划失败" : _Error);
    } catch (...) {
        if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
        throw;
    }
    _Undo->End();
    SyncNestingPersistence(*Scene_);
    return MakeResponse(ObjectMap{{"schema", std::string("icax.assembly-process-commit")},
        {"schemaVersion", 1ull}, {"planId", _PlanID}, {"planDigest", _Digest},
        {"entityIds", _EntityTexts}, {"replayed", false},
        {"snapshot", BuildSnapshot(*Scene_, ApplicationContext_, false)}});
}
