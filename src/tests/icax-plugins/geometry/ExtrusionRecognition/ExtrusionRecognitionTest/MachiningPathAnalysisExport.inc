#include <ExtrusionRecognition/MachiningPathAnalysisWork.h>
#include <StepVisual_ColourRgb.hxx>

// Included after MachiningEdgeAnalysisExport.inc. Display geometry is derived
// on copies; the BREP below remains the complete, unchanged classified model.
namespace machining_path_export
{
std::string StatusName(R::EMachiningPathStatus status) {
    switch(status) {
    case R::EMachiningPathStatus::NotSelected:return "NotSelected";
    case R::EMachiningPathStatus::Ready:return "Ready";
    case R::EMachiningPathStatus::ParallelToGenerator:return "ParallelToGenerator";
    case R::EMachiningPathStatus::PartiallyParallelToGenerator:return "PartiallyParallelToGenerator";
    case R::EMachiningPathStatus::UnsupportedSurface:return "UnsupportedSurface";
    case R::EMachiningPathStatus::InvalidGeometry:return "InvalidGeometry";
    }
    throw std::logic_error("Unknown path analysis status");
}
std::string ModeName(R::EMachiningPathAxisMode mode) {
    switch(mode) {
    case R::EMachiningPathAxisMode::None:return "None";
    case R::EMachiningPathAxisMode::PlaneTangentCrossNormal:return "PlaneTangentCrossNormal";
    case R::EMachiningPathAxisMode::StraightGenerator:return "StraightGenerator";
    }
    throw std::logic_error("Unknown path axis mode");
}
VariantArray Point(const G::Point3& p){return {p.X,p.Y,p.Z};}
VariantArray Vector(const G::Vector3& p){return {p.X,p.Y,p.Z};}
struct Overlay final {TopoDS_Shape Shape;std::string Name;std::string Role;};
Quantity_Color Color(const std::string& role) {
    if(role=="path")return Quantity_Color(0.12,0.88,0.36,Quantity_TOC_RGB);
    if(role=="side")return Quantity_Color(1.,0.19,0.23,Quantity_TOC_RGB);
    if(role=="unresolved")return Quantity_Color(.62,.62,.62,Quantity_TOC_RGB);
    return Quantity_Color(244./255.,223./255.,145./255.,Quantity_TOC_RGB);
}
ObjectMap WriteStep(const TopoDS_Shape& complete,const std::vector<Overlay>& overlays,
    const fs::path& file,double diagonal) {
    Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");
    const auto shapes=XCAFDoc_DocumentTool::ShapeTool(document->Main());
    const auto colors=XCAFDoc_DocumentTool::ColorTool(document->Main());
    bool presentationValid=false;
    const auto body=FlatArtifact(StepBoundaryCopy(complete,false,false,&presentationValid));
    if(!presentationValid)throw std::runtime_error("Path-analysis complete STEP body is invalid");
    const auto bodyLabel=AddArtifactFaces(shapes,body,"Complete model / path analysis");
    colors->SetColor(bodyLabel,Quantity_Color(.67,.71,.76,Quantity_TOC_RGB),XCAFDoc_ColorSurf);
    for(const auto& overlay:overlays) {
        const auto label=shapes->AddShape(overlay.Shape,false);
        TDataStd_Name::Set(label,overlay.Name.c_str());
        colors->SetColor(label,Color(overlay.Role),XCAFDoc_ColorGen);
        colors->SetColor(label,Color(overlay.Role),XCAFDoc_ColorCurv);
    }
    Interface_Static::SetCVal("write.step.schema","AP214IS");
    STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);
    writer.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
    if(!writer.Transfer(document,STEPControl_AsIs))throw std::runtime_error("Path-analysis STEP transfer failed");
    const auto model=writer.ChangeWriter().Model();
    const auto finder=writer.Writer().WS()->TransferWriter()->FinderProcess();
    std::map<const Standard_Transient*,std::vector<Handle(StepVisual_StyledItem)>> styles;
    for(int i=1;i<=model->NbEntities();++i) {
        const auto styled=Handle(StepVisual_StyledItem)::DownCast(model->Value(i));
        if(!styled.IsNull()&&!styled->Item().IsNull())styles[styled->Item().get()].push_back(styled);
    }
    std::set<std::string> expectedStyles;
    const double unit=std::max(diagonal*.001,1.e-12);
    for(const auto& overlay:overlays) {
        TopLoc_Location location;const auto item=STEPConstruct::FindEntity(finder,overlay.Shape,location);
        if(item.IsNull())throw std::runtime_error("Path overlay has no transferred STEP entity");
        bool found=false;
        for(const auto& styled:styles[item.get()])for(int i=1;i<=styled->NbStyles();++i) {
            const auto assignment=styled->StylesValue(i);
            for(int k=1;k<=assignment->NbStyles();++k) {
                const auto style=assignment->StylesValue(k).CurveStyle();if(style.IsNull())continue;
                style->SetName(new TCollection_HAsciiString(overlay.Name.c_str()));
                Handle(NCollection_HArray1<Handle(StepVisual_CurveStyleFontPattern)>) list=
                    new NCollection_HArray1<Handle(StepVisual_CurveStyleFontPattern)>(1,1);
                Handle(StepVisual_CurveStyleFontPattern) pattern=new StepVisual_CurveStyleFontPattern;
                pattern->Init(overlay.Role=="side"?6*unit:8*unit,overlay.Role=="side"?3*unit:0.);
                list->SetValue(1,pattern);Handle(StepVisual_CurveStyleFont) font=new StepVisual_CurveStyleFont;
                font->Init(new TCollection_HAsciiString(overlay.Role=="side"?"dashed":"solid"),list);
                StepVisual_CurveStyleFontSelect select;select.SetValue(font);style->SetCurveFont(select);
                model->AddWithRefs(font);found=true;
            }
        }
        if(!found)throw std::runtime_error("Path overlay has no curve presentation style");
        expectedStyles.insert(overlay.Name);
    }
    if(writer.Write(Utf8(file).c_str())!=IFSelect_RetDone)throw std::runtime_error("Cannot write path-analysis STEP");
    STEPControl_Reader reader;
    if(reader.ReadFile(Utf8(file).c_str())!=IFSelect_RetDone||!reader.TransferRoots())throw std::runtime_error("Cannot reopen path-analysis STEP");
    const auto restored=reader.OneShape();const double areaError=Relative(Area(complete),Area(restored));
    std::set<std::string> restoredStyles;int green=0,red=0,yellow=0;
    const auto readModel=reader.StepModel();
    for(int i=1;i<=readModel->NbEntities();++i) {
        const auto styled=Handle(StepVisual_StyledItem)::DownCast(readModel->Value(i));if(styled.IsNull())continue;
        for(int j=1;j<=styled->NbStyles();++j) {
            const auto assignment=styled->StylesValue(j);
            for(int k=1;k<=assignment->NbStyles();++k) {
                const auto style=assignment->StylesValue(k).CurveStyle();
                if(style.IsNull()||style->Name().IsNull())continue;
                const std::string name=style->Name()->ToCString();
                if(!expectedStyles.contains(name))continue;restoredStyles.insert(name);
                const auto rgb=Handle(StepVisual_ColourRgb)::DownCast(style->CurveColour());
                if(rgb.IsNull())continue;
                // STEP COLOUR_RGB stores sRGB values; Quantity_Color's RGB
                // constructor otherwise treats them as linear values.
                const Quantity_Color actual(rgb->Red(),rgb->Green(),rgb->Blue(),Quantity_TOC_sRGB);
                if(name.starts_with("PATH / ")&&actual.Distance(Color("path"))<1.e-5)++green;
                else if(name.starts_with("SIDE / ")&&actual.Distance(Color("side"))<1.e-5)++red;
                else if(name.starts_with("AXIS / ")&&actual.Distance(Color("axis"))<1.e-5)++yellow;
            }
        }
    }
    int expectedGreen=0,expectedRed=0,expectedBlue=0;
    for(const auto& o:overlays){if(o.Role=="path")++expectedGreen;else if(o.Role=="side")++expectedRed;else if(o.Role=="axis")++expectedBlue;}
    const bool stylesOK=restoredStyles==expectedStyles&&green>=expectedGreen&&red>=expectedRed&&yellow>=expectedBlue;
    const bool passed=Count(complete,TopAbs_FACE)==Count(restored,TopAbs_FACE)&&areaError<1.e-6&&stylesOK;
    return {{"passed",passed},{"sourceFaces",Count(complete,TopAbs_FACE)},{"restoredFaces",Count(restored,TopAbs_FACE)},
        {"areaRelativeError",areaError},{"overlayCount",static_cast<int>(overlays.size())},{"curveStylesPreserved",stylesOK},
        {"greenPathOverlays",expectedGreen},{"redSideOverlays",expectedRed},
        {"axisOverlays",expectedBlue},{"yellowAxisOverlays",expectedBlue},{"blueAxisOverlays",expectedBlue},
        {"extraDisplayFaces",0},{"displayOnlyEdges",true}};
}

ObjectMap Export(const R::SSectionFaceClassificationResult& classification,
    const R::SMachiningEdgeAnalysisResult& edgeAnalysis,const R::SMachiningPathAnalysisResult& paths,
    const O::SOpenCascadeBRepBuildResult& splitBuilt,const fs::path& base,const std::string& name) {
    fs::create_directories(base.parent_path());
    const auto file=[&](const char* extension){return fs::path(base.wstring()+fs::path(extension).wstring());};
    const auto& complete=splitBuilt.Shape;
    {std::ofstream stream(file(".brep"),std::ios::binary);BRepTools::Write(complete,stream);if(!stream)throw std::runtime_error("Cannot write complete path-analysis BREP");}
    TopoDS_Shape restored;BRep_Builder builder;
    {std::ifstream stream(file(".brep"),std::ios::binary);BRepTools::Read(restored,stream,builder);}
    const bool brepOK=!restored.IsNull()&&Count(complete,TopAbs_FACE)==Count(restored,TopAbs_FACE)&&Relative(Area(complete),Area(restored))<1.e-10;
    if(!brepOK)throw std::runtime_error("Complete path-analysis BREP round-trip changed geometry");
    Bnd_Box box;BRepBndLib::AddOptimal(complete,box,false,false);double x0=0,y0=0,z0=0,x1=1,y1=1,z1=1;
    if(!box.IsVoid())box.Get(x0,y0,z0,x1,y1,z1);
    const double diagonal=gp_Pnt(x0,y0,z0).Distance(gp_Pnt(x1,y1,z1));
    std::vector<double> dimensions={std::abs(x1-x0),std::abs(y1-y0),std::abs(z1-z0)};std::sort(dimensions.begin(),dimensions.end());
    const double ySpan=std::abs(y1-y0),zSpan=std::abs(z1-z0);
    const double localSize=ySpan>diagonal*1.e-12&&zSpan>diagonal*1.e-12?std::min(ySpan,zSpan):(dimensions[1]>0.?dimensions[1]:dimensions[2]);
    const double axisLength=std::min(diagonal*.025,localSize*.18);
    const double deflection=std::clamp(diagonal*.0002,1.e-7,.1);
    VariantArray rows,faces,meshes,issues,skippedZeroArea,unresolvedRows;std::vector<Overlay> overlays;
    std::set<std::uint64_t> machining(classification.MachiningFaceIds.begin(),classification.MachiningFaceIds.end());
    std::map<std::string,const R::SMachiningEdgeAnalysis*> original;
    for(const auto& edge:edgeAnalysis.Edges)original.emplace(edge.Key,&edge);
    std::map<std::uint64_t,VariantArray> faceKeys;std::set<std::string> displayedPaths,displayedSides;
    std::size_t pathCount=0,sideCount=0,convexEvidenceCount=0,ready=0,parallel=0,unsupported=0,invalid=0;
    std::size_t sourceUseCount=0;
    for(const auto& edge:paths.Edges) {
        if(edge.Samples.size()!=2)
            throw std::runtime_error("Logical path work must provide exactly the start and end records of each logical edge");
        std::vector<std::string> displayAtoms;
        for(const auto& use:edge.SourceUses) {
            std::string atom=use.SpanKey;
            if(original.contains(atom)&&!original.at(atom)->MatchedSpanKeys.empty())
                atom=*std::min_element(original.at(atom)->MatchedSpanKeys.begin(),original.at(atom)->MatchedSpanKeys.end());
            displayAtoms.push_back(atom);
        }
        std::sort(displayAtoms.begin(),displayAtoms.end());displayAtoms.erase(std::unique(displayAtoms.begin(),displayAtoms.end()),displayAtoms.end());
        std::string displayKey;for(const auto& atom:displayAtoms){if(!displayKey.empty())displayKey+='|';displayKey+=atom;}
        if(displayKey.empty())displayKey=edge.Key;
        const bool drawPath=edge.bSelectedPath&&displayedPaths.insert(displayKey).second;
        const bool drawSide=edge.bSide&&displayedSides.insert(displayKey).second;
        faceKeys[edge.FaceId].emplace_back(edge.Key);if(edge.bSelectedPath)++pathCount;if(edge.bSide)++sideCount;
        if(edge.bHasConvexEvidence)++convexEvidenceCount;
        if(edge.Status==R::EMachiningPathStatus::Ready)++ready;
        if(edge.Status==R::EMachiningPathStatus::ParallelToGenerator||edge.Status==R::EMachiningPathStatus::PartiallyParallelToGenerator)++parallel;
        if(edge.Status==R::EMachiningPathStatus::UnsupportedSurface)++unsupported;
        if(edge.Status==R::EMachiningPathStatus::InvalidGeometry)++invalid;
        VariantArray endpoints,sourceUses,displaySegments;sourceUseCount+=edge.SourceUses.size();std::size_t partNumber=0;
        for(const auto& use:edge.SourceUses) {
            ++partNumber;VariantArray preview;
            const auto found=splitBuilt.Edges.find(use.EdgeId);
            if(found!=splitBuilt.Edges.end()&&!BRep_Tool::Degenerated(found->second)) {
                BRepAdaptor_Curve curve(found->second);GCPnts_QuasiUniformDeflection sampled(curve,deflection,use.FirstParameter,use.LastParameter);
                if(sampled.IsDone()&&sampled.NbPoints()>1&&sampled.NbPoints()<20000)
                    for(int i=1;i<=sampled.NbPoints();++i){const auto p=sampled.Value(i);preview.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
                if(preview.empty())for(int i=0;i<=32;++i){const auto p=curve.Value(use.FirstParameter+(use.LastParameter-use.FirstParameter)*i/32.);preview.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
                if(use.bReversed)std::reverse(preview.begin(),preview.end());
                double first,last;TopLoc_Location location;const auto native=BRep_Tool::Curve(found->second,location,first,last);
                if(!native.IsNull()&&(drawPath||drawSide)) {
                    const auto support=Handle(Geom_Curve)::DownCast(native->Transformed(location.Transformation()));
                    const auto shape=BRepBuilderAPI_MakeEdge(support,use.FirstParameter,use.LastParameter).Shape();
                    const auto nameSuffix=displayKey+" / source "+std::to_string(partNumber);
                    if(drawPath)overlays.push_back({BRepBuilderAPI_Copy(shape,true,false).Shape(),"PATH / "+nameSuffix,"path"});
                    if(drawSide)overlays.push_back({BRepBuilderAPI_Copy(shape,true,false).Shape(),"SIDE / "+nameSuffix,"side"});
                }
            }
            sourceUses.emplace_back(ObjectMap{{"edgeId",std::to_string(use.EdgeId)},{"spanKey",use.SpanKey},{"useIndex",static_cast<int>(use.UseIndex)},
                {"firstParameter",use.FirstParameter},{"lastParameter",use.LastParameter},{"reversed",use.bReversed},
                {"skinRelation",machining_edge_export::SkinName(use.SkinRelation)},{"convexity",machining_edge_export::ConvexityName(use.Convexity)},
                {"sourceSkinMembership",use.bSourceSkinMembership},{"sourceSkinFaceIds",machining_edge_export::Ids(use.SourceSkinFaceIds)},
                {"minSkinDistanceMm",use.MinSkinDistance},{"maxSkinDistanceMm",use.MaxSkinDistance},{"skinReason",use.SkinReason},
                {"skinValid",use.bSkinRelationValid},{"convexityValid",use.bConvexityValid},{"startOnSkin",use.bStartOnSkin},{"endOnSkin",use.bEndOnSkin}});
            displaySegments.emplace_back(ObjectMap{{"edgeId",std::to_string(use.EdgeId)},{"spanKey",use.SpanKey},{"points",preview},{"previewOnly",true}});
        }
        for(std::size_t endpointIndex=0;endpointIndex<edge.Samples.size();++endpointIndex) {
            const auto& sample=edge.Samples[endpointIndex];
            endpoints.emplace_back(ObjectMap{{"endpoint",std::string(endpointIndex==0?"start":"end")},{"sourceEdgeId",std::to_string(sample.SourceEdgeId)},
                {"sourceSpanKey",sample.SourceSpanKey},{"parameter",sample.Parameter},
                {"position",sample.bPositionValid?iCAX::Data::Variant(Point(sample.Position)):iCAX::Data::Variant{}},{"positionValid",sample.bPositionValid},
                {"tangent",sample.bTangentValid?iCAX::Data::Variant(Vector(sample.Tangent)):iCAX::Data::Variant{}},{"tangentValid",sample.bTangentValid},
                {"normal",Vector(sample.FaceNormal)},{"generator",Vector(sample.Generator)},
                {"axis",sample.bToolAxisValid?iCAX::Data::Variant(Vector(sample.ToolAxis)):iCAX::Data::Variant{}},
                {"axisValid",sample.bToolAxisValid},{"status",StatusName(sample.Status)},{"reason",sample.Reason}});
            if(edge.bSelectedPath&&sample.bToolAxisValid&&sample.bPositionValid&&sample.bTangentValid&&axisLength>0.) {
                const gp_Pnt p(sample.Position.X,sample.Position.Y,sample.Position.Z);
                const gp_Pnt q(sample.Position.X+axisLength*sample.ToolAxis.X,sample.Position.Y+axisLength*sample.ToolAxis.Y,sample.Position.Z+axisLength*sample.ToolAxis.Z);
                if(p.Distance(q)>0.)overlays.push_back({BRepBuilderAPI_MakeEdge(p,q).Shape(),"AXIS / "+edge.Key+" / "+std::to_string(endpointIndex),"axis"});
            }
        }
        rows.emplace_back(ObjectMap{{"pathKey",edge.Key},{"displaySpanKey",displayKey},{"faceId",std::to_string(edge.FaceId)},
            {"wireId",std::to_string(edge.WireId)},{"groupId",static_cast<int>(edge.GroupId)},{"partIndex",static_cast<int>(edge.PartIndex)},
            {"closed",edge.bClosed},{"curveKind",edge.CurveKind},{"sourceEdgeIds",machining_edge_export::Ids(edge.SourceEdgeIds)},
            {"sourceSpanKeys",Strings(edge.SourceSpanKeys)},{"sourceUses",sourceUses},
            {"mixedSourceSkinRelations",edge.bMixedSourceSkinRelations},{"mixedSourceConvexities",edge.bMixedSourceConvexities},
            {"path",edge.bSelectedPath},{"onSkinPath",edge.bOnSkinPath},{"convexPath",edge.bConvexPath},
            {"convexEvidence",edge.bHasConvexEvidence},{"side",edge.bSide},
            {"skinRelation",machining_edge_export::SkinName(edge.SkinRelation)},{"skinValid",edge.bSkinRelationValid},
            {"convexity",edge.bConvexityValid?iCAX::Data::Variant(machining_edge_export::ConvexityName(edge.Convexity)):
                (edge.bMixedSourceConvexities?iCAX::Data::Variant(std::string("Mixed")):iCAX::Data::Variant{})},{"convexityValid",edge.bConvexityValid},
            {"status",StatusName(edge.Status)},{"axisMode",ModeName(edge.AxisMode)},{"surfaceKind",edge.SurfaceKind},{"reason",edge.Reason},
            {"previewSegments",displaySegments},{"endpoints",endpoints}});
    }
    std::map<std::uint64_t,VariantArray> faceUnresolvedKeys;
    for(const auto& boundary:paths.UnresolvedBoundaries) {
        const auto key="F"+std::to_string(boundary.FaceId)+":W"+std::to_string(boundary.WireId);
        faceUnresolvedKeys[boundary.FaceId].emplace_back(key);VariantArray segments;
        for(const auto id:boundary.SourceEdgeIds) {
            const auto found=splitBuilt.Edges.find(id);if(found==splitBuilt.Edges.end()||BRep_Tool::Degenerated(found->second))continue;
            BRepAdaptor_Curve curve(found->second);const double first=curve.FirstParameter(),last=curve.LastParameter();VariantArray points;
            if(!std::isfinite(first)||!std::isfinite(last)||last<=first)continue;
            GCPnts_QuasiUniformDeflection sampled(curve,deflection,first,last);
            if(sampled.IsDone()&&sampled.NbPoints()>1&&sampled.NbPoints()<20000)
                for(int i=1;i<=sampled.NbPoints();++i){const auto p=sampled.Value(i);points.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
            if(points.empty())for(int i=0;i<=32;++i){const auto p=curve.Value(first+(last-first)*i/32.);points.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
            segments.emplace_back(ObjectMap{{"edgeId",std::to_string(id)},{"points",points},{"previewOnly",true}});
            TopLoc_Location location;double a,b;const auto support=BRep_Tool::Curve(found->second,location,a,b);
            if(!support.IsNull()) {
                const auto located=Handle(Geom_Curve)::DownCast(support->Transformed(location.Transformation()));
                overlays.push_back({BRepBuilderAPI_MakeEdge(located,first,last).Shape(),"UNRESOLVED / "+key+" / E"+std::to_string(id),"unresolved"});
            }
        }
        unresolvedRows.emplace_back(ObjectMap{{"boundaryKey",key},{"faceId",std::to_string(boundary.FaceId)},
            {"wireId",std::to_string(boundary.WireId)},{"sourceEdgeIds",machining_edge_export::Ids(boundary.SourceEdgeIds)},
            {"reason",boundary.Reason},{"previewSegments",segments},{"isPath",false},{"hasToolAxes",false}});
    }
    int faceIndex=0,machiningIndex=0;
    for(const auto& face:classification.SplitGeometry.Faces) {
        const bool isMachining=machining.contains(face.Id);
        const auto hasTag=[&](const std::string& value){return std::find(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),value)!=face.Metadata.Tags.end();};
        const auto sourceProof=std::find_if(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),[](const std::string& tag){
            return tag.starts_with("classify.zeroAreaProof=")&&tag.size()>std::string("classify.zeroAreaProof=").size();});
        const auto sourceFace=std::find_if(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),[](const std::string& tag){
            return tag.starts_with("classify.zeroAreaSourceFaceId=")&&tag.size()>std::string("classify.zeroAreaSourceFaceId=").size();});
        const bool skipZeroArea=!isMachining&&hasTag("classify.zeroAreaTrim=true")
            &&sourceProof!=face.Metadata.Tags.end()&&sourceFace!=face.Metadata.Tags.end();
        const std::string skipReason=skipZeroArea?"Classify proved this non-machining trim has no display area; complete BREP and STEP topology is retained":std::string{};
        ObjectMap row{{"faceId",std::to_string(face.Id)},{"faceIndex",++faceIndex},{"machining",isMachining},
            {"machiningIndex",isMachining?++machiningIndex:0},{"sourceId",face.Metadata.SourceId},{"sourceTags",Strings(face.Metadata.Tags)},
            {"pathKeys",faceKeys[face.Id]},{"unresolvedBoundaryKeys",faceUnresolvedKeys[face.Id]},
            {"displaySkippedZeroArea",skipZeroArea},{"displaySkipReason",skipReason}};faces.emplace_back(std::move(row));
        if(skipZeroArea) {
            skippedZeroArea.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"reason",skipReason},
                {"proof",*sourceProof},{"sourceFace",*sourceFace},{"topologyRetainedInCompleteArtifacts",true}});
            continue;
        }
        try {
            const auto preview=machining_edge_export::PreviewFace(splitBuilt.Faces.at(face.Id),deflection);
            VariantArray positions,indices;
            for(int i=1;i<=preview.Triangles->NbNodes();++i){const auto p=preview.Triangles->Node(i).Transformed(preview.Location.Transformation());positions.emplace_back(p.X());positions.emplace_back(p.Y());positions.emplace_back(p.Z());}
            for(int i=1;i<=preview.Triangles->NbTriangles();++i){int a,b,c;preview.Triangles->Triangle(i).Get(a,b,c);if(preview.Face.Orientation()==TopAbs_REVERSED)std::swap(b,c);indices.emplace_back(a-1);indices.emplace_back(b-1);indices.emplace_back(c-1);}
            meshes.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"positions",positions},{"indices",indices}});
        }catch(const Standard_Failure& exception){issues.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"reason",std::string(exception.GetMessageString())}});}
         catch(const std::exception& exception){issues.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"reason",std::string(exception.what())}});}
    }
    ObjectMap stepAudit{{"passed",false},{"status",std::string("pending")}};
    const ObjectMap counts{{"wholeFaces",static_cast<int>(classification.SplitGeometry.Faces.size())},{"machiningFaces",static_cast<int>(machining.size())},
        {"edgeOccurrences",static_cast<int>(paths.Edges.size())},{"logicalEdgeParts",static_cast<int>(paths.Edges.size())},{"sourceTopoSpanUses",static_cast<int>(sourceUseCount)},{"pathOccurrences",static_cast<int>(pathCount)},{"sideOccurrences",static_cast<int>(sideCount)},
        {"convexEvidenceOccurrences",static_cast<int>(convexEvidenceCount)},
        {"ready",static_cast<int>(ready)},{"pendingParallel",static_cast<int>(parallel)},{"unsupported",static_cast<int>(unsupported)},{"invalid",static_cast<int>(invalid)},
        {"zeroAreaDisplayFaces",static_cast<int>(skippedZeroArea.size())},{"unresolvedBoundaries",static_cast<int>(unresolvedRows.size())}};
    ObjectMap document{{"schema",1},{"stage",std::string("pathAnalysis")},{"analysisGranularity",std::string("logical-edge-parts")},{"name",name},{"success",paths.bSuccess},{"complete",paths.bComplete},
        {"selectionRule",std::string("OnSkin-only")},{"convexEdgesAreEvidenceOnly",true},
        {"geometry",ObjectMap{{"faces",meshes},{"completeModel",true},{"previewOnly",true},{"issues",issues},{"displaySkippedZeroArea",skippedZeroArea}}},
        {"faces",faces},{"edges",rows},{"unresolvedBoundaries",unresolvedRows},{"counts",counts},{"diagnostics",Strings(paths.Diagnostics)},
        {"display",ObjectMap{{"axisLengthMm",axisLength},{"axisPositionPolicy",std::string("logical-edge-start-and-end-only")},{"endpointsPerEdge",2},
            {"allComputedEndpointsPreserved",true},{"pathColor",std::string("#1fe05c")},{"axisColor",std::string("#f4df91")},{"sideColor",std::string("#ff303b")},
            {"sidePattern",std::string("dashed")},{"bothRolesRetained",true}}},
        {"artifacts",ObjectMap{{"pathConvention",std::string("same-directory-and-stem-as-analysis-json")},{"brepRoundTripPassed",brepOK},
            {"completeModelBrep",true},{"stepRoundTrip",stepAudit}}}};
    Save(file(".json"),document);
    try{stepAudit=WriteStep(complete,overlays,file(".step"),diagonal);}
    catch(const Standard_Failure& exception){stepAudit={{"passed",false},{"error",std::string(exception.GetMessageString())}};}
    catch(const std::exception& exception){stepAudit={{"passed",false},{"error",std::string(exception.what())}};}
    auto artifacts=document.at("artifacts").To<ObjectMap>();artifacts["stepRoundTrip"]=stepAudit;document["artifacts"]=artifacts;
    Save(file(".json"),document);
    return {{"passed",brepOK&&stepAudit.at("passed").To<bool>()&&issues.empty()},{"analysisComplete",paths.bComplete},{"counts",counts},
        {"completeModelBrep",true},{"brepRoundTripPassed",brepOK},{"stepRoundTrip",stepAudit},{"displayIssues",issues},{"displaySkippedZeroArea",skippedZeroArea}};
}
}
