// Business adapter: exact topology IDs and recipes live in CAD drawing state.
// The shared kernel owns geometry; this adapter owns validation and resources.
std::string PartDrawingPreviewKey(const ObjectMap& Payload_, bool Required_ = false)
{
    const auto _Key = Required_ ? GetRequiredText(Payload_, "previewResourceKey", 96)
        : GetString(Payload_, "previewResourceKey", "default");
    const auto _Safe = [](unsigned char Value_) {
        return (Value_ >= 'a' && Value_ <= 'z') || (Value_ >= 'A' && Value_ <= 'Z')
            || (Value_ >= '0' && Value_ <= '9') || Value_ == '-' || Value_ == '_';
    };
    if (_Key.empty() || _Key.size() > 96 || !std::all_of(_Key.begin(), _Key.end(), _Safe))
        throw std::invalid_argument("CAD 预览资源标识无效");
    return _Key;
}

iCAX::Interaction::CInvocationResult HandleReleasePartDrawingPreview(
    const iCAX::Interaction::CInvocation& Request_, const iCAX::Application::IApplicationContext&,
    iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_ || Request_.Payload.size() > 64 * 1024)
        throw std::invalid_argument("CAD 预览释放请求无效");
    const auto _Key = PartDrawingPreviewKey(DecodeObjectPayload(Request_), true);
    PartDrawingPreviewCaches().Release(*Scene_, _Key);
    auto& _Resources = Scene_->Resources();
    std::vector<std::string> _URLs;
    for (const auto& _Info : _Resources.GetManifest(true)) {
        const auto _Owner = _Info.Metadata.find("cad-preview-owner");
        if (_Info.IsRuntimeOnly() && _Owner != _Info.Metadata.end() && _Owner->second == _Key)
            _URLs.push_back(_Info.Key.Source);
    }
    // Derived render resources depend on the source BRep. Resolve that order
    // through the pool's real dependency guard, including archived versions.
    // Exact imported bases and finished parts are outside this editor scope.
    std::uint64_t _Released = 0;
    while (!_URLs.empty()) {
        const auto _Before = _URLs.size();
        std::erase_if(_URLs, [&](const auto& _URL) {
            const auto _Result = _Resources.DiscardRuntimeResource(_URL);
            if (_Result == iCAX::Resource::EResourceMutationResult::PreconditionFailed) return false;
            if (_Result == iCAX::Resource::EResourceMutationResult::Removed) ++_Released;
            return true;
        });
        if (_Before == _URLs.size()) throw std::runtime_error("CAD 预览资源仍被使用");
    }
    return MakeResponse(ObjectMap{{"released", true}, {"resourceCount", _Released}});
}

void MarkPartDrawingPreviewGeometry(iCAX::Project::ISceneContext& Scene_,
    const std::string& Owner_, const std::string& URL_)
{
    auto& _Resources = Scene_.Resources();
    std::set<std::string> _Visited;
    std::vector<std::string> _Pending{URL_};
    while (!_Pending.empty()) {
        const auto _Current = std::move(_Pending.back()); _Pending.pop_back();
        if (_Current.empty() || !_Visited.insert(_Current).second) continue;
        auto _Info = _Resources.GetInfo(_Current);
        if (!_Info || !_Info->IsRuntimeOnly()) continue;
        const auto _Owner = _Info->Metadata.find("cad-preview-owner");
        if (_Owner != _Info->Metadata.end() && _Owner->second != Owner_)
            throw std::logic_error("CAD 预览资源属于其他编辑会话");
        if (_Owner == _Info->Metadata.end()) {
            _Info->Metadata["cad-preview-owner"] = Owner_;
            if (!_Resources.UpdateInfo(_Current, *_Info))
                throw std::runtime_error("无法记录 CAD 预览资源归属");
        }
        // Render meshes reference an exact staged BRep via Source. Ownership
        // follows real resource references, never the hashed resource URI.
        if (!_Info->Source.empty()) _Pending.push_back(_Info->Source);
        for (const auto& _Dependency : _Info->Dependencies) _Pending.push_back(_Dependency.URL);
    }
}

void MarkPartDrawingPreviewResources(iCAX::Project::ISceneContext& Scene_,
    const std::string& Owner_, const ObjectMap& Response_)
{
    const auto _Mark = [&](const ObjectMap& Object_, const std::string& Field_) {
        const auto _Found = Object_.find(Field_);
        if (_Found != Object_.end() && _Found->second.Is<ObjectMap>())
            MarkPartDrawingPreviewGeometry(Scene_, Owner_, GetString(_Found->second.To<ObjectMap>(), "url"));
    };
    for (const auto* _Field : {"geometry", "baseGeometry", "toolGeometry", "cutBoundaryGeometry"})
        _Mark(Response_, _Field);
    if (const auto _Tools = Response_.find("toolPreviews"); _Tools != Response_.end())
        for (const auto& _Value : _Tools->second.To<VariantArray>()) _Mark(_Value.To<ObjectMap>(), "geometry");
    if (const auto _Found = Response_.find("topology"); _Found != Response_.end()) {
        const auto _Topology = _Found->second.To<ObjectMap>();
        for (const auto* _Field : {"edges", "faces"})
            for (const auto& _Value : _Topology.at(_Field).To<VariantArray>()) _Mark(_Value.To<ObjectMap>(), "geometry");
    }
}

TopoDS_Shape OwnPartDrawingBlank(const TopoDS_Shape& Shape_)
{
    // Use the same owned topology in preview, creation and edits. Geometric
    // copies also separate surface/curve handles that an extrusion may share;
    // that sharing is part of the exact BRep serialization used by recipes.
    return iCAX::Tasks::Run([&] {
        return BRepBuilderAPI_Copy(Shape_, true, false).Shape();
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
}

TopoDS_Shape MakePersistablePartDrawingBlank(const TopoDS_Shape& Shape_)
{
    // Generated/rebased stock has no persisted topology yet. Select from the
    // representation that will actually be saved, including its edge and face
    // assembly order. Existing imported bases already have that representation
    // and never enter this path or a profile reverse-solution.
    return iCAX::Tasks::Run([&] {
        const auto _Owned = BRepBuilderAPI_Copy(Shape_, true, false).Shape();
        const auto _Model = iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_Owned, "", "", 0.025);
        const auto _Rebuilt = iCAX::OpenCascade::BuildOpenCascadeShape(_Model);
        if (!_Rebuilt.bOK || _Rebuilt.Shape.IsNull())
            throw std::runtime_error("无法准备三维绘制的持久化基准");
        return _Rebuilt.Shape;
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
}

TopoDS_Shape EvaluateTubePartSurfaceRegions(const TopoDS_Shape& Shape_, const ObjectMap& Drawing_,
    SPunchPreviewDiagnostics& State_, iCAX::Project::ISceneContext& Scene_)
{
    if (!Drawing_.contains("sketch")) return Shape_;
    const auto _Sketch = GetRequiredObject(Drawing_, "sketch");
    if (_Sketch.at("entities").To<VariantArray>().empty() && !_Sketch.contains("endCuts")) return Shape_;
    const auto _Service = Scene_.Services().Resolve<iCAX::CAD::ICadFeatureModelService>();
    const auto _Base = State_.ModelResult.Base;
    if (_Base.Shape.IsNull()) throw std::logic_error("Missing common CAD construction base");
    const auto _Cut = ApplySceneTubeSideSketch(OwnPartDrawingBlank(_Base.Shape), _Sketch, Scene_);
    if (!_Cut.bOK) throw std::invalid_argument(_Cut.Diagnostic);
    if (!_Cut.Changed) return Shape_;
    return iCAX::Tasks::Run([&] {
        const auto _Removal = _Service->DifferenceNative(_Base, {{"retained",_Cut.Shape}});
        auto _Cuts = State_.ModelResult.Cuts;
        _Cuts.push_back({"surface-regions",_Removal.Shape});
        State_.ModelResult = _Service->DifferenceNative(_Base,_Cuts);
        return State_.ModelResult.Shape;
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
}

iCAX::CAD::SCadTopologyResult InspectPartDrawingTopology(const TopoDS_Shape& Shape_,
    const iCAX::CAD::ICadBevelService& Service_)
{
    const auto _Result = iCAX::Tasks::Run([&] {
        return Service_.InspectNative(Shape_);
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    if (!_Result.bOK) throw std::invalid_argument(_Result.Diagnostic);
    return _Result;
}

std::string PartDrawingTopologyRevision(const TopoDS_Shape& Shape_)
{
    // Include exact surfaces, curves, locations and topology. Display samples
    // cannot establish identity: two different trimmed faces can share them.
    // Ignore triangulation and normals so remeshing cannot invalidate recipes.
    const auto _Bytes = iCAX::Tasks::Run([&] {
        std::ostringstream _Stream;
        BRepTools::Write(Shape_, _Stream, false, false, TopTools_FormatVersion_CURRENT);
        return _Stream.str();
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    return ContentDigest("part-drawing-exact-topology", _Bytes);
}

TopoDS_Shape ApplyPartDrawingBevels(const TopoDS_Shape& Shape_, const ObjectMap& Drawing_,
    const iCAX::CAD::ICadBevelService& Service_,
    const iCAX::CAD::SCadTopologyResult* Inspected_ = nullptr,
    const iCAX::CAD::SCadFeatureModelResult* Model_ = nullptr)
{
    const auto _It = Drawing_.find("bevels");
    if (_It == Drawing_.end()) return Shape_;
    if (!_It->second.Is<VariantArray>()) throw std::invalid_argument("坡口列表无效");
    const auto _Recipes = _It->second.To<VariantArray>();
    if (_Recipes.size() > 200) throw std::invalid_argument("一个零件最多支持 200 个坡口定义");
    std::vector<iCAX::CAD::SCadBevelOperation> _Operations;
    std::set<std::string> _IDs;
    std::optional<iCAX::CAD::SCadTopologyResult> _OwnedTopology;
    const iCAX::CAD::SCadTopologyResult* _Topology = Inspected_;
    std::string _Revision;
    std::set<std::uint64_t> _SelectedEdges;
    for (const auto& _Value : _Recipes) {
        if (!_Value.Is<ObjectMap>()) throw std::invalid_argument("坡口定义无效");
        const auto _Recipe = _Value.To<ObjectMap>();
        const auto _ID = GetRequiredText(_Recipe, "id", 80);
        if (!_IDs.insert(_ID).second) throw std::invalid_argument("坡口定义标识重复");
        const auto _Bevel = GetRequiredObject(_Recipe, "bevel");
        const auto _Enabled = _Bevel.find("enabled");
        if (_Enabled != _Bevel.end() && !_Enabled->second.Is<bool>())
            throw std::invalid_argument("坡口启用标记无效");
        if (_Enabled != _Bevel.end() && !_Enabled->second.To<bool>()) continue;
        if (!_Topology) {
            _OwnedTopology = InspectPartDrawingTopology(Shape_, Service_);
            _Topology = &*_OwnedTopology;
        }
        if (_Revision.empty()) _Revision = PartDrawingTopologyRevision(Shape_);
        const bool _Named = _Recipe.contains("edgeNames");
        if (!_Named && GetString(_Recipe, "topologyRevision") != _Revision)
            throw std::invalid_argument("坡口所选边已经变化，请在当前成品上重新选择");
        const auto _Edges = _Recipe.find("edgeIds");
        if (_Edges == _Recipe.end() || !_Edges->second.Is<VariantArray>())
            throw std::invalid_argument("请选择坡口作用的边");
        auto _EdgeIDs = _Edges->second.To<VariantArray>();
        const auto _ResolveName = [&](const auto& Names_, const std::string& Name_) -> std::uint64_t {
            std::uint64_t resolved = 0;
            for (const auto& [id, name] : Names_) if (name == Name_) {
                if (resolved) throw std::invalid_argument("坡口开口引用存在歧义，请重新选择");
                resolved = id;
            }
            if (!resolved) throw std::invalid_argument("坡口所属开口已删除或拓扑已改变，请重新选择");
            return resolved;
        };
        if (_Named) {
            if (!Model_ || !_Recipe.at("edgeNames").Is<VariantArray>())
                throw std::invalid_argument("坡口开口引用无效");
            const auto names = _Recipe.at("edgeNames").To<VariantArray>();
            if (names.size() != _EdgeIDs.size()) throw std::invalid_argument("坡口边引用数量不匹配");
            _EdgeIDs.clear();
            for (const auto& name : names) {
                if (!name.Is<std::string>() || name.To<std::string>().empty()) throw std::invalid_argument("坡口开口引用为空");
                _EdgeIDs.emplace_back(_ResolveName(Model_->EdgeNames, name.To<std::string>()));
            }
        }
        if (_EdgeIDs.empty() || _EdgeIDs.size() > 500)
            throw std::invalid_argument("坡口边数量须为 1 至 500");
        const auto _Side = GetString(_Bevel, "side", "outer");
        const auto _Angle = GetDouble(_Bevel, "angleDegrees", 45.0);
        const auto _Root = GetDouble(_Bevel, "rootFace", 0.0);
        if ((_Side != "outer" && _Side != "inner" && _Side != "both")
            || !std::isfinite(_Angle) || _Angle <= 0 || _Angle >= 90
            || !std::isfinite(_Root) || _Root < 0)
            throw std::invalid_argument("坡口侧别、角度或根部钝边无效");
        auto _ReferenceFace = GetUInt64(_Recipe, "referenceFaceId", 0);
        if (_Named && _ReferenceFace) {
            const auto name = GetString(_Recipe, "referenceFaceName");
            if (name.empty()) throw std::invalid_argument("坡口基准面缺少稳定引用，请重新选择");
            _ReferenceFace = _ResolveName(Model_->FaceNames, name);
        }
        for (const auto& _Edge : _EdgeIDs) {
            const auto _EdgeID = GetUInt64(ObjectMap{{"id", _Edge}}, "id", 0);
            const auto _Found = std::find_if(_Topology->Edges.begin(), _Topology->Edges.end(),
                [&](const auto& _Candidate) { return _Candidate.EdgeID == _EdgeID; });
            if (_Found == _Topology->Edges.end()) throw std::invalid_argument("坡口边不属于当前零件");
            if (_ReferenceFace && std::find(_Found->AdjacentFaceIDs.begin(), _Found->AdjacentFaceIDs.end(),
                _ReferenceFace) == _Found->AdjacentFaceIDs.end())
                throw std::invalid_argument("坡口参考面必须与所选边相邻");
            if (!_SelectedEdges.insert(_EdgeID).second) throw std::invalid_argument("同一条边不能重复设置坡口");
            _Operations.push_back({_EdgeID, _ReferenceFace, _Side, _Angle, _Root});
        }
    }
    if (_Operations.empty()) return Shape_;
    const auto _Result = iCAX::Tasks::Run([&] {
        return Service_.ApplyNative(Shape_, _Operations);
    }, iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    if (!_Result.bOK || _Result.Shape.IsNull()) throw std::invalid_argument(_Result.Diagnostic);
    return _Result.Shape;
}

iCAX::Resource::CResourceReference StorePartDrawingSelectionGeometry(
    iCAX::Project::ISceneContext& Scene_, const std::string& Key_,
    iCAX::Resource::CFlatBufferResource Geometry_, const std::string& Owner_)
{
    auto& _Resources = Scene_.Resources();
    const auto _URL = _Resources.MakeNamedResourceURL(Key_);
    const auto _Version = _Resources.GetVersion(_URL);
    const auto _Existing = _Resources.Get<iCAX::Resource::CFlatBufferResource>(_URL, _Version);
    if (_Existing && _Existing->Size() == Geometry_.Size()
        && std::equal(_Existing->Bytes().begin(), _Existing->Bytes().end(), Geometry_.Bytes().begin()))
        return {_URL, _Version};
    iCAX::Resource::CResourceInfo _Info;
    _Info.Name = "CAD 边面选择（未保存）";
    _Info.ResourceTypeID = "render.geometry.flatbuffer";
    _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
    _Info.nSchemaVersion = 1;
    _Info.Metadata["cad-preview-owner"] = Owner_;
    iCAX::Resource::CResourceInfo _Stored;
    _Resources.PutVersioned<iCAX::Resource::CFlatBufferResource>(_URL,
        std::make_shared<iCAX::Resource::CFlatBufferResource>(std::move(Geometry_)), _Info,
        iCAX::Resource::EResourceVersionCondition::None, 0, &_Stored);
    if (!_Stored.nVersion) throw std::runtime_error("无法生成 CAD 边面选择显示资源");
    return {_URL, _Stored.nVersion};
}

ObjectMap PartDrawingTopologySnapshot(const iCAX::CAD::SCadTopologyResult& Topology_,
    const std::string& Revision_, iCAX::Project::ISceneContext& Scene_, const std::string& Prefix_, const std::string& Owner_,
    const iCAX::CAD::SCadFeatureModelResult* Model_ = nullptr)
{
    using namespace iCAX::Render;
    VariantArray _Edges, _Faces;
    std::map<std::string, std::uint64_t> _FaceOrdinals;
    for (const auto& _Edge : Topology_.Edges) {
        iCAX::OpenCascade::COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        if (_Edge.Points.size() < 2) continue;
        SRenderPolylineData _Line;
        VariantArray _Points, _Adjacent;
        for (const auto& _Point : _Edge.Points) {
            _Line.Points.push_back({static_cast<float>(_Point[0]), static_cast<float>(_Point[1]), static_cast<float>(_Point[2])});
            _Points.emplace_back(VariantArray{_Point[0], _Point[1], _Point[2]});
        }
        _Line.Ranges.push_back({0, static_cast<uint32_t>(_Line.Points.size())});
        const auto _Ref = StorePartDrawingSelectionGeometry(Scene_, Prefix_ + "/edge/" + std::to_string(_Edge.EdgeID),
            MakeRenderGeometryResource(_Line), Owner_);
        for (auto _ID : _Edge.AdjacentFaceIDs) _Adjacent.emplace_back(_ID);
        _Edges.emplace_back(ObjectMap{{"id", _Edge.EdgeID}, {"points", _Points}, {"adjacentFaceIds", _Adjacent},
            {"persistentName", Model_ && Model_->EdgeNames.contains(_Edge.EdgeID) ? Model_->EdgeNames.at(_Edge.EdgeID) : std::string()},
            {"geometry", ObjectMap{{"url", _Ref.URL}, {"version", _Ref.nVersion}, {"kind", std::string("polyline")}}}});
    }
    for (const auto& _Face : Topology_.Faces) {
        iCAX::OpenCascade::COpenCascadeCancellationScope::ThrowIfCancellationRequested();
        if (_Face.TriangleVertices.empty()) continue;
        SRenderMeshData _Mesh;
        for (const auto& _Point : _Face.TriangleVertices)
            _Mesh.Positions.push_back({static_cast<float>(_Point[0]), static_cast<float>(_Point[1]), static_cast<float>(_Point[2])});
        const auto _Ref = StorePartDrawingSelectionGeometry(Scene_, Prefix_ + "/face/" + std::to_string(_Face.FaceID),
            MakeRenderGeometryResource(_Mesh), Owner_);
        VariantArray _IDs;
        for (auto _ID : _Face.EdgeIDs) _IDs.emplace_back(_ID);
        const auto _SurfaceKind = _Face.SurfaceKind;
        const std::string _Name = _SurfaceKind == "plane" ? "平面"
            : _SurfaceKind == "cylinder" ? "圆柱面"
            : _SurfaceKind == "cone" ? "圆锥面"
            : _SurfaceKind == "sphere" ? "球面"
            : _SurfaceKind == "torus" ? "圆环面" : "曲面";
        const auto _Label = _Name + " " + std::to_string(++_FaceOrdinals[_Name]);
        _Faces.emplace_back(ObjectMap{{"id", _Face.FaceID}, {"edgeIds", _IDs},
            {"persistentName", Model_ && Model_->FaceNames.contains(_Face.FaceID) ? Model_->FaceNames.at(_Face.FaceID) : std::string()},
            {"surfaceKind", _SurfaceKind}, {"label", _Label},
            {"geometry", ObjectMap{{"url", _Ref.URL}, {"version", _Ref.nVersion}, {"kind", std::string("mesh")}}}});
    }
    return {{"revision", Revision_}, {"edges", _Edges}, {"faces", _Faces}};
}

iCAX::Resource::CResourceReference EnsurePartDrawingTopologyMaterial(iCAX::Project::ISceneContext& Scene_)
{
    auto& _Resources = Scene_.Resources();
    const auto _URL = _Resources.MakeNamedResourceURL("tube-designer/material/cad-topology-selection");
    if (!_Resources.GetVersion(_URL)) {
        auto _Material = std::make_shared<iCAX::Render::SRenderMaterialData>();
        _Material->nDataVersion = 1;
        _Material->nColorRGBA = 0x8FC7E514u;
        _Material->nAmbientRGBA = 0x8FC7E514u;
        _Material->nSpecularRGBA = 0xDCEEF514u;
        _Material->nLineWidth = 1.0f;
        iCAX::Resource::CResourceInfo _Info;
        _Info.Name = "CAD 拓扑选择辅助面";
        _Info.ResourceTypeID = iCAX::Render::SRenderMaterialData::kResourceTypeName;
        _Info.Persistence = iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        _Resources.PutVersioned<iCAX::Render::SRenderMaterialData>(_URL, std::move(_Material), _Info);
    }
    return iCAX::RenderInteraction::EnsureFrontendMaterialResource(_Resources, _URL);
}
