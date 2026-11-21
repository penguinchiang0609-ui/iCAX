#include <ExtrusionRecognition/MachiningFaceCutCoverageWork.h>

// Included after the existing edge/path presentation helpers. Analysis results
// retain native curve parameters. Tessellation below is presentation only.
namespace machining_face_cut_coverage_export
{
std::string KindName(R::EFaceCutCoverageKind value) {
    switch(value) {
    case R::EFaceCutCoverageKind::Partial:return "Partial";
    case R::EFaceCutCoverageKind::Through:return "Through";
    case R::EFaceCutCoverageKind::Pending:return "Pending";
    }
    throw std::logic_error("Unknown face-cut coverage kind");
}

std::string EvidenceName(R::EFaceCutEvidenceKind value) {
    switch(value) {
    case R::EFaceCutEvidenceKind::SkinExit:return "SkinExit";
    case R::EFaceCutEvidenceKind::SkinExitBoundarySet:return "SkinExitBoundarySet";
    case R::EFaceCutEvidenceKind::ConvexBoundary:return "ConvexBoundary";
    case R::EFaceCutEvidenceKind::SideCompletion:return "SideCompletion";
    case R::EFaceCutEvidenceKind::ConcaveTermination:return "ConcaveTermination";
    case R::EFaceCutEvidenceKind::OtherBoundary:return "OtherBoundary";
    }
    throw std::logic_error("Unknown face-cut evidence kind");
}

Quantity_Color Color(R::EFaceCutCoverageKind value) {
    if(value==R::EFaceCutCoverageKind::Through)return Quantity_Color(.12,.88,.36,Quantity_TOC_RGB);
    if(value==R::EFaceCutCoverageKind::Partial)return Quantity_Color(.96,.62,.04,Quantity_TOC_RGB);
    return Quantity_Color(.58,.64,.72,Quantity_TOC_RGB);
}

struct Overlay final {
    TopoDS_Shape Shape;std::string Name;R::EFaceCutCoverageKind Kind;
    bool PointMarker=false;gp_Pnt MarkerPosition;
};

ObjectMap WriteStep(const TopoDS_Shape& complete,const std::vector<Overlay>& overlays,const fs::path& file) {
    Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");
    const auto shapes=XCAFDoc_DocumentTool::ShapeTool(document->Main());
    const auto colors=XCAFDoc_DocumentTool::ColorTool(document->Main());
    bool valid=false;const auto body=FlatArtifact(StepBoundaryCopy(complete,false,false,&valid));
    if(!valid)throw std::runtime_error("Invalid complete face-cut coverage STEP presentation");
    const auto bodyLabel=AddArtifactFaces(shapes,body,"Complete model / face-cut coverage");
    colors->SetColor(bodyLabel,Quantity_Color(.67,.71,.76,Quantity_TOC_RGB),XCAFDoc_ColorSurf);
    for(const auto& overlay:overlays) {
        const auto label=shapes->AddShape(overlay.Shape,false);
        TDataStd_Name::Set(label,overlay.Name.c_str());
        colors->SetColor(label,Color(overlay.Kind),XCAFDoc_ColorGen);
        colors->SetColor(label,Color(overlay.Kind),XCAFDoc_ColorCurv);
    }
    Interface_Static::SetCVal("write.step.schema","AP214IS");
    STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);
    writer.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
    if(!writer.Transfer(document,STEPControl_AsIs))throw std::runtime_error("Cannot transfer coverage STEP");
    if(writer.Write(Utf8(file).c_str())!=IFSelect_RetDone)throw std::runtime_error("Cannot write coverage STEP");
    STEPCAFControl_Reader reader;reader.SetColorMode(true);reader.SetNameMode(true);
    Handle(TDocStd_Document) restoredDocument=new TDocStd_Document("BinXCAF");
    if(reader.ReadFile(Utf8(file).c_str())!=IFSelect_RetDone||!reader.Transfer(restoredDocument))
        throw std::runtime_error("Cannot reopen coverage STEP");
    const auto restoredShapes=XCAFDoc_DocumentTool::ShapeTool(restoredDocument->Main());
    const auto restoredColors=XCAFDoc_DocumentTool::ColorTool(restoredDocument->Main());
    TDF_LabelSequence roots;restoredShapes->GetFreeShapes(roots);
    BRep_Builder builder;TopoDS_Compound restored;builder.MakeCompound(restored);
    for(int i=1;i<=roots.Length();++i)builder.Add(restored,restoredShapes->GetShape(roots.Value(i)));
    std::map<std::string,Quantity_Color> expected;
    std::map<std::string,gp_Pnt> expectedPoints;
    for(const auto& overlay:overlays) {
        expected.emplace(overlay.Name,Color(overlay.Kind));
        if(overlay.PointMarker)expectedPoints.emplace(overlay.Name,overlay.MarkerPosition);
    }
    std::set<std::string> found,foundPoints;bool colorsOK=true,pointsOK=true;
    double maxPointError=0.;
    for(TDF_ChildIterator it(restoredDocument->Main(),true);it.More();it.Next()) {
        Handle(TDataStd_Name) name;if(!it.Value().FindAttribute(TDataStd_Name::GetID(),name))continue;
        const std::string text=TCollection_AsciiString(name->Get()).ToCString();
        const auto expectedColor=expected.find(text);if(expectedColor==expected.end())continue;
        Quantity_Color color;
        if(!restoredColors->GetColor(it.Value(),XCAFDoc_ColorCurv,color)
            &&!restoredColors->GetColor(it.Value(),XCAFDoc_ColorGen,color))continue;
        colorsOK=colorsOK&&color.Distance(expectedColor->second)<1.e-5;found.insert(text);
        const auto expectedPoint=expectedPoints.find(text);
        if(expectedPoint!=expectedPoints.end()) {
            TopTools_IndexedMapOfShape vertices;
            TopExp::MapShapes(restoredShapes->GetShape(it.Value()),TopAbs_VERTEX,vertices);
            if(vertices.Extent()!=1)pointsOK=false;
            else {
                const double error=BRep_Tool::Pnt(TopoDS::Vertex(vertices(1))).Distance(expectedPoint->second);
                maxPointError=std::max(maxPointError,error);pointsOK=pointsOK&&error<=Precision::Confusion();
                foundPoints.insert(text);
            }
        }
    }
    const double areaError=Relative(Area(complete),Area(restored));
    const bool passed=Count(complete,TopAbs_FACE)==Count(restored,TopAbs_FACE)&&areaError<1.e-6
        &&found.size()==expected.size()&&colorsOK&&pointsOK&&foundPoints.size()==expectedPoints.size();
    return {{"passed",passed},{"sourceFaces",Count(complete,TopAbs_FACE)},
        {"restoredFaces",Count(restored,TopAbs_FACE)},{"areaRelativeError",areaError},
        {"overlayCount",static_cast<int>(overlays.size())},{"restoredOverlayNames",static_cast<int>(found.size())},
        {"curveStylesPreserved",found.size()==expected.size()&&colorsOK},{"displayOnlyEdges",true},
        {"pointMarkerCount",static_cast<int>(expectedPoints.size())},{"restoredPointMarkers",static_cast<int>(foundPoints.size())},
        {"pointMarkerPositionsPreserved",pointsOK&&foundPoints.size()==expectedPoints.size()},
        {"pointMarkerMaxPositionErrorMm",maxPointError}};
}

VariantArray Preview(const O::SOpenCascadeBRepBuildResult& built,std::uint64_t edgeId,
    double first,double last,double deflection) {
    VariantArray points;const auto found=built.Edges.find(edgeId);
    if(found==built.Edges.end()||BRep_Tool::Degenerated(found->second))return points;
    BRepAdaptor_Curve curve(found->second);
    GCPnts_QuasiUniformDeflection display(curve,deflection,first,last);
    if(display.IsDone()&&display.NbPoints()>1&&display.NbPoints()<20000)
        for(int i=1;i<=display.NbPoints();++i){const auto p=display.Value(i);points.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
    if(points.empty())for(int i=0;i<=32;++i){const auto p=curve.Value(first+(last-first)*i/32.);points.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
    return points;
}

VariantArray TargetProperties(const R::SMachiningEdgeAnalysisResult& analysis,
    const R::SMachiningFaceCutCoverageSpan& span) {
    VariantArray values;
    for(const auto& edge:analysis.Edges) {
        if(edge.EdgeId!=span.SourceEdgeId)continue;
        if(std::find(edge.MachiningFaceIds.begin(),edge.MachiningFaceIds.end(),span.FaceId)==edge.MachiningFaceIds.end())continue;
        const double first=std::max(span.FirstParameter,edge.FirstParameter),last=std::min(span.LastParameter,edge.LastParameter);
        if(!(last>first))continue;
        values.emplace_back(ObjectMap{{"targetSpanKey",edge.Key},{"firstParameter",first},{"lastParameter",last},
            {"skinRelation",machining_edge_export::SkinName(edge.SkinRelation)},{"skinValid",edge.bSkinRelationValid},
            {"convexity",machining_edge_export::ConvexityName(edge.Convexity)},{"convexityValid",edge.bConvexityValid},
            {"sourceSkinMembership",edge.bSourceSkinMembership},{"sourceSkinFaceIds",machining_edge_export::Ids(edge.SourceSkinFaceIds)}});
    }
    return values;
}

ObjectMap Export(const R::SSectionFaceClassificationResult& classification,
    const R::SMachiningEdgeAnalysisResult& edgeAnalysis,const R::SMachiningPathAnalysisResult& paths,
    const R::SMachiningFaceCutCoverageResult& coverage,const O::SOpenCascadeBRepBuildResult& built,
    const fs::path& base,const std::string& name) {
    fs::create_directories(base.parent_path());
    const auto file=[&](const char* extension){return fs::path(base.wstring()+fs::path(extension).wstring());};
    const auto& complete=built.Shape;
    {std::ofstream stream(file(".brep"),std::ios::binary);BRepTools::Write(complete,stream);if(!stream)throw std::runtime_error("Cannot write complete coverage BREP");}
    TopoDS_Shape restored;BRep_Builder builder;
    {std::ifstream stream(file(".brep"),std::ios::binary);BRepTools::Read(restored,stream,builder);}
    const bool brepOK=!restored.IsNull()&&Count(complete,TopAbs_FACE)==Count(restored,TopAbs_FACE)&&Relative(Area(complete),Area(restored))<1.e-10;
    if(!brepOK)throw std::runtime_error("Complete coverage BREP round trip changed geometry");
    Bnd_Box bounds;BRepBndLib::AddOptimal(complete,bounds,false,false);double x=0,y=0,z=0,X=1,Y=1,Z=1;
    if(!bounds.IsVoid())bounds.Get(x,y,z,X,Y,Z);
    const double diagonal=gp_Pnt(x,y,z).Distance(gp_Pnt(X,Y,Z)),deflection=std::clamp(diagonal*.0002,1.e-7,.1);
    VariantArray sourceRows,arrivalRows,evidenceRows,spanRows,faces,meshes,issues,skipped,referenceEdges,sideRows;
    std::map<std::uint64_t,std::vector<const R::SMachiningEdgeAnalysis*>> referenceSpans;
    for(const auto& edge:edgeAnalysis.Edges)referenceSpans[edge.EdgeId].push_back(&edge);
    std::map<std::uint64_t,std::set<std::uint64_t>> unresolvedOwners;
    std::map<std::uint64_t,VariantArray> unresolvedReferences;
    for(const auto& boundary:paths.UnresolvedBoundaries)for(const auto edgeId:boundary.SourceEdgeIds) {
        referenceSpans.try_emplace(edgeId);unresolvedOwners[edgeId].insert(boundary.FaceId);
        unresolvedReferences[edgeId].emplace_back(ObjectMap{{"faceId",std::to_string(boundary.FaceId)},
            {"wireId",std::to_string(boundary.WireId)},{"reason",boundary.Reason}});
    }
    for(const auto& [edgeId,spans]:referenceSpans) {
        const auto found=built.Edges.find(edgeId);if(found==built.Edges.end()||BRep_Tool::Degenerated(found->second))continue;
        BRepAdaptor_Curve curve(found->second);std::set<std::uint64_t> owners=unresolvedOwners[edgeId],sourceOwners=unresolvedOwners[edgeId];VariantArray properties,previews;
        for(const auto* span:spans) {
            owners.insert(span->MachiningFaceIds.begin(),span->MachiningFaceIds.end());
            sourceOwners.insert(span->SourceMachiningFaceIds.begin(),span->SourceMachiningFaceIds.end());
            properties.emplace_back(ObjectMap{{"edgeKey",span->Key},{"firstParameter",span->FirstParameter},{"lastParameter",span->LastParameter},
                {"skinRelation",span->bSkinRelationValid?iCAX::Data::Variant(machining_edge_export::SkinName(span->SkinRelation)):iCAX::Data::Variant{}},
                {"convexity",span->bConvexityValid?iCAX::Data::Variant(machining_edge_export::ConvexityName(span->Convexity)):iCAX::Data::Variant{}},
                {"skinValid",span->bSkinRelationValid},{"convexityValid",span->bConvexityValid},
                {"skinReason",span->SkinReason},{"convexityReason",span->ConvexityReason}});
        }
        previews.emplace_back(ObjectMap{{"edgeId",std::to_string(edgeId)},
            {"points",Preview(built,edgeId,curve.FirstParameter(),curve.LastParameter(),deflection)},{"previewOnly",true}});
        referenceEdges.emplace_back(ObjectMap{{"edgeId",std::to_string(edgeId)},
            {"machiningFaceIds",machining_edge_export::Ids(std::vector<std::uint64_t>(owners.begin(),owners.end()))},
            {"sourceMachiningFaceIds",machining_edge_export::Ids(std::vector<std::uint64_t>(sourceOwners.begin(),sourceOwners.end()))},
            {"firstParameter",curve.FirstParameter()},{"lastParameter",curve.LastParameter()},
            {"spans",properties},{"unresolvedBoundaries",unresolvedReferences[edgeId]},
            {"previewSegments",previews},{"previewOnly",true}});
    }
    std::map<std::string,const R::SMachiningPathEdgeAnalysis*> pathByKey;
    for(const auto& path:paths.Edges)pathByKey.emplace(path.Key,&path);
    std::size_t completeSources=0;
    for(const auto& source:coverage.Sources) {
        if(source.bComplete)++completeSources;
        ObjectMap row{{"faceId",std::to_string(source.FaceId)},{"sourcePathKey",source.SourcePathKey},
            {"sourceEdgeIds",machining_edge_export::Ids(source.SourceEdgeIds)},{"complete",source.bComplete},
            {"classificationProofComplete",source.bComplete},{"exitCorrespondenceExpanded",source.bExitCorrespondenceExpanded},
            {"method",source.Method},{"reason",source.Reason}};
        const auto found=pathByKey.find(source.SourcePathKey);
        if(found!=pathByKey.end()) {
            const auto& path=*found->second;VariantArray uses,endpoints,previews;
            for(const auto& use:path.SourceUses) {
                uses.emplace_back(ObjectMap{{"edgeId",std::to_string(use.EdgeId)},{"spanKey",use.SpanKey},
                    {"firstParameter",use.FirstParameter},{"lastParameter",use.LastParameter},{"reversed",use.bReversed},
                    {"skinRelation",machining_edge_export::SkinName(use.SkinRelation)},
                    {"convexity",machining_edge_export::ConvexityName(use.Convexity)}});
                auto points=Preview(built,use.EdgeId,use.FirstParameter,use.LastParameter,deflection);
                if(use.bReversed)std::reverse(points.begin(),points.end());
                previews.emplace_back(ObjectMap{{"edgeId",std::to_string(use.EdgeId)},
                    {"firstParameter",use.FirstParameter},{"lastParameter",use.LastParameter},
                    {"points",points},{"previewOnly",true}});
            }
            for(std::size_t i=0;i<path.Samples.size();++i) {
                const auto& endpoint=path.Samples[i];
                endpoints.emplace_back(ObjectMap{{"endpoint",std::string(i==0?"start":"end")},
                    {"sourceEdgeId",std::to_string(endpoint.SourceEdgeId)},{"parameter",endpoint.Parameter},
                    {"position",endpoint.bPositionValid?iCAX::Data::Variant(machining_path_export::Point(endpoint.Position)):iCAX::Data::Variant{}},
                    {"positionValid",endpoint.bPositionValid},
                    {"axis",endpoint.bToolAxisValid?iCAX::Data::Variant(machining_path_export::Vector(endpoint.ToolAxis)):iCAX::Data::Variant{}},
                    {"axisValid",endpoint.bToolAxisValid},{"status",machining_path_export::StatusName(endpoint.Status)}});
            }
            row["onSkinPath"]=path.bOnSkinPath;row["convexPath"]=path.bConvexPath;
            row["convexEvidence"]=path.bHasConvexEvidence;
            row["axisMode"]=machining_path_export::ModeName(path.AxisMode);row["surfaceKind"]=path.SurfaceKind;
            row["sourceUses"]=uses;row["endpoints"]=endpoints;row["previewSegments"]=previews;
        }
        sourceRows.emplace_back(std::move(row));
    }
    for(const auto& arrival:coverage.Arrivals) {
        arrivalRows.emplace_back(ObjectMap{{"faceId",std::to_string(arrival.FaceId)},{"sourcePathKey",arrival.SourcePathKey},
            {"sourceEdgeId",std::to_string(arrival.SourceEdgeId)},{"sourceFirstParameter",arrival.SourceFirstParameter},
            {"sourceLastParameter",arrival.SourceLastParameter},{"targetEdgeId",std::to_string(arrival.TargetEdgeId)},
            {"targetFirstParameter",arrival.TargetFirstParameter},{"targetLastParameter",arrival.TargetLastParameter},
            {"targetSpanKey",arrival.TargetSpanKey},{"targetSkinRelation",machining_edge_export::SkinName(arrival.TargetSkinRelation)},
            {"targetConvexity",machining_edge_export::ConvexityName(arrival.TargetConvexity)},
            {"targetSkinValid",arrival.bTargetSkinRelationValid},{"targetConvexityValid",arrival.bTargetConvexityValid},
            {"method",arrival.Method},{"reason",arrival.Reason}});
    }
    for(const auto& side:coverage.SideCorrespondences)sideRows.emplace_back(ObjectMap{
        {"arrivalIndex",static_cast<int>(side.ArrivalIndex)},{"sidePathKey",side.SidePathKey},{"sideSpanKey",side.SideSpanKey}});
    for(std::size_t i=0;i<coverage.Evidence.size();++i) {
        const auto& evidence=coverage.Evidence[i];
        ObjectMap row{{"index",static_cast<int>(i)},
            {"faceId",std::to_string(evidence.FaceId)},{"sourcePathKey",evidence.SourcePathKey},
            {"sourceEdgeId",std::to_string(evidence.SourceEdgeId)},
            {"sourceFirstParameter",evidence.SourceFirstParameter},{"sourceLastParameter",evidence.SourceLastParameter},
            {"hasArrival",evidence.bHasArrival},{"classificationProofComplete",evidence.bComplete},
            {"exitCorrespondenceExpanded",evidence.bExitCorrespondenceExpanded},
            {"kind",EvidenceName(evidence.Kind)},{"through",evidence.bThrough},{"complete",evidence.bComplete},{"reason",evidence.Reason}};
        if(evidence.bHasArrival) {
            row["witnessEdgeId"]=std::to_string(evidence.WitnessEdgeId);row["witnessLogicEdgeId"]=evidence.WitnessLogicEdgeId;
            row["witnessSpanKey"]=evidence.WitnessSpanKey;row["witnessFirstParameter"]=evidence.WitnessFirstParameter;
            row["witnessLastParameter"]=evidence.WitnessLastParameter;row["arrivalIndex"]=static_cast<int>(evidence.ArrivalIndex);
        }
        VariantArray witnesses;
        for(const auto& witness:evidence.BoundaryWitnesses)witnesses.emplace_back(ObjectMap{
            {"edgeId",std::to_string(witness.EdgeId)},{"spanKey",witness.SpanKey},
            {"firstParameter",witness.FirstParameter},{"lastParameter",witness.LastParameter},
            {"sourceSkinFaceIds",machining_edge_export::Ids(witness.SourceSkinFaceIds)},{"skinReason",witness.SkinReason}});
        row["boundaryWitnesses"]=witnesses;evidenceRows.emplace_back(std::move(row));
    }
    std::vector<Overlay> overlays;VariantArray stepPointMarkers;
    std::size_t through=0,partial=0,pending=0;
    for(std::size_t i=0;i<coverage.Coverage.size();++i) {
        const auto& span=coverage.Coverage[i];
        if(span.Kind==R::EFaceCutCoverageKind::Through)++through;
        else if(span.Kind==R::EFaceCutCoverageKind::Partial)++partial;else ++pending;
        const auto key="F"+std::to_string(span.FaceId)+":E"+std::to_string(span.SourceEdgeId)+":C"+std::to_string(i+1);
        VariantArray indices;for(const auto index:span.ArrivalIndices)indices.emplace_back(static_cast<int>(index));
        VariantArray evidenceIndices;for(const auto index:span.EvidenceIndices)evidenceIndices.emplace_back(static_cast<int>(index));
        VariantArray previews;previews.emplace_back(ObjectMap{{"edgeId",std::to_string(span.SourceEdgeId)},
            {"firstParameter",span.FirstParameter},{"lastParameter",span.LastParameter},
            {"points",Preview(built,span.SourceEdgeId,span.FirstParameter,span.LastParameter,deflection)},{"previewOnly",true}});
        spanRows.emplace_back(ObjectMap{{"key",key},{"faceId",std::to_string(span.FaceId)},
            {"sourcePathKey",span.SourcePathKey},{"sourceEdgeId",std::to_string(span.SourceEdgeId)},
            {"targetEdgeId",std::to_string(span.TargetEdgeId)},{"firstParameter",span.FirstParameter},{"lastParameter",span.LastParameter},
            {"sourcePathKeys",Strings(span.SourcePathKeys)},{"baselineSourcePathKeys",Strings(span.BaselineSourcePathKeys)},
            {"evidenceIndices",evidenceIndices},{"mappingComplete",span.bMappingComplete},{"reason",span.Reason},
            {"classificationProofComplete",span.bMappingComplete},{"exitCorrespondenceExpanded",span.bExitCorrespondenceExpanded},
            {"arrivalIndices",indices},{"kind",KindName(span.Kind)},{"faceMappingComplete",span.bFaceMappingComplete},
            {"sourceProperties",TargetProperties(edgeAnalysis,span)},{"targetProperties",TargetProperties(edgeAnalysis,span)},{"previewSegments",previews}});
        const auto found=built.Edges.find(span.SourceEdgeId);
        if(found!=built.Edges.end()&&!BRep_Tool::Degenerated(found->second)) {
            TopLoc_Location location;double a,b;const auto curve=BRep_Tool::Curve(found->second,location,a,b);
            if(!curve.IsNull()) {
                const auto located=Handle(Geom_Curve)::DownCast(curve->Transformed(location.Transformation()));
                BRepBuilderAPI_MakeEdge edgeBuilder(located,span.FirstParameter,span.LastParameter);
                if(edgeBuilder.IsDone()) {
                    overlays.push_back({edgeBuilder.Shape(),"COVERAGE / "+key+" / "+KindName(span.Kind),span.Kind});
                } else {
                    // Keep the strictly positive analytical span unchanged. OCC refuses
                    // nonperiodic edge domains no wider than gp::Resolution(); a point
                    // is an explicit display representation, never a zero-length result.
                    const double width=span.LastParameter-span.FirstParameter;
                    const bool pointOnly=edgeBuilder.Error()==BRepBuilderAPI_LineThroughIdenticPoints
                        &&std::isfinite(span.FirstParameter)&&std::isfinite(span.LastParameter)
                        &&span.LastParameter>span.FirstParameter&&width<=gp::Resolution();
                    std::ostringstream details;details<<std::setprecision(17)
                        <<"native=["<<span.FirstParameter<<","<<span.LastParameter<<"]";
                    if(!pointOnly)throw std::runtime_error("Cannot build coverage STEP overlay "+key+" "+details.str()
                        +" builderError="+std::to_string(static_cast<int>(edgeBuilder.Error())));
                    const auto point=located->Value(span.FirstParameter);
                    TopoDS_Vertex marker;BRep_Builder markerBuilder;markerBuilder.MakeVertex(marker,point,Precision::Confusion());
                    const auto name="COVERAGE / "+key+" / "+KindName(span.Kind)+" / point-marker / "+details.str();
                    overlays.push_back({marker,name,span.Kind,true,point});
                    stepPointMarkers.emplace_back(ObjectMap{{"coverageKey",key},{"faceId",std::to_string(span.FaceId)},
                        {"sourcePathKey",span.SourcePathKey},{"sourceEdgeId",std::to_string(span.SourceEdgeId)},
                        {"firstParameter",span.FirstParameter},{"lastParameter",span.LastParameter},
                        {"position",VariantArray{point.X(),point.Y(),point.Z()}},
                        {"builderError",static_cast<int>(edgeBuilder.Error())},{"nativeParameterResolution",gp::Resolution()},
                        {"reason",std::string("positive-native-interval-below-kernel-edge-resolution")},{"displayOnly",true}});
                }
            }
        }
    }
    const std::set<std::uint64_t> machining(classification.MachiningFaceIds.begin(),classification.MachiningFaceIds.end());
    int faceIndex=0,machiningIndex=0;
    for(const auto& face:classification.SplitGeometry.Faces) {
        const bool selected=machining.contains(face.Id);
        const auto proof=std::find_if(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),[](const auto& tag){
            return tag.starts_with("classify.zeroAreaProof=")&&tag.size()>std::string("classify.zeroAreaProof=").size();});
        const auto sourceFace=std::find_if(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),[](const auto& tag){
            return tag.starts_with("classify.zeroAreaSourceFaceId=")&&tag.size()>std::string("classify.zeroAreaSourceFaceId=").size();});
        const bool skip=!selected&&std::find(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),"classify.zeroAreaTrim=true")!=face.Metadata.Tags.end()
            &&proof!=face.Metadata.Tags.end()&&sourceFace!=face.Metadata.Tags.end();
        faces.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"faceIndex",++faceIndex},{"machining",selected},
            {"machiningIndex",selected?++machiningIndex:0},{"sourceId",face.Metadata.SourceId},{"sourceTags",Strings(face.Metadata.Tags)},
            {"displaySkippedZeroArea",skip}});
        if(skip){skipped.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"proof",*proof},{"sourceFace",*sourceFace},
            {"topologyRetainedInCompleteArtifacts",true}});continue;}
        try {
            const auto preview=machining_edge_export::PreviewFace(built.Faces.at(face.Id),deflection);
            VariantArray positions,indices;
            for(int i=1;i<=preview.Triangles->NbNodes();++i){const auto p=preview.Triangles->Node(i).Transformed(preview.Location.Transformation());positions.emplace_back(p.X());positions.emplace_back(p.Y());positions.emplace_back(p.Z());}
            for(int i=1;i<=preview.Triangles->NbTriangles();++i){int a,b,c;preview.Triangles->Triangle(i).Get(a,b,c);if(preview.Face.Orientation()==TopAbs_REVERSED)std::swap(b,c);indices.emplace_back(a-1);indices.emplace_back(b-1);indices.emplace_back(c-1);}
            meshes.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"positions",positions},{"indices",indices}});
        }catch(const Standard_Failure& error){issues.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"reason",std::string(error.GetMessageString())}});}
         catch(const std::exception& error){issues.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"reason",std::string(error.what())}});}
    }
    ObjectMap stepAudit{{"passed",false}};
    try{stepAudit=WriteStep(complete,overlays,file(".step"));}
    catch(const Standard_Failure& error){stepAudit={{"passed",false},{"error",std::string(error.GetMessageString())}};}
    catch(const std::exception& error){stepAudit={{"passed",false},{"error",std::string(error.what())}};}
    ObjectMap counts{{"wholeFaces",static_cast<int>(classification.SplitGeometry.Faces.size())},{"machiningFaces",static_cast<int>(machining.size())},
        {"sourcePaths",static_cast<int>(coverage.Sources.size())},{"completeSources",static_cast<int>(completeSources)},
        {"incompleteSources",static_cast<int>(coverage.Sources.size()-completeSources)},{"arrivals",static_cast<int>(coverage.Arrivals.size())},
        {"evidence",static_cast<int>(coverage.Evidence.size())},
        {"coverageSpans",static_cast<int>(coverage.Coverage.size())},{"throughSpans",static_cast<int>(through)},
        {"sideCorrespondences",static_cast<int>(coverage.SideCorrespondences.size())},
        {"partialSpans",static_cast<int>(partial)},{"pendingSpans",static_cast<int>(pending)},
        {"incompleteFaces",static_cast<int>(coverage.IncompleteFaceIds.size())},{"geometryIssues",static_cast<int>(issues.size())}};
    const ObjectMap artifacts{{"pathConvention",std::string("same-directory-and-stem-as-analysis-json")},
        {"brepRoundTripPassed",brepOK},{"completeModelBrep",true},{"stepRoundTrip",stepAudit},
        {"artifactAvailable",ObjectMap{{"brep",true},{"step",stepAudit.at("passed")},{"json",true}}}};
    Save(file(".json"),{{"schema",3},{"stage",std::string("faceCutCoverage")},{"name",name},
        {"coverageDomain",std::string("source-skin-native-intervals")},
        {"success",coverage.bSuccess&&brepOK&&stepAudit.at("passed").To<bool>()},{"complete",coverage.bComplete},
        {"sources",sourceRows},{"arrivals",arrivalRows},{"evidence",evidenceRows},
        {"coverage",spanRows},{"sideCorrespondences",sideRows},{"referenceEdges",referenceEdges},{"faces",faces},{"counts",counts},
        {"incompleteFaceIds",machining_edge_export::Ids(coverage.IncompleteFaceIds)},{"diagnostics",Strings(coverage.Diagnostics)},
        {"geometry",ObjectMap{{"faces",meshes},{"completeModel",true},{"complete",issues.empty()},
            {"previewStatus",std::string(issues.empty()?"passed":"partial")},{"previewOnly",true},{"issues",issues},{"displaySkippedZeroArea",skipped}}},
        {"display",ObjectMap{{"throughColor",std::string("#1fe05c")},{"partialColor",std::string("#f59e0b")},
            {"pendingColor",std::string("#94a3b8")},{"previewOnly",true},{"stepPointMarkers",stepPointMarkers}}},{"artifacts",artifacts}});
    return {{"passed",brepOK&&stepAudit.at("passed").To<bool>()},
        {"completeModelBrep",true},{"brepRoundTripPassed",brepOK},{"stepRoundTrip",stepAudit},{"counts",counts},{"displayIssues",issues}};
}
}
