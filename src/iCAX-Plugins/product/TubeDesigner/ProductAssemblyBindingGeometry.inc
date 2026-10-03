// Included after the punch executor. Binding cutters are already positioned in
// the normalized manufacturing part's coordinates; replay never runs a tool
// script or reconstructs the original tube, so existing machining is retained.
std::string AssemblyBindingSourceDigest(const CProductInstanceComponent& Product_,
    const CGenerationRunComponent& Run_)
{
    const ObjectMap _Source{
        {"productEntityId", UuidToString(Product_.GetEntity()->GetID())},
        {"generationRunId", UuidToString(Run_.GetEntity()->GetID())},
        {"templateId", Run_.GetTemplateID()},
        {"templateVersion", Run_.GetTemplateVersion()},
        {"packageDigest", Run_.GetPackageDigest()},
        {"parameters", Product_.GetParameters()},
        {"sketches", Product_.GetSketches()}
    };
    return ContentDigest("product-assembly-source-v1",
        iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(_Source)));
}

std::string AssemblyBindingsRevision(const ObjectMap& Bindings_)
{
    return ContentDigest("product-assembly-bindings-v1",
        iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(Variant(Bindings_)));
}

VariantArray FrozenAssemblyCuts(const std::vector<SPunchFeature>& Features_,
    const SPunchEnds& Ends_)
{
    VariantArray _Cuts;
    const auto _Append = [&](const ObjectMap& Data_, const std::string& Target_,
        const std::string& Key_) {
        const auto _Found = Data_.find("frozenCut");
        if (_Found == Data_.end()) return;
        if (!_Found->second.Is<ObjectMap>())
            throw std::runtime_error("装配加工缺少有效的固化刀具");
        auto _Reference = _Found->second.To<ObjectMap>();
        if (GetString(_Reference, "url").empty()
            || GetUInt64(_Reference, "version", 0) == 0
            || GetString(_Reference, "coordinateSpace") != "part")
            throw std::runtime_error("装配加工的固化刀具位置或版本无效");
        _Reference["target"] = Target_;
        _Reference["key"] = Key_;
        _Cuts.emplace_back(std::move(_Reference));
    };
    for (const auto& _Feature : Features_)
        if (_Feature.Enabled) _Append(_Feature.TemplateData, "side", _Feature.ID);
    _Append(Ends_.Start.TemplateData, "end", "start");
    _Append(Ends_.End.TemplateData, "end", "end");
    if (_Cuts.empty())
        throw std::invalid_argument("该工艺没有可应用的实际切削加工");
    return _Cuts;
}

template<typename TAddReference>
void CollectAssemblyBindingCutReferences(const ObjectMap& Bindings_,
    const TAddReference& AddReference_)
{
    for (const auto& [_BindingID, _Value] : Bindings_)
    {
        if (!_Value.Is<ObjectMap>())
            throw std::runtime_error("已保存的装配工艺记录无效");
        const auto _Binding = _Value.To<ObjectMap>();
        const auto _Parts = _Binding.find("parts");
        if (_Parts == _Binding.end() || !_Parts->second.Is<VariantArray>())
            throw std::runtime_error("已保存的装配工艺缺少下料加工记录");
        for (const auto& _PartValue : _Parts->second.To<VariantArray>())
        {
            if (!_PartValue.Is<ObjectMap>())
                throw std::runtime_error("已保存的装配零件记录无效");
            const auto _Part = _PartValue.To<ObjectMap>();
            const auto _Cuts = _Part.find("frozenCuts");
            if (_Cuts == _Part.end() || !_Cuts->second.Is<VariantArray>())
                throw std::runtime_error("已保存的装配零件缺少固化刀具");
            for (const auto& _CutValue : _Cuts->second.To<VariantArray>())
            {
                if (!_CutValue.Is<ObjectMap>())
                    throw std::runtime_error("已保存的装配刀具记录无效");
                const auto _Cut = _CutValue.To<ObjectMap>();
                const auto _URL = GetString(_Cut, "url");
                const auto _Version = GetUInt64(_Cut, "version", 0);
                if (_URL.empty() || !_Version || GetString(_Cut,"coordinateSpace") != "part")
                    throw std::runtime_error("已保存的装配刀具位置或版本无效");
                AddReference_(_URL, _Version);
            }
        }
    }
}

void ValidateAssemblyBindingsSource(const ObjectMap& Bindings_,
    const CProductInstanceComponent& Product_, const CGenerationRunComponent& Run_)
{
    if (Bindings_.empty()) return;
    const auto _Digest = AssemblyBindingSourceDigest(Product_, Run_);
    for (const auto& [_BindingID, _Value] : Bindings_)
        if (!_Value.Is<ObjectMap>()
            || GetString(_Value.To<ObjectMap>(), "sourceDigest") != _Digest)
            throw std::invalid_argument("产品尺寸、草图或生成版本已变化，请重新检查或移除原装配加工");
}

TopoDS_Shape ApplyFrozenAssemblyBindings(iCAX::Project::ISceneContext& Scene_,
    const ObjectMap& Bindings_, const std::string& ItemKey_, const TopoDS_Shape& Base_)
{
    std::vector<SPunchFeature> _Frozen;
    // Validate all references up front, including records for other members.
    CollectAssemblyBindingCutReferences(Bindings_, [](const std::string&, std::uint64_t) {});
    for (const auto& [_BindingID, _Value] : Bindings_)
    {
        const auto _Binding = _Value.To<ObjectMap>();
        std::set<std::string> _Keys;
        for (const auto& _PartValue : _Binding.at("parts").To<VariantArray>())
        {
            const auto _Part = _PartValue.To<ObjectMap>();
            const auto _Key = GetString(_Part, "itemKey");
            if (_Key.empty() || !_Keys.insert(_Key).second)
                throw std::runtime_error("装配工艺的零件身份缺失或重复");
            if (_Key != ItemKey_) continue;
            for (const auto& _CutValue : _Part.at("frozenCuts").To<VariantArray>())
            {
                const auto _Cut = _CutValue.To<ObjectMap>();
                const auto _BRep = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(
                    GetString(_Cut, "url"), GetUInt64(_Cut, "version", 0));
                if (!_BRep)
                    throw std::runtime_error("装配加工的固化刀具资源缺失，无法恢复同一加工结果");
                const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(*_BRep);
                if (!_Rebuilt.bOK || _Rebuilt.Shape.IsNull())
                    throw std::runtime_error("装配加工的固化刀具无法恢复");
                SPunchFeature _Feature;
                _Feature.ID = _BindingID + ":" + std::to_string(_Frozen.size());
                _Feature.FrozenCut = _Rebuilt.Shape;
                // One immutable cutter may contain a complete hole array. It
                // is replayed once as a group, never positioned a second time.
                _Feature.FrozenCutInstanceCount = 1;
                _Frozen.push_back(std::move(_Feature));
            }
        }
    }
    if (_Frozen.empty()) return Base_;
    return iCAX::Tasks::Run([&] {
        return BuildPunchGeometry(Base_, _Frozen, SPunchEnds{});
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
}

// Recover the rigid coordinate relationship that disassembly used before any
// part-local cuts. A declared member frame alone is not proof: the candidate
// transform must reproduce the host's actual manufacturing normalization.
VariantArray AssemblyTransformMatrix(const gp_Trsf& Transform_)
{
    return VariantArray{
        Transform_.Value(1,1),Transform_.Value(1,2),Transform_.Value(1,3),Transform_.Value(1,4),
        Transform_.Value(2,1),Transform_.Value(2,2),Transform_.Value(2,3),Transform_.Value(2,4),
        Transform_.Value(3,1),Transform_.Value(3,2),Transform_.Value(3,3),Transform_.Value(3,4),
        0.0,0.0,0.0,1.0,
    };
}

std::pair<gp_Pnt,gp_Pnt> AssemblyMemberFrame(const ObjectMap& Properties_)
{
    const auto _Frame = GetRequiredObject(Properties_,"assemblyFrame.member");
    const auto _Point = [&_Frame](const char* Key_) {
        const auto _It = _Frame.find(Key_);
        if (_It == _Frame.end() || !_It->second.Is<VariantArray>())
            throw std::invalid_argument(std::string("构件未声明有效的 ") + Key_);
        const auto _Values = _It->second.To<VariantArray>();
        if (_Values.size() != 3)
            throw std::invalid_argument("构件中心线端点须有三个坐标");
        const double _X = ToDouble(_Values[0],Key_);
        const double _Y = ToDouble(_Values[1],Key_);
        const double _Z = ToDouble(_Values[2],Key_);
        if (!std::isfinite(_X) || !std::isfinite(_Y) || !std::isfinite(_Z))
            throw std::invalid_argument("构件中心线端点坐标不是有限数值");
        return gp_Pnt(_X,_Y,_Z);
    };
    return {_Point("start"),_Point("end")};
}

// Stations are measured along the directed, finished-product centerline. The
// member's BRep may extend beyond either logical node before its selected
// joint processes cut the actual blank.
struct SAssemblyMemberStockInterval final
{
    double StartStation = 0.0;
    double EndStation = 0.0;
    bool Declared = false;
};

SAssemblyMemberStockInterval AssemblyMemberStockInterval(
    const ObjectMap& Properties_, const double LogicalLength_)
{
    if (!std::isfinite(LogicalLength_) || LogicalLength_ <= 0.0)
        throw std::invalid_argument("构件逻辑中心线长度无效");
    const auto _Frame = GetRequiredObject(Properties_,"assemblyFrame.member");
    const auto _It = _Frame.find("stockInterval");
    if (_It == _Frame.end()) return {0.0,LogicalLength_,false};
    if (!_It->second.Is<ObjectMap>())
        throw std::invalid_argument("构件物理原管区间须为对象");
    const auto _Interval = _It->second.To<ObjectMap>();
    const auto _Number = [&_Interval](const char* Key_) {
        const auto _Field = _Interval.find(Key_);
        if (_Field == _Interval.end())
            throw std::invalid_argument(std::string("构件物理原管区间缺少 ") + Key_);
        const double _Value = ToDouble(_Field->second,Key_);
        if (!std::isfinite(_Value))
            throw std::invalid_argument(std::string("构件物理原管区间 ") + Key_ + " 不是有限数值");
        return _Value;
    };
    const double _Start = _Number("startStation");
    const double _End = _Number("endStation");
    constexpr double _StationTolerance = 1.0e-8;
    if (_Start > _StationTolerance || _End < LogicalLength_ - _StationTolerance
        || _End - _Start <= _StationTolerance)
        throw std::invalid_argument("物理原管区间没有覆盖完整的逻辑构件");
    return {_Start,_End,true};
}

// This geometry include precedes ProductAssemblyBindings.inc, so its array
// reads cannot depend on the endpoint helper declared in that later include.
VariantArray AssemblyGeometryArray(const ObjectMap& Object_, const char* Key_)
{
    const auto _It=Object_.find(Key_);
    if (_It==Object_.end() || !_It->second.Is<VariantArray>())
        throw std::invalid_argument(std::string("装配几何缺少有效的 ")+Key_);
    return _It->second.To<VariantArray>();
}

// A section boundary can have its own UV origin; its envelope need not be
// centred on that origin. Recover that datum only when it reproduces the exact
// committed, uncut physical blank and the finished-product member axis.
ObjectMap VerifiedAssemblySectionFrame(const TopoDS_Shape& Source_,
    const ObjectMap& ManufacturingProperties_, const ObjectMap& DisplayProperties_,
    const ObjectMap& Profile_)
{
    const auto _Read = [](const ObjectMap& Properties_) {
        const auto _Member = GetRequiredObject(Properties_,"assemblyFrame.member");
        const auto _Frame = GetRequiredObject(_Member,"sectionFrame");
        const auto _Coordinates = [&_Frame](const char* Key_, const std::size_t Count_) {
            const auto _It = _Frame.find(Key_);
            if (_It == _Frame.end() || !_It->second.Is<VariantArray>())
                throw std::invalid_argument(std::string("真实截面基准缺少 ") + Key_);
            const auto _Values = _It->second.To<VariantArray>();
            if (_Values.size() != Count_)
                throw std::invalid_argument(std::string("真实截面基准坐标数量无效: ") + Key_);
            std::vector<double> _Result;
            for (const auto& _Value : _Values)
            {
                const double _Number = ToDouble(_Value,Key_);
                if (!std::isfinite(_Number))
                    throw std::invalid_argument("真实截面基准包含非有限数值");
                _Result.push_back(_Number);
            }
            return _Result;
        };
        return std::array<std::vector<double>,4>{_Coordinates("originAtStart",3),
            _Coordinates("xAxis",3),_Coordinates("yAxis",3),
            _Coordinates("centerlineUV",2)};
    };
    const auto _Manufacturing = _Read(ManufacturingProperties_);
    const auto _Display = _Read(DisplayProperties_);
    for (std::size_t _Field=0; _Field<_Manufacturing.size(); ++_Field)
        for (std::size_t _Coordinate=0; _Coordinate<_Manufacturing[_Field].size(); ++_Coordinate)
            if (std::abs(_Manufacturing[_Field][_Coordinate]
                - _Display[_Field][_Coordinate]) > 1.0e-8)
                throw std::invalid_argument("成品与制造文档的真实截面基准不一致");
    const auto& _OriginValues = _Manufacturing[0];
    const auto& _XValues = _Manufacturing[1];
    const auto& _YValues = _Manufacturing[2];
    const auto& _Datum = _Manufacturing[3];
    const gp_Pnt _Origin(_OriginValues[0],_OriginValues[1],_OriginValues[2]);
    const gp_Vec _X(_XValues[0],_XValues[1],_XValues[2]);
    const gp_Vec _Y(_YValues[0],_YValues[1],_YValues[2]);
    if (std::abs(_X.Magnitude()-1.0)>1.0e-8
        || std::abs(_Y.Magnitude()-1.0)>1.0e-8
        || std::abs(_X.Dot(_Y))>1.0e-8)
        throw std::invalid_argument("真实截面方向不是正交单位坐标系");
    const gp_Vec _Z = _X.Crossed(_Y);
    const auto [_LogicalStart,_LogicalEnd] = AssemblyMemberFrame(ManufacturingProperties_);
    const gp_Vec _MemberAxis(_LogicalStart,_LogicalEnd);
    const double _LogicalLength = _MemberAxis.Magnitude();
    if (_LogicalLength <= 0.05 || _Z.Dot(_MemberAxis)/_LogicalLength < 1.0-1.0e-8)
        throw std::invalid_argument("真实截面法向与构件逻辑轴向不一致");
    if (_Origin.Translated(_X*_Datum[0]+_Y*_Datum[1]).Distance(_LogicalStart) > 0.05)
        throw std::invalid_argument("真实截面基准与产品逻辑中心线不一致");
    const auto _Stock = AssemblyMemberStockInterval(ManufacturingProperties_,_LogicalLength);
    const double _StockLength = _Stock.EndStation-_Stock.StartStation;
    const auto _StockOrigin = _Origin.Translated(_Z*_Stock.StartStation);
    gp_Trsf _Placement;
    _Placement.SetValues(_X.X(),_Y.X(),_Z.X(),_StockOrigin.X(),
        _X.Y(),_Y.Y(),_Z.Y(),_StockOrigin.Y(),
        _X.Z(),_Y.Z(),_Z.Z(),_StockOrigin.Z());
    const auto _Replayed = BRepBuilderAPI_Transform(
        BuildProfileExtrusion(Profile_,_StockLength),_Placement,true).Shape();
    if (DrawingReplayError(Source_,_Replayed)>1.0e-6)
        throw std::invalid_argument("截面基准无法复现已提交的真实物理原管");
    const auto _Contours = AssemblyGeometryArray(Profile_,"contours");
    if (_Contours.empty() || _Contours.size()>64)
        throw std::invalid_argument("提供截面的轮廓数量无效");
    std::vector<TopoDS_Shape> _LoopSolids;
    std::vector<double> _LoopAreas;
    for (const auto& _Contour : _Contours)
    {
        const auto _Loop = BuildProfileExtrusion(
            ObjectMap{{"contours",VariantArray{_Contour}}},1.0);
        const double _Area = ShapeMaterialVolume(_Loop);
        if (_Area<=1.0e-8)
            throw std::invalid_argument("提供截面包含无效的闭合轮廓");
        _LoopSolids.push_back(_Loop);
        _LoopAreas.push_back(_Area);
    }
    const auto _Largest = std::max_element(_LoopAreas.begin(),_LoopAreas.end());
    const auto _OuterIndex = static_cast<std::size_t>(
        std::distance(_LoopAreas.begin(),_Largest));
    for (std::size_t _Index=0; _Index<_LoopSolids.size(); ++_Index)
    {
        if (_Index==_OuterIndex) continue;
        BRepAlgoAPI_Cut _Outside(_LoopSolids[_Index],_LoopSolids[_OuterIndex]);
        _Outside.SetFuzzyValue(1.0e-6);
        _Outside.Build();
        if (!_Outside.IsDone() || _Outside.Shape().IsNull()
            || ShapeMaterialVolume(_Outside.Shape())>1.0e-6)
            throw std::invalid_argument("提供截面不是单一外轮廓及其内部孔洞");
    }
    ObjectMap _Result{{"centerlineUV",VariantArray{_Datum[0],_Datum[1]}},
        {"xAxisWorld",VariantArray{_X.X(),_X.Y(),_X.Z()}},
        {"yAxisWorld",VariantArray{_Y.X(),_Y.Y(),_Y.Z()}},
        {"outerContourIndex",static_cast<std::uint64_t>(_OuterIndex)},
        {"verification",std::string("committed-source-brep-replay")}};
    const auto _MemberFrame=GetRequiredObject(ManufacturingProperties_,"assemblyFrame.member");
    const auto _DisplayMemberFrame=GetRequiredObject(DisplayProperties_,"assemblyFrame.member");
    const auto _SourceSection=GetRequiredObject(_MemberFrame,"sectionFrame");
    const auto _DisplaySection=GetRequiredObject(_DisplayMemberFrame,"sectionFrame");
    const bool _HasFaceNormals=_SourceSection.contains("faceNormals");
    if (_HasFaceNormals!=_DisplaySection.contains("faceNormals"))
        throw std::invalid_argument("成品与制造文档的产品连接面方向声明不一致");
    if (_HasFaceNormals)
    {
        const auto _SourceFaces=GetRequiredObject(_SourceSection,"faceNormals");
        const auto _DisplayFaces=GetRequiredObject(_DisplaySection,"faceNormals");
        if (_SourceFaces.size()!=4 || _DisplayFaces.size()!=4)
            throw std::invalid_argument("产品连接面须声明四个正交方向");
        ObjectMap _VerifiedFaces;
        std::map<std::string,gp_Vec> _Vectors;
        for (const char* _Name : {"top","bottom","left","right"})
        {
            const auto _SourceValues=AssemblyGeometryArray(_SourceFaces,_Name);
            const auto _DisplayValues=AssemblyGeometryArray(_DisplayFaces,_Name);
            if (_SourceValues.size()!=3 || _DisplayValues.size()!=3)
                throw std::invalid_argument("产品连接面方向坐标无效");
            VariantArray _Saved;
            double _Coordinates[3];
            for (std::size_t _Index=0; _Index<3; ++_Index)
            {
                const double _Value=ToDouble(_SourceValues[_Index],_Name);
                const double _Shown=ToDouble(_DisplayValues[_Index],_Name);
                if (!std::isfinite(_Value) || !std::isfinite(_Shown)
                    || std::abs(_Value-_Shown)>1.0e-8)
                    throw std::invalid_argument("成品与制造文档的产品连接面方向不一致");
                _Coordinates[_Index]=_Value;
                _Saved.emplace_back(_Value);
            }
            const gp_Vec _Normal(_Coordinates[0],_Coordinates[1],_Coordinates[2]);
            if (std::abs(_Normal.Magnitude()-1.0)>1.0e-8
                || std::abs(_Normal.Dot(_MemberAxis))/_LogicalLength>1.0e-8)
                throw std::invalid_argument("产品连接面方向不是垂直构件轴线的单位向量");
            _Vectors.emplace(_Name,_Normal);
            _VerifiedFaces[_Name]=std::move(_Saved);
        }
        if ((_Vectors.at("top")+_Vectors.at("bottom")).Magnitude()>1.0e-8
            || (_Vectors.at("left")+_Vectors.at("right")).Magnitude()>1.0e-8
            || std::abs(_Vectors.at("top").Dot(_Vectors.at("right")))>1.0e-8)
            throw std::invalid_argument("产品四个连接面方向不成正交对向");
        _Result["faceNormalsWorld"]=std::move(_VerifiedFaces);
    }
    return _Result;
}

// A single-wall insert may remove only the selected entry wall. For verified
// rectangular sections, check the branch's entire outer footprint against the
// host's *verified* inner opening. A zero-overlap BRep test cannot detect an
// opening that also removes the opposite wall.
struct SAssemblySectionPoint2d final
{
    double U=0.0, V=0.0;
    double X() const { return U; }
    double Y() const { return V; }
    double Distance(const SAssemblySectionPoint2d& Other_) const {
        return std::hypot(U-Other_.U,V-Other_.V);
    }
};

std::array<SAssemblySectionPoint2d,4> AssemblyRectangularContour(const Variant& Contour_)
{
    if (!Contour_.Is<ObjectMap>())
        throw std::invalid_argument("单面插入需要有效的矩形截面轮廓");
    const auto _Contour = Contour_.To<ObjectMap>();
    if (GetString(_Contour,"kind") != "path" || !_Contour.contains("closed")
        || !_Contour.at("closed").Is<bool>() || !_Contour.at("closed").To<bool>())
        throw std::invalid_argument("单面插入仅支持已验证的闭合矩形截面");
    const auto _Segments = AssemblyGeometryArray(_Contour,"segments");
    if (_Segments.size()!=4)
        throw std::invalid_argument("单面插入尚不支持非矩形截面的内孔核对");
    std::array<SAssemblySectionPoint2d,4> _Corners;
    const auto _Read = [](const Variant& Point_) {
        if (!Point_.Is<VariantArray>())
            throw std::invalid_argument("矩形截面点无效");
        const auto _Values = Point_.To<VariantArray>();
        if (_Values.size()!=2)
            throw std::invalid_argument("矩形截面点无效");
        const double _U=ToDouble(_Values[0],"section.u");
        const double _V=ToDouble(_Values[1],"section.v");
        if (!std::isfinite(_U) || !std::isfinite(_V))
            throw std::invalid_argument("矩形截面点不是有限数值");
        return SAssemblySectionPoint2d{_U,_V};
    };
    for (std::size_t _Index=0; _Index<4; ++_Index)
    {
        if (!_Segments[_Index].Is<ObjectMap>())
            throw std::invalid_argument("矩形截面边无效");
        const auto _Segment = _Segments[_Index].To<ObjectMap>();
        if (GetString(_Segment,"kind")!="line")
            throw std::invalid_argument("单面插入尚不支持曲线截面的内孔核对");
        _Corners[_Index] = _Read(_Segment.at("start"));
        const auto _End = _Read(_Segment.at("end"));
        const auto _Next = _Segments[(_Index+1)%4].To<ObjectMap>();
        if (_End.Distance(_Read(_Next.at("start")))>1.0e-6)
            throw std::invalid_argument("矩形截面轮廓未首尾相接");
    }
    const auto _MinMaxU=std::minmax_element(_Corners.begin(),_Corners.end(),
        [](const SAssemblySectionPoint2d& A_,const SAssemblySectionPoint2d& B_) { return A_.X()<B_.X(); });
    const auto _MinMaxV=std::minmax_element(_Corners.begin(),_Corners.end(),
        [](const SAssemblySectionPoint2d& A_,const SAssemblySectionPoint2d& B_) { return A_.Y()<B_.Y(); });
    const double _MinU=_MinMaxU.first->X(), _MaxU=_MinMaxU.second->X();
    const double _MinV=_MinMaxV.first->Y(), _MaxV=_MinMaxV.second->Y();
    if (_MaxU-_MinU<=1.0e-6 || _MaxV-_MinV<=1.0e-6)
        throw std::invalid_argument("矩形截面尺寸无效");
    for (std::size_t _Index=0; _Index<4; ++_Index)
    {
        const auto& _P=_Corners[_Index];
        const auto& _Q=_Corners[(_Index+1)%4];
        if (!(std::abs(_P.X()-_Q.X())<=1.0e-6
            || std::abs(_P.Y()-_Q.Y())<=1.0e-6)
            || !(std::abs(_P.X()-_MinU)<=1.0e-6 || std::abs(_P.X()-_MaxU)<=1.0e-6)
            || !(std::abs(_P.Y()-_MinV)<=1.0e-6 || std::abs(_P.Y()-_MaxV)<=1.0e-6))
            throw std::invalid_argument("单面插入尚不支持非矩形截面的内孔核对");
    }
    return _Corners;
}

// A rounded rectangular opening contains this central rectangle; the outer
// rounded section is contained by its full bounding rectangle. Checking the
// larger branch against the smaller opening is deliberately conservative.
std::array<SAssemblySectionPoint2d,4> AssemblyConservativeSectionRectangle(
    const Variant& Contour_, const bool InnerOpening_)
{
    if (!Contour_.Is<ObjectMap>())
        throw std::invalid_argument("单面插入需要有效的闭合截面轮廓");
    const auto _Contour=Contour_.To<ObjectMap>();
    const auto _Segments=AssemblyGeometryArray(_Contour,"segments");
    if (_Segments.size()==4)
        return AssemblyRectangularContour(Contour_);
    if (GetString(_Contour,"kind")!="path" || !_Contour.contains("closed")
        || !_Contour.at("closed").Is<bool>() || !_Contour.at("closed").To<bool>()
        || _Segments.size()!=8)
        throw std::invalid_argument("单面插入尚不支持此截面的保守内孔核对");
    const auto _Read=[](const Variant& Value_) {
        if (!Value_.Is<VariantArray>())
            throw std::invalid_argument("圆角矩形截面点无效");
        const auto _Values=Value_.To<VariantArray>();
        if (_Values.size()!=2)
            throw std::invalid_argument("圆角矩形截面点无效");
        const double _U=ToDouble(_Values[0],"section.u");
        const double _V=ToDouble(_Values[1],"section.v");
        if (!std::isfinite(_U) || !std::isfinite(_V))
            throw std::invalid_argument("圆角矩形截面点不是有限数值");
        return SAssemblySectionPoint2d{_U,_V};
    };
    std::array<ObjectMap,8> _Edges;
    std::array<SAssemblySectionPoint2d,8> _Starts, _Ends;
    double _MinU=std::numeric_limits<double>::infinity();
    double _MaxU=-_MinU, _MinV=_MinU, _MaxV=-_MinU;
    for (std::size_t _Index=0; _Index<8; ++_Index)
    {
        if (!_Segments[_Index].Is<ObjectMap>())
            throw std::invalid_argument("圆角矩形截面边无效");
        _Edges[_Index]=_Segments[_Index].To<ObjectMap>();
        if (!_Edges[_Index].contains("start") || !_Edges[_Index].contains("end"))
            throw std::invalid_argument("圆角矩形截面边缺少端点");
        _Starts[_Index]=_Read(_Edges[_Index].at("start"));
        _Ends[_Index]=_Read(_Edges[_Index].at("end"));
        _MinU=std::min({_MinU,_Starts[_Index].U,_Ends[_Index].U});
        _MaxU=std::max({_MaxU,_Starts[_Index].U,_Ends[_Index].U});
        _MinV=std::min({_MinV,_Starts[_Index].V,_Ends[_Index].V});
        _MaxV=std::max({_MaxV,_Starts[_Index].V,_Ends[_Index].V});
    }
    constexpr double _Tol=1.0e-5;
    if (_MaxU-_MinU<=_Tol || _MaxV-_MinV<=_Tol)
        throw std::invalid_argument("圆角矩形截面尺寸无效");
    std::set<std::size_t> _CornersSeen;
    double _MaxRadius=0.0;
    std::size_t _LineCount=0, _ArcCount=0;
    for (std::size_t _Index=0; _Index<8; ++_Index)
    {
        if (_Ends[_Index].Distance(_Starts[(_Index+1)%8])>_Tol)
            throw std::invalid_argument("圆角矩形截面轮廓未首尾相接");
        const auto _Kind=GetString(_Edges[_Index],"kind");
        const auto& _Start=_Starts[_Index];
        const auto& _End=_Ends[_Index];
        if (_Kind=="line")
        {
            ++_LineCount;
            const bool _Horizontal=std::abs(_Start.V-_End.V)<=_Tol
                && (std::abs(_Start.V-_MinV)<=_Tol || std::abs(_Start.V-_MaxV)<=_Tol);
            const bool _Vertical=std::abs(_Start.U-_End.U)<=_Tol
                && (std::abs(_Start.U-_MinU)<=_Tol || std::abs(_Start.U-_MaxU)<=_Tol);
            if ((!_Horizontal && !_Vertical) || _Start.Distance(_End)<=_Tol)
                throw std::invalid_argument("单面插入仅支持标准圆角矩形截面");
            continue;
        }
        if (_Kind!="arc" || !_Edges[_Index].contains("middle"))
            throw std::invalid_argument("单面插入仅支持标准圆角矩形截面");
        ++_ArcCount;
        const auto _Middle=_Read(_Edges[_Index].at("middle"));
        const double _RadiusU=std::abs(_Start.U-_End.U);
        const double _RadiusV=std::abs(_Start.V-_End.V);
        if (_RadiusU<=_Tol || std::abs(_RadiusU-_RadiusV)>_Tol
            || _RadiusU*2>=std::min(_MaxU-_MinU,_MaxV-_MinV)+_Tol)
            throw std::invalid_argument("单面插入仅支持标准四分之一圆角");
        bool _FoundCorner=false;
        for (std::size_t _Corner=0; _Corner<4; ++_Corner)
        {
            const double _CornerU=(_Corner&1) ? _MaxU : _MinU;
            const double _CornerV=(_Corner&2) ? _MaxV : _MinV;
            const double _InU=(_Corner&1) ? -1.0 : 1.0;
            const double _InV=(_Corner&2) ? -1.0 : 1.0;
            const auto _OnSides=[&](const SAssemblySectionPoint2d& A_,
                const SAssemblySectionPoint2d& B_) {
                return std::abs(A_.U-(_CornerU+_InU*_RadiusU))<=_Tol
                    && std::abs(A_.V-_CornerV)<=_Tol
                    && std::abs(B_.U-_CornerU)<=_Tol
                    && std::abs(B_.V-(_CornerV+_InV*_RadiusU))<=_Tol;
            };
            const double _Q=_RadiusU/std::sqrt(2.0);
            const double _ExpectedU=_CornerU+_InU*(_RadiusU-_Q);
            const double _ExpectedV=_CornerV+_InV*(_RadiusU-_Q);
            if ((_OnSides(_Start,_End) || _OnSides(_End,_Start))
                && std::abs(_Middle.U-_ExpectedU)<=_Tol
                && std::abs(_Middle.V-_ExpectedV)<=_Tol)
            {
                if (!_CornersSeen.insert(_Corner).second)
                    throw std::invalid_argument("圆角矩形重复声明同一角");
                _FoundCorner=true;
                break;
            }
        }
        if (!_FoundCorner)
            throw std::invalid_argument("单面插入仅支持标准外凸四分之一圆角");
        _MaxRadius=std::max(_MaxRadius,_RadiusU);
    }
    if (_LineCount!=4 || _ArcCount!=4 || _CornersSeen.size()!=4)
        throw std::invalid_argument("单面插入仅支持四边四圆角截面");
    const double _Inset=InnerOpening_ ? _MaxRadius : 0.0;
    if (_MaxU-_MinU<=2*_Inset+_Tol || _MaxV-_MinV<=2*_Inset+_Tol)
        throw std::invalid_argument("单面插入的保守内孔尺寸不足");
    return {SAssemblySectionPoint2d{_MinU+_Inset,_MinV+_Inset},
        {_MaxU-_Inset,_MinV+_Inset},{_MaxU-_Inset,_MaxV-_Inset},
        {_MinU+_Inset,_MaxV-_Inset}};
}

void ValidateVerifiedSectionSingleInsert(const ObjectMap& Plan_,
    const VariantArray& Participants_)
{
    for (const auto& _CheckValue : AssemblyGeometryArray(Plan_,"fitChecks"))
    {
        const auto _Check = _CheckValue.To<ObjectMap>();
        if (GetString(_Check,"kind")!="end-side-single-insert") continue;
        const auto _Roles = GetRequiredObject(_Check,"roles");
        const auto _FindRole = [&Participants_](const std::string& Role_) {
            for (const auto& _Value : Participants_)
            {
                const auto _Participant=_Value.To<ObjectMap>();
                if (GetString(_Participant,"role")==Role_) return _Participant;
            }
            throw std::invalid_argument("单面插入缺少真实产品构件");
        };
        const auto _Host=_FindRole(GetRequiredText(_Roles,"host"));
        const auto _Branch=_FindRole(GetRequiredText(_Roles,"branch"));
        const auto _HostSection=GetRequiredObject(_Host,"section");
        const auto _BranchSection=GetRequiredObject(_Branch,"section");
        if (!_HostSection.contains("sectionFrame")
            || !_BranchSection.contains("sectionFrame"))
            throw std::invalid_argument("单面插入的两件真实截面必须同时可验证");
        const auto _HostFrame=GetRequiredObject(_HostSection,"sectionFrame");
        const auto _BranchFrame=GetRequiredObject(_BranchSection,"sectionFrame");
        if (GetString(_HostFrame,"verification")!="committed-source-brep-replay"
            || GetString(_BranchFrame,"verification")!="committed-source-brep-replay")
            throw std::invalid_argument("单面插入的截面基准未通过真实原管验证");
        const auto _ReadVector=[](const ObjectMap& Frame_,const char* Key_) {
            const auto _Values=AssemblyGeometryArray(Frame_,Key_);
            if (_Values.size()!=3) throw std::invalid_argument("单面插入截面方向无效");
            return gp_Vec(ToDouble(_Values[0],Key_),ToDouble(_Values[1],Key_),
                ToDouble(_Values[2],Key_));
        };
        const auto _BranchU=_ReadVector(_BranchFrame,"xAxisToolPart");
        const auto _BranchV=_ReadVector(_BranchFrame,"yAxisToolPart");
        const auto _HostU=_ReadVector(_BranchFrame,"hostXAxisToolPart");
        const auto _HostV=_ReadVector(_BranchFrame,"hostYAxisToolPart");
        const auto _FaceNormal=_ReadVector(_BranchFrame,"hostFaceNormalToolPart");
        const auto _BranchDatum=AssemblyGeometryArray(_BranchFrame,"centerlineUV");
        const auto _HostDatum=AssemblyGeometryArray(_HostFrame,"centerlineUV");
        if (_BranchDatum.size()!=2 || _HostDatum.size()!=2)
            throw std::invalid_argument("单面插入截面中心线基准无效");
        const auto _HostAnchor=GetRequiredObject(_Host,"anchor");
        const auto _BranchAnchor=GetRequiredObject(_Branch,"anchor");
        const auto _ProductBranchAnchor=GetRequiredObject(_Branch,"productAnchor");
        (void)GetRequiredText(_HostAnchor,"face");
        (void)GetRequiredText(_BranchAnchor,"end");
        const auto _ProductEnd=GetRequiredText(_ProductBranchAnchor,"end");
        if (_ProductEnd!="start" && _ProductEnd!="end")
            throw std::invalid_argument("单面插入的支管端向无效");
        const gp_Vec _Away=(_ProductEnd=="start" ? 1.0 : -1.0)*_BranchU.Crossed(_BranchV);
        if (std::abs(_Away.Magnitude()-1.0)>1.0e-6
            || std::abs(_FaceNormal.Magnitude()-1.0)>1.0e-6
            || _Away.Dot(_FaceNormal)<=1.0e-6)
            throw std::invalid_argument("单面插入的支管轴线没有从声明的主管连接面向外伸出");
        // Check the selected opening's actual one-way cutter instruction.
        const auto _ProcessIds=AssemblyGeometryArray(_Check,"processIds");
        if (_ProcessIds.empty())
            throw std::invalid_argument("单面插入缺少已声明的开口工序");
        std::set<std::string> _OpeningIDs;
        for (const auto& _ID : _ProcessIds)
        {
            if (!_ID.Is<std::string>() || _ID.To<std::string>().empty())
                throw std::invalid_argument("单面插入的开口工序标识无效");
            _OpeningIDs.insert("assembly:"+_ID.To<std::string>());
        }
        bool _FoundOpening=false;
        for (const auto& _PartValue : AssemblyGeometryArray(Plan_,"parts"))
        {
            const auto _Part=_PartValue.To<ObjectMap>();
            if (GetString(_Part,"role")!=GetRequiredText(_Roles,"host")) continue;
            for (const auto& _FeatureValue : AssemblyGeometryArray(_Part,"features"))
            {
                const auto _Feature=_FeatureValue.To<ObjectMap>();
                if (!_OpeningIDs.contains(GetString(_Feature,"id"))) continue;
                if (_FoundOpening)
                    throw std::invalid_argument("单面插入出现重复主管开口刀具");
                const auto _Direction=GetRequiredText(_Feature,"direction");
                if (_Direction!="positive" && _Direction!="negative")
                    throw std::invalid_argument("单面插入刀具没有限定单侧方向");
                const double _Angle=ToDouble(_Feature.at("angle"),"angle")
                    * 3.14159265358979323846/180.0;
                const double _Azimuth=ToDouble(_Feature.at("azimuth"),"azimuth")
                    * 3.14159265358979323846/180.0;
                if (!std::isfinite(_Angle) || !std::isfinite(_Azimuth))
                    throw std::invalid_argument("单面插入刀具的三维姿态无效");
                gp_Vec _ToolAxis(std::cos(_Angle),
                    std::sin(_Angle)*std::sin(_Azimuth),
                    std::sin(_Angle)*std::cos(_Azimuth));
                if (_Direction=="negative") _ToolAxis.Reverse();
                if (_ToolAxis.Dot(_FaceNormal)<=1.0e-6)
                    throw std::invalid_argument("单面插入刀具没有沿真实入口面向外限定范围");
                if (_ToolAxis.Dot(_Away)<1.0-1.0e-6)
                    throw std::invalid_argument("单面插入刀具轴线与真实支管轴线不一致");
                _FoundOpening=true;
            }
        }
        if (!_FoundOpening)
            throw std::invalid_argument("单面插入缺少主管实际开口刀具");
        const auto _HostContours=AssemblyGeometryArray(_HostSection,"contours");
        const auto _BranchContours=AssemblyGeometryArray(_BranchSection,"contours");
        const auto _HostOuter=GetUInt64(_HostFrame,"outerContourIndex",_HostContours.size());
        const auto _BranchOuter=GetUInt64(_BranchFrame,"outerContourIndex",_BranchContours.size());
        if (_HostContours.size()!=2 || _HostOuter>=2 || _BranchOuter>=_BranchContours.size())
            throw std::invalid_argument("单面插入需要主管一个已验证的矩形内孔");
        const auto _Cavity=AssemblyConservativeSectionRectangle(
            _HostContours[1-_HostOuter],true);
        const auto _Footprint=AssemblyConservativeSectionRectangle(
            _BranchContours[_BranchOuter],false);
        const auto _MinMaxU=std::minmax_element(_Cavity.begin(),_Cavity.end(),
            [](const SAssemblySectionPoint2d& A_,const SAssemblySectionPoint2d& B_) { return A_.X()<B_.X(); });
        const auto _MinMaxV=std::minmax_element(_Cavity.begin(),_Cavity.end(),
            [](const SAssemblySectionPoint2d& A_,const SAssemblySectionPoint2d& B_) { return A_.Y()<B_.Y(); });
        const double _Gap=GetDouble(_Check,"gapMm",0.0);
        if (!std::isfinite(_Gap) || _Gap<0)
            throw std::invalid_argument("单面插入配合间隙无效");
        for (const auto& _Corner : _Footprint)
        {
            const gp_Vec _Delta=_BranchU*(_Corner.X()-ToDouble(_BranchDatum[0],"branch.u"))
                +_BranchV*(_Corner.Y()-ToDouble(_BranchDatum[1],"branch.v"));
            const double _U=ToDouble(_HostDatum[0],"host.u")+_Delta.Dot(_HostU);
            const double _V=ToDouble(_HostDatum[1],"host.v")+_Delta.Dot(_HostV);
            if (_U<_MinMaxU.first->X()+_Gap-0.05
                || _U>_MinMaxU.second->X()-_Gap+0.05
                || _V<_MinMaxV.first->Y()+_Gap-0.05
                || _V>_MinMaxV.second->Y()-_Gap+0.05)
                throw std::invalid_argument("支管外轮廓超出主管内孔，单面插入会破坏非入口壁面");
        }
    }
}

ObjectMap AssemblySourceToPartMapping(const TopoDS_Shape& Source_,
    const ObjectMap& ManufacturingProperties_, const ObjectMap& DisplayProperties_,
    const ObjectMap& PartProperties_, const double PartLength_)
{
    const auto _Unsupported = [](const std::string& Reason_) {
        return ObjectMap{{"status",std::string("unsupported")},{"reason",Reason_}};
    };
    try
    {
        if (Source_.IsNull() || !BRepCheck_Analyzer(Source_).IsValid())
            return _Unsupported("已提交的制造原管实体无效");
        if (ManufacturingPartKind(ManufacturingProperties_) != "tube")
            return _Unsupported("仅支持有明确轴向的管件坐标映射");
        const auto [_Start,_End] = AssemblyMemberFrame(ManufacturingProperties_);
        const auto [_DisplayStart,_DisplayEnd] = AssemblyMemberFrame(DisplayProperties_);
        constexpr double _PositionTolerance = 0.05; // mm, only for source-frame metadata
        if (_Start.Distance(_DisplayStart) > _PositionTolerance
            || _End.Distance(_DisplayEnd) > _PositionTolerance)
            return _Unsupported("成品与制造文档的构件中心线不一致");
        const gp_Vec _DirectedAxis(_Start,_End);
        const double _SourceLength = _DirectedAxis.Magnitude();
        const auto _Stock = AssemblyMemberStockInterval(ManufacturingProperties_,_SourceLength);
        const auto _DisplayStock = AssemblyMemberStockInterval(DisplayProperties_,_SourceLength);
        const double _StockLength = _Stock.EndStation - _Stock.StartStation;
        // Old display recipes could declare a blank interval. Pure display
        // recipes describe the finished centerline, not manufacturing reserve.
        if (_DisplayStock.Declared && (!_Stock.Declared
            || std::abs(_Stock.StartStation - _DisplayStock.StartStation) > _PositionTolerance
            || std::abs(_Stock.EndStation - _DisplayStock.EndStation) > _PositionTolerance))
            return _Unsupported("成品与制造文档的物理原管区间不一致");
        if (!std::isfinite(_SourceLength) || _SourceLength <= _PositionTolerance
            || !std::isfinite(PartLength_) || PartLength_ <= 0.0
            || PartLength_ > _StockLength + _PositionTolerance)
            return _Unsupported("构件中心线、物理原管区间或当前下料长度无效");
        const auto _DirectedStartAtMinX = ManufacturingSourceStartAtMinX(ManufacturingProperties_);
        if (!_DirectedStartAtMinX)
            return _Unsupported("制造文档没有声明原管的起端方向");
        const auto _Known = ManufacturingProperties_.find("tubeDesigner.manufacturingAxis");
        if (_Known == ManufacturingProperties_.end() || !_Known->second.Is<VariantArray>()
            || _Known->second.To<VariantArray>().size() != 3)
            return _Unsupported("制造文档没有声明原管轴向");
        const auto _AxisValues = _Known->second.To<VariantArray>();
        gp_Dir _Axis(ToDouble(_AxisValues[0],"manufacturingAxis"),
            ToDouble(_AxisValues[1],"manufacturingAxis"),
            ToDouble(_AxisValues[2],"manufacturingAxis"));
        if (std::abs(_Axis.Dot(gp_Dir(_DirectedAxis))) < 1.0 - 1.0e-8)
            return _Unsupported("制造轴向与产品构件中心线不平行");
        if (std::abs(_Axis.X()) > 1.0e-8 ? _Axis.X() < 0.0
            : std::abs(_Axis.Y()) > 1.0e-8 ? _Axis.Y() < 0.0 : _Axis.Z() < 0.0)
            _Axis.Reverse();
        gp_Trsf _Rotation;
        _Rotation.SetRotation(gp_Quaternion(gp_Vec(_Axis),gp_Vec(1.0,0.0,0.0)));
        const auto _Rotated = BRepBuilderAPI_Transform(Source_,_Rotation,true).Shape();
        const auto _RotatedBounds = ShapeBounds(_Rotated);
        const auto _Minimum = _RotatedBounds.at("min").To<VariantArray>();
        const auto _Maximum = _RotatedBounds.at("max").To<VariantArray>();
        gp_Trsf _Translation;
        _Translation.SetTranslation(gp_Vec(
            -(ToDouble(_Minimum[0],"min.x") + ToDouble(_Maximum[0],"max.x")) * 0.5,
            -(ToDouble(_Minimum[1],"min.y") + ToDouble(_Maximum[1],"max.y")) * 0.5,
            -(ToDouble(_Minimum[2],"min.z") + ToDouble(_Maximum[2],"max.z")) * 0.5));
        const gp_Trsf _SourceToPart = _Translation.Multiplied(_Rotation);
        const auto _Candidate = BRepBuilderAPI_Transform(Source_,_SourceToPart,true).Shape();
        const auto _Normalized = NormalizeManufacturingShape(Source_,ManufacturingProperties_);
        if (DrawingReplayError(_Normalized,_Candidate) > 1.0e-6)
            return _Unsupported("声明轴向无法复现真实下料坐标系");
        const auto _Bounds = ShapeBounds(_Normalized);
        const auto _Low = _Bounds.at("min").To<VariantArray>();
        const auto _High = _Bounds.at("max").To<VariantArray>();
        const double _MinX = ToDouble(_Low[0],"min.x");
        const double _MaxX = ToDouble(_High[0],"max.x");
        const gp_Pnt _PartStart = _Start.Transformed(_SourceToPart);
        const gp_Pnt _PartEnd = _End.Transformed(_SourceToPart);
        const gp_Vec _UnitAxis{gp_Dir(_DirectedAxis)};
        const gp_Pnt _StockWorldStart = _Start.Translated(_UnitAxis * _Stock.StartStation);
        const gp_Pnt _StockWorldEnd = _Start.Translated(_UnitAxis * _Stock.EndStation);
        const gp_Pnt _PartStockStart = _StockWorldStart.Transformed(_SourceToPart);
        const gp_Pnt _PartStockEnd = _StockWorldEnd.Transformed(_SourceToPart);
        const bool _StartAtMinX = _PartStart.X() < _PartEnd.X();
        if (_StartAtMinX != *_DirectedStartAtMinX
            || std::abs(_PartStockStart.X() - (_StartAtMinX ? _MinX : _MaxX)) > _PositionTolerance
            || std::abs(_PartStockEnd.X() - (_StartAtMinX ? _MaxX : _MinX)) > _PositionTolerance
            || std::abs((_MaxX - _MinX) - _StockLength) > _PositionTolerance
            || std::abs((_PartEnd.X() - _PartStart.X())
                - (_StartAtMinX ? _SourceLength : -_SourceLength)) > _PositionTolerance)
            return _Unsupported("构件物理原管区间无法对应真实 BRep 的两个制造端");
        const auto _PartDirection = PartProperties_.find("manufacturing.sourceStartAtMinX");
        if (_PartDirection == PartProperties_.end() || !_PartDirection->second.Is<bool>()
            || _PartDirection->second.To<bool>() != _StartAtMinX)
            return _Unsupported("当前下料件的起端方向与已提交制造文档不一致");
        return ObjectMap{{"status",std::string("verified")},
            {"sourceToPart",AssemblyTransformMatrix(_SourceToPart)},
            {"partToSource",AssemblyTransformMatrix(_SourceToPart.Inverted())},
            {"sourceLength",_SourceLength},
            {"stockLength",_StockLength},
            {"stockStartStation",_Stock.StartStation},
            {"stockEndStation",_Stock.EndStation},
            {"stockStartAllowance",-_Stock.StartStation},
            {"stockEndAllowance",_Stock.EndStation - _SourceLength},
            {"stockIntervalDeclared",_Stock.Declared},
            {"sourceStartPartEnd",std::string(_StartAtMinX ? "start" : "end")},
            {"sourceEndPartEnd",std::string(_StartAtMinX ? "end" : "start")},
            {"sourceStartPartX",_PartStart.X()},
            {"sourceEndPartX",_PartEnd.X()},
            {"stockStartPartX",_PartStockStart.X()},
            {"stockEndPartX",_PartStockEnd.X()},
            {"stationToPartXSign",_StartAtMinX ? 1.0 : -1.0},
            {"verification",std::string("normalized-source-brep-replay")}};
    }
    catch (const std::exception& Error_)
    {
        return _Unsupported(Error_.what());
    }
}
