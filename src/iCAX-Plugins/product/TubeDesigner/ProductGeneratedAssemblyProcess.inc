// The manufacturing planner assigns logical members to physical stocks.
// The host executes the same local process functions used by the standalone
// editor, once per stock, and freezes the resulting resources in the run.
constexpr auto kProductAssemblyProcessSource = "tubeDesigner.assemblyProcessSource";
constexpr auto kProductAssemblyProcessNative = "tubeDesigner.assemblyProcessNative";

std::size_t ProductProcessSolidCount(const TopoDS_Shape& Shape_)
{
    std::size_t _Count = 0;
    for (TopExp_Explorer _It(Shape_, TopAbs_SOLID); _It.More(); _It.Next()) ++_Count;
    return _Count;
}

std::string ProductProcessProfileKey(const ObjectMap& Input_)
{
    return iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(
        Variant(AssemblyProcessProfileInput(Input_)));
}

bool ProductProcessProfilesMatch(const ObjectMap& Left_, const ObjectMap& Right_)
{
    // Allocated stock snapshots contain geometric data only, whereas observed
    // members also carry catalogue metadata. The exact contours define the
    // material; shell-derived dimensions can differ by floating-point rounding.
    if (!Left_.contains("contours") || !Right_.contains("contours")
        || (Left_.at("contours") != Right_.at("contours")
            && !CanonicalParameterMapsEqual(ProfileContoursPayload(Left_), ProfileContoursPayload(Right_))))
        return false;
    for (const auto* _Field : { "width", "depth", "wallThickness" }) {
        const auto _Left = GetDouble(Left_, _Field, 0);
        const auto _Right = GetDouble(Right_, _Field, 0);
        if (!std::isfinite(_Left) || !std::isfinite(_Right) || std::abs(_Left - _Right) > 1e-6)
            return false;
    }
    return true;
}

gp_Trsf ProductProcessSpanPlacement(const ObjectMap& Span_)
{
    const auto _Placement = GetRequiredObject(Span_, "placement");
    const auto _Origin = _Placement.at("origin").To<VariantArray>();
    const auto _X = _Placement.at("xAxis").To<VariantArray>();
    const auto _Y = _Placement.at("yAxis").To<VariantArray>();
    const auto _Z = _Placement.at("zAxis").To<VariantArray>();
    gp_Trsf _Result;
    _Result.SetValues(ToDouble(_X[0], "x"), ToDouble(_Y[0], "y"), ToDouble(_Z[0], "z"), ToDouble(_Origin[0], "origin"),
        ToDouble(_X[1], "x"), ToDouble(_Y[1], "y"), ToDouble(_Z[1], "z"), ToDouble(_Origin[1], "origin"),
        ToDouble(_X[2], "x"), ToDouble(_Y[2], "y"), ToDouble(_Z[2], "z"), ToDouble(_Origin[2], "origin"));
    return _Result;
}

TopoDS_Shape ProductProcessSpanShape(const TopoDS_Shape& Stock_, const ObjectMap& Span_)
{
    const auto _Start = Span_.at("start").To<VariantArray>(), _End = Span_.at("end").To<VariantArray>();
    const double _Length = gp_Vec(gp_Pnt(ToDouble(_Start[0], "start"), ToDouble(_Start[1], "start"), ToDouble(_Start[2], "start")),
        gp_Pnt(ToDouble(_End[0], "end"), ToDouble(_End[1], "end"), ToDouble(_End[2], "end"))).Magnitude();
    const double _Low = GetDouble(Span_, "stockStart", 0);
    const double _High = _Low + GetDouble(Span_, "startReserve", 0) + _Length + GetDouble(Span_, "endReserve", 0);
    const auto _Bounds = ShapeBounds(Stock_);
    const auto _Min = _Bounds.at("min").To<VariantArray>(), _Max = _Bounds.at("max").To<VariantArray>();
    const double _Y = ToDouble(_Min[1], "min.y") - 1, _Z = ToDouble(_Min[2], "min.z") - 1;
    const auto _Box = BRepPrimAPI_MakeBox(gp_Pnt(_Low, _Y, _Z), _High - _Low,
        ToDouble(_Max[1], "max.y") - _Y + 1, ToDouble(_Max[2], "max.z") - _Z + 1).Shape();
    BRepAlgoAPI_Common _Common;
    NCollection_List<TopoDS_Shape> _Arguments, _Tools;
    _Arguments.Append(Stock_); _Tools.Append(_Box);
    _Common.SetArguments(_Arguments); _Common.SetTools(_Tools);
    _Common.SetNonDestructive(true); _Common.Build();
    if (!_Common.IsDone() || _Common.Shape().IsNull()) throw std::runtime_error("无法裁剪实际加工母材的成品来源区间");
    return _Common.Shape();
}

double ProductProcessContactArea(const TopoDS_Shape& Left_, const TopoDS_Shape& Right_)
{
    double _Area = 0;
    for (TopExp_Explorer _A(Left_, TopAbs_FACE); _A.More(); _A.Next()) {
        const auto _FaceA = TopoDS::Face(_A.Current());
        const BRepAdaptor_Surface _SurfaceA(_FaceA);
        if (_SurfaceA.GetType() != GeomAbs_Plane) continue;
        const auto _PlaneA = _SurfaceA.Plane();
        for (TopExp_Explorer _B(Right_, TopAbs_FACE); _B.More(); _B.Next()) {
            const auto _FaceB = TopoDS::Face(_B.Current());
            const BRepAdaptor_Surface _SurfaceB(_FaceB);
            if (_SurfaceB.GetType() != GeomAbs_Plane) continue;
            const auto _PlaneB = _SurfaceB.Plane();
            if (std::abs(_PlaneA.Axis().Direction().Dot(_PlaneB.Axis().Direction())) < 1 - 1.e-7
                || _PlaneA.Distance(_PlaneB.Location()) > 1.e-5) continue;
            BRepAlgoAPI_Common _Common;
            NCollection_List<TopoDS_Shape> _Arguments, _Tools;
            _Arguments.Append(_FaceA); _Tools.Append(_FaceB);
            _Common.SetArguments(_Arguments); _Common.SetTools(_Tools);
            _Common.SetNonDestructive(true); _Common.Build();
            if (!_Common.IsDone()) throw std::runtime_error("无法核查实际加工实体的接触面");
            if (_Common.Shape().IsNull()) continue;
            GProp_GProps _Properties; BRepGProp::SurfaceProperties(_Common.Shape(), _Properties);
            _Area += _Properties.Mass();
        }
    }
    return _Area;
}

VariantArray ValidateProductProcessContacts(const ObjectMap& Plan_,
    const std::map<std::string, const iCAX::TemplateRuntime::SModelItem*>& Items_,
    const SProductAssemblyProcessGeometry& Geometry_,
    iCAX::TubeDesigner::CDisassemblyProgress* Progress_)
{
    VariantArray _Checks;
    const auto _Requirements = Plan_.find("contactRequirements");
    if (_Requirements == Plan_.end()) return _Checks;
    const auto _Contacts = _Requirements->second.To<VariantArray>();
    struct SContactInput final {
        ObjectMap Parts;
        VariantArray Roles;
        std::string Instance;
        std::array<VariantArray, 2> Spans;
        std::array<TopoDS_Shape, 2> Bodies;
        double MinimumArea = 0, MaximumVolume = 0, Area = 0, Volume = 0;
    };
    std::vector<SContactInput> _Inputs;
    _Inputs.reserve(_Contacts.size());
    for (const auto& _Value : _Contacts) {
        const auto _Requirement = _Value.To<ObjectMap>();
        if (GetString(_Requirement, "kind") != "positive-area-contact")
            throw std::invalid_argument("产品工艺接触校核类型尚未支持");
        const auto _Roles = _Requirement.at("roles").To<VariantArray>();
        if (_Roles.size() != 2) throw std::invalid_argument("产品工艺接触要求必须对应两件实际构件");
        const auto _Parts = GetRequiredObject(_Requirement, "partIds"), _Stocks = GetRequiredObject(_Requirement, "stockIds");
        const double _MinimumArea = GetDouble(_Requirement, "minimumContactAreaMm2", 0);
        const double _MaximumVolume = GetDouble(_Requirement, "maximumIntersectionVolumeMm3", 0);
        if (!std::isfinite(_MinimumArea) || _MinimumArea <= 0 || !std::isfinite(_MaximumVolume) || _MaximumVolume < 0)
            throw std::invalid_argument("产品工艺接触要求缺少有效面积和穿透容差");
        SContactInput _Input;
        _Input.Parts = _Parts; _Input.Roles = _Roles;
        _Input.Instance = GetRequiredText(_Requirement, "instanceId", 160);
        _Input.MinimumArea = _MinimumArea; _Input.MaximumVolume = _MaximumVolume;
        for (std::size_t _Index = 0; _Index < 2; ++_Index) {
            const auto _Role = _Roles[_Index].To<std::string>();
            const auto _StockID = GetRequiredText(_Stocks, _Role, 160);
            const auto _PartID = GetRequiredText(_Parts, _Role, 256);
            if (!Items_.contains(_StockID)) throw std::invalid_argument("产品工艺接触要求引用未知母材");
            const auto& _Item = *Items_.at(_StockID);
            // Each contact job owns its two actual processed stocks. No worker
            // reads a scene, a resource cache or topology used by another job.
            _Input.Bodies[_Index] = BRepBuilderAPI_Copy(Geometry_.Shapes.at(_Item.Key), true, false).Shape();
            for (const auto& _Source : _Item.Properties.at("manufacturing.sourceMembers").To<VariantArray>())
                if (GetString(_Source.To<ObjectMap>(), "itemKey") == _PartID)
                    _Input.Spans[_Index] = _Source.To<ObjectMap>().at("spans").To<VariantArray>();
            if (_Input.Spans[_Index].empty()) throw std::invalid_argument("产品工艺接触要求缺少实际成品来源区间: " + _PartID);
        }
        _Inputs.push_back(std::move(_Input));
    }
    const auto _MeasureContact = [](SContactInput& Input_) {
        for (const auto& _LeftSpan : Input_.Spans[0]) {
            const auto _Left = _LeftSpan.To<ObjectMap>();
            const auto _LeftBody = ProductProcessSpanShape(Input_.Bodies[0], _Left);
            const auto _WorldToLeft = ProductProcessSpanPlacement(_Left);
            for (const auto& _RightSpan : Input_.Spans[1]) {
                const auto _Right = _RightSpan.To<ObjectMap>();
                auto _RightBody = ProductProcessSpanShape(Input_.Bodies[1], _Right);
                const auto _RightToLeft = _WorldToLeft * ProductProcessSpanPlacement(_Right).Inverted();
                _RightBody = BRepBuilderAPI_Transform(_RightBody, _RightToLeft, true).Shape();
                BRepAlgoAPI_Common _Common;
                NCollection_List<TopoDS_Shape> _Arguments, _Tools;
                _Arguments.Append(_LeftBody); _Tools.Append(_RightBody);
                _Common.SetArguments(_Arguments); _Common.SetTools(_Tools);
                _Common.SetNonDestructive(true); _Common.Build();
                if (!_Common.IsDone()) throw std::runtime_error("无法核查实际加工实体的节点穿透");
                if (!_Common.Shape().IsNull()) {
                    GProp_GProps _Properties; BRepGProp::VolumeProperties(_Common.Shape(), _Properties);
                    Input_.Volume += std::abs(_Properties.Mass());
                }
                Input_.Area += ProductProcessContactArea(_LeftBody, _RightBody);
            }
        }
    };
    iCAX::OpenCascade::detail::SGeometryTaskBatch _Tasks;
    _Tasks.Tasks.reserve(_Inputs.size());
    const auto _Workers = AssemblyNativeGeometryConcurrency();
    std::size_t _Completed = 0;
    const auto _Complete = [&](std::size_t Index_) {
        _Tasks.Tasks[Index_].Result();
        const auto& _Input = _Inputs[Index_];
        const auto _Area = _Input.Area, _Volume = _Input.Volume;
        const auto _MinimumArea = _Input.MinimumArea, _MaximumVolume = _Input.MaximumVolume;
        const auto& _Instance = _Input.Instance;
        const auto& _Roles = _Input.Roles;
        const auto& _Parts = _Input.Parts;
        if (Progress_) Progress_->Set("validation", _Checks.size(), _Contacts.size(), "正在检查零件连接");
        if (!std::isfinite(_Area) || !std::isfinite(_Volume) || _Area <= 1.e-8
            || _Area + 1.e-5 < _MinimumArea || _Volume > _MaximumVolume + 1.e-7)
            throw std::runtime_error("工艺实例 " + _Instance + " 实际节点接触不适用 ["
                + _Roles[0].To<std::string>() + ": " + GetString(_Parts, _Roles[0].To<std::string>())
                + " / " + _Roles[1].To<std::string>() + ": " + GetString(_Parts, _Roles[1].To<std::string>())
                + "]：接触面积 "
                + std::to_string(_Area) + " mm²，要求至少 " + std::to_string(_MinimumArea)
                + " mm²；穿透体积 " + std::to_string(_Volume) + " mm³");
        _Checks.emplace_back(ObjectMap{{"instanceId", _Instance}, {"roles", _Roles}, {"status", std::string("pass")},
            {"contactAreaMm2", _Area}, {"intersectionVolumeMm3", _Volume}, {"scope", std::string("actual-processed-stock-source-spans")}});
    };
    for (auto& _Input : _Inputs) {
        if (_Tasks.Tasks.size() - _Completed >= _Workers) _Complete(_Completed++);
        _Tasks.Tasks.push_back(iCAX::Tasks::Run([&_Input, &_MeasureContact] {
            _MeasureContact(_Input);
        }, iCAX::OpenCascade::detail::GeometryTaskScheduler()));
    }
    while (_Completed < _Tasks.Tasks.size()) _Complete(_Completed++);
    if (Progress_) Progress_->Set("validation", _Checks.size(), _Contacts.size(), "零件连接检查完成");
    return _Checks;
}

TopoDS_Shape ProductProcessResourceShape(const ObjectMap& Reference_,
    iCAX::Project::ISceneContext& Scene_)
{
    const auto _Resource = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
        GetRequiredText(Reference_, "url", 2048), GetUInt64(Reference_, "version", 0));
    if (!_Resource) throw std::runtime_error("已保存的产品工艺实体资源缺失");
    const auto _Shape = iCAX::OpenCascade::BuildOpenCascadeShape(*_Resource);
    if (!_Shape.bOK || _Shape.Shape.IsNull() || !BRepCheck_Analyzer(_Shape.Shape).IsValid()
        || ProductProcessSolidCount(_Shape.Shape) != 1)
        throw std::runtime_error("已保存的产品工艺实体不是有效的单实体");
    return _Shape.Shape;
}

TopoDS_Shape RecoverProductAssemblyProcessShape(
    const iCAX::TemplateRuntime::SNeutralModel& Model_, const std::string& ItemKey_,
    iCAX::Project::ISceneContext& Scene_)
{
    const auto _Native = Model_.Extensions.find(kProductAssemblyProcessNative);
    if (_Native == Model_.Extensions.end()) return {};
    const auto& _Stocks = GetRequiredObjectView(std::get<ObjectMap>(_Native->second.m_Value), "stocks");
    for (const auto& [_ID, _Value] : _Stocks) {
        const auto& _Stock = std::get<ObjectMap>(_Value.m_Value);
        if (GetString(_Stock, "itemKey") == ItemKey_)
            return ProductProcessResourceShape(GetRequiredObjectView(_Stock, "processedGeometry"), Scene_);
    }
    return {};
}

// A feature ID labels one editable node; the tool runtime never receives it
// as geometry input. Keep every placement, array/skip rule, tool revision,
// parameter, receiving section and frozen resource in the key. Array-group
// IDs are deliberately retained because their skip rules do consume them.
void SetProductProcessFeatureID(SPunchFeature& Feature_, const std::string& ID_)
{
    Feature_.ID = ID_;
    if (const auto _It = Feature_.TemplateData.find("frozenTool");
        _It != Feature_.TemplateData.end()) {
        auto& _Frozen = std::get<ObjectMap>(_It->second.m_Value);
        if (_Frozen.contains("instance")) {
            auto& _Instance = std::get<ObjectMap>(_Frozen.at("instance").m_Value);
            _Instance["id"] = ID_;
        }
    }
}

ObjectMap ProductProcessLocalGeometryInput(const ObjectMap& Request_,
    const ObjectMap& Profile_, const std::vector<SPunchFeature>& Features_,
    const SPunchEnds& Ends_, const ObjectMap& Part_, const VariantArray& Secondary_,
    const std::string& UserTools_)
{
    auto _Input = Request_;
    VariantArray _Features;
    for (const auto& _Feature : Features_) _Features.emplace_back(PunchFeatureSnapshot(_Feature));
    _Input["features"] = std::move(_Features);
    _Input["ends"] = PunchEndsSnapshot(Ends_);
    _Input["profileSnapshot"] = Profile_;
    _Input["userTools"] = UserTools_;
    // Resolution has already produced the complete section snapshot. Its
    // catalogue/stock identifier is not consumed by any local CAD operation;
    // keep the reference contract but exclude that business identifier.
    if (const auto _Reference = _Input.find("profileRef"); _Reference != _Input.end()) {
        auto& _ResolvedReference = std::get<ObjectMap>(_Reference->second.m_Value);
        _ResolvedReference.erase("id");
    }
    bool _UsesWorldFrame = !Secondary_.empty();
    if (const auto _It = _Input.find("neutralOperations"); _It != _Input.end()) {
        auto& _Operations = std::get<VariantArray>(_It->second.m_Value);
        for (auto& _Value : _Operations) {
            auto& _Entry = std::get<ObjectMap>(_Value.m_Value);
            // ApplyAssemblyNeutralOperations consumes geometrySpace, the full
            // node graph and operation arguments. These three names only
            // describe the owner in the plan, never the local machining.
            _Entry.erase("instanceId"); _Entry.erase("operationId");
            auto& _Operation = std::get<ObjectMap>(_Entry.at("operation").m_Value);
            for (const auto* _Key : {"operationId", "instanceId", "stockId"}) _Operation.erase(_Key);
            _UsesWorldFrame = _UsesWorldFrame || GetString(_Entry, "geometrySpace", "document") == "document";
        }
    }
    // Document-space operations and secondary product cutters consume the
    // actual stock pose. Stock-local operations do not. Never merge differing
    // world-space requests just because their raw length and section match.
    if (_UsesWorldFrame) _Input["stockToWorld"] = Part_.at("matrix");
    if (!Secondary_.empty()) _Input["secondaryCutters"] = Secondary_;
    return _Input;
}

std::string ProductProcessManufacturingIdentity(const ObjectMap& LocalInput_,
    const ObjectMap& Properties_, const ObjectMap& Plan_, const std::string& StockID_)
{
    ObjectMap _Identity{{"localMachining", LocalInput_}};
    ObjectMap _Material;
    for (const auto& [_Key, _Value] : Properties_)
        if (_Key.starts_with("manufacturing.material") || _Key == "material"
            || _Key == "materialGrade" || _Key == "surfaceFinish" || _Key == "finish")
            _Material[_Key] = _Value;
    _Identity["material"] = std::move(_Material);
    VariantArray _Forming;
    if (const auto _It = Plan_.find("forming"); _It != Plan_.end())
        for (const auto& _Value : _It->second.To<VariantArray>()) {
            auto _Fold = _Value.To<ObjectMap>();
            if (GetString(_Fold, "stockId") != StockID_) continue;
            // Keep the complete forming recipe and its actual coordinate frame.
            // Merely sharing the unfolded cutter geometry proves no fold equivalence.
            _Fold.erase("stockId"); _Fold.erase("instanceId");
            _Forming.emplace_back(std::move(_Fold));
        }
    _Identity["forming"] = std::move(_Forming);
    return iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_Identity);
}

// Aggregation changes manufacturing entities, never the physical assembly
// inputs. Every subsequent execution restores these owned source objects before
// applying per-member drawings or assembly cuts and proving equivalence again.
bool RestoreIdenticalManufacturingSourceItems(ObjectMap& Document_)
{
    auto _Extensions = GetRequiredObject(Document_, "extensions");
    const auto _Found = _Extensions.find("tubeDesigner.identicalManufacturingSource");
    if (_Found == _Extensions.end()) return false;
    if (GetUInt64(_Extensions, "tubeDesigner.identicalManufacturingPartsVersion", 0) != 1)
        throw std::runtime_error("相同加工零件缺少有效的来源协议");
    const auto _Source = _Found->second.To<ObjectMap>();
    const auto _NativeBefore = GetRequiredObject(_Extensions, kProductAssemblyProcessNative);
    const auto _FrozenBefore = GetRequiredObject(_NativeBefore, "stocks");
    std::map<std::string, ObjectMap> _OriginalProperties;
    for (const auto& _Value : _Source.at("items").To<VariantArray>()) {
        const auto _Item = _Value.To<ObjectMap>(); const auto _Properties = GetRequiredObject(_Item, "properties");
        const auto _ID = GetString(_Properties, "tubeDesigner.assemblyProcessStockId");
        if (!_ID.empty() && !_OriginalProperties.emplace(_ID, _Properties).second)
            throw std::runtime_error("相同加工零件的来源包含重复母材");
    }
    std::set<std::string> _CoverageBefore;
    for (const auto& _Value : Document_.at("items").To<VariantArray>()) {
        const auto _Item = _Value.To<ObjectMap>(); const auto _Properties = GetRequiredObject(_Item, "properties");
        if (GetString(_Properties, "tubeDesigner.assemblyProcessStockId").empty()) continue;
        const auto _IDs = _Properties.at("manufacturing.sourceStockIds").To<VariantArray>();
        VariantArray _Sources; std::uint64_t _Quantity = 0;
        for (const auto& _IDValue : _IDs) {
            const auto _ID = _IDValue.To<std::string>();
            if (!_OriginalProperties.contains(_ID) || !_FrozenBefore.contains(_ID)
                || !_CoverageBefore.insert(_ID).second
                || GetString(_FrozenBefore.at(_ID).To<ObjectMap>(), "itemKey") != GetRequiredText(_Item, "key"))
                throw std::runtime_error("相同加工零件的来源母材映射无效");
            const auto& _Original = _OriginalProperties.at(_ID);
            const auto _Added = _Original.at("manufacturing.sourceMembers").To<VariantArray>();
            _Sources.insert(_Sources.end(), _Added.begin(), _Added.end());
            const auto _AddedQuantity = GetUInt64(_Original, "quantity", 1);
            if (_AddedQuantity == 0 || _AddedQuantity > 9007199254740991ull - _Quantity)
                throw std::runtime_error("相同加工零件的来源数量无效");
            _Quantity += _AddedQuantity;
        }
        if (_IDs.empty() || GetUInt64(_Properties, "quantity", 0) != _Quantity
            || _Properties.at("manufacturing.sourceMembers") != Variant(_Sources))
            throw std::runtime_error("相同加工零件的数量或构件来源与制造快照不一致");
    }
    if (_CoverageBefore.size() != _FrozenBefore.size() || _OriginalProperties.size() != _FrozenBefore.size())
        throw std::runtime_error("相同加工零件的制造快照未完整覆盖原始母材");
    for (const auto* _Key : {"items", "outputs"})
        Document_[_Key] = _Source.at(_Key);
    if (_Source.contains("roots")) Document_["roots"] = _Source.at("roots");
    else Document_.erase("roots");
    if (_Source.contains("tables")) Document_["tables"] = _Source.at("tables");
    else Document_.erase("tables");
    _Extensions["tubeDesigner.manufacturingPartCount"] = _Source.at("partCount");
    if (_Source.contains("assemblyOutputSets"))
        _Extensions["tubeDesigner.assemblyOutputSets"] = _Source.at("assemblyOutputSets");
    auto _Native = GetRequiredObject(_Extensions, kProductAssemblyProcessNative);
    auto _Stocks = GetRequiredObject(_Native, "stocks");
    std::set<std::string> _Restored;
    for (const auto& _Value : Document_.at("items").To<VariantArray>()) {
        const auto _Item = _Value.To<ObjectMap>();
        const auto _Properties = GetRequiredObject(_Item, "properties");
        const auto _ID = GetString(_Properties, "tubeDesigner.assemblyProcessStockId");
        if (_ID.empty()) continue;
        if (!_Stocks.contains(_ID) || !_Restored.insert(_ID).second)
            throw std::runtime_error("相同加工零件的原始母材映射无效");
        auto _Stock = _Stocks.at(_ID).To<ObjectMap>();
        _Stock["itemKey"] = _Item.at("key");
        _Stocks[_ID] = std::move(_Stock);
    }
    if (_Restored.size() != _Stocks.size())
        throw std::runtime_error("相同加工零件的原始母材映射不完整");
    _Native["stocks"] = std::move(_Stocks);
    _Extensions[kProductAssemblyProcessNative] = std::move(_Native);
    _Extensions.erase("tubeDesigner.identicalManufacturingSource");
    Document_["extensions"] = std::move(_Extensions);
    return true;
}

std::vector<std::size_t> MergeIdenticalManufacturingParts(
    SPreparedProductDisassembly& Prepared_, const SProductAssemblyProcessGeometry& Geometry_)
{
    const char* _Phase = "input";
    try {
    std::vector<std::size_t> _Indices(Prepared_.Parts.size());
    for (std::size_t _Index = 0; _Index < _Indices.size(); ++_Index) _Indices[_Index] = _Index;
    auto& _Document = Prepared_.ManufacturingModel;
    auto _Extensions = GetRequiredObject(_Document, "extensions");
    if (GetUInt64(_Extensions, "tubeDesigner.identicalManufacturingPartsVersion", 0) != 1)
        return _Indices;
    std::map<std::string, std::size_t> _ByIdentity;
    std::map<std::string, std::string> _Replacement;
    std::vector<SPreparedManufacturingPart> _Parts;
    _Parts.reserve(Prepared_.Parts.size());
    _Phase = "group-recipes";
    for (std::size_t _Index = 0; _Index < Prepared_.Parts.size(); ++_Index) {
        auto& _Part = Prepared_.Parts[_Index];
        const auto _Identity = Geometry_.ManufacturingIdentities.find(_Part.StableKey);
        // Per-member editing has its own geometry after the template recipe.
        // Keep it independent until a complete edited recipe proves sameness.
        const bool _Eligible = _Identity != Geometry_.ManufacturingIdentities.end()
            && !_Part.ItemProperties.contains("tubeDesigner.sideSketch");
        std::size_t _Target = _Parts.size();
        if (_Eligible) {
            const auto _Key = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
                {"recipe", _Identity->second}, {"profile", _Part.TubeProfile},
                {"length", _Part.Length}});
            const auto [_Found, _Inserted] = _ByIdentity.emplace(_Key, _Target);
            _Target = _Found->second;
        }
        _Indices[_Index] = _Target;
        if (_Target == _Parts.size()) {
            _Parts.push_back(std::move(_Part));
            auto& _Properties = _Parts.back().ItemProperties;
            if (_Properties.contains("tubeDesigner.assemblyProcessStockId"))
                _Properties["manufacturing.sourceStockIds"] = VariantArray{
                    GetRequiredText(_Properties, "tubeDesigner.assemblyProcessStockId")};
        } else {
            auto& _Canonical = _Parts.at(_Target);
            if (_Part.Quantity > 9007199254740991ull - _Canonical.Quantity)
                throw std::invalid_argument("相同加工零件合并数量超出有效范围");
            _Canonical.Quantity += _Part.Quantity;
            auto _Sources = _Canonical.ItemProperties.at("manufacturing.sourceMembers").To<VariantArray>();
            const auto _Added = _Part.ItemProperties.at("manufacturing.sourceMembers").To<VariantArray>();
            _Sources.insert(_Sources.end(), _Added.begin(), _Added.end());
            _Canonical.ItemProperties["manufacturing.sourceMembers"] = std::move(_Sources);
            auto _IDs = _Canonical.ItemProperties.at("manufacturing.sourceStockIds").To<VariantArray>();
            _IDs.emplace_back(GetRequiredText(_Part.ItemProperties, "tubeDesigner.assemblyProcessStockId"));
            _Canonical.ItemProperties["manufacturing.sourceStockIds"] = std::move(_IDs);
            _Canonical.ItemProperties["quantity"] = _Canonical.Quantity;
        }
    }
    // Moved strings are unspecified; derive all original keys from the model,
    // whose output order is exactly the prepared part order.
    _Phase = "source-model";
    const auto _OriginalModel = iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(_Document);
    const auto& _Output = ManufacturingOutput(_OriginalModel);
    if (_Output.ItemKeys.size() != _Indices.size())
        throw std::logic_error("相同加工零件的制造输出顺序不一致");
    _Replacement.clear();
    for (std::size_t _Index = 0; _Index < _Indices.size(); ++_Index)
        _Replacement[_Output.ItemKeys[_Index]] = _Parts[_Indices[_Index]].StableKey;
    _Phase = "source-snapshot";
    ObjectMap _Source{{"items", _Document.at("items")},
        {"outputs", _Document.at("outputs")},
        {"partCount", static_cast<unsigned long long>(_Indices.size())}};
    if (_Document.contains("roots")) _Source["roots"] = _Document.at("roots");
    if (_Document.contains("tables")) _Source["tables"] = _Document.at("tables");
    if (_Extensions.contains("tubeDesigner.assemblyOutputSets"))
        _Source["assemblyOutputSets"] = _Extensions.at("tubeDesigner.assemblyOutputSets");
    const auto _ReplaceKeys = [&](const VariantArray& Keys_) {
        VariantArray _Keys; std::set<std::string> _Seen;
        for (const auto& _Value : Keys_) {
            const auto _Key = _Value.To<std::string>();
            const auto _New = _Replacement.contains(_Key) ? _Replacement.at(_Key) : _Key;
            if (_Seen.insert(_New).second) _Keys.emplace_back(_New);
        }
        return _Keys;
    };
    std::map<std::string, const SPreparedManufacturingPart*> _ByKey;
    for (std::size_t _Index = 0; _Index < _Parts.size(); ++_Index) {
        _Parts[_Index].Index = _Index + 1;
        _ByKey.emplace(_Parts[_Index].StableKey, &_Parts[_Index]);
    }
    _Phase = "output-items";
    VariantArray _Items;
    for (const auto& _Value : _Document.at("items").To<VariantArray>()) {
        auto _Item = _Value.To<ObjectMap>(); const auto _Key = GetRequiredText(_Item, "key");
        if (_Replacement.contains(_Key) && _Replacement.at(_Key) != _Key) continue;
        if (_ByKey.contains(_Key)) {
            auto _Properties = GetRequiredObject(_Item, "properties");
            const auto& _Part = *_ByKey.at(_Key);
            _Properties["quantity"] = _Part.Quantity;
            for (const auto* _Name : {"manufacturing.sourceMembers", "manufacturing.sourceStockIds"})
                if (_Part.ItemProperties.contains(_Name)) _Properties[_Name] = _Part.ItemProperties.at(_Name);
            _Item["properties"] = std::move(_Properties);
        }
        if (_Item.contains("children")) _Item["children"] = _ReplaceKeys(_Item.at("children").To<VariantArray>());
        _Items.emplace_back(std::move(_Item));
    }
    _Document["items"] = std::move(_Items);
    if (_Document.contains("roots"))
        _Document["roots"] = _ReplaceKeys(_Document.at("roots").To<VariantArray>());
    _Phase = "output-sets";
    auto _Outputs = _Document.at("outputs").To<VariantArray>();
    for (auto& _Value : _Outputs) {
        auto _Set = _Value.To<ObjectMap>(); _Set["items"] = _ReplaceKeys(_Set.at("items").To<VariantArray>());
        _Value = std::move(_Set);
    }
    _Document["outputs"] = std::move(_Outputs);
    _Phase = "tables";
    if (_Document.contains("tables")) {
      auto _Tables = _Document.at("tables").To<VariantArray>();
      for (auto& _Value : _Tables) {
        auto _Table = _Value.To<ObjectMap>(); VariantArray _Rows;
        for (const auto& _RowValue : _Table.at("rows").To<VariantArray>()) {
            auto _Row = _RowValue.To<ObjectMap>(); const auto _Key = GetString(_Row, "itemKey");
            if (_Replacement.contains(_Key) && _Replacement.at(_Key) != _Key) continue;
            if (_ByKey.contains(_Key)) {
                auto _Values = GetRequiredObject(_Row, "values");
                _Values["quantity"] = _ByKey.at(_Key)->Quantity; _Row["values"] = std::move(_Values);
            }
            _Rows.emplace_back(std::move(_Row));
        }
        _Table["rows"] = std::move(_Rows); _Value = std::move(_Table);
      }
      _Document["tables"] = std::move(_Tables);
    }
    _Phase = "assembly-sets";
    if (_Extensions.contains("tubeDesigner.assemblyOutputSets")) {
        auto _Sets = _Extensions.at("tubeDesigner.assemblyOutputSets").To<VariantArray>();
        for (auto& _Value : _Sets) {
            auto _Set = _Value.To<ObjectMap>(); _Set["itemKeys"] = _ReplaceKeys(_Set.at("itemKeys").To<VariantArray>());
            _Value = std::move(_Set);
        }
        _Extensions["tubeDesigner.assemblyOutputSets"] = std::move(_Sets);
    }
    _Phase = "stock-coverage";
    auto _Native = GetRequiredObject(_Extensions, kProductAssemblyProcessNative);
    auto _Stocks = GetRequiredObject(_Native, "stocks");
    std::set<std::string> _Coverage;
    for (const auto& _Part : _Parts)
        if (const auto _It = _Part.ItemProperties.find("manufacturing.sourceStockIds"); _It != _Part.ItemProperties.end())
            for (const auto& _Value : _It->second.To<VariantArray>()) {
                const auto _ID = _Value.To<std::string>();
                if (!_Stocks.contains(_ID) || !_Coverage.insert(_ID).second)
                    throw std::logic_error("相同加工零件没有唯一覆盖原始工艺母材");
                auto _Stock = _Stocks.at(_ID).To<ObjectMap>(); _Stock["itemKey"] = _Part.StableKey;
                _Stocks[_ID] = std::move(_Stock);
            }
    if (_Coverage.size() != _Stocks.size()) throw std::logic_error("相同加工零件未完整覆盖原始工艺母材");
    _Native["stocks"] = std::move(_Stocks); _Extensions[kProductAssemblyProcessNative] = std::move(_Native);
    _Extensions["tubeDesigner.manufacturingPartCount"] = static_cast<unsigned long long>(_Parts.size());
    if (_Parts.size() != _Indices.size()) _Extensions["tubeDesigner.identicalManufacturingSource"] = std::move(_Source);
    _Document["extensions"] = std::move(_Extensions);
    Prepared_.Parts = std::move(_Parts);
    return _Indices;
    } catch (const std::exception& Error_) {
        throw std::runtime_error(std::string("相同加工零件/") + _Phase + ": " + Error_.what());
    }
}

ObjectMap* ProductProcessCutData(std::vector<SPunchFeature>& Features_,
    SPunchEnds& Ends_, const SPunchCut& Cut_)
{
    if (Cut_.Target == "end") return &(Cut_.Key == "start" ? Ends_.Start : Ends_.End).TemplateData;
    for (auto& _Feature : Features_) if (_Feature.ID == Cut_.Key) return &_Feature.TemplateData;
    throw std::logic_error("产品刀具节点记录不匹配");
}

SProductAssemblyProcessGeometry PrepareProductAssemblyProcessGeometry(
    const iCAX::TemplateRuntime::SNeutralModel& Model_, ObjectMap& Document_,
    const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::ISceneContext& Scene_,
    iCAX::TubeDesigner::CDisassemblyProgress* Progress_)
{
    const bool _ProfilePerformance = GetEnvironmentVariableA("ICAX_PROFILE_DISASSEMBLY", nullptr, 0) > 0;
    const auto _Started = std::chrono::steady_clock::now();
    const auto _Milliseconds = [](const auto& Start_) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start_).count();
    };
    ObjectMap _Performance;
    const auto _Accumulate = [&](const char* Key_, double Value_) {
        _Performance[Key_] = GetDouble(_Performance, Key_, 0) + Value_;
    };
    const auto _Mark = [&](const char* Phase_) {
        if (_ProfilePerformance) std::fprintf(stderr, "Disassembly/product-process/%s %.3f ms\n", Phase_, _Milliseconds(_Started));
    };
    SProductAssemblyProcessGeometry _Result;
    const auto _SourceIt = Model_.Extensions.find(kProductAssemblyProcessSource);
    if (_SourceIt == Model_.Extensions.end()) return _Result;
    const auto& _Source = std::get<ObjectMap>(_SourceIt->second.m_Value);
    if (GetString(_Source, "schema") != "icax.assembly-process-source"
        || GetUInt64(_Source, "schemaVersion", 0) != 1)
        throw std::invalid_argument("产品装配工艺源格式无效");
    const auto _SourceDigest = ContentDigest("product-process-source",
        iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_Source));
    const auto& _Output = ManufacturingOutput(Model_);
    const bool _MergeIdentical = GetUInt64(Model_.Extensions, "tubeDesigner.identicalManufacturingPartsVersion", 0) == 1;
    std::map<std::string, const iCAX::TemplateRuntime::SModelItem*> _Items;
    for (const auto& _Key : _Output.ItemKeys) {
        const auto& _Item = FindModelItem(Model_, _Key);
        const auto _StockID = GetString(_Item.Properties, "tubeDesigner.assemblyProcessStockId");
        if (!_StockID.empty() && !_Items.emplace(_StockID, &_Item).second)
            throw std::invalid_argument("产品制造输出重复引用同一根工艺母材");
    }
    const auto& _Stocks = std::get<VariantArray>(_Source.at("stocks").m_Value);
    if (_Stocks.size() != _Items.size() || _Stocks.empty())
        throw std::invalid_argument("产品工艺母材必须与真实制造输出一一对应");
    if (const auto _Saved = Model_.Extensions.find(kProductAssemblyProcessNative);
        _Saved != Model_.Extensions.end()) {
        const auto& _Native = std::get<ObjectMap>(_Saved->second.m_Value);
        if (GetString(_Native, "schema") != "icax.product-assembly-process-native"
            || GetUInt64(_Native, "schemaVersion", 0) != 1
            || GetString(_Native, "sourceDigest") != _SourceDigest)
            throw std::runtime_error("已保存的产品工艺源与制造快照不一致");
        const auto& _FrozenStocks = GetRequiredObjectView(_Native, "stocks");
        const auto& _FrozenPlan = GetRequiredObjectView(_Native, "plan");
        if (_FrozenStocks.size() != _Items.size())
            throw std::runtime_error("已保存的产品工艺制造快照数量不一致");
        for (const auto& [_ID, _Item] : _Items) {
            if (Progress_) Progress_->Set("geometry", _Result.Shapes.size(), _Items.size(), "正在生成零件形状");
            const auto& _Stock = GetRequiredObjectView(_FrozenStocks, _ID);
            if (GetString(_Stock, "itemKey") != _Item->Key)
                throw std::runtime_error("已保存的产品工艺母材身份不一致");
            const auto& _Reference = GetRequiredObjectView(_Stock, "processedGeometry");
            _Result.Shapes[_Item->Key] = ProductProcessResourceShape(_Reference, Scene_);
            _Result.Resources[_Item->Key] = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                GetRequiredText(_Reference, "url", 2048), GetUInt64(_Reference, "version", 0));
            _Result.Profiles[_Item->Key] = GetRequiredObject(_Stock, "profile");
            if (_MergeIdentical) {
                const auto _Request = GetRequiredObject(_Stock, "request");
                auto _Features = ReadPunchFeatures(_Request, "features");
                for (std::size_t _Index = 0; _Index < _Features.size(); ++_Index)
                    SetProductProcessFeatureID(_Features[_Index], "feature." + std::to_string(_Index));
                const auto _Secondary = _Item->Properties.at("tubeDesigner.assemblyProcessSecondaryCutters").To<VariantArray>();
                const auto _LocalInput = ProductProcessLocalGeometryInput(_Request,
                    _Result.Profiles.at(_Item->Key), _Features, ReadPunchEnds(_Request), _Stock,
                    _Secondary, GetRequiredText(_Native, "userToolsIdentity", 16 * 1024 * 1024));
                const auto _Identity = ProductProcessManufacturingIdentity(_LocalInput, _Item->Properties, _FrozenPlan, _ID);
                if (_Identity != GetRequiredText(_Stock, "manufacturingIdentity", 16 * 1024 * 1024))
                    throw std::runtime_error("已保存的相同加工零件配方与制造快照不一致");
                _Result.ManufacturingIdentities[_Item->Key] = _Identity;
            }
        }
        if (Progress_) Progress_->Set("geometry", _Result.Shapes.size(), _Items.size(), "零件形状生成完成");
        return _Result;
    }
    std::map<std::string, ObjectMap> _Profiles;
    for (const auto& _Value : _Stocks) {
        const auto& _Stock = std::get<ObjectMap>(_Value.m_Value);
        const auto _ID = GetRequiredText(_Stock, "id", 160);
        if (!_Items.contains(_ID)) throw std::invalid_argument("产品工艺引用不存在的母材");
        const auto& _Properties = _Items.at(_ID)->Properties;
        const auto& _Profile = GetRequiredObjectView(_Properties, _Properties.contains("tubeDesigner.assemblyProcessStockProfile")
            ? "tubeDesigner.assemblyProcessStockProfile" : "tubeDesigner.profile");
        const auto _Key = ProductProcessProfileKey(_Stock);
        if (const auto _Existing = _Profiles.find(_Key); _Existing != _Profiles.end()
            && !CanonicalParameterMapsEqual(_Existing->second, _Profile))
            throw std::invalid_argument("同一产品管型标识引用不同截面");
        _Profiles.try_emplace(_Key, _Profile);
    }
    // A joint also observes receiving members that are not themselves the
    // allocated stock. These section snapshots come from the validated product
    // document, not from a standalone client-authored process-plan request.
    for (const auto& _Value : std::get<VariantArray>(_Source.at("instances").m_Value)) {
        const auto& _Input = GetRequiredObjectView(std::get<ObjectMap>(_Value.m_Value), "processInput");
        for (const auto& [_Role, _PartValue] : GetRequiredObjectView(_Input, "parts")) {
            const auto& _Part = std::get<ObjectMap>(_PartValue.m_Value);
            const auto _Key = ProductProcessProfileKey(_Part);
            const auto& _Section = GetRequiredObjectView(_Part, "section");
            if (!_Section.contains("profile")) {
                if (!_Profiles.contains(_Key))
                    throw std::invalid_argument("产品工艺参与构件缺少真实截面: " + _Role);
                continue;
            }
            const auto& _Profile = GetRequiredObjectView(_Section, "profile");
            if (const auto _Existing = _Profiles.find(_Key); _Existing != _Profiles.end()) {
                if (!ProductProcessProfilesMatch(_Existing->second, _Profile))
                    throw std::invalid_argument("同一产品工艺管型引用不同参与截面: " + _Role);
            } else _Profiles[_Key] = _Profile;
        }
    }
    auto _Payload = _Source;
    const iCAX::Interaction::CInvocation _PunchRequest;
    _Mark("resolve-profiles");
    _Payload.erase("schema"); _Payload.erase("schemaVersion");
    const auto _PlanStart = std::chrono::steady_clock::now();
    _Mark("plan-begin");
    auto _Plan = ResolveAssemblyProcessPlanParameters(std::move(_Payload), Application_, ProductContext_, &_Profiles);
    _Accumulate("plan", _Milliseconds(_PlanStart));
    _Mark("plan-complete");
    std::vector<std::string> _CutterRoots;
    for (const auto& [_ID, _Item] : _Items)
        if (const auto _Found = _Item->Properties.find("tubeDesigner.assemblyProcessSecondaryCutters");
            _Found != _Item->Properties.end())
            for (const auto& _Key : _Found->second.To<VariantArray>()) _CutterRoots.push_back(_Key.To<std::string>());
    const auto _Cutters = iCAX::OpenCascade::EvaluateNeutralModel(Model_, _CutterRoots);
    _Mark("secondary-cutters");
    ObjectMap _FrozenStocks;
    const auto _Batch = UuidToString(iCAX::Data::GenerateNewUUID());
    const auto _UserTools = ProductContext_
        ? ListPunchToolUserDataRecords(*GetUserDataStore(ProductContext_)) : VariantArray{};
    const auto _UserToolsIdentity = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_UserTools);
    struct SLocalPrototype final {
        TopoDS_Shape Base, Shape;
        ObjectMap Request, Profile, PunchPerformance;
        gp_Trsf DocumentToStock;
        VariantArray Secondary;
        std::vector<SPunchFeature> Features;
        SPunchEnds Ends;
        std::vector<SPunchCut> Cuts;
        std::size_t BaseConversion = 0, FinalConversion = 0;
    };
    struct SStockResult final {
        std::string ID;
        ObjectMap Part, Request, Profile;
        std::vector<std::string> FeatureIDs;
        std::size_t Prototype = 0;
    };
    std::vector<SLocalPrototype> _Prototypes;
    std::map<std::string, std::size_t> _PrototypeByInput;
    std::map<std::string, TopoDS_Shape> _BaseByInput;
    std::vector<SStockResult> _StocksPrepared;
    std::set<std::string> _StockIDs;
    std::vector<iCAX::OpenCascade::SBRepConversionInput> _Conversions;
    std::size_t _ReuseCount = 0, _PlainCount = 0;
    std::ofstream _InputEvidence;
    if (_ProfilePerformance) {
        const auto _Directory = std::filesystem::current_path() / "output/tests/disassembly-performance";
        std::filesystem::create_directories(_Directory);
        _InputEvidence.open(_Directory / "local-inputs.jsonl");
    }
    _Mark("prototypes-begin");
    for (const auto& _Value : std::get<VariantArray>(_Plan.at("manufacturingParts").m_Value)) {
        if (Progress_) Progress_->Set("geometry", 0, _Items.size(), "正在准备零件形状");
        const auto& _Part = std::get<ObjectMap>(_Value.m_Value);
        const auto _ID = GetRequiredText(_Part, "blankId", 160);
        if (!_Items.contains(_ID) || !_StockIDs.insert(_ID).second)
            throw std::runtime_error("工艺函数返回重复或不存在的产品母材");
        const auto& _Item = *_Items.at(_ID);
        auto _Manufacturing = GetRequiredObject(_Part, "request");
        const auto _Length = GetDouble(_Manufacturing, "length", 0);
        if (std::abs(_Length - GetDouble(_Item.Properties, "tubeDesigner.assemblyProcessStockLength",
            GetDouble(_Item.Properties, "length", 0))) > 0.001)
            throw std::invalid_argument("产品工艺母材长度与制造输出不一致");
        const auto& _Profile = _Profiles.at(ProductProcessProfileKey(_Manufacturing));
        const auto _Section = [&](ObjectMap Item_) {
            if (!Item_.contains("section")) return Item_;
            auto _Section = GetRequiredObject(Item_, "section");
            if (_Section.contains("profileRef")) {
                const auto _Key = ProductProcessProfileKey(_Section);
                _Section["profile"] = _Profiles.contains(_Key) ? _Profiles.at(_Key)
                    : ResolvePunchBlankProfile(_Section, Application_, ProductContext_);
                Item_["section"] = std::move(_Section);
            }
            return Item_;
        };
        if (_Manufacturing.contains("features")) {
            auto _Features = _Manufacturing.at("features").To<VariantArray>();
            for (auto& _Feature : _Features) _Feature = _Section(_Feature.To<ObjectMap>());
            _Manufacturing["features"] = std::move(_Features);
        }
        if (_Manufacturing.contains("ends")) {
            auto _Ends = GetRequiredObject(_Manufacturing, "ends");
            for (auto& [_Key, _End] : _Ends) _End = _Section(_End.To<ObjectMap>());
            _Manufacturing["ends"] = std::move(_Ends);
        }
        auto _Features = ReadPunchFeatures(_Manufacturing, "features");
        auto _Ends = ReadPunchEnds(_Manufacturing);
        std::vector<std::string> _FeatureIDs;
        std::set<std::string> _UniqueFeatureIDs;
        for (std::size_t _Index = 0; _Index < _Features.size(); ++_Index) {
            if (!_UniqueFeatureIDs.insert(_Features[_Index].ID).second)
                throw std::invalid_argument("刀具节点 ID 不能重复");
            _FeatureIDs.push_back(_Features[_Index].ID);
            SetProductProcessFeatureID(_Features[_Index], "feature." + std::to_string(_Index));
        }
        const auto _SecondaryIt = _Item.Properties.find("tubeDesigner.assemblyProcessSecondaryCutters");
        const auto _Secondary = _SecondaryIt == _Item.Properties.end()
            ? VariantArray{} : _SecondaryIt->second.To<VariantArray>();
        const auto _LocalInput = ProductProcessLocalGeometryInput(_Manufacturing, _Profile,
            _Features, _Ends, _Part, _Secondary, _UserToolsIdentity);
        // Keep the exact canonical document as the key: a digest collision or
        // a length/profile-only match must never merge different machining.
        const auto _InputKey = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_LocalInput);
        if (_InputEvidence) _InputEvidence << _InputKey << '\n';
        auto _Found = _PrototypeByInput.find(_InputKey);
        std::size_t _PrototypeIndex;
        if (_Found != _PrototypeByInput.end()) {
            _PrototypeIndex = _Found->second;
            ++_ReuseCount;
        } else {
            _PrototypeIndex = _Prototypes.size();
            SLocalPrototype _Prototype;
            _Prototype.Features = std::move(_Features);
            _Prototype.Ends = std::move(_Ends);
            _Prototype.Request = _Manufacturing;
            _Prototype.Profile = _Profile;
            _Prototype.DocumentToStock = (_Part.contains("matrix") ? AssemblyPairRigidMatrix(_Part,"matrix") : gp_Trsf()).Inverted();
            _Prototype.Secondary = _Secondary;
            const auto _BlankStart = std::chrono::steady_clock::now();
            const auto _BaseKey = iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(ObjectMap{
                {"profile", _Profile}, {"length", _Length}});
            if (const auto _Base = _BaseByInput.find(_BaseKey); _Base != _BaseByInput.end())
                _Prototype.Base = _Base->second;
            else {
                _Prototype.Base = BuildPunchBlank(_Profile, _Length);
                _BaseByInput.emplace(_BaseKey, _Prototype.Base);
            }
            _Accumulate("blank", _Milliseconds(_BlankStart));
            const bool _PlainBlank = _Prototype.Features.empty()
                && _Prototype.Ends.Start.Type == "keep" && _Prototype.Ends.End.Type == "keep"
                && _Prototype.Ends.Start.TemplateData.empty() && _Prototype.Ends.End.TemplateData.empty();
            if (_PlainBlank) {
                _Prototype.Shape = _Prototype.Base;
                ++_PlainCount;
            }
            _Prototypes.push_back(std::move(_Prototype));
            _PrototypeByInput.emplace(_InputKey, _PrototypeIndex);
        }
        _StocksPrepared.push_back({_ID, _Part, std::move(_Manufacturing), _Profile,
            std::move(_FeatureIDs), _PrototypeIndex});
        _Result.Profiles[_Item.Key] = _Profile;
    }
    if (_StocksPrepared.size() != _Items.size()) throw std::runtime_error("工艺函数没有完整生成产品母材");
    if (_InputEvidence) _InputEvidence.flush();
    // The prototype vector is now stable. Prepare Python/tool/resource data
    // only on the caller, then run bounded jobs over private CAD inputs. Each
    // job owns one prototype's snapshots, cuts and timings; ordered completion
    // is the only point where results reach coordinator state.
    const auto _PunchStart = std::chrono::steady_clock::now();
    iCAX::OpenCascade::detail::SGeometryTaskBatch _PunchTasks;
    std::vector<std::size_t> _PunchIndices;
    _PunchTasks.Tasks.reserve(_Prototypes.size());
    _PunchIndices.reserve(_Prototypes.size());
    // Tool snapshots may evaluate their own neutral Boolean jobs on the shared
    // eight-thread CAD scheduler; leave capacity for those nested tasks even
    // when the diagnostic override requests eight workers.
    const auto _PunchWorkers = std::min<std::size_t>(4, AssemblyNativeGeometryConcurrency());
    std::size_t _PunchCompleted = 0;
    const auto _CompletePunch = [&](std::size_t Index_) {
        _PunchTasks.Tasks[Index_].Result();
        const auto& _Prototype = _Prototypes[_PunchIndices[Index_]];
        for (const auto& [_Key, _Value] : _Prototype.PunchPerformance)
            _Accumulate(_Key.c_str(), ToDouble(_Value, _Key));
    };
    for (std::size_t _Index = 0; _Index < _Prototypes.size(); ++_Index) {
        auto& _Prototype = _Prototypes[_Index];
        if (!_Prototype.Shape.IsNull()) continue;
        // Retain the synchronous path as a diagnostic material oracle.
        if (_PunchWorkers == 1) {
            _Prototype.Shape = EvaluatePunch(_Prototype.Base, _Prototype.Profile, _Prototype.Features,
                _Prototype.Ends, _PunchRequest, Application_, Scene_, {}, false, &_Prototype.Cuts,
                nullptr, nullptr, false, 0, _UserTools, EPunchEvaluationPurpose::ProductManufacturing,
                &_Prototype.PunchPerformance);
            for (const auto& [_Key, _Value] : _Prototype.PunchPerformance)
                _Accumulate(_Key.c_str(), ToDouble(_Value, _Key));
            continue;
        }
        if (_PunchTasks.Tasks.size() - _PunchCompleted >= _PunchWorkers)
            _CompletePunch(_PunchCompleted++);
        std::function<TopoDS_Shape()> _Geometry;
        EvaluatePunch(_Prototype.Base, _Prototype.Profile, _Prototype.Features,
            _Prototype.Ends, _PunchRequest, Application_, Scene_, {}, false, &_Prototype.Cuts,
            nullptr, nullptr, false, 0, _UserTools, EPunchEvaluationPurpose::ProductManufacturing,
            &_Prototype.PunchPerformance, &_Geometry);
        _PunchIndices.push_back(_Index);
        _PunchTasks.Tasks.push_back(iCAX::Tasks::Run([&_Prototype, _Geometry = std::move(_Geometry)] {
            _Prototype.Shape = _Geometry();
        }, iCAX::OpenCascade::detail::GeometryTaskScheduler()));
    }
    while (_PunchCompleted < _PunchTasks.Tasks.size()) _CompletePunch(_PunchCompleted++);
    _Accumulate("punchWall", _Milliseconds(_PunchStart));
    if (_ProfilePerformance) std::fprintf(stderr, "Disassembly/product-process/prototypes-complete stocks=%zu unique=%zu reused=%zu %.3f ms\n",
        _StocksPrepared.size(), _Prototypes.size(), _ReuseCount, _Milliseconds(_Started));
    // Reserve the exact old base/final/cutter resource order before any work
    // starts. Workers receive private geometry and never see scene resources.
    for (std::size_t _Index = 0; _Index < _Prototypes.size(); ++_Index) {
        auto& _Prototype = _Prototypes[_Index];
        const auto _Prefix = "tube-designer/product-process/" + _Batch + "/prototype/" + std::to_string(_Index);
        _Prototype.BaseConversion = _Conversions.size();
        _Conversions.push_back({_Prototype.Base, "产品原始母材", Scene_.Resources().MakeNamedResourceURL(_Prefix + "/base")});
        _Prototype.FinalConversion = _Conversions.size();
        _Conversions.push_back({{}, "产品工艺制造件", Scene_.Resources().MakeNamedResourceURL(_Prefix + "/processed")});
    }
    struct SCutConversion final { std::size_t Prototype, Cut, Conversion; };
    std::vector<SCutConversion> _CutConversions;
    for (std::size_t _Index = 0; _Index < _Prototypes.size(); ++_Index) {
        auto& _Prototype = _Prototypes[_Index];
        for (std::size_t _CutIndex = 0; _CutIndex < _Prototype.Cuts.size(); ++_CutIndex) {
            const auto& _Cut = _Prototype.Cuts[_CutIndex];
            const auto* _Data = ProductProcessCutData(_Prototype.Features, _Prototype.Ends, _Cut);
            if (_Data->contains("toolSnapshot") && _Data->contains("frozenCut")
                && GetString(GetRequiredObject(*_Data, "toolSnapshot"), "resolution") == "frozen") continue;
            const auto _URL = Scene_.Resources().MakeNamedResourceURL("tube-designer/product-process/" + _Batch
                + "/prototype/" + std::to_string(_Index) + "/cut/" + std::to_string(_CutIndex));
            _CutConversions.push_back({_Index, _CutIndex, _Conversions.size()});
            _Conversions.push_back({_Cut.Shape, GetString(*_Data, "toolLabel", "固化刀具"), _URL});
        }
    }
    // Diagnostic oracle only: explicitly compare the previous sequential
    // conversion path. Failure never retries and never publishes partial work.
    // Experimental overlap remains an explicit diagnostic until its timing
    // gate passes. Ordinary manufacturing uses the validated batch converter.
    const bool _PipelineConversions = GetEnvironmentVariableA("ICAX_PROFILE_DISASSEMBLY_PIPELINE", nullptr, 0) > 0
        && GetEnvironmentVariableA("ICAX_PROFILE_DISASSEMBLY_SEQUENTIAL", nullptr, 0) == 0;
    std::unique_ptr<iCAX::TubeDesigner::detail::CProductBRepConversionPipeline> _ConversionPipeline;
    const auto _PipelineStart = std::chrono::steady_clock::now();
    if (_PipelineConversions) {
        _ConversionPipeline = std::make_unique<iCAX::TubeDesigner::detail::CProductBRepConversionPipeline>(
            _Conversions.size(), 0.025);
        for (const auto& _Prototype : _Prototypes)
            _ConversionPipeline->Submit(_Prototype.BaseConversion, _Conversions[_Prototype.BaseConversion]);
        for (const auto& _Cut : _CutConversions)
            _ConversionPipeline->Submit(_Cut.Conversion, _Conversions[_Cut.Conversion]);
    }
    std::vector<bool> _FinalReady(_Prototypes.size(), false);
    const auto _FinalizePrototype = [&](std::size_t Index_, const TopoDS_Shape& Shape_) {
        if (_FinalReady.at(Index_)) throw std::logic_error("产品母材形状重复完成");
        auto& _Prototype = _Prototypes[Index_];
        _Prototype.Shape = Shape_;
        const auto _ValidationStart = std::chrono::steady_clock::now();
        if (!_Prototype.Secondary.empty()) {
            NCollection_List<TopoDS_Shape> _Bases, _Tools; _Bases.Append(_Prototype.Shape);
            for (const auto& _Key : _Prototype.Secondary)
                _Tools.Append(BRepBuilderAPI_Transform(_Cutters.At(_Key.To<std::string>()), _Prototype.DocumentToStock, true).Shape());
            BRepAlgoAPI_Cut _Cut; _Cut.SetArguments(_Bases); _Cut.SetTools(_Tools);
            _Cut.SetNonDestructive(true); _Cut.Build();
            if (!_Cut.IsDone() || _Cut.Shape().IsNull()) throw std::runtime_error("产品母材附加工序布尔切除失败");
            _Prototype.Shape = _Cut.Shape();
        }
        // The batch adapter already checked this exact final topology. A
        // secondary cut creates new topology and must be checked again.
        const bool _NeutralValidated = _Prototype.Secondary.empty()
            && _Prototype.Request.contains("neutralOperations")
            && !std::get<VariantArray>(_Prototype.Request.at("neutralOperations").m_Value).empty();
        if (_Prototype.Shape.IsNull() || (!_NeutralValidated && !BRepCheck_Analyzer(_Prototype.Shape).IsValid())
            || ProductProcessSolidCount(_Prototype.Shape) != 1)
            throw std::runtime_error("产品工艺没有生成有效的单实体母材");
        _Accumulate("solidValidation", _Milliseconds(_ValidationStart));
        _Conversions[_Prototype.FinalConversion].Shape = _Prototype.Shape;
        _FinalReady[Index_] = true;
        if (_ConversionPipeline)
            _ConversionPipeline->Submit(_Prototype.FinalConversion, _Conversions[_Prototype.FinalConversion]);
    };
    std::vector<SAssemblyNeutralOperationInput> _NeutralInputs;
    for (const auto& _Prototype : _Prototypes)
        _NeutralInputs.push_back({_Prototype.Shape, _Prototype.Request, _Prototype.DocumentToStock});
    if (Progress_) Progress_->Set("geometry", 0, _Items.size(), "正在计算零件形状");
    const auto _NeutralStart = std::chrono::steady_clock::now();
    _Mark("neutral-batch-begin");
    ObjectMap _NeutralPerformance;
    auto _NeutralShapes = ApplyAssemblyNeutralOperationsBatch(_NeutralInputs, &_NeutralPerformance, true,
        _PipelineConversions ? std::function<void(std::size_t, const TopoDS_Shape&)>(_FinalizePrototype)
            : std::function<void(std::size_t, const TopoDS_Shape&)>{});
    _Accumulate("neutral", _Milliseconds(_NeutralStart));
    for (const auto& [_Key, _Value] : _NeutralPerformance) _Performance[_Key] = _Value;
    _Mark("neutral-batch-complete");
    if (!_PipelineConversions)
        for (std::size_t _Index = 0; _Index < _Prototypes.size(); ++_Index)
            _FinalizePrototype(_Index, _NeutralShapes[_Index]);
    if (std::find(_FinalReady.begin(), _FinalReady.end(), false) != _FinalReady.end())
        throw std::logic_error("产品母材形状没有完整完成");
    for (const auto& _Stock : _StocksPrepared)
        _Result.Shapes[_Items.at(_Stock.ID)->Key] = _Prototypes[_Stock.Prototype].Shape;
    if (Progress_) Progress_->Set("geometry", _Items.size(), _Items.size(), "零件形状生成完成");
    const auto _ContactStart = std::chrono::steady_clock::now();
    _Mark("contacts-begin");
    _Plan["nativeContactChecks"] = ValidateProductProcessContacts(_Plan, _Items, _Result, Progress_);
    _Accumulate("contacts", _Milliseconds(_ContactStart));
    _Mark("contacts-complete");
    // The caller has finished every final CAD shape and contact check. Await the
    // one private conversion worker before publishing any geometry resources.
    if (Progress_) Progress_->Set("convert", 0, _Conversions.size(), "正在整理零件形状");
    const auto _ConvertStart = std::chrono::steady_clock::now();
    _Mark("convert-begin");
    auto _Converted = _ConversionPipeline ? _ConversionPipeline->Finish()
        : iCAX::OpenCascade::ConvertOpenCascadeShapesToBRep(
            _Conversions, 0.025, AssemblyNativeGeometryConcurrency());
    _Accumulate("convert", _Milliseconds(_ConvertStart));
    _Performance["pipelineWall"] = _Milliseconds(_PipelineStart);
    if (_ConversionPipeline) {
        _Performance["convertWait"] = _ConversionPipeline->FinishWaitMilliseconds();
        _Performance["convertWork"] = _ConversionPipeline->ConversionMilliseconds();
        _Performance["convertCopy"] = _ConversionPipeline->CopyMilliseconds();
        _Performance["convertBackpressure"] = _ConversionPipeline->BackpressureMilliseconds();
        _Performance["convertPeakQueued"] = static_cast<unsigned long long>(_ConversionPipeline->PeakQueued());
        _Performance["convertUnique"] = static_cast<unsigned long long>(_ConversionPipeline->UniqueInputs());
    }
    _Mark("convert-complete");
    if (Progress_) Progress_->Set("convert", _Conversions.size(), _Conversions.size(), "零件形状整理完成");
    const auto _StoreStart = std::chrono::steady_clock::now();
    std::vector<iCAX::Resource::CResourceReference> _References;
    _References.reserve(_Conversions.size());
    for (std::size_t _Index = 0; _Index < _Conversions.size(); ++_Index)
        _References.push_back(StorePreparedBRep(Scene_, _Conversions[_Index].SourceID,
            _Conversions[_Index].DisplayName, std::move(_Converted[_Index])));
    for (const auto& _Entry : _CutConversions) {
        auto& _Prototype = _Prototypes[_Entry.Prototype];
        const auto& _Cut = _Prototype.Cuts[_Entry.Cut];
        auto* _Data = ProductProcessCutData(_Prototype.Features, _Prototype.Ends, _Cut);
        const auto& _Reference = _References[_Entry.Conversion];
        (*_Data)["frozenCut"] = ObjectMap{{"url", _Reference.URL}, {"version", _Reference.nVersion},
            {"coordinateSpace", std::string("part")}, {"instanceCount", static_cast<unsigned long long>(_Cut.InstanceCount)}};
    }
    for (auto& _Stock : _StocksPrepared) {
        const auto& _Item = *_Items.at(_Stock.ID);
        const auto& _Prototype = _Prototypes[_Stock.Prototype];
        auto _Features = _Prototype.Features;
        VariantArray _Snapshots;
        for (std::size_t _Index = 0; _Index < _Features.size(); ++_Index) {
            SetProductProcessFeatureID(_Features[_Index], _Stock.FeatureIDs.at(_Index));
            _Snapshots.emplace_back(PunchFeatureSnapshot(_Features[_Index]));
        }
        _Stock.Request["features"] = std::move(_Snapshots);
        _Stock.Request["ends"] = PunchEndsSnapshot(_Prototype.Ends);
        std::string _ManufacturingIdentity;
        if (_MergeIdentical) {
            const auto _LocalInput = ProductProcessLocalGeometryInput(_Stock.Request, _Stock.Profile,
                _Prototype.Features, _Prototype.Ends, _Stock.Part, _Prototype.Secondary, _UserToolsIdentity);
            _ManufacturingIdentity = ProductProcessManufacturingIdentity(_LocalInput, _Item.Properties, _Plan, _Stock.ID);
            _Result.ManufacturingIdentities[_Item.Key] = _ManufacturingIdentity;
        }
        const auto& _BaseRef = _References[_Prototype.BaseConversion];
        const auto& _FinalRef = _References[_Prototype.FinalConversion];
        _FrozenStocks[_Stock.ID] = ObjectMap{{"itemKey", _Item.Key}, {"profile", _Stock.Profile},
            {"request", std::move(_Stock.Request)}, {"operationIds", _Stock.Part.at("operationIds")}, {"matrix", _Stock.Part.at("matrix")},
            {"baseGeometry", ObjectMap{{"url", _BaseRef.URL}, {"version", _BaseRef.nVersion}}},
            {"processedGeometry", ObjectMap{{"url", _FinalRef.URL}, {"version", _FinalRef.nVersion}}}};
        if (_MergeIdentical)
            std::get<ObjectMap>(_FrozenStocks.at(_Stock.ID).m_Value)["manufacturingIdentity"] = _ManufacturingIdentity;
        _Result.Resources[_Item.Key] = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(_FinalRef.URL, _FinalRef.nVersion);
    }
    _Accumulate("store", _Milliseconds(_StoreStart));
    _Mark("store-complete");
    auto _Extensions = GetRequiredObject(Document_, "extensions");
    _Extensions[kProductAssemblyProcessNative] = ObjectMap{{"schema", std::string("icax.product-assembly-process-native")},
        {"schemaVersion", 1ull}, {"sourceDigest", _SourceDigest}, {"plan", _Plan}, {"stocks", std::move(_FrozenStocks)}};
    if (_MergeIdentical)
        std::get<ObjectMap>(_Extensions.at(kProductAssemblyProcessNative).m_Value)["userToolsIdentity"] = _UserToolsIdentity;
    Document_["extensions"] = std::move(_Extensions);
    if (_ProfilePerformance) {
        _Performance["total"] = _Milliseconds(_Started);
        _Performance["stocks"] = static_cast<unsigned long long>(_StocksPrepared.size());
        _Performance["unique"] = static_cast<unsigned long long>(_Prototypes.size());
        _Performance["reused"] = static_cast<unsigned long long>(_ReuseCount);
        _Performance["plain"] = static_cast<unsigned long long>(_PlainCount);
        _Performance["conversions"] = static_cast<unsigned long long>(_Conversions.size());
        std::fprintf(stderr, "Disassembly/product-process %s\n",
            iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(_Performance).c_str());
    }
    return _Result;
}
