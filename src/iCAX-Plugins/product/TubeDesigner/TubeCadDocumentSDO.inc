// Business ownership of the shared geometric document. Geometry validation,
// constraints and editor projections stay in the registered CAD service.

// Creation UIs are input adapters. Compile their exact construction bodies
// once; all subsequent CAD views edit this portable geometric document.
std::string AppendTubeCadConstruction(iCAX::GeometryData::Tube::CTubeCadDocument& Document_,
    const iCAX::GeometryData::Tube::CTubeCadDocument& Construction_, const std::string& Prefix_)
{
    namespace T = iCAX::GeometryData::Tube;
    auto _Geometry = Construction_.Construction;
    // Solid nodes, UV features and owned exact bodies have separate namespaces.
    // Labels, metadata and interpretation descriptions are never identifiers.
    std::map<std::string,std::string> _NodeNames,_BodyNames;
    for (const auto& _Node : _Geometry.SolidNodes) _NodeNames[_Node.ID] = Prefix_ + "node/" + _Node.ID;
    for (const auto& [_ID,_Body] : Construction_.Bodies) _BodyNames[_ID] = Prefix_ + "body/" + _ID;
    const auto _Rename = [](std::string& ID_,const auto& Names_) {
        if (const auto _Found = Names_.find(ID_);_Found != Names_.end()) ID_ = _Found->second;
    };
    const auto _Support = [&](T::SBoundarySurfaceRef& Ref_) { _Rename(Ref_.ExtrusionNodeID,_NodeNames); };
    const auto _Boundary = [&](T::SMaterialBoundaryReference& Ref_) {
        _Rename(Ref_.HostNodeID,_NodeNames); if (Ref_.Support) _Support(*Ref_.Support);
    };
    const auto _Opening = [&](T::SFeatureOpeningRef& Ref_) {
        _Rename(Ref_.SourceFeatureNodeID,_NodeNames); if (Ref_.Boundary) _Boundary(*Ref_.Boundary);
    };
    for (auto& _Node : _Geometry.SolidNodes) {
        _Rename(_Node.ID,_NodeNames);
        std::visit([&](auto& _Data) {
            using D = std::decay_t<decltype(_Data)>;
            if constexpr (std::is_same_v<D,T::SBooleanNode>)
                for (auto& _Child : _Data.Children) _Rename(_Child,_NodeNames);
            else if constexpr (std::is_same_v<D,T::STransformNode>||std::is_same_v<D,T::SConnectedComponentNode>) _Rename(_Data.Child,_NodeNames);
            else if constexpr (std::is_same_v<D,T::STopologyBevelNode>) {_Rename(_Data.Child,_NodeNames);
                for(auto& _Selection:_Data.Selections)_Rename(_Selection.WrappedSourceNodeID,_NodeNames);}
            else if constexpr (std::is_same_v<D,T::SWrappedVolumeNode>) _Support(_Data.Support);
            else if constexpr (std::is_same_v<D,T::SOpeningProfileSweepNode>) {
                _Rename(_Data.SourceCutNodeID,_NodeNames);_Rename(_Data.EvaluatedVolumeNodeID,_NodeNames);
                if (_Data.TargetOpening) _Opening(*_Data.TargetOpening);
            } else if constexpr (std::is_same_v<D,T::SCompositeVolumeNode> || std::is_same_v<D,T::SResidualBRepNode>)
                _Rename(_Data.BRepResourceID,_BodyNames);
            else if constexpr (std::is_same_v<D,T::SAlternativeNode>)
                for (auto& _Candidate : _Data.Candidates) _Rename(_Candidate.CandidateNodeID,_NodeNames);
        },_Node.Data);
        if (!_Node.Relations) continue;
        auto& _Relations = *_Node.Relations;
        for (auto& _ID : _Relations.DependsOnNodeIDs) _Rename(_ID,_NodeNames);
        if (_Relations.Extent) {
            _Rename(_Relations.Extent->HostNodeID,_NodeNames);
            for (auto& _Ref : _Relations.Extent->Boundaries) _Boundary(_Ref);
        }
        if (_Relations.Placement) {
            _Rename(_Relations.Placement->HostNodeID,_NodeNames);
            if (_Relations.Placement->Support) _Support(*_Relations.Placement->Support);
        }
        if (_Relations.MaterialEffect) {
            _Rename(_Relations.MaterialEffect->HostNodeID,_NodeNames);
            for (auto& _ID : _Relations.MaterialEffect->SourceNodeIDs) _Rename(_ID,_NodeNames);
        }
        for (auto& _Evidence : _Relations.SurfaceEvidence) _Rename(_Evidence.SourceResourceID,_BodyNames);
        for (auto& _Span : _Relations.MaterialSpans) {
            if (_Span.Start.Boundary) _Boundary(*_Span.Start.Boundary);
            if (_Span.End.Boundary) _Boundary(*_Span.End.Boundary);
        }
    }
    for (auto& _UV : _Geometry.UVFeatures) {
        _UV.ID = Prefix_ + "uv/" + _UV.ID;
        std::visit([&](auto& _Data) { _Support(_Data.Support); },_UV.Data);
    }
    _Rename(_Geometry.BaseNodeID,_NodeNames);_Rename(_Geometry.RootNodeID,_NodeNames);
    Document_.Construction.SolidNodes.insert(Document_.Construction.SolidNodes.end(),
        _Geometry.SolidNodes.begin(),_Geometry.SolidNodes.end());
    Document_.Construction.UVFeatures.insert(Document_.Construction.UVFeatures.end(),
        _Geometry.UVFeatures.begin(),_Geometry.UVFeatures.end());
    for (const auto& [_ID,_Body] : Construction_.Bodies) Document_.Bodies.emplace(_BodyNames.at(_ID),_Body);
    return _Geometry.RootNodeID;
}
namespace TubeCadWorkspace {
std::optional<ObjectMap> EndView(const iCAX::GeometryData::Tube::SSolidNode&,const TopoDS_Shape&,double);
}
iCAX::GeometryData::Tube::CTubeCadDocument CompileTubePartCadDocument(
    iCAX::Project::ISceneContext& Scene_, const TopoDS_Shape& Base_, const TopoDS_Shape& Result_,
    const iCAX::CAD::SCadFeatureModelResult* Model_ = nullptr,
    const std::vector<SPunchFeature>* Features_ = nullptr)
{
    namespace T = iCAX::GeometryData::Tube;
    const auto _Service = Scene_.Services().Resolve<iCAX::CAD::ITubeCadDocumentService>();
    const auto _ModelService = Scene_.Services().Resolve<iCAX::CAD::ICadFeatureModelService>();
    if (!_Service || !_ModelService) throw std::runtime_error("管材 CAD 文档服务不可用");
    iCAX::OpenCascade::COpenCascadeCancellationScope _Cancellation([&] { return Scene_.IsStopRequested(); });
    const auto _Encode = [&](const TopoDS_Shape& Shape_) {
        return iCAX::Tasks::Run([&] {
            return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(Shape_,"","",0.025);
        },iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    };
    auto _Document = _Service->Import(_Encode(Base_)).Document;
    const auto _StockNode = std::find_if(_Document.Construction.SolidNodes.begin(),
        _Document.Construction.SolidNodes.end(),[&](const auto& _Node) { return _Node.ID == _Document.Construction.BaseNodeID; });
    const auto* _Stock = _StockNode == _Document.Construction.SolidNodes.end() ? nullptr
        : std::get_if<T::SExtrudedRegionNode>(&_StockNode->Data);
    const auto _StockFrame = _Stock ? std::optional<iCAX::GeometryData::Placement3>(_Stock->Frame) : std::nullopt;
    const auto _StockRoot = _Document.Construction.RootNodeID;
    std::vector<iCAX::CAD::SCadNamedShape> _Cuts = Model_ ? Model_->Cuts
        : std::vector<iCAX::CAD::SCadNamedShape>{};
    const auto _Constructed = Model_ ? Model_->Shape : Base_;
    const auto _Replay = iCAX::Tasks::Run([&] {
        return iCAX::OpenCascade::ValidateTubeCSGShapeReplay(Result_,_Constructed);
    },iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    if (!_Replay.bEquivalent) {
        const auto _Removal = iCAX::Tasks::Run([&] {
            return _ModelService->DifferenceNative({"constructed",_Constructed},{{"result",Result_}});
        },iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
        _Cuts.push_back({"surface-profile-result",_Removal.Shape});
    }
    std::vector<std::string> _Operands{_StockRoot};
    std::size_t _Ordinal = 0;
    for (const auto& _Cut : _Cuts) {
        const auto _Label = _Cut.ID.starts_with("side:") ? std::string("孔构造")
            : _Cut.ID == "end:start" ? std::string("起点端部构造")
            : _Cut.ID == "end:end" ? std::string("终点端部构造") : std::string("表面与开口构造");
        std::vector<std::string> _Instances;
        for (TopExp_Explorer _Solids(_Cut.Shape,TopAbs_SOLID);_Solids.More();_Solids.Next()) {
            const auto _Body = _Encode(_Solids.Current());
            const auto _ValidBody = iCAX::OpenCascade::BuildOpenCascadeShape(_Body);
            if (!_ValidBody.bOK || _ValidBody.Shape.IsNull() || !BRepCheck_Analyzer(_ValidBody.Shape).IsValid())
                throw std::invalid_argument("CAD 创建构造体不是有效封闭实体");
            T::CTubeCadDocument _Construction;
            const auto _End=_Cut.ID.starts_with("end:")&&_Stock
                ?TubeCadWorkspace::EndView(T::SSolidNode{"end","",T::SResidualBRepNode{},std::nullopt,std::nullopt,{{"creationConstruction",_Cut.ID}}},_Solids.Current(),_Stock->Last-_Stock->First):std::nullopt;
            if(_End) {
                const double _Angle=GetDouble(*_End,"angle",0)*std::numbers::pi/180;
                const double _Rotation=GetDouble(*_End,"rotation",0)*std::numbers::pi/180;
                const bool _Start=_Cut.ID=="end:start";
                _Construction.Construction.BaseNodeID=_Construction.Construction.RootNodeID="end-plane";
                _Construction.Construction.SolidNodes.push_back({"end-plane",_Label,T::SHalfSpaceNode{
                    iCAX::GeometryData::Plane3{{_Start?GetDouble(*_End,"trim",0):_Stock->Last-_Stock->First-GetDouble(*_End,"trim",0),0,0},
                        {1,-std::tan(_Angle)*std::cos(_Rotation),-std::tan(_Angle)*std::sin(_Rotation)},{0,1,0}},_Start}});
            }
            else try { _Construction = _Service->Import(_Body).Document; }
            catch (const std::invalid_argument& _Error) {
                iCAX::OpenCascade::COpenCascadeCancellationScope::ThrowIfCancellationRequested();
                // An unsupported LOCAL construction remains exact and openly
                // exposes only composite capabilities. Never substitute a
                // default primitive or re-import the entire finished part.
                T::SCompositeVolumeNode _Composite;
                _Composite.BRepResourceID = "exact";
                _Composite.EditCapabilities = {T::ECompositeEditCapability::WholeTransform,
                    T::ECompositeEditCapability::BooleanReuse,T::ECompositeEditCapability::ManualFeaturePromotion};
                _Composite.Metadata["conversionDiagnostic"] = _Error.what();
                _Construction.Construction.BaseNodeID = _Construction.Construction.RootNodeID = "construction";
                _Construction.Construction.SolidNodes.push_back({"construction",_Label,_Composite});
                _Construction.Bodies.emplace("exact",_Body);
            }
            const auto _Prefix = "construction:" + std::to_string(++_Ordinal) + ":";
            const auto _Root = AppendTubeCadConstruction(_Document,_Construction,_Prefix);
            for (auto& _Node : _Document.Construction.SolidNodes) {
                if(_Node.ID.starts_with(_Prefix)) {
                    _Node.Metadata["constructionOwner"]=_Root;
                    if(_Node.Relations&&_Node.Relations->Extent)_Node.Relations->Extent->RecomputeAgainstHost=false;
                }
                if (_Node.ID != _Root) continue;
                _Node.Label = _Label + " " + std::to_string(_Ordinal);
                _Node.Metadata["creationConstruction"] = _Cut.ID;
                if (!_Node.Relations) _Node.Relations.emplace();
                _Node.Relations->DependsOnNodeIDs.push_back(_Document.Construction.BaseNodeID);
                T::SFeatureMaterialEffect _Effect;
                _Effect.Role = _Cut.ID.starts_with("side:") ? T::EFeatureMaterialRole::Penetration
                    : _Cut.ID.starts_with("end:") ? T::EFeatureMaterialRole::Truncation
                    : T::EFeatureMaterialRole::UnclassifiedRemoval;
                _Effect.HostNodeID = _Document.Construction.BaseNodeID;
                _Effect.SourceNodeIDs = {_Root};
                _Node.Relations->MaterialEffect = _Effect;
                if (!Features_ || !_StockFrame) continue;
                const auto _Feature = std::find_if(Features_->begin(),Features_->end(),[&](const auto& _Item) {
                    return _Cut.ID == "side:" + _Item.ID;
                });
                auto* _Primitive = std::get_if<T::SExtrudedRegionNode>(&_Node.Data);
                if (_Feature == Features_->end() || !_Primitive || _Feature->ToolInPartCoordinates
                    || !_Feature->FrozenCut.IsNull()) continue;
                T::SFeatureExtentConstraint _Extent;
                _Extent.HostNodeID = _Document.Construction.BaseNodeID;
                _Extent.Rule = _Feature->BlindHole ? T::EFeatureExtentRule::BlindFromHostBoundary
                    : T::EFeatureExtentRule::ThroughSelectedHostBoundaries;
                _Extent.RecomputeAgainstHost = true;
                _Extent.PrimitiveFirst.Value = _Primitive->First;
                _Extent.PrimitiveLast.Value = _Primitive->Last;
                if (_Feature->BlindHole) {
                    _Extent.Depth = T::SScalarParameter{_Feature->CutDepth};
                    const auto _Bounds = ShapeBounds(_Solids.Current());
                    iCAX::GeometryData::Vector3 _Normal;
                    if (_Feature->Face == "round") {
                        const auto _Y = _Primitive->Frame.Location.Y - _StockFrame->Location.Y;
                        const auto _Z = _Primitive->Frame.Location.Z - _StockFrame->Location.Z;
                        _Normal = {0,_Y,_Z};
                    } else if (_Feature->Face == "top" || _Feature->Face == "bottom")
                        _Normal = {0,0,_Feature->Face == "top" ? 1.0 : -1.0};
                    else _Normal = {0,_Feature->Face == "left" ? -1.0 : 1.0,0};
                    // Opposite instances have their outward direction reversed.
                    const auto _Center = ShapeBounds(Base_);
                    const auto _CutMin = _Bounds.at("min").To<VariantArray>();
                    const auto _CutMax = _Bounds.at("max").To<VariantArray>();
                    const auto _HostMin = _Center.at("min").To<VariantArray>();
                    const auto _HostMax = _Center.at("max").To<VariantArray>();
                    const double _Dot = _Primitive->Frame.ZDirection.Y * _Normal.Y
                        + _Primitive->Frame.ZDirection.Z * _Normal.Z;
                    const double _Position = (_CutMin[1].To<double>() + _CutMax[1].To<double>()
                        - _HostMin[1].To<double>() - _HostMax[1].To<double>()) * _Normal.Y
                        + (_CutMin[2].To<double>() + _CutMax[2].To<double>()
                        - _HostMin[2].To<double>() - _HostMax[2].To<double>()) * _Normal.Z;
                    const double _OutwardDot = _Feature->Opposite && _Position < 0 ? -_Dot : _Dot;
                    _Extent.Direction = _OutwardDot < 0 ? T::EExtentDirection::Positive : T::EExtentDirection::Negative;
                }
                _Node.Relations->Extent = _Extent;
                _Node.Relations->Metadata["layoutDatum"] = _Feature->LayoutDatum;
                T::SHostPlacementConstraint _Placement;
                _Placement.Rule = T::EHostPlacementRule::RelativeToHostFrame;
                _Placement.HostNodeID = _Document.Construction.BaseNodeID;
                const auto _Frame = [](const auto& _Value) {
                    gp_Trsf _Transform;
                    _Transform.SetValues(_Value.XDirection.X,_Value.YDirection.X,_Value.ZDirection.X,_Value.Location.X,
                        _Value.XDirection.Y,_Value.YDirection.Y,_Value.ZDirection.Y,_Value.Location.Y,
                        _Value.XDirection.Z,_Value.YDirection.Z,_Value.ZDirection.Z,_Value.Location.Z);
                    return _Transform;
                };
                const auto _Local = _Frame(*_StockFrame).Inverted() * _Frame(_Primitive->Frame);
                _Placement.LocalFrame.Location = {_Local.Value(1,4),_Local.Value(2,4),_Local.Value(3,4)};
                _Placement.LocalFrame.XDirection = {_Local.Value(1,1),_Local.Value(2,1),_Local.Value(3,1)};
                _Placement.LocalFrame.YDirection = {_Local.Value(1,2),_Local.Value(2,2),_Local.Value(3,2)};
                _Placement.LocalFrame.ZDirection = {_Local.Value(1,3),_Local.Value(2,3),_Local.Value(3,3)};
                _Node.Relations->Placement = _Placement;
            }
            _Instances.push_back(_Root);
        }
        if (_Instances.size() == 1) _Operands.push_back(_Instances.front());
        else if (!_Instances.empty()) {
            const auto _Group = "construction-group:" + std::to_string(_Ordinal);
            _Document.Construction.SolidNodes.push_back({_Group,_Label,
                T::SBooleanNode{T::EBooleanOperation::Union,_Instances}});
            _Operands.push_back(_Group);
        }
    }
    if (_Operands.size() > 1) {
        _Document.Construction.RootNodeID = "part-result";
        _Document.Construction.SolidNodes.push_back({"part-result","零件材料区域",
            T::SBooleanNode{T::EBooleanOperation::Difference,_Operands}});
    }
    _Document.Metadata["authoringAuthority"] = "neutral-cad-document";
    const auto _Evaluated = _Service->Create(_Document.Construction,_Document.Bodies);
    const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(_Evaluated.Evaluation.Shape);
    const auto _Proof = iCAX::Tasks::Run([&] {
        return iCAX::OpenCascade::ValidateTubeCSGShapeReplay(Result_,_Built.Shape);
    },iCAX::OpenCascade::detail::GeometryTaskScheduler()).Result();
    if (!_Built.bOK || !_Proof.bEquivalent)
        throw std::invalid_argument("创建输入编译成中性 CAD 文档后未通过独立几何等价验证");
    auto _Result = _Evaluated.Document;
    _Result.Metadata = _Document.Metadata;
    return _Result;
}

struct STubePartCadContext final
{
    iCAX::Data::uuid PartID;
    std::shared_ptr<CManufacturingPartComponent> Part;
    std::shared_ptr<CTubePartCadComponent> Component;
    std::shared_ptr<iCAX::GeometryData::Tube::CTubeCadDocument> Document;
    std::string DocumentResourceID;
    std::uint64_t DocumentResourceVersion = 0;
};

STubePartCadContext ReadTubePartCadContext(const ObjectMap& Payload_,
    iCAX::Project::ISceneContext& Scene_)
{
    STubePartCadContext _Context;
    _Context.PartID = ParseRequiredUuid(GetRequiredText(Payload_, "partEntityId", 80), "partEntityId");
    const auto _Entity = Scene_.Database().GetEntity(_Context.PartID);
    _Context.Part = GetComponent<CManufacturingPartComponent>(_Entity);
    _Context.Component = GetComponent<CTubePartCadComponent>(_Entity);
    if (!_Context.Part || !IsIndependentNestingPart(*_Context.Part) || !_Context.Component)
        throw std::invalid_argument("零件没有可编辑的中性管材 CAD 文档");
    if (Payload_.contains("resourceVersion") && GetUInt64(Payload_, "resourceVersion", 0)
        != _Context.Part->GetManufacturingGeometryResourceVersion())
        throw std::invalid_argument("零件已改变，请重新读取管材 CAD 文档");
    _Context.DocumentResourceID = _Context.Component->GetDocumentResourceID();
    _Context.DocumentResourceVersion = _Context.Component->GetDocumentResourceVersion();
    _Context.Document = Scene_.Resources().Get<iCAX::GeometryData::Tube::CTubeCadDocument>(
        _Context.DocumentResourceID, _Context.DocumentResourceVersion);
    if (!_Context.Document) throw std::runtime_error("管材 CAD 文档版本缺失");
    return _Context;
}

ObjectMap TubePartCadSnapshot(const STubePartCadContext& Context_)
{
    namespace T = iCAX::GeometryData::Tube;
    VariantArray _Nodes;
    for (const auto& _Node : Context_.Document->Construction.SolidNodes) {
        const bool _SketchEditable = std::holds_alternative<T::SExtrudedRegionNode>(_Node.Data)
            || std::holds_alternative<T::STaperedRegionNode>(_Node.Data)
            || std::holds_alternative<T::SWrappedVolumeNode>(_Node.Data)
            || std::holds_alternative<T::SOpeningProfileSweepNode>(_Node.Data);
        auto _Data = iCAX::CAD::TubeCadJson::Encode(_Node.Data).To<ObjectMap>();
        _Nodes.emplace_back(ObjectMap{{"nodeId",_Node.ID},{"label",_Node.Label},
            {"kind",_Data.at("type")},{"base",_Node.ID == Context_.Document->Construction.BaseNodeID},
            {"sketchEditable",_SketchEditable},{"geometryEditable",_SketchEditable
                || std::holds_alternative<T::SHalfSpaceNode>(_Node.Data)
                || std::holds_alternative<T::STransformNode>(_Node.Data)}});
    }
    return {{"partEntityId", UuidToString(Context_.PartID)},
        {"resourceId", Context_.Part->GetManufacturingGeometryResourceID()},
        {"resourceVersion", Context_.Part->GetManufacturingGeometryResourceVersion()},
        {"documentResourceId", Context_.Component->GetDocumentResourceID()},
        {"documentResourceVersion", Context_.Component->GetDocumentResourceVersion()},
        {"revision", std::to_string(Context_.Document->Revision)},
        {"geometry", iCAX::CAD::TubeCadJson::Encode(Context_.Document->Geometry)},
        {"placement", iCAX::CAD::TubeCadJson::Encode(Context_.Document->Placement)},
        {"construction", iCAX::CAD::TubeCadJson::Encode(Context_.Document->Construction)}, {"nodes",_Nodes}};
}

#include "TubeCadWorkspaceSDO.inc"

iCAX::CAD::STubeCadEditResult EvaluateTubeCadEditPayload(const ObjectMap& Payload_,
    const STubePartCadContext& Context_, iCAX::Project::ISceneContext& Scene_,
    const iCAX::Interaction::CInvocation& Request_,const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* Product_)
{
    const auto _Revision = iCAX::CAD::TubeCadJson::Decode<std::uint64_t>(GetRequiredText(Payload_,"revision",24));
    if(_Revision!=Context_.Document->Revision)throw std::invalid_argument("Tube CAD revision conflict");
    const auto _Service = Scene_.Services().Resolve<iCAX::CAD::ITubeCadDocumentService>();
    if (!_Service) throw std::runtime_error("管材 CAD 文档服务不可用");
    if(Payload_.contains("boundaryGeometry")) {
        const auto core=iCAX::CAD::TubeCadJson::Decode<iCAX::GeometryData::Tube::CTubeBoundaryGeometry>(Payload_.at("boundaryGeometry"));
        return _Service->EditGeometry(*Context_.Document,_Revision,core);
    }
    if(Payload_.contains("authoringEdit")) {
        const auto _Edit=GetRequiredObject(Payload_,"authoringEdit");
        const auto _Mode=GetString(_Edit,"mode");
        auto _Draft=*Context_.Document;
        if(_Mode=="side")_Draft=TubeCadWorkspace::EditSide(*Context_.Document,_Edit,Scene_,0);
        else if(_Mode=="drawing")_Draft=TubeCadWorkspace::EditDrawing(*Context_.Document,_Edit,Context_,Request_,Application_,Product_,Scene_);
        else throw std::invalid_argument("CAD 工作区编辑模式尚未定义");
        std::map<std::string,iCAX::GeometryData::BRepModel> _Additional;
        for(const auto& [_ID,_Body]:_Draft.Bodies)if(!Context_.Document->Bodies.contains(_ID))_Additional.emplace(_ID,_Body);
        return _Service->EditWithBodies(*Context_.Document,_Revision,_Draft.Construction,_Additional);
    }
    if (Payload_.contains("sketch")) {
        const auto _Sketch = GetRequiredObject(Payload_,"sketch");
        auto _View = _Service->ReadSketch(*Context_.Document,GetRequiredText(_Sketch,"nodeId",240));
        _View.Revision = _Revision;
        _View.Geometry = iCAX::CAD::TubeCadJson::Decode<decltype(_View.Geometry)>(_Sketch.at("geometry"));
        return _Service->EditSketch(*Context_.Document,_View);
    }
    const auto _Geometry = iCAX::CAD::TubeCadJson::Decode<iCAX::GeometryData::Tube::CTubeNeutralGeometry>(Payload_.at("construction"));
    return _Service->Edit(*Context_.Document,_Revision,_Geometry);
}

std::string TubeCadPreviewKey(const ObjectMap& Payload_)
{
    const auto _Key = GetRequiredText(Payload_,"previewResourceKey",96);
    if (!std::all_of(_Key.begin(),_Key.end(),[](unsigned char _Value) {
        return (_Value >= 'a' && _Value <= 'z') || (_Value >= 'A' && _Value <= 'Z')
            || (_Value >= '0' && _Value <= '9') || _Value == '-' || _Value == '_';
    })) throw std::invalid_argument("管材 CAD 预览会话标识无效");
    return _Key;
}

ObjectMap StoreTubeCadPreviewMesh(iCAX::Project::ISceneContext& Scene_,const std::string& Key_,
    const std::string& Owner_,iCAX::GeometryData::BRepModel Body_)
{
    auto& resources=Scene_.Resources();
    const auto body=StoreSharedPreparedBRep(Scene_,resources.MakeNamedResourceURL(Key_),"CAD 草稿显示",std::make_shared<iCAX::GeometryData::BRepModel>(std::move(Body_)));
    const auto mesh=iCAX::RenderInteraction::EnsureFrontendGeometryResource(resources,body.URL,iCAX::Render::ERenderGeometryKind::Mesh);
    for(const auto& url:{body.URL,mesh.URL}) {
        auto info=resources.GetInfo(url);if(!info||!info->IsRuntimeOnly())throw std::logic_error("CAD 草稿必须使用运行时资源");
        info->Metadata["tube-cad-preview-owner"]=Owner_;if(!resources.UpdateInfo(url,*info))throw std::runtime_error("无法记录 CAD 草稿资源归属");
    }
    return {{"url",mesh.URL},{"version",mesh.nVersion}};
}

iCAX::Interaction::CInvocationResult HandlePreviewTubeCadDocument(
    const iCAX::Interaction::CInvocation& Request_, const iCAX::Application::IApplicationContext& Application_,
    iCAX::Product::IProductContext* Product_, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_ || Request_.Payload.size() > 16 * 1024 * 1024)
        throw std::invalid_argument("管材 CAD 预览需要有效的场景请求");
    const auto _Payload = DecodeObjectPayload(Request_);
    if (!_Payload.contains("resourceVersion")) throw std::invalid_argument("管材 CAD 预览缺少零件版本");
    const auto _Key = TubeCadPreviewKey(_Payload);
    const auto _Context = ReadTubePartCadContext(_Payload,*Scene_);
    iCAX::OpenCascade::COpenCascadeCancellationScope _Cancellation([Scene_] { return Scene_->IsStopRequested(); });
    auto _Edited = EvaluateTubeCadEditPayload(_Payload,_Context,*Scene_,Request_,Application_,Product_);
    const auto _Current = ReadTubePartCadContext(_Payload,*Scene_);
    if (_Current.DocumentResourceID != _Context.DocumentResourceID
        || _Current.DocumentResourceVersion != _Context.DocumentResourceVersion)
        throw std::invalid_argument("零件已改变，请重新读取管材 CAD 草稿");
    const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(_Edited.Evaluation.Shape);
    if (!_Built.bOK || _Built.Shape.IsNull()) throw std::invalid_argument("管材 CAD 预览实体无法恢复");
    auto& _Resources = Scene_->Resources();
    const auto _BRep = StoreSharedPreparedBRep(*Scene_,_Resources.MakeNamedResourceURL(
        "tube-designer/tube-cad-preview/" + _Key),"管材 CAD 草稿预览",
        std::make_shared<iCAX::GeometryData::BRepModel>(std::move(_Edited.Evaluation.Shape)));
    const auto _Mesh = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
        _Resources,_BRep.URL,iCAX::Render::ERenderGeometryKind::Mesh);
    for (const auto& _URL : {_BRep.URL,_Mesh.URL}) {
        auto _Info = _Resources.GetInfo(_URL);
        if (!_Info || !_Info->IsRuntimeOnly()) throw std::logic_error("CAD 草稿资源必须只存在于运行时");
        _Info->Metadata["tube-cad-preview-owner"] = _Key;
        if (!_Resources.UpdateInfo(_URL,*_Info)) throw std::runtime_error("无法记录 CAD 草稿资源归属");
    }
    const auto _Material = EnsurePunchPreviewBlankMaterial(*Scene_);
    auto _BaseBounds = ShapeBounds(_Built.Shape);
    if (const auto _Base = _Edited.Evaluation.ConstructionBodies.find(_Edited.Document.Construction.BaseNodeID);
        _Base != _Edited.Evaluation.ConstructionBodies.end()) {
        const auto _BaseShape = iCAX::OpenCascade::BuildOpenCascadeShape(_Base->second);
        if (_BaseShape.bOK && !_BaseShape.Shape.IsNull()) _BaseBounds = ShapeBounds(_BaseShape.Shape);
    }
    VariantArray _Diagnostics;
    for (const auto& _Text : _Edited.Evaluation.Diagnostics) _Diagnostics.emplace_back(_Text);
    ObjectMap _Result{{"partEntityId",UuidToString(_Context.PartID)},
        {"revision",std::to_string(_Context.Document->Revision)},
        {"documentGeometry",iCAX::CAD::TubeCadJson::Encode(_Edited.Document.Geometry)},
        {"documentPlacement",iCAX::CAD::TubeCadJson::Encode(_Edited.Document.Placement)},
        {"documentConstruction",iCAX::CAD::TubeCadJson::Encode(_Edited.Document.Construction)},
        {"geometry",ObjectMap{{"url",_Mesh.URL},{"version",_Mesh.nVersion}}},
        {"material",ObjectMap{{"url",_Material.URL},{"version",_Material.nVersion}}},
        {"baseBounds",_BaseBounds},{"bounds",ShapeBounds(_Built.Shape)},{"diagnostics",_Diagnostics}};
    const auto _Native=_Edited.Document.Construction.SolidNodes.empty()
        ? iCAX::OpenCascade::STubeCSGEvaluationResult{true,_Built.Shape}
        : TubeCadWorkspace::NativeEvaluation(_Edited.Document);
    const auto _BasePreview=_Edited.Evaluation.ConstructionBodies.find(_Edited.Document.Construction.BaseNodeID);
    _Result["baseGeometry"]=StoreTubeCadPreviewMesh(*Scene_,"tube-designer/tube-cad-preview/"+_Key+"/base",_Key,
        _BasePreview==_Edited.Evaluation.ConstructionBodies.end()?_Edited.Evaluation.Shape:_BasePreview->second);
    _Result["baseMaterial"]=_Result.at("material");
    const auto _ToolMaterial=EnsureComponentCSGOperandMaterial(*Scene_);
    _Result["toolMaterial"]=ObjectMap{{"url",_ToolMaterial.URL},{"version",_ToolMaterial.nVersion}};
    VariantArray _Tools;std::size_t _ToolOrdinal=0;
    for(const auto& _Node:_Edited.Document.Construction.SolidNodes) {
        if(_Node.ID==_Edited.Document.Construction.BaseNodeID||!_Node.Relations||!_Node.Relations->MaterialEffect)continue;
        if(_Node.Relations->MaterialEffect->HostNodeID!=_Edited.Document.Construction.BaseNodeID)continue;
        if(_Node.Metadata.contains("constructionOwner")&&_Node.Metadata.at("constructionOwner")!=_Node.ID)continue;
        const auto _Body=_Edited.Evaluation.ConstructionBodies.find(_Node.ID);if(_Body==_Edited.Evaluation.ConstructionBodies.end())continue;
        auto _DisplayBody=_Body->second;
        if(std::holds_alternative<iCAX::GeometryData::Tube::SHalfSpaceNode>(_Node.Data)) {
            // Infinite clipping planes author the part; their visible cutting
            // material is finite and belongs to the current stock.
            BRepAlgoAPI_Common _Clip;NCollection_List<TopoDS_Shape> _Arguments,_ClipTools;
            _Arguments.Append(_Native.NodeShapes.at(_Edited.Document.Construction.BaseNodeID));_ClipTools.Append(_Native.NodeShapes.at(_Node.ID));
            _Clip.SetArguments(_Arguments);_Clip.SetTools(_ClipTools);_Clip.SetNonDestructive(true);
            iCAX::OpenCascade::COpenCascadeCancellationScope::Build(_Clip);
            if(!_Clip.IsDone()||_Clip.Shape().IsNull())throw std::invalid_argument("端部裁切预览无法与当前原材求交");
            _DisplayBody=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_Clip.Shape(),"","",.025);
        }
        const auto _MeshRef=StoreTubeCadPreviewMesh(*Scene_,"tube-designer/tube-cad-preview/"+_Key+"/tool/"+std::to_string(++_ToolOrdinal),_Key,std::move(_DisplayBody));
        const auto _Source=_Node.Metadata.find("creationConstruction");
        const bool _End=_Source!=_Node.Metadata.end()&&(_Source->second=="end:start"||_Source->second=="end:end");
        _Tools.emplace_back(ObjectMap{{"target",std::string(_End?"end":"feature")},{"key",_End?_Source->second.substr(4):_Node.ID},{"geometry",_MeshRef}});
    }
    _Result["toolPreviews"]=_Tools;
    if(GetString(_Payload,"authoringView")=="drawing"||_Payload.contains("authoringEdit")||(_Payload.contains("includeTopology")&&_Payload.at("includeTopology").To<bool>())) {
        auto _SelectionShape=_Native.Shape;
        if(!_Edited.Document.Construction.SolidNodes.empty())
            if(const auto* _Modifier=std::get_if<iCAX::GeometryData::Tube::STopologyBevelNode>(&TubeCadWorkspace::Node(_Edited.Document,_Edited.Document.Construction.RootNodeID).Data))
                _SelectionShape=_Native.NodeShapes.at(_Modifier->Child);
        const auto _Signature=iCAX::CAD::CadExactTopologySignature(_SelectionShape);
        const auto _Topology=InspectPartDrawingTopology(_SelectionShape,*Scene_->Services().Resolve<iCAX::CAD::ICadBevelService>());
        iCAX::CAD::SCadFeatureModelResult _Names;
        for(const auto& _Edge:_Topology.Edges)_Names.EdgeNames[_Edge.EdgeID]=TubeCadWorkspace::SelectionName(_Signature,"edge",_Edge.EdgeID);
        for(const auto& _Face:_Topology.Faces)_Names.FaceNames[_Face.FaceID]=TubeCadWorkspace::SelectionName(_Signature,"face",_Face.FaceID);
        auto _Snapshot=PartDrawingTopologySnapshot(_Topology,_Signature,*Scene_,"tube-designer/tube-cad-preview/"+_Key+"/topology",_Key,&_Names);
        for(const auto* _Kind:{"edges","faces"})for(const auto& _Value:_Snapshot.at(_Kind).To<VariantArray>()) {
            const auto _URL=GetString(GetRequiredObject(_Value.To<ObjectMap>(),"geometry"),"url");
            auto _Info=Scene_->Resources().GetInfo(_URL);_Info->Metadata["tube-cad-preview-owner"]=_Key;
            if(!Scene_->Resources().UpdateInfo(_URL,*_Info))throw std::runtime_error("无法记录 CAD 选择资源归属");
        }
        const auto _TopologyMaterial=EnsurePartDrawingTopologyMaterial(*Scene_);
        _Result["topology"]=_Snapshot;_Result["topologyMaterial"]=ObjectMap{{"url",_TopologyMaterial.URL},{"version",_TopologyMaterial.nVersion}};
        _Result["edgeMaterial"]=_Result.at("toolMaterial");
    }
    return MakeResponse(_Result);
}

iCAX::Interaction::CInvocationResult HandleReleaseTubeCadDocumentPreview(
    const iCAX::Interaction::CInvocation& Request_, const iCAX::Application::IApplicationContext&,
    iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("释放管材 CAD 预览需要场景上下文");
    const auto _Key = TubeCadPreviewKey(DecodeObjectPayload(Request_));
    auto& _Resources = Scene_->Resources();
    std::set<std::string> _Owned;
    for (const auto& _Info : _Resources.GetManifest(true)) {
        const auto _Owner = _Info.Metadata.find("tube-cad-preview-owner");
        if (_Info.IsRuntimeOnly() && _Owner != _Info.Metadata.end() && _Owner->second == _Key)
            _Owned.insert(_Info.Key.Source);
    }
    std::uint64_t _Released = 0;
    while (!_Owned.empty()) {
        const auto _Before = _Owned.size();
        std::erase_if(_Owned,[&](const auto& _URL) {
            const auto _Result = _Resources.DiscardRuntimeResource(_URL);
            if (_Result == iCAX::Resource::EResourceMutationResult::PreconditionFailed) return false;
            if (_Result == iCAX::Resource::EResourceMutationResult::Removed) ++_Released;
            return true;
        });
        if (_Before == _Owned.size()) throw std::runtime_error("CAD 草稿预览仍被使用，请等待显示资源释放");
    }
    return MakeResponse(ObjectMap{{"released",true},{"resourceCount",_Released}});
}

iCAX::Interaction::CInvocationResult HandleGetTubeCadDocument(
    const iCAX::Interaction::CInvocation& Request_, const iCAX::Application::IApplicationContext&,
    iCAX::Product::IProductContext*, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("管材 CAD 编辑需要场景上下文");
    const auto _Payload = DecodeObjectPayload(Request_);
    const auto _Context = ReadTubePartCadContext(_Payload, *Scene_);
    auto _Result = TubePartCadSnapshot(_Context);
    if(_Payload.contains("authoringView")) {
        const auto _Mode=GetString(_Payload,"authoringView");
        if(_Mode=="side")_Result["authoringView"]=TubeCadWorkspace::SideView(*_Context.Document,*Scene_);
        else if(_Mode=="drawing")_Result["authoringView"]=TubeCadWorkspace::DrawingView(*_Context.Document,_Context);
        else throw std::invalid_argument("CAD 工作区视图尚未定义");
        auto _View=_Result.at("authoringView").To<ObjectMap>();
        const auto _Material=EnsurePunchPreviewBlankMaterial(*Scene_);
        _View["preview"]=ObjectMap{{"geometry",ObjectMap{{"url",_Context.Part->GetThumbnailGeometryResourceID()},{"version",_Context.Part->GetThumbnailGeometryResourceVersion()}}},
            {"baseGeometry",ObjectMap{{"url",_Context.Part->GetThumbnailGeometryResourceID()},{"version",_Context.Part->GetThumbnailGeometryResourceVersion()}}},
            {"material",ObjectMap{{"url",_Material.URL},{"version",_Material.nVersion}}},{"baseMaterial",ObjectMap{{"url",_Material.URL},{"version",_Material.nVersion}}},
            {"baseBounds",ShapeBounds(TubeCadWorkspace::Native(Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
                _Context.Part->GetManufacturingGeometryResourceID(),_Context.Part->GetManufacturingGeometryResourceVersion())?*Scene_->Resources().Get<iCAX::GeometryData::BRepModel>(
                _Context.Part->GetManufacturingGeometryResourceID(),_Context.Part->GetManufacturingGeometryResourceVersion()):iCAX::GeometryData::BRepModel{}))}};
        _Result["authoringView"]=_View;
    }
    if (_Payload.contains("nodeId")) {
        const auto _Service = Scene_->Services().Resolve<iCAX::CAD::ITubeCadDocumentService>();
        if (!_Service) throw std::runtime_error("管材 CAD 文档服务不可用");
        const auto _View = _Service->ReadSketch(*_Context.Document, GetRequiredText(_Payload, "nodeId", 240));
        _Result["sketch"] = ObjectMap{{"nodeId", _View.NodeID}, {"revision", std::to_string(_View.Revision)},
            {"geometry", iCAX::CAD::TubeCadJson::Encode(_View.Geometry)},
            {"constructionFrame", iCAX::CAD::TubeCadJson::Encode(_View.ConstructionFrame)},
            {"surfaceSupport", iCAX::CAD::TubeCadJson::Encode(_View.SurfaceSupport)}};
    }
    return MakeResponse(_Result);
}

iCAX::Interaction::CInvocationResult HandleApplyTubeCadDocument(
    const iCAX::Interaction::CInvocation& Request_, const iCAX::Application::IApplicationContext& ApplicationContext_,
    iCAX::Product::IProductContext* Product_, iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext* Scene_)
{
    if (!Scene_) throw std::invalid_argument("管材 CAD 编辑需要场景上下文");
    const auto _Payload = DecodeObjectPayload(Request_);
    if (!_Payload.contains("resourceVersion")) throw std::invalid_argument("管材 CAD 保存缺少零件版本");
    const auto _Context = ReadTubePartCadContext(_Payload, *Scene_);
    ObjectMap _MainMetadata;
    if(_Payload.contains("authoringEdit")) {
        const auto _Authoring=GetRequiredObject(_Payload,"authoringEdit");
        if(GetString(_Authoring,"mode")=="drawing"&&_Authoring.contains("main")) {
            const auto _Main=GetRequiredObject(_Authoring,"main");
            if(_Main.contains("name")) {
                const auto _Name=TrimText(GetString(_Main,"name"));
                if(_Name.empty()||_Name.size()>160||_Name.find('\0')!=std::string::npos)
                    throw std::invalid_argument("零件名称不能为空且不能超过 160 个字符");
                _MainMetadata["name"]=_Name;
            }
            if(_Main.contains("quantity"))_MainMetadata["quantity"]=ValidateInstanceQuantity(GetUInt64(_Main,"quantity",0));
            if(_Main.contains("material")) {
                const auto _Material=TrimText(GetString(_Main,"material"));
                if(_Material.size()>240||_Material.find('\0')!=std::string::npos)throw std::invalid_argument("材料名称不能超过 240 个字符");
                _MainMetadata["material"]=_Material;
            }
        }
    }
    iCAX::OpenCascade::COpenCascadeCancellationScope _Cancellation(
        [Scene_] { return Scene_->IsStopRequested(); });
    auto _Edited = EvaluateTubeCadEditPayload(_Payload,_Context,*Scene_,Request_,ApplicationContext_,Product_);
    const auto _Built = iCAX::OpenCascade::BuildOpenCascadeShape(_Edited.Evaluation.Shape);
    if (!_Built.bOK || _Built.Shape.IsNull()) throw std::runtime_error("管材 CAD 结果无法恢复");
    if (std::abs(_Edited.Document.Placement.XDirection.X - 1.0) > 1e-7
        || std::abs(_Edited.Document.Placement.XDirection.Y) > 1e-7 || std::abs(_Edited.Document.Placement.XDirection.Z) > 1e-7)
        throw std::invalid_argument("下料零件的原材轴向必须保持在零件局部 X 轴");
    const auto _LocalBlank=iCAX::OpenCascade::BuildTubeBoundaryStock(_Edited.Document.Geometry);
    const auto _PlacedBlank=BRepBuilderAPI_Transform(_LocalBlank,TubeCadWorkspace::Frame(_Edited.Document.Placement),true).Shape();
    const auto _Fitters = iCAX::ExtrusionRecognition::DiscoverPythonSectionFitters(
        PathToUTF8(ResolveSystemProfileRoot(ApplicationContext_)));
    iCAX::ExtrusionRecognition::CExtrusionRecognitionService _RecognitionService;
    const auto _Recognition = _RecognitionService.RecognizePythonFitters(
        iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(_PlacedBlank,"","",0.025), _Fitters);
    if (_Recognition.Status != iCAX::ExtrusionRecognition::ERecognitionStatus::Success
        && _Recognition.Status != iCAX::ExtrusionRecognition::ERecognitionStatus::SectionTypeNotMatched)
        throw std::invalid_argument("修改后的原材截面无法用于直管下料");
    const auto _Profile = ProfileSnapshot(_Recognition, "neutral-tube-cad");
    const auto _EnvelopeLength = GetDouble(ShapeBounds(_Built.Shape), "width", 0);
    if (!std::isfinite(_EnvelopeLength) || _EnvelopeLength <= 0.02)
        throw std::invalid_argument("管材 CAD 结果轴向长度无效");
    const auto _Document = StoreTubeCadDocument(*Scene_, "tube-designer/nesting/cad/"
        + UuidToString(_Context.PartID), _Context.Part->GetName(), _Edited.Document,_Context.DocumentResourceID);
    const auto _Geometry = StorePreparedBRep(*Scene_, EditableNestingGeometryResourceID(
        *Scene_, _Context.PartID, *_Context.Part), _Context.Part->GetName(), std::move(_Edited.Evaluation.Shape));
    const auto _Thumbnail = iCAX::RenderInteraction::EnsureFrontendGeometryResource(
        Scene_->Resources(), _Geometry.URL, iCAX::Render::ERenderGeometryKind::Mesh);
    auto _Properties = _Context.Part->GetItemProperties();
    auto _Measurement = MeasureFinalPartGeometry(_Built.Shape, _Geometry.URL, _Geometry.nVersion);
    _Measurement["nestingEnvelopeLength"] = _EnvelopeLength;
    _Measurement["normalizedAxis"] = std::string("+X");
    _Properties["manufacturing.geometryMeasurement"] = _Measurement;
    if(_MainMetadata.contains("material"))_Properties["manufacturing.material"]=_MainMetadata.at("material");
    auto& _DB = Scene_->Database();
    auto _Undo = _DB.BeginUndoCommand("Edit neutral tube CAD document");
    auto& _Transaction = _DB.BeginTransaction("Edit neutral tube CAD document");
    bool _Committing = false;
    try {
        const auto _Current = ReadTubePartCadContext(_Payload, *Scene_);
        if (_Current.DocumentResourceID != _Context.DocumentResourceID
            || _Current.DocumentResourceVersion != _Context.DocumentResourceVersion)
            throw std::invalid_argument("零件已改变，请重新读取后保存");
        _Transaction.ModifyComponent(_Context.PartID, CTubePartCadComponent::S_ClassName, {
            {CTubePartCadComponent::PropertyName_DocumentResourceID, PropertyValue(_Document.URL)},
            {CTubePartCadComponent::PropertyName_DocumentResourceVersion, PropertyValue(_Document.nVersion)}});
        iCAX::Data::PropertySet _PartChanges{
            {CManufacturingPartComponent::PropertyName_Length, PropertyValue(_EnvelopeLength)},
            {CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceID, PropertyValue(_Geometry.URL)},
            {CManufacturingPartComponent::PropertyName_ManufacturingGeometryResourceVersion, PropertyValue(_Geometry.nVersion)},
            {CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceID, PropertyValue(_Thumbnail.URL)},
            {CManufacturingPartComponent::PropertyName_ThumbnailGeometryResourceVersion, PropertyValue(_Thumbnail.nVersion)},
            {CManufacturingPartComponent::PropertyName_ItemProperties, PropertyValue(_Properties)}};
        if(_MainMetadata.contains("name"))_PartChanges[CManufacturingPartComponent::PropertyName_Name]=_MainMetadata.at("name");
        if(_MainMetadata.contains("quantity")) {
            _PartChanges[CManufacturingPartComponent::PropertyName_Quantity]=_MainMetadata.at("quantity");
            _PartChanges[CManufacturingPartComponent::PropertyName_QuantityOverride]=PropertyValue(0ull);
        }
        _Transaction.ModifyComponent(_Context.PartID,CManufacturingPartComponent::S_ClassName,_PartChanges);
        QueueUpsertComponent(_Transaction, _Context.Part->GetEntity(), _Context.PartID,
            CTubeProfileComponent::S_ClassName, TubeProfileComponentPropertiesFromSnapshot(_Profile));
        const auto _Meta = _DB.GetMetaEntity();
        if (const auto _Root = GetComponent<CTubeDesignerRootComponent>(_Meta)) {
            auto _Task = _Root->GetNestingTask();
            _Task["request"] = ObjectMap(); _Task["result"] = ObjectMap();
            _Task["revision"] = UuidToString(iCAX::Data::GenerateNewUUID());
            _Transaction.ModifyComponent(_Meta->GetID(), CTubeDesignerRootComponent::S_ClassName,
                {{CTubeDesignerRootComponent::PropertyName_NestingTask, PropertyValue(_Task)}});
        }
        std::string _Error; _Committing = true;
        if (!_DB.CommitTransaction(_Transaction, _Error))
            throw std::runtime_error(_Error.empty() ? "保存管材 CAD 文档失败" : _Error);
    } catch (...) {
        if (!_Committing) { try { _DB.CancelTransaction(_Transaction); } catch (...) {} }
        throw;
    }
    _Undo->End(); SyncNestingPersistence(*Scene_);
    auto _Result = TubePartCadSnapshot(ReadTubePartCadContext(ObjectMap{
        {"partEntityId", UuidToString(_Context.PartID)}}, *Scene_));
    _Result["snapshot"] = BuildSnapshot(*Scene_, ApplicationContext_, false);
    return MakeResponse(_Result);
}
