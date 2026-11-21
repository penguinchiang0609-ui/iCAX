#include <ExtrusionRecognition/MachiningEdgeAnalysisWork.h>
#include <Poly_Triangulation.hxx>
#include <STEPConstruct.hxx>
#include <XSControl_WorkSession.hxx>
#include <XSControl_TransferWriter.hxx>
#include <StepVisual_StyledItem.hxx>
#include <StepVisual_CurveStyle.hxx>
#include <StepVisual_CurveStyleFont.hxx>
#include <StepVisual_CurveStyleFontPattern.hxx>
#include <StepVisual_CurveStyleFontSelect.hxx>
#include <StepVisual_PresentationStyleAssignment.hxx>
#include <TCollection_HAsciiString.hxx>

namespace machining_edge_export
{
std::string SkinName(R::ESkinRelation value) {
    switch(value) {
    case R::ESkinRelation::OnSkin:return "OnSkin";
    case R::ESkinRelation::OffSkin:return "OffSkin";
    case R::ESkinRelation::PartialOnSkin:return "PartialOnSkin";
    }
    throw std::logic_error("Invalid skin relationship enum");
}
std::string ConvexityName(R::EEdgeConvexity value) {
    switch(value) {
    case R::EEdgeConvexity::Convex:return "Convex";
    case R::EEdgeConvexity::Concave:return "Concave";
    case R::EEdgeConvexity::Coplanar:return "Coplanar";
    case R::EEdgeConvexity::Free:return "Free";
    }
    throw std::logic_error("Invalid convexity enum");
}
std::string ColorName(const R::SMachiningEdgeAnalysis& edge) {
    if(!edge.bSkinRelationValid)return "#EF4444";
    switch(edge.SkinRelation) {
    case R::ESkinRelation::OnSkin:return "#00B8D9";
    case R::ESkinRelation::OffSkin:return "#F59E0B";
    case R::ESkinRelation::PartialOnSkin:return "#A855F7";
    }
    throw std::logic_error("Invalid skin color enum");
}
std::string PatternName(const R::SMachiningEdgeAnalysis& edge) {
    if(!edge.bConvexityValid)return "unresolved";
    switch(edge.Convexity) {
    case R::EEdgeConvexity::Convex:return "solid";
    case R::EEdgeConvexity::Concave:return "dashed";
    case R::EEdgeConvexity::Coplanar:return "dash-dot";
    case R::EEdgeConvexity::Free:return "dotted";
    }
    throw std::logic_error("Invalid line pattern enum");
}
Quantity_Color EdgeColor(const R::SMachiningEdgeAnalysis& edge) {
    const auto text=ColorName(edge);
    const unsigned rgb=std::stoul(text.substr(1),nullptr,16);
    return Quantity_Color(((rgb>>16)&255)/255.,((rgb>>8)&255)/255.,(rgb&255)/255.,Quantity_TOC_RGB);
}
VariantArray Ids(const std::vector<std::uint64_t>& values) {
    VariantArray result;for(const auto value:values)result.emplace_back(std::to_string(value));return result;
}
VariantArray Point(const G::Point3& point) {return {point.X,point.Y,point.Z};}

struct FacePreview final {
    TopoDS_Face Face;
    Handle(Poly_Triangulation) Triangles;
    TopLoc_Location Location;
    std::string Preparation="source-face-copy";
    double Deflection=0;
};
FacePreview PreviewFace(const TopoDS_Face& original,const double deflection) {
    FacePreview result;result.Deflection=deflection;
    result.Face=TopoDS::Face(BRepBuilderAPI_Copy(original,true,false).Shape());
    BRepMesh_IncrementalMesh first(result.Face,deflection,false,.25,false);
    result.Triangles=BRep_Tool::Triangulation(result.Face,result.Location);
    if(!result.Triangles.IsNull())return result;
    // A loose source vertex can hide a very narrow, positive-area trim from
    // the mesher. Reuse the independently checked STEP presentation trim on a
    // copy, then choose deflection from the face width rather than tube length.
    bool valid=false;const auto prepared=StepBoundaryCopy(original,false,false,&valid);
    if(!valid||Count(prepared,TopAbs_FACE)!=1||Relative(Area(original),Area(prepared))>1e-6)
        throw std::runtime_error("Cannot prepare a faithful display trim for the original face");
    for(TopExp_Explorer it(prepared,TopAbs_FACE);it.More();it.Next()){result.Face=TopoDS::Face(it.Current());break;}
    GProp_GProps length;BRepGProp::LinearProperties(result.Face,length);
    const double width=2*Area(result.Face)/std::max(length.Mass(),1e-12);
    result.Deflection=std::clamp(std::min(deflection,width*.05),1e-7,deflection);
    IMeshTools_Parameters parameters;parameters.Deflection=result.Deflection;
    parameters.DeflectionInterior=result.Deflection;parameters.Angle=.25;parameters.AngleInterior=.5;
    parameters.MinSize=1e-7;parameters.AdjustMinSize=true;parameters.ForceFaceDeflection=false;
    BRepTools::Clean(result.Face,true);BRepMesh_IncrementalMesh retry(result.Face,parameters);
    result.Triangles=BRep_Tool::Triangulation(result.Face,result.Location);
    result.Preparation="checked-exact-parametric-display-trim-with-local-face-deflection";
    if(result.Triangles.IsNull())throw std::runtime_error("Display meshing failed after exact-trim and local-width preparation");
    return result;
}

// The BREP remains the original complete machining subset. STEP fonts are
// presentation metadata on exact edge overlays, never tube/mesh geometry.
ObjectMap WriteStep(const TopoDS_Shape& machining,const O::SOpenCascadeBRepBuildResult& built,
    const R::SMachiningEdgeAnalysisResult& analysis,const fs::path& file,double diagonal) {
    Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");
    const auto shapes=XCAFDoc_DocumentTool::ShapeTool(document->Main());
    const auto colors=XCAFDoc_DocumentTool::ColorTool(document->Main());
    bool presentationValid=false;
    const auto body=FlatArtifact(StepBoundaryCopy(machining,false,false,&presentationValid));
    if(!presentationValid)throw std::runtime_error("Invalid edge-analysis STEP presentation faces");
    const auto bodyLabel=AddArtifactFaces(shapes,body,"Machining faces / edge analysis");
    colors->SetColor(bodyLabel,Quantity_Color(0.7,0.72,0.75,Quantity_TOC_RGB),XCAFDoc_ColorSurf);
    std::vector<std::pair<TopoDS_Shape,const R::SMachiningEdgeAnalysis*>> overlays;
    std::map<std::string,const R::SMachiningEdgeAnalysis*> byId;
    for(const auto& edge:analysis.Edges)byId.emplace(edge.Key,&edge);
    for(const auto& edge:analysis.Edges) {
        if(!edge.bSkinRelationValid||!edge.bConvexityValid)throw std::runtime_error("A machining analysis span has no enum pair");
        if(!edge.MatchedSpanKeys.empty()) {
            bool consistent=true;
            for(const auto& id:edge.MatchedSpanKeys) {
                const auto other=byId.find(id);
                if(other==byId.end()||other->second->bSkinRelationValid!=edge.bSkinRelationValid
                    ||other->second->bConvexityValid!=edge.bConvexityValid
                    ||(edge.bSkinRelationValid&&other->second->SkinRelation!=edge.SkinRelation)
                    ||(edge.bConvexityValid&&other->second->Convexity!=edge.Convexity))consistent=false;
            }
            if(!consistent)throw std::runtime_error("Equivalent machining spans have inconsistent enum pairs");
            if(edge.Key!=*std::min_element(edge.MatchedSpanKeys.begin(),edge.MatchedSpanKeys.end()))continue;
        }
        const auto found=built.Edges.find(edge.EdgeId);
        if(found==built.Edges.end()||BRep_Tool::Degenerated(found->second))continue;
        double first,last;const auto curve=BRep_Tool::Curve(found->second,first,last);
        if(curve.IsNull()||edge.LastParameter<=edge.FirstParameter)throw std::runtime_error("Invalid machining span range");
        const auto overlay=BRepBuilderAPI_MakeEdge(curve,edge.FirstParameter,edge.LastParameter).Shape();
        const auto label=shapes->AddShape(overlay,false);
        const auto name=edge.Key+" / "+
            (edge.bSkinRelationValid?SkinName(edge.SkinRelation):"unresolved")+" / "+
            (edge.bConvexityValid?ConvexityName(edge.Convexity):"unresolved");
        TDataStd_Name::Set(label,name.c_str());
        colors->SetColor(label,EdgeColor(edge),XCAFDoc_ColorGen);
        colors->SetColor(label,EdgeColor(edge),XCAFDoc_ColorCurv);
        overlays.emplace_back(overlay,&edge);
    }
    Interface_Static::SetCVal("write.step.schema","AP214IS");
    STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);
    writer.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
    if(!writer.Transfer(document,STEPControl_AsIs))throw std::runtime_error("Edge-analysis STEP transfer failed");
    const auto model=writer.ChangeWriter().Model();
    const auto finder=writer.Writer().WS()->TransferWriter()->FinderProcess();
    std::map<const Standard_Transient*,std::vector<Handle(StepVisual_StyledItem)>> stylesByItem;
    for(int i=1;i<=model->NbEntities();++i) {
        const auto styled=Handle(StepVisual_StyledItem)::DownCast(model->Value(i));
        if(!styled.IsNull()&&!styled->Item().IsNull())stylesByItem[styled->Item().get()].push_back(styled);
    }
    int fontCount=0;std::set<std::string> expectedFonts;
    const double unit=std::max(diagonal*0.001,1e-6);
    for(const auto& [shape,edge]:overlays) {
        if(!edge->bConvexityValid)continue;
        expectedFonts.insert(edge->Key+" / "+ConvexityName(edge->Convexity));
        TopLoc_Location location;const auto item=STEPConstruct::FindEntity(finder,shape,location);
        if(item.IsNull())continue;
        for(const auto& styled:stylesByItem[item.get()]) {
            for(int j=1;j<=styled->NbStyles();++j) {
                const auto assignment=styled->StylesValue(j);
                for(int k=1;k<=assignment->NbStyles();++k) {
                    const auto style=assignment->StylesValue(k).CurveStyle();if(style.IsNull())continue;
                    style->SetName(new TCollection_HAsciiString((edge->Key+" / "+ConvexityName(edge->Convexity)).c_str()));
                    std::vector<std::pair<double,double>> pattern;
                    switch(edge->Convexity) {
                    case R::EEdgeConvexity::Convex:pattern={{8*unit,0}};break;
                    case R::EEdgeConvexity::Concave:pattern={{6*unit,3*unit}};break;
                    case R::EEdgeConvexity::Coplanar:pattern={{6*unit,2*unit},{unit,2*unit}};break;
                    case R::EEdgeConvexity::Free:pattern={{unit,3*unit}};break;
                    }
                    Handle(NCollection_HArray1<Handle(StepVisual_CurveStyleFontPattern)>) list=
                        new NCollection_HArray1<Handle(StepVisual_CurveStyleFontPattern)>(1,static_cast<int>(pattern.size()));
                    for(int n=1;n<=static_cast<int>(pattern.size());++n) {
                        Handle(StepVisual_CurveStyleFontPattern) part=new StepVisual_CurveStyleFontPattern;
                        part->Init(pattern[n-1].first,pattern[n-1].second);list->SetValue(n,part);
                    }
                    Handle(StepVisual_CurveStyleFont) font=new StepVisual_CurveStyleFont;
                    font->Init(new TCollection_HAsciiString(PatternName(*edge).c_str()),list);
                    StepVisual_CurveStyleFontSelect selected;selected.SetValue(font);style->SetCurveFont(selected);
                    model->AddWithRefs(font);++fontCount;
                }
            }
        }
    }
    fs::create_directories(file.parent_path());
    if(writer.Write(Utf8(file).c_str())!=IFSelect_RetDone)throw std::runtime_error("Edge-analysis STEP write failed");
    STEPControl_Reader read;
    if(read.ReadFile(Utf8(file).c_str())!=IFSelect_RetDone||!read.TransferRoots())throw std::runtime_error("Edge-analysis STEP read-back failed");
    const auto result=read.OneShape();
    const auto areaError=Relative(Area(machining),Area(result));
    std::set<std::string> restoredFonts;
    const auto readModel=read.StepModel();
    for(int i=1;i<=readModel->NbEntities();++i) {
        const auto styled=Handle(StepVisual_StyledItem)::DownCast(readModel->Value(i));
        if(styled.IsNull()||styled->Item().IsNull())continue;
        for(int j=1;j<=styled->NbStyles();++j) {
            const auto assignment=styled->StylesValue(j);
            for(int k=1;k<=assignment->NbStyles();++k) {
                const auto style=assignment->StylesValue(k).CurveStyle();
                if(style.IsNull()||style->Name().IsNull()||style->CurveFont().CurveStyleFont().IsNull())continue;
                restoredFonts.insert(style->Name()->ToCString());
            }
        }
    }
    const bool fontsOK=restoredFonts==expectedFonts;
    const bool passed=Count(machining,TopAbs_FACE)==Count(result,TopAbs_FACE)&&areaError<1e-6&&fontsOK;
    return {{"passed",passed},{"sourceFaceCount",Count(machining,TopAbs_FACE)},
        {"roundTripFaceCount",Count(result,TopAbs_FACE)},{"areaRelativeError",areaError},
        {"edgeOverlayCount",static_cast<int>(overlays.size())},{"stepCurveFontCount",fontCount},
        {"expectedStyledEdges",static_cast<int>(expectedFonts.size())},{"roundTripStyledEdges",static_cast<int>(restoredFonts.size())},{"fontCoveragePassed",fontsOK},
        {"lineStylePolicy",std::string("STEP curve fonts; third-party reader display support varies. Browser uses screen-pixel patterns.")},
        {"displayOnlyEdges",true},{"extraDisplayFaces",0}};
}

ObjectMap Export(const R::SSectionFaceClassificationResult& classification,
    const R::SMachiningEdgeAnalysisResult& analysis,const R::SMachiningEdgeAnalysisOptions& options,
    const O::SOpenCascadeBRepBuildResult& splitBuilt,const TopoDS_Shape& machining,
    const fs::path& base,const std::string& name) {
    fs::create_directories(base.parent_path());
    const auto file=[&](const char* extension){return fs::path(base.wstring()+fs::path(extension).wstring());};
    {std::ofstream stream(file(".brep"),std::ios::binary);BRepTools::Write(machining,stream);if(!stream)throw std::runtime_error("Cannot write complete edge-analysis BREP");}
    TopoDS_Shape restored;BRep_Builder builder;
    {std::ifstream stream(file(".brep"),std::ios::binary);BRepTools::Read(restored,stream,builder);}
    const bool brepOK=!restored.IsNull()&&Count(restored,TopAbs_FACE)==static_cast<int>(analysis.Faces.size())
        &&Relative(Area(machining),Area(restored))<1e-10;
    if(!brepOK)throw std::runtime_error("Edge-analysis complete BREP round-trip mismatch");
    Bnd_Box box;BRepBndLib::AddOptimal(machining,box,false,false);
    double diagonal=1;if(!box.IsVoid()){double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);diagonal=gp_Pnt(x0,y0,z0).Distance(gp_Pnt(x1,y1,z1));}
    const double deflection=std::clamp(diagonal*0.0002,1e-6,0.1);
    VariantArray edgeRows,faceRows,sourceRows,excludedRows,meshes,displayIssues;int unresolvedSkin=0,unresolvedConvexity=0;
    std::map<std::uint64_t,VariantArray> sourceSpanKeys;
    ObjectMap skinCounts,convexityCounts;
    for(const auto& edge:analysis.Edges) {
        if(!edge.bSkinRelationValid||!edge.bConvexityValid)throw std::runtime_error("Incomplete machining enum pair must not be published");
        sourceSpanKeys[edge.EdgeId].emplace_back(edge.Key);
        VariantArray samples,evidence,angles;
        for(const auto& sample:edge.Samples) {
            samples.emplace_back(Point(sample.Position));
            evidence.emplace_back(ObjectMap{{"parameter",sample.Parameter},{"position",Point(sample.Position)},
                {"skinDistanceMm",sample.SkinDistance},{"onSkin",sample.bOnSkin}});
        }
        const auto found=splitBuilt.Edges.find(edge.EdgeId);
        if(found!=splitBuilt.Edges.end()&&!BRep_Tool::Degenerated(found->second)) {
            BRepAdaptor_Curve curve(found->second);
            GCPnts_QuasiUniformDeflection sampled(curve,deflection,edge.FirstParameter,edge.LastParameter);
            if(sampled.IsDone()&&sampled.NbPoints()>1&&sampled.NbPoints()<20000) {
                samples.clear();for(int i=1;i<=sampled.NbPoints();++i){const auto p=sampled.Value(i);samples.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
            }
        }
        for(const auto& sample:edge.AngleSamples)angles.emplace_back(ObjectMap{{"parameter",sample.Parameter},{"signedAngleRadians",sample.SignedAngleRadians}});
        if(!edge.bSkinRelationValid)++unresolvedSkin;
        if(!edge.bConvexityValid)++unresolvedConvexity;
        const auto skinKey=edge.bSkinRelationValid?SkinName(edge.SkinRelation):"unresolved";
        const auto convexityKey=edge.bConvexityValid?ConvexityName(edge.Convexity):"unresolved";
        skinCounts[skinKey]=(skinCounts.contains(skinKey)?skinCounts.at(skinKey).To<int>():0)+1;
        convexityCounts[convexityKey]=(convexityCounts.contains(convexityKey)?convexityCounts.at(convexityKey).To<int>():0)+1;
        VariantArray matchedSpanKeys;for(const auto& key:edge.MatchedSpanKeys)matchedSpanKeys.emplace_back(key);
        edgeRows.emplace_back(ObjectMap{{"edgeKey",edge.Key},{"edgeId",std::to_string(edge.EdgeId)},
            {"segmentId",static_cast<int>(edge.SegmentId)},{"firstParameter",edge.FirstParameter},{"lastParameter",edge.LastParameter},
            {"matchedSpanKeys",matchedSpanKeys},{"faceIds",Ids(edge.MachiningFaceIds)},
            {"sourceFaceIds",Ids(edge.SourceMachiningFaceIds)},{"matchedEdgeIds",Ids(edge.MatchedEdgeIds)},
            {"partialOverlapEdgeIds",Ids(edge.PartialOverlapEdgeIds)},{"rejectedCoincidentEdgeIds",Ids(edge.RejectedCoincidentEdgeIds)},
            {"adjacencyMethod",edge.AdjacencyMethod},{"adjacencyReason",edge.AdjacencyReason},{"maxAdjacencyGapMm",edge.MaxAdjacencyGap},
            {"orientationRepaired",edge.bOrientationRepaired},{"orientationAnchorFaceId",std::to_string(edge.OrientationAnchorFaceId)},
            {"orientationFlippedFaceIds",Ids(edge.OrientationFlippedFaceIds)},{"orientationMethod",edge.OrientationMethod},
            {"skinRelation",edge.bSkinRelationValid?iCAX::Data::Variant(skinKey):iCAX::Data::Variant{}},
            {"convexity",edge.bConvexityValid?iCAX::Data::Variant(convexityKey):iCAX::Data::Variant{}},
            {"skinValid",edge.bSkinRelationValid},{"convexityValid",edge.bConvexityValid},
            {"skinReason",edge.SkinReason},{"convexityReason",edge.ConvexityReason},
            {"reason",edge.SkinReason+"; "+edge.ConvexityReason},{"startOnSkin",edge.bStartOnSkin},{"endOnSkin",edge.bEndOnSkin},
            {"minSkinDistanceMm",edge.MinSkinDistance},{"maxSkinDistanceMm",edge.MaxSkinDistance},
            {"sourceSkinMembership",edge.bSourceSkinMembership},{"sourceSkinFaceIds",Ids(edge.SourceSkinFaceIds)},
            {"minSignedAngleRadians",edge.MinSignedAngleRadians},{"maxSignedAngleRadians",edge.MaxSignedAngleRadians},
            {"angleSamples",angles},{"samples",samples},{"classificationSamples",evidence},
            {"color",ColorName(edge)},{"linePattern",PatternName(edge)}});
    }
    for(const auto& [edgeId,keys]:sourceSpanKeys) {
        const auto found=splitBuilt.Edges.find(edgeId);if(found==splitBuilt.Edges.end())throw std::runtime_error("Source edge missing");
        BRepAdaptor_Curve curve(found->second);
        sourceRows.emplace_back(ObjectMap{{"edgeId",std::to_string(edgeId)},{"firstParameter",curve.FirstParameter()},
            {"lastParameter",curve.LastParameter()},{"segmentKeys",keys}});
    }
    for(const auto& edge:analysis.ExcludedEdges)excludedRows.emplace_back(ObjectMap{{"edgeId",std::to_string(edge.EdgeId)},
        {"faceIds",Ids(edge.SourceMachiningFaceIds)},{"reason",edge.ConvexityReason.empty()?edge.SkinReason:edge.ConvexityReason}});
    int faceIndex=0;
    std::map<std::uint64_t,const G::BRepFace*> sourceFaces;
    for(const auto& sourceFace:classification.SplitGeometry.Faces)sourceFaces.emplace(sourceFace.Id,&sourceFace);
    for(const auto& face:analysis.Faces) {
        VariantArray wires;std::set<std::uint64_t> edgeIds;
        for(const auto& wire:face.Wires) {
            VariantArray groups;
            for(const auto& group:wire.Groups) {
                VariantArray uses;
                for(const auto& use:group.SourceUses){uses.emplace_back(ObjectMap{{"edgeId",std::to_string(use.EdgeId)},{"reversed",use.bReversed}});edgeIds.insert(use.EdgeId);}
                groups.emplace_back(ObjectMap{{"groupId",static_cast<int>(group.GroupId)},{"curveKind",group.CurveKind},{"closed",group.bClosed},{"sourceUses",uses}});
            }
            wires.emplace_back(ObjectMap{{"wireId",std::to_string(wire.WireId)},{"ordered",wire.bOrdered},{"reason",wire.Reason},{"groups",groups}});
        }
        VariantArray ids,keys;for(const auto id:edgeIds){ids.emplace_back(std::to_string(id));if(sourceSpanKeys.contains(id))for(const auto& key:sourceSpanKeys.at(id))keys.push_back(key);}
        ObjectMap faceRow{{"faceId",std::to_string(face.FaceId)},{"faceIndex",++faceIndex},{"edgeIds",ids},{"edgeKeys",keys},{"wires",wires}};
        if(sourceFaces.contains(face.FaceId)) {
            faceRow["sourceId"]=sourceFaces.at(face.FaceId)->Metadata.SourceId;
            faceRow["sourceTags"]=Strings(sourceFaces.at(face.FaceId)->Metadata.Tags);
        }
        faceRows.emplace_back(std::move(faceRow));
        const auto found=splitBuilt.Faces.find(face.FaceId);
        if(found==splitBuilt.Faces.end())throw std::runtime_error("Edge-analysis face missing from stable neutral map");
        try {
        const auto preview=PreviewFace(found->second,deflection);
        const auto& copy=preview.Face;const auto& triangulation=preview.Triangles;const auto& location=preview.Location;
        VariantArray positions,indices;
        for(int i=1;i<=triangulation->NbNodes();++i){auto p=triangulation->Node(i).Transformed(location.Transformation());positions.emplace_back(p.X());positions.emplace_back(p.Y());positions.emplace_back(p.Z());}
        for(int i=1;i<=triangulation->NbTriangles();++i){int a,b,c;triangulation->Triangle(i).Get(a,b,c);if(copy.Orientation()==TopAbs_REVERSED)std::swap(b,c);indices.emplace_back(a-1);indices.emplace_back(b-1);indices.emplace_back(c-1);}
        meshes.emplace_back(ObjectMap{{"faceId",std::to_string(face.FaceId)},{"positions",positions},{"indices",indices},
            {"displayPreparation",preview.Preparation},{"displayDeflectionMm",preview.Deflection}});
        } catch(const Standard_Failure& exception) {
            displayIssues.emplace_back(ObjectMap{{"faceId",std::to_string(face.FaceId)},{"reason",std::string(exception.GetMessageString())}});
        } catch(const std::exception& exception) {
            displayIssues.emplace_back(ObjectMap{{"faceId",std::to_string(face.FaceId)},{"reason",std::string(exception.what())}});
        }
    }
    ObjectMap stepAudit{{"passed",false},{"status",std::string("pending")}};
    VariantArray classifiedDomains;
    for(const auto& face:classification.NonMachiningGeometry.Faces)
        if(std::any_of(face.Metadata.Tags.begin(),face.Metadata.Tags.end(),[](const auto& tag){return tag.starts_with("classify.");}))
            classifiedDomains.emplace_back(ObjectMap{{"faceId",std::to_string(face.Id)},{"sourceId",face.Metadata.SourceId},{"sourceTags",Strings(face.Metadata.Tags)}});
    ObjectMap document{{"schema",1},{"stage",std::string("edgeAnalysis")},{"name",name},
        {"success",analysis.bSuccess},{"complete",analysis.bComplete},{"adjacencyScope",std::string("machining-faces-only")},
        {"geometry",ObjectMap{{"faces",meshes},{"previewOnly",true},{"displayDeflectionMm",deflection},{"issues",displayIssues}}},
        {"faces",faceRows},{"edges",edgeRows},{"sourceEdges",sourceRows},{"excludedEdges",excludedRows},{"diagnostics",Strings(analysis.Diagnostics)},
        {"classificationReview",ObjectMap{{"diagnostics",Strings(classification.Diagnostics)},{"nonMachiningDerivedFaces",classifiedDomains},
            {"machiningFaces",static_cast<int>(classification.MachiningGeometry.Faces.size())},
            {"nonMachiningFaces",static_cast<int>(classification.NonMachiningGeometry.Faces.size())}}},
        {"options",ObjectMap{{"skinDistanceToleranceMm",options.dSkinDistanceTolerance},{"angularToleranceRadians",options.dAngularToleranceRadians},
            {"curveSampleCount",static_cast<int>(options.nCurveSampleCount)},{"angleSampleCount",static_cast<int>(options.nAngleSampleCount)}}},
        {"counts",ObjectMap{{"faces",static_cast<int>(analysis.Faces.size())},{"edges",static_cast<int>(analysis.Edges.size())},
            {"sourceEdges",static_cast<int>(sourceRows.size())},{"excludedTopologyEdges",static_cast<int>(excludedRows.size())},
            {"unresolvedSkin",unresolvedSkin},{"unresolvedConvexity",unresolvedConvexity},{"skin",skinCounts},{"convexity",convexityCounts}}},
        {"samplingNotice",std::string("Geometric distance and dihedral evidence are finite samples, not a continuous error certificate.")},
        {"artifacts",ObjectMap{{"pathConvention",std::string("same-directory-and-stem-as-analysis-json")},
            {"brepExtension",std::string(".brep")},{"stepExtension",std::string(".step")},{"brepRoundTripPassed",brepOK},{"stepRoundTrip",stepAudit}}},
        {"legend",ObjectMap{{"skin",ObjectMap{{"OnSkin",std::string("#00B8D9")},{"OffSkin",std::string("#F59E0B")},{"PartialOnSkin",std::string("#A855F7")}}},
            {"convexity",ObjectMap{{"Convex",std::string("solid")},{"Concave",std::string("dashed")},{"Coplanar",std::string("dash-dot")},{"Free",std::string("dotted")}}}}}};
    Save(file(".json"),document);
    try { stepAudit=WriteStep(machining,splitBuilt,analysis,file(".step"),diagonal); }
    catch(const Standard_Failure& exception) {stepAudit={{"passed",false},{"error",std::string(exception.GetMessageString())}};}
    catch(const std::exception& exception) {stepAudit={{"passed",false},{"error",std::string(exception.what())}};}
    auto artifacts=document.at("artifacts").To<ObjectMap>();artifacts["stepRoundTrip"]=stepAudit;
    document["artifacts"]=artifacts;Save(file(".json"),document);
    return {{"passed",brepOK&&stepAudit.at("passed").To<bool>()&&displayIssues.empty()},{"complete",analysis.bComplete},
        {"faceCount",static_cast<int>(analysis.Faces.size())},{"edgeCount",static_cast<int>(analysis.Edges.size())},
        {"unresolvedSkin",unresolvedSkin},{"unresolvedConvexity",unresolvedConvexity},{"displayIssues",displayIssues},{"brepRoundTripPassed",brepOK},{"stepRoundTrip",stepAudit}};
}
}

struct SClassifyCheckpoint final {
    R::SSectionFaceClassificationResult Classification;
    R::SSectionWiresResult Section;
    std::string Source;
};

SClassifyCheckpoint LoadClassifyCheckpoint(const fs::path& checkpoint) {
    const auto readObject=[](const fs::path& path){std::ifstream stream(path,std::ios::binary);
        if(!stream)throw std::runtime_error("Missing checkpoint JSON: "+Utf8(path));
        const std::string value((std::istreambuf_iterator<char>(stream)),{});return Json::Parse(value).To<ObjectMap>();};
    const auto readNeutral=[](const fs::path& path){std::ifstream stream(path,std::ios::binary);
        if(!stream)throw std::runtime_error("Missing exact neutral checkpoint: "+Utf8(path));
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
        return G::Persistence::Deserialize(bytes);};
    const auto performance=readObject(checkpoint/"performance.json");
    if(performance.at("status").To<std::string>()!="passed")throw std::runtime_error("Upstream checkpoint did not pass");
    const auto stages=performance.at("stages").To<ObjectMap>();
    for(const auto name:{"heal","align","contour","classify"})
        if(!stages.at(name).To<ObjectMap>().at("passed").To<bool>())throw std::runtime_error("Incomplete upstream checkpoint stage");
    R::SSectionFaceClassificationResult classification;classification.bSuccess=true;
    classification.SplitGeometry=readNeutral(checkpoint/"split.icaxbrep");
    classification.MachiningGeometry=readNeutral(checkpoint/"machining.icaxbrep");
    classification.NonMachiningGeometry=readNeutral(checkpoint/"stock.icaxbrep");
    std::set<std::uint64_t> assigned;
    for(const auto& face:classification.MachiningGeometry.Faces){classification.MachiningFaceIds.push_back(face.Id);if(!assigned.insert(face.Id).second)throw std::runtime_error("Duplicate machining checkpoint face");}
    for(const auto& face:classification.NonMachiningGeometry.Faces){classification.NonMachiningFaceIds.push_back(face.Id);if(!assigned.insert(face.Id).second)throw std::runtime_error("Overlapping checkpoint partitions");}
    if(assigned.size()!=classification.SplitGeometry.Faces.size())throw std::runtime_error("Checkpoint face partitions are incomplete");
    for(const auto& face:classification.SplitGeometry.Faces)if(!assigned.contains(face.Id))throw std::runtime_error("Checkpoint face IDs disagree");
    const auto curves=readNeutral(checkpoint/"section-curves.icaxbrep");
    const auto domains=readObject(checkpoint/"section-domains.json");
    R::SSectionWiresResult section;section.bSuccess=true;std::size_t curveIndex=0;
    for(const auto& wireValue:domains.at("wires").To<VariantArray>()) {
        const auto data=wireValue.To<ObjectMap>();R::SSectionWire wire;
        wire.bInner=data.at("inner").To<bool>();wire.bClosed=data.at("closed").To<bool>();wire.dSignedArea=data.at("area").To<double>();
        if(!wire.bClosed)throw std::runtime_error("Checkpoint section wire is open");
        for(const auto& edgeValue:data.at("edges").To<VariantArray>()) {
            const auto values=edgeValue.To<ObjectMap>();R::SSectionWireEdge edge;
            if(curveIndex>=curves.Curves2.size()||curves.Curves2[curveIndex].Id!=curveIndex+1)throw std::runtime_error("Checkpoint curve IDs or domains disagree");
            edge.Curve=curves.Curves2[curveIndex++].Geometry;
            edge.SourceEdgeId=std::stoull(values.at("sourceEdgeId").To<std::string>());
            edge.SourceFaceId=std::stoull(values.at("sourceFaceId").To<std::string>());
            edge.Range={values.at("first").To<double>(),values.at("last").To<double>()};edge.bReversed=values.at("reversed").To<bool>();
            const auto a=values.at("start").To<VariantArray>(),b=values.at("end").To<VariantArray>();
            edge.Start={a.at(0).To<double>(),a.at(1).To<double>()};edge.End={b.at(0).To<double>(),b.at(1).To<double>()};
            const auto curve=O::BuildOpenCascadeCurve2(edge.Curve);if(curve.IsNull())throw std::runtime_error("Invalid exact checkpoint section curve");
            const auto start=curve->Value(edge.bReversed?edge.Range.Last:edge.Range.First),end=curve->Value(edge.bReversed?edge.Range.First:edge.Range.Last);
            if(start.Distance(gp_Pnt2d(edge.Start.X,edge.Start.Y))>.0011||end.Distance(gp_Pnt2d(edge.End.X,edge.End.Y))>.0011)
                throw std::runtime_error("Checkpoint section endpoint/range mismatch");
            wire.Edges.push_back(std::move(edge));
        }
        if(wire.Edges.empty())throw std::runtime_error("Empty checkpoint wire");
        for(std::size_t i=0;i<wire.Edges.size();++i){const auto a=wire.Edges[i].End,b=wire.Edges[(i+1)%wire.Edges.size()].Start;
            if(std::hypot(a.X-b.X,a.Y-b.Y)>.0011)throw std::runtime_error("Disconnected checkpoint section");}
        section.Wires.push_back(std::move(wire));
    }
    if(curveIndex!=curves.Curves2.size()||section.Wires.empty())throw std::runtime_error("Unused or absent checkpoint section curves");
    return {std::move(classification),std::move(section),performance.at("source").To<std::string>()};
}

// Replay an explicitly selected, validated neutral Classify checkpoint.
int RunMachiningEdgeCheckpoint(const fs::path& checkpoint,const fs::path& root,
    const std::string& group,const std::string& id,
    const std::function<ObjectMap(const R::SSectionFaceClassificationResult&,const R::SSectionWiresResult&,
        const R::SMachiningEdgeAnalysisResult&,const O::SOpenCascadeBRepBuildResult&)>& nextWork={},
    const std::string& nextStage="pathAnalysis",const bool exportEdgeArtifacts=true,
    const int partialExitCode=1) {
    const auto reportPath=root/"Reports"/fs::u8path(group)/(id+".json");
    ObjectMap report{{"stage",std::string("edgeAnalysis")},{"status",std::string("running")},
        {"upstreamCheckpoint",Utf8(checkpoint)},{"upstreamStagesReused",true}};
    Save(reportPath,report);
    try {
        const auto [classification,section,source]=LoadClassifyCheckpoint(checkpoint);
        const auto built=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
        if(!built.bOK||!BRepCheck_Analyzer(built.Shape,true,true).IsValid())throw std::runtime_error("Invalid exact checkpoint split geometry");
        BRep_Builder builder;TopoDS_Compound machining;builder.MakeCompound(machining);
        for(const auto& face:classification.MachiningGeometry.Faces){auto shape=built.Faces.at(face.Id);
            shape.Orientation(face.Orientation==G::ETopologyOrientation::Reversed?TopAbs_REVERSED:TopAbs_FORWARD);builder.Add(machining,shape);}
        const auto before=G::Persistence::Serialize(classification.SplitGeometry);
        const auto began=Clock::now();R::SMachiningEdgeAnalysisOptions options;
        const auto analysis=R::MachiningEdgeAnalysisWork{}.Run(classification,section,options);
        const auto seconds=std::chrono::duration<double>(Clock::now()-began).count();
        if(!analysis.bSuccess) {report["diagnostics"]=Strings(analysis.Diagnostics);throw std::runtime_error("Checkpoint machining edge analysis failed");}
        const auto artifacts=exportEdgeArtifacts
            ?machining_edge_export::Export(classification,analysis,options,built,machining,
                root/"EdgeAnalysis"/fs::u8path(group)/id,group)
            :ObjectMap{{"passed",true},{"exported",false}};
        const bool unchanged=before==G::Persistence::Serialize(classification.SplitGeometry);
        bool passed=artifacts.at("passed").To<bool>()&&unchanged;
        const ObjectMap stage{{"status",std::string(passed?"passed":"failed")},{"serviceSuccess",analysis.bSuccess},
            {"analysisComplete",analysis.bComplete},{"computeSeconds",seconds},{"artifactAudit",artifacts},
            {"upstreamStagesReused",true},{"diagnostics",Strings(analysis.Diagnostics)},{"checkpointCadUnchanged",unchanged}};
        ObjectMap stageReports{{"edgeAnalysis",stage}};
        std::string finalStatus=passed?"passed":"failed";
        if(nextWork&&passed) {
            report["stage"]=nextStage;report["stages"]=stageReports;Save(reportPath,report);
            const auto next=nextWork(classification,section,analysis,built);stageReports[nextStage]=next;
            finalStatus=next.at("status").To<std::string>();passed=finalStatus=="passed";report["stage"]=nextStage;
        }
        report["stages"]=stageReports;report["status"]=finalStatus;
        report["upstreamSource"]=source;Save(reportPath,report);
        return passed?0:finalStatus=="partial"?partialExitCode:1;
    } catch(const Standard_Failure& exception) {report["error"]=std::string(exception.GetMessageString());}
      catch(const std::exception& exception) {report["error"]=std::string(exception.what());}
    report["status"]=std::string("failed");Save(reportPath,report);return 1;
}
