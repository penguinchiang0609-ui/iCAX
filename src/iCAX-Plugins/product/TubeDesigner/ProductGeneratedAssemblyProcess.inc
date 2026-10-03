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
        || !CanonicalParameterMapsEqual(ProfileContoursPayload(Left_), ProfileContoursPayload(Right_)))
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
    BRepAlgoAPI_Common _Common(Stock_, _Box);
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
            BRepAlgoAPI_Common _Common(_FaceA, _FaceB);
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
    const SProductAssemblyProcessGeometry& Geometry_)
{
    VariantArray _Checks;
    const auto _Requirements = Plan_.find("contactRequirements");
    if (_Requirements == Plan_.end()) return _Checks;
    for (const auto& _Value : _Requirements->second.To<VariantArray>()) {
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
        std::array<VariantArray, 2> _Spans;
        std::array<TopoDS_Shape, 2> _Bodies;
        for (std::size_t _Index = 0; _Index < 2; ++_Index) {
            const auto _Role = _Roles[_Index].To<std::string>();
            const auto _StockID = GetRequiredText(_Stocks, _Role, 160);
            const auto _PartID = GetRequiredText(_Parts, _Role, 256);
            if (!Items_.contains(_StockID)) throw std::invalid_argument("产品工艺接触要求引用未知母材");
            const auto& _Item = *Items_.at(_StockID);
            _Bodies[_Index] = Geometry_.Shapes.at(_Item.Key);
            for (const auto& _Source : _Item.Properties.at("manufacturing.sourceMembers").To<VariantArray>())
                if (GetString(_Source.To<ObjectMap>(), "itemKey") == _PartID)
                    _Spans[_Index] = _Source.To<ObjectMap>().at("spans").To<VariantArray>();
            if (_Spans[_Index].empty()) throw std::invalid_argument("产品工艺接触要求缺少实际成品来源区间: " + _PartID);
        }
        double _Area = 0, _Volume = 0;
        for (const auto& _LeftSpan : _Spans[0]) {
            const auto _Left = _LeftSpan.To<ObjectMap>();
            const auto _LeftBody = ProductProcessSpanShape(_Bodies[0], _Left);
            const auto _WorldToLeft = ProductProcessSpanPlacement(_Left);
            for (const auto& _RightSpan : _Spans[1]) {
                const auto _Right = _RightSpan.To<ObjectMap>();
                auto _RightBody = ProductProcessSpanShape(_Bodies[1], _Right);
                const auto _RightToLeft = _WorldToLeft * ProductProcessSpanPlacement(_Right).Inverted();
                _RightBody = BRepBuilderAPI_Transform(_RightBody, _RightToLeft, true).Shape();
                BRepAlgoAPI_Common _Common(_LeftBody, _RightBody);
                if (!_Common.IsDone()) throw std::runtime_error("无法核查实际加工实体的节点穿透");
                if (!_Common.Shape().IsNull()) {
                    GProp_GProps _Properties; BRepGProp::VolumeProperties(_Common.Shape(), _Properties);
                    _Volume += std::abs(_Properties.Mass());
                }
                _Area += ProductProcessContactArea(_LeftBody, _RightBody);
            }
        }
        const auto _Instance = GetRequiredText(_Requirement, "instanceId", 160);
        if (!std::isfinite(_Area) || !std::isfinite(_Volume) || _Area <= 1.e-8
            || _Area + 1.e-5 < _MinimumArea || _Volume > _MaximumVolume + 1.e-7)
            throw std::runtime_error("工艺实例 " + _Instance + " 实际节点接触不适用：接触面积 "
                + std::to_string(_Area) + " mm²，要求至少 " + std::to_string(_MinimumArea)
                + " mm²；穿透体积 " + std::to_string(_Volume) + " mm³");
        _Checks.emplace_back(ObjectMap{{"instanceId", _Instance}, {"roles", _Roles}, {"status", std::string("pass")},
            {"contactAreaMm2", _Area}, {"intersectionVolumeMm3", _Volume}, {"scope", std::string("actual-processed-stock-source-spans")}});
    }
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
    const auto _Stocks = GetRequiredObject(_Native->second.To<ObjectMap>(), "stocks");
    for (const auto& [_ID, _Value] : _Stocks) {
        const auto _Stock = _Value.To<ObjectMap>();
        if (GetString(_Stock, "itemKey") == ItemKey_)
            return ProductProcessResourceShape(GetRequiredObject(_Stock, "processedGeometry"), Scene_);
    }
    return {};
}

SProductAssemblyProcessGeometry PrepareProductAssemblyProcessGeometry(
    const iCAX::TemplateRuntime::SNeutralModel& Model_, ObjectMap& Document_,
    const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* ProductContext_, iCAX::Project::ISceneContext& Scene_)
{
    SProductAssemblyProcessGeometry _Result;
    const auto _SourceIt = Model_.Extensions.find(kProductAssemblyProcessSource);
    if (_SourceIt == Model_.Extensions.end()) return _Result;
    const auto _Source = _SourceIt->second.To<ObjectMap>();
    if (GetString(_Source, "schema") != "icax.assembly-process-source"
        || GetUInt64(_Source, "schemaVersion", 0) != 1)
        throw std::invalid_argument("产品装配工艺源格式无效");
    const auto _SourceDigest = ContentDigest("product-process-source",
        iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Source)));
    const auto& _Output = ManufacturingOutput(Model_);
    std::map<std::string, const iCAX::TemplateRuntime::SModelItem*> _Items;
    for (const auto& _Key : _Output.ItemKeys) {
        const auto& _Item = FindModelItem(Model_, _Key);
        const auto _StockID = GetString(_Item.Properties, "tubeDesigner.assemblyProcessStockId");
        if (!_StockID.empty() && !_Items.emplace(_StockID, &_Item).second)
            throw std::invalid_argument("产品制造输出重复引用同一根工艺母材");
    }
    const auto _Stocks = _Source.at("stocks").To<VariantArray>();
    if (_Stocks.size() != _Items.size() || _Stocks.empty())
        throw std::invalid_argument("产品工艺母材必须与真实制造输出一一对应");
    if (const auto _Saved = Model_.Extensions.find(kProductAssemblyProcessNative);
        _Saved != Model_.Extensions.end()) {
        const auto _Native = _Saved->second.To<ObjectMap>();
        if (GetString(_Native, "schema") != "icax.product-assembly-process-native"
            || GetUInt64(_Native, "schemaVersion", 0) != 1
            || GetString(_Native, "sourceDigest") != _SourceDigest)
            throw std::runtime_error("已保存的产品工艺源与制造快照不一致");
        const auto _FrozenStocks = GetRequiredObject(_Native, "stocks");
        if (_FrozenStocks.size() != _Items.size())
            throw std::runtime_error("已保存的产品工艺制造快照数量不一致");
        for (const auto& [_ID, _Item] : _Items) {
            const auto _Stock = GetRequiredObject(_FrozenStocks, _ID);
            if (GetString(_Stock, "itemKey") != _Item->Key)
                throw std::runtime_error("已保存的产品工艺母材身份不一致");
            const auto _Reference = GetRequiredObject(_Stock, "processedGeometry");
            _Result.Shapes[_Item->Key] = ProductProcessResourceShape(_Reference, Scene_);
            _Result.Resources[_Item->Key] = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                GetRequiredText(_Reference, "url", 2048), GetUInt64(_Reference, "version", 0));
            _Result.Profiles[_Item->Key] = GetRequiredObject(_Stock, "profile");
        }
        return _Result;
    }
    std::map<std::string, ObjectMap> _Profiles;
    for (const auto& _Value : _Stocks) {
        const auto _Stock = _Value.To<ObjectMap>();
        const auto _ID = GetRequiredText(_Stock, "id", 160);
        if (!_Items.contains(_ID)) throw std::invalid_argument("产品工艺引用不存在的母材");
        const auto& _Properties = _Items.at(_ID)->Properties;
        const auto _Profile = GetRequiredObject(_Properties, _Properties.contains("tubeDesigner.assemblyProcessStockProfile")
            ? "tubeDesigner.assemblyProcessStockProfile" : "tubeDesigner.profile");
        const auto _Key = ProductProcessProfileKey(_Stock);
        if (const auto _Existing = _Profiles.find(_Key); _Existing != _Profiles.end()
            && !CanonicalParameterMapsEqual(_Existing->second, _Profile))
            throw std::invalid_argument("同一产品管型标识引用不同截面");
        _Profiles[_Key] = _Profile;
    }
    // A joint also observes receiving members that are not themselves the
    // allocated stock. These section snapshots come from the validated product
    // document, not from a standalone client-authored process-plan request.
    for (const auto& _Value : _Source.at("instances").To<VariantArray>()) {
        const auto _Input = GetRequiredObject(_Value.To<ObjectMap>(), "processInput");
        for (const auto& [_Role, _PartValue] : GetRequiredObject(_Input, "parts")) {
            const auto _Part = _PartValue.To<ObjectMap>();
            const auto _Key = ProductProcessProfileKey(_Part);
            const auto _Section = GetRequiredObject(_Part, "section");
            if (!_Section.contains("profile")) {
                if (!_Profiles.contains(_Key))
                    throw std::invalid_argument("产品工艺参与构件缺少真实截面: " + _Role);
                continue;
            }
            const auto _Profile = GetRequiredObject(_Section, "profile");
            if (const auto _Existing = _Profiles.find(_Key); _Existing != _Profiles.end()) {
                if (!ProductProcessProfilesMatch(_Existing->second, _Profile))
                    throw std::invalid_argument("同一产品工艺管型引用不同参与截面: " + _Role);
            } else _Profiles[_Key] = _Profile;
        }
    }
    auto _Payload = _Source;
    _Payload.erase("schema"); _Payload.erase("schemaVersion");
    auto _Request = iCAX::Interaction::CInvocation{};
    const auto _Text = iCAX::Data::VariantSerializer::Serialize(Variant(_Payload));
    _Request.Payload.assign(_Text.begin(), _Text.end());
    auto _Plan = ResolveAssemblyProcessPlanRequest(_Request, Application_, ProductContext_, &_Profiles);
    std::vector<std::string> _CutterRoots;
    for (const auto& [_ID, _Item] : _Items)
        if (const auto _Found = _Item->Properties.find("tubeDesigner.assemblyProcessSecondaryCutters");
            _Found != _Item->Properties.end())
            for (const auto& _Key : _Found->second.To<VariantArray>()) _CutterRoots.push_back(_Key.To<std::string>());
    const auto _Cutters = iCAX::OpenCascade::EvaluateNeutralModel(Model_, _CutterRoots);
    ObjectMap _FrozenStocks;
    const auto _Batch = UuidToString(iCAX::Data::GenerateNewUUID());
    const auto _UserTools = ProductContext_
        ? ListPunchToolUserDataRecords(*GetUserDataStore(ProductContext_)) : VariantArray{};
    for (const auto& _Value : _Plan.at("manufacturingParts").To<VariantArray>()) {
        const auto _Part = _Value.To<ObjectMap>();
        const auto _ID = GetRequiredText(_Part, "blankId", 160);
        if (!_Items.contains(_ID) || _FrozenStocks.contains(_ID))
            throw std::runtime_error("工艺函数返回重复或不存在的产品母材");
        const auto& _Item = *_Items.at(_ID);
        auto _Manufacturing = GetRequiredObject(_Part, "request");
        const auto _Length = GetDouble(_Manufacturing, "length", 0);
        if (std::abs(_Length - GetDouble(_Item.Properties, "tubeDesigner.assemblyProcessStockLength",
            GetDouble(_Item.Properties, "length", 0))) > 0.001)
            throw std::invalid_argument("产品工艺母材长度与制造输出不一致");
        const auto _Profile = _Profiles.at(ProductProcessProfileKey(_Manufacturing));
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
        const auto _Base = BuildPunchBlank(_Profile, _Length);
        auto _Shape = EvaluatePunch(_Base, _Profile, _Features, _Ends, _Request,
            Application_, Scene_, {}, false, nullptr, nullptr, nullptr, false,
            0, _UserTools);
        const auto _StockToWorld = _Part.contains("matrix") ? AssemblyPairRigidMatrix(_Part,"matrix") : gp_Trsf();
        _Shape = ApplyAssemblyNeutralOperations(_Shape,_Manufacturing,_StockToWorld.Inverted());
        if (const auto _Secondary = _Item.Properties.find("tubeDesigner.assemblyProcessSecondaryCutters");
            _Secondary != _Item.Properties.end() && !_Secondary->second.To<VariantArray>().empty()) {
            NCollection_List<TopoDS_Shape> _Bases, _Tools; _Bases.Append(_Shape);
            gp_Trsf _StockToWorld;
            if (_Part.contains("matrix")) _StockToWorld = AssemblyPairRigidMatrix(_Part, "matrix");
            const auto _WorldToStock = _StockToWorld.Inverted();
            for (const auto& _Key : _Secondary->second.To<VariantArray>())
                _Tools.Append(BRepBuilderAPI_Transform(_Cutters.At(_Key.To<std::string>()), _WorldToStock, true).Shape());
            BRepAlgoAPI_Cut _Cut; _Cut.SetArguments(_Bases); _Cut.SetTools(_Tools);
            _Cut.SetNonDestructive(true); _Cut.Build();
            if (!_Cut.IsDone() || _Cut.Shape().IsNull()) throw std::runtime_error("产品母材附加工序布尔切除失败");
            _Shape = _Cut.Shape();
        }
        if (_Shape.IsNull() || !BRepCheck_Analyzer(_Shape).IsValid() || ProductProcessSolidCount(_Shape) != 1)
            throw std::runtime_error("产品工艺没有生成有效的单实体母材: " + _ID);
        const auto _Prefix = "tube-designer/product-process/" + _Batch + "/" + std::to_string(_FrozenStocks.size());
        const auto _BaseRef = StorePunchBRep(Scene_, _Prefix + "/base", "产品原始母材", _Base, _Request);
        const auto _FinalRef = StorePunchBRep(Scene_, _Prefix + "/processed", "产品工艺制造件", _Shape, _Request);
        VariantArray _FeaturesSnapshot;
        for (const auto& _Feature : _Features) _FeaturesSnapshot.emplace_back(PunchFeatureSnapshot(_Feature));
        _Manufacturing["features"] = std::move(_FeaturesSnapshot);
        _Manufacturing["ends"] = PunchEndsSnapshot(_Ends);
        _FrozenStocks[_ID] = ObjectMap{{"itemKey", _Item.Key}, {"profile", _Profile},
            {"request", std::move(_Manufacturing)}, {"operationIds", _Part.at("operationIds")}, {"matrix", _Part.at("matrix")},
            {"baseGeometry", ObjectMap{{"url", _BaseRef.URL}, {"version", _BaseRef.nVersion}}},
            {"processedGeometry", ObjectMap{{"url", _FinalRef.URL}, {"version", _FinalRef.nVersion}}}};
        _Result.Shapes[_Item.Key] = std::move(_Shape);
        _Result.Profiles[_Item.Key] = _Profile;
        _Result.Resources[_Item.Key] = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
            _FinalRef.URL, _FinalRef.nVersion);
    }
    if (_FrozenStocks.size() != _Items.size()) throw std::runtime_error("工艺函数没有完整生成产品母材");
    _Plan["nativeContactChecks"] = ValidateProductProcessContacts(_Plan, _Items, _Result);
    auto _Extensions = GetRequiredObject(Document_, "extensions");
    _Extensions[kProductAssemblyProcessNative] = ObjectMap{{"schema", std::string("icax.product-assembly-process-native")},
        {"schemaVersion", 1ull}, {"sourceDigest", _SourceDigest}, {"plan", _Plan}, {"stocks", std::move(_FrozenStocks)}};
    Document_["extensions"] = std::move(_Extensions);
    return _Result;
}
