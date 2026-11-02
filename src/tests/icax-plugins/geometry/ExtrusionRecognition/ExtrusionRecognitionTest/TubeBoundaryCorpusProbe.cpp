// Standalone corpus consumer of the current Classify and tube boundary core.
// Reuse the stage probe's exact neutral/JSON/geometry helpers, not its old outputs.
#define wmain TubeStageUnusedWmain
#include "TubeStageProbe.cpp"
#undef wmain
#include "OpenCascadeResourceImport/TubeBoundaryGeometry.h"
#include "OpenCascadeResourceImport/OpenCascadeTubeCSGConverter.h"
#include "GeometryData/TubeNeutralPersistence.h"
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>

namespace {
G::BRepModel CorpusRead(const fs::path& file) {
    std::ifstream stream(file,std::ios::binary);
    if(!stream)throw std::runtime_error("Missing neutral input: "+Utf8(file));
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});
    return G::Persistence::Deserialize(bytes);
}
void CorpusBytes(const fs::path& file,const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(file,std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(!output)throw std::runtime_error("Cannot persist core evidence");
}
R::SSectionWiresResult CorpusSection(const fs::path& directory) {
    const auto curves=CorpusRead(directory/"section-curves.icaxbrep");
    std::ifstream stream(directory/"section-domains.json",std::ios::binary);
    const std::string value((std::istreambuf_iterator<char>(stream)),{});
    const auto domains=Json::Parse(value).To<ObjectMap>();
    R::SSectionWiresResult section;section.bSuccess=true;std::size_t curveIndex=0;
    for(const auto& entry:domains.at("wires").To<VariantArray>()) {
        const auto data=entry.To<ObjectMap>();R::SSectionWire wire;
        wire.bInner=data.at("inner").To<bool>();wire.bClosed=data.at("closed").To<bool>();wire.dSignedArea=data.at("area").To<double>();
        if(!wire.bClosed)throw std::runtime_error("Cached section wire is open");
        for(const auto& item:data.at("edges").To<VariantArray>()) {
            const auto values=item.To<ObjectMap>();R::SSectionWireEdge edge;
            if(curveIndex>=curves.Curves2.size()||curves.Curves2[curveIndex].Id!=curveIndex+1)throw std::runtime_error("Cached section curve identity mismatch");
            edge.Curve=curves.Curves2[curveIndex++].Geometry;
            edge.SourceEdgeId=std::stoull(values.at("sourceEdgeId").To<std::string>());
            edge.SourceFaceId=std::stoull(values.at("sourceFaceId").To<std::string>());
            edge.Range={values.at("first").To<double>(),values.at("last").To<double>()};edge.bReversed=values.at("reversed").To<bool>();
            const auto a=values.at("start").To<VariantArray>(),b=values.at("end").To<VariantArray>();
            edge.Start={a.at(0).To<double>(),a.at(1).To<double>()};edge.End={b.at(0).To<double>(),b.at(1).To<double>()};
            const auto curve=O::BuildOpenCascadeCurve2(edge.Curve);if(curve.IsNull())throw std::runtime_error("Cached exact section curve is invalid");
            if(curve->Value(edge.bReversed?edge.Range.Last:edge.Range.First).Distance(gp_Pnt2d(edge.Start.X,edge.Start.Y))>.0011
                ||curve->Value(edge.bReversed?edge.Range.First:edge.Range.Last).Distance(gp_Pnt2d(edge.End.X,edge.End.Y))>.0011)
                throw std::runtime_error("Cached section endpoint/range mismatch");
            wire.Edges.push_back(std::move(edge));
        }
        if(wire.Edges.empty())throw std::runtime_error("Cached section has an empty wire");
        for(std::size_t i=0;i<wire.Edges.size();++i)if(std::hypot(wire.Edges[i].End.X-wire.Edges[(i+1)%wire.Edges.size()].Start.X,
            wire.Edges[i].End.Y-wire.Edges[(i+1)%wire.Edges.size()].Start.Y)>.0011)throw std::runtime_error("Cached section is disconnected");
        section.Wires.push_back(std::move(wire));
    }
    if(curveIndex!=curves.Curves2.size()||section.Wires.empty())throw std::runtime_error("Unused/absent cached section curves");
    return section;
}
G::Tube::CPlanarRegion2 CorpusRegion(const R::SSectionWiresResult& section) {
    G::Tube::CPlanarRegion2 result;std::size_t w=0;
    for(const auto& wire:section.Wires) {
        G::Tube::SRegionLoop2 loop;loop.ID="section/loop/"+std::to_string(++w);std::size_t e=0;
        for(const auto& edge:wire.Edges) {G::CurveSegment2 curve;curve.Curve=edge.Curve;curve.Range={edge.Range.First,edge.Range.Last,edge.bReversed};
            // Section extraction retains supporting Line2 plus a finite trim.
            // The authoring region uses finite Segment2 for that exact span.
            if(std::holds_alternative<G::Line2>(edge.Curve)||std::holds_alternative<G::Ray2>(edge.Curve)||std::holds_alternative<G::Segment2>(edge.Curve)) {
                const auto support=O::BuildOpenCascadeCurve2(edge.Curve);
                const auto a=support->Value(edge.Range.First),b=support->Value(edge.Range.Last);
                curve.Curve=G::Segment2{{a.X(),a.Y()},{b.X(),b.Y()}};curve.Range={0,1,edge.bReversed};
            }
            loop.Curves.push_back({loop.ID+"/edge/"+std::to_string(++e),std::move(curve)});}
        if((wire.bInner&&wire.dSignedArea>0)||(!wire.bInner&&wire.dSignedArea<0)) {
            std::reverse(loop.Curves.begin(),loop.Curves.end());for(auto& curve:loop.Curves)curve.Segment.Range.Reversed=!curve.Segment.Range.Reversed;
        }
        result.Boundaries.push_back(std::move(loop));
    }
    return result;
}
TopoDS_Shape CorpusMaterialReference(const TopoDS_Shape& source,ObjectMap& report) {
    if(Count(source,TopAbs_SOLID)) {report["sourceReferenceMethod"]=std::string("original-solid-containers");return source;}
    TopTools_IndexedMapOfShape shells,sourceFaces;TopExp::MapShapes(source,TopAbs_SHELL,shells);TopExp::MapShapes(source,TopAbs_FACE,sourceFaces);
    report["sourceShells"]=shells.Extent();
    if(shells.Extent()!=1||!BRep_Tool::IsClosed(shells(1)))return source;
    BRepBuilderAPI_MakeSolid make(TopoDS::Shell(shells(1)));
    if(!make.IsDone())return source;auto solid=make.Solid();
    if(!BRepLib::OrientClosedSolid(solid)||!BRepCheck_Analyzer(solid,true).IsValid())return source;
    TopTools_IndexedMapOfShape assembled;TopExp::MapShapes(solid,TopAbs_FACE,assembled);
    if(assembled.Extent()!=sourceFaces.Extent())return source;
    for(int i=1;i<=assembled.Extent();++i)if(!sourceFaces.Contains(assembled(i)))return source;
    if(Relative(Area(source),Area(solid))>=1e-12)return source;
    report["sourceReferenceMethod"]=std::string("existing-closed-shell-container-only; identical-face-TShapes-supports-and-trims; material-orientation-certified");
    report["referenceNoFacesAddedOrRemoved"]=true;report["referenceExactNativeFaceIdentityPreserved"]=true;
    return solid;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 2;
    const std::wstring mode=argv[1];const fs::path input=argv[2],out=argv[3];fs::create_directories(out);
    const auto began=Clock::now();std::string stage="input";ObjectMap report{{"status",std::string("running")},{"mode",Utf8(argv[1])},{"input",Utf8(input)}};
    const auto save=[&]{report["stage"]=stage;report["seconds"]=std::chrono::duration<double>(Clock::now()-began).count();Save(out/"result.json",report);};save();
    try {
        R::CExtrudeRecognizesService service;G::BRepModel aligned;R::SSectionWiresResult section;
        G::BRepModel source=CorpusRead(mode==L"--cache"?input/"aligned.icaxbrep":input);const auto sourceBytes=G::Persistence::Serialize(source);
        if(mode==L"--cache") {aligned=source;stage="cached-section-validation";save();section=CorpusSection(input);}
        else if(mode==L"--healed"||mode==L"--aligned") {
            if(mode==L"--healed") {
                stage="align";save();const auto direction=service.Recognize(source);
                report["alignDiagnostics"]=Strings(direction.Diagnostics);
                if(!direction.bSuccess)throw std::runtime_error("Current axis recognition failed");aligned=direction.AlignedGeometry;
            } else aligned=source;
            stage="contour";save();section=service.ExtractSectionWires(aligned);
            report["contourDiagnostics"]=Strings(section.Diagnostics);
            if(!section.bSuccess)throw std::runtime_error("Current exact section extraction failed");
        } else throw std::runtime_error("Unknown corpus input mode");
        const auto alignedBytes=G::Persistence::Serialize(aligned);CorpusBytes(out/"aligned.icaxbrep",alignedBytes);
        report["sectionWires"]=int(section.Wires.size());report["alignedFaces"]=int(aligned.Faces.size());
        const auto original=Build(aligned);report["sourceSolids"]=Count(original,TopAbs_SOLID);report["sourceValid"]=BRepCheck_Analyzer(original,true).IsValid();
        const auto materialReference=CorpusMaterialReference(original,report);report["sourceReferenceSolids"]=Count(materialReference,TopAbs_SOLID);
        G::BRepModel classificationInput=aligned;
        if(!Count(original,TopAbs_SOLID)&&Count(materialReference,TopAbs_SOLID)) {
            classificationInput=O::ConvertOpenCascadeShapeToBRepWithDisplay(materialReference,"","",[](const TopoDS_Face&,std::uint64_t){return G::Triangulation3{};});
            const auto proof=O::ValidateTubeCSGShapeReplay(materialReference,Build(classificationInput),1e-6);
            if(!proof.bEquivalent)throw std::runtime_error("Closed-shell reference neutral conversion changed material");
            report["referenceNeutralRoundtripPassed"]=true;
        }
        stage="classify";save();R::SFeatureToolpathOptions options;options.bSingleExtrudedStock=true;
        const auto classification=service.ClassifySectionFaces(classificationInput,section,options);
        report["classificationDiagnostics"]=Strings(classification.Diagnostics);
        report["machiningFaces"]=int(classification.MachiningGeometry.Faces.size());report["nonMachiningFaces"]=int(classification.NonMachiningGeometry.Faces.size());
        if(!classification.bSuccess)throw std::runtime_error("Current geometric Classify failed");
        G::Tube::CTubeCadDocument document;document.Revision=1;document.Geometry.Section=CorpusRegion(section);document.Geometry.MachiningFaces=classification.MachiningGeometry;
        VariantArray orientations;const auto explicitBuilt=O::ReconstructOpenCascadeShape(document.Geometry.MachiningFaces);
        std::map<const void*,std::uint64_t> faceIds;for(const auto& [id,face]:explicitBuilt.Faces)faceIds[face.TShape().get()]=id;
        for(TopExp_Explorer faces(explicitBuilt.Shape,TopAbs_FACE);faces.More();faces.Next())orientations.emplace_back(ObjectMap{{"faceId",std::to_string(faceIds.at(faces.Current().TShape().get()))},{"orientation",int(faces.Current().Orientation())}});
        report["machiningFaceOccurrences"]=orientations;
        const auto coreBytes=G::Tube::Persistence::Serialize(document);CorpusBytes(out/"candidate.ictcad",coreBytes);
        stage="restore-core";save();const auto restored=O::RestoreTubeBoundary(document.Geometry,1e-6);
        report["restoredSolids"]=Count(restored,TopAbs_SOLID);report["restoredFaces"]=Count(restored,TopAbs_FACE);
        report["sourceAreaMm2"]=Area(original);report["restoredAreaMm2"]=Area(restored);
        const double areaError=Relative(Area(original),Area(restored));report["areaRelativeError"]=areaError;
        stage="source-equivalence";save();
        if(!Count(materialReference,TopAbs_SOLID))throw std::runtime_error("Core restored; independent source material proof unresolved: source has no solid or single existing closed shell container");
        const auto replay=O::ValidateTubeCSGShapeReplay(materialReference,restored,1e-6);
        report["materialEquivalent"]=replay.bEquivalent;report["missingVolumeMm3"]=replay.MissingVolume;report["unexpectedVolumeMm3"]=replay.UnexpectedVolume;
        report["equivalenceDiagnostics"]=Strings(replay.Diagnostics);
        if(!replay.bEquivalent)throw std::runtime_error("Restored core changes source material in one or both directions");
        if(!(areaError<1e-6))throw std::runtime_error("Restored core changes complete source boundary area");
        stage="reopen-core";save();const auto reopened=G::Tube::Persistence::DeserializeDocument(coreBytes);
        if(G::Tube::Persistence::Serialize(reopened)!=coreBytes)throw std::runtime_error("Core binary roundtrip changed exact records");
        const auto replayed=O::RestoreTubeBoundary(reopened.Geometry,1e-6);const auto cold=O::ValidateTubeCSGShapeReplay(materialReference,replayed,1e-6);
        if(!cold.bEquivalent||Relative(Area(original),Area(replayed))>=1e-6)throw std::runtime_error("Saved core cannot independently restore the original material and boundary");
        report["sourceNeutralUnchanged"]=sourceBytes==G::Persistence::Serialize(source)&&alignedBytes==G::Persistence::Serialize(aligned);
        if(!report.at("sourceNeutralUnchanged").To<bool>())throw std::runtime_error("Source neutral CAD changed");
        BRepTools::Write(replayed,(out/"whole-model.brep").string().c_str());CorpusBytes(out/"core.ictcad",coreBytes);
        report["coreOnlyRestore"]=true;report["binaryReopenPassed"]=true;report["status"]=std::string("passed");stage="complete";save();return 0;
    }catch(const Standard_Failure& error){report["error"]=std::string(error.GetMessageString());}
     catch(const std::exception& error){report["error"]=std::string(error.what());}
    report["status"]=std::string("failed");save();return 1;
}
