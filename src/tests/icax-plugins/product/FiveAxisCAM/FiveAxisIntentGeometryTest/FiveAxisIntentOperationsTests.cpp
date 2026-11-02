#include <gtest/gtest.h>
#include <FiveAxisCAM/FiveAxisIntentOperations.h>
#include <FiveAxisCAM/FiveAxisIntentProjection.h>
#include <FiveAxisCAM/FiveAxisIntentPoseField.h>
#include <OpenCascadeResourceImport/CadReferenceService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <GeometryServices/OffsetCurveTraversal.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <Database/IMetaRegistry.h>
#include <Database/IRepository.h>
#include <Database/MetaRegistrationCatalog.h>
#include <Data/VariantSerializer.h>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Circ.hxx>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <thread>

namespace
{
using namespace iCAX::FiveAxisCAM;
using namespace iCAX::CAM::Intent;
using namespace iCAX::GeometryData;
using namespace iCAX::GeometryServices;
using Map=IntentMap;using Array=IntentArray;
template<class T> const T& Record(const std::vector<T>& values,std::uint64_t id)
{for(const auto& value:values)if(value.Id==id)return value;throw std::runtime_error("Test geometry record missing");}
ParameterRange Range(const SEditableSegment& segment)
{return std::visit([](const auto& exact){if constexpr(requires{exact.Source.Range;})return exact.Source.Range;else return exact.Range;},segment.Curve);}
Map SurfacePose(){return {{"mode",std::string("Continuous")},{"initialization",std::string("SurfaceNormal")},{"leadAngleRad",0.},{"sideAngleRad",0.}};}
Map KerfPolicy(){return {{"owner",std::string("Cam")},{"widthMm",2.},{"miterLimit",4.},{"measurementSection",std::string("Surface")},{"cornerMode",std::string("Miter")}};}
Map Lead(const std::string& kind,double length=0)
{Map value{{"kind",kind},{"placement",std::string("ScrapSide")},{"lengthMm",length},{"angleRad",0.}};if(kind=="Arc")value["radiusMm"]=1.;return value;}
std::shared_ptr<iCAX::Database::IRepository> Repository()
{auto meta=iCAX::Database::CreateMetaRegistry();iCAX::Database::CMetaRegistrationCatalog::ReplayAll(*meta);return iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID(),meta);}
std::vector<std::string> BackendPaths()
{
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);const auto directory=std::filesystem::path(executable).parent_path();std::vector<std::string> paths;
    for(const auto* name:{L"OpenCascadeResourceImport.dll",L"OpenCascadeGeometryServices.dll"}){const auto path=directory/name;if(!LoadLibraryW(path.c_str()))throw std::runtime_error("Test backend cannot load");paths.push_back(path.string());}return paths;
}
BRepModel PlateWithHole()
{
    BRepBuilderAPI_MakePolygon polygon;for(const auto point:{gp_Pnt(-30,-30,0),gp_Pnt(30,-30,0),gp_Pnt(30,30,0),gp_Pnt(-30,30,0)})polygon.Add(point);polygon.Close();
    auto hole=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2({},gp::DZ()),10)).Edge()).Wire();hole.Reverse();BRepBuilderAPI_MakeFace face(polygon.Wire());face.Add(hole);
    return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(face.Face(),"registration plate","intent-operations",1e-7);
}
BRepModel PlateWithSquareHole()
{
    BRepBuilderAPI_MakePolygon outer,hole;for(const auto point:{gp_Pnt(-30,-30,0),gp_Pnt(30,-30,0),gp_Pnt(30,30,0),gp_Pnt(-30,30,0)})outer.Add(point);outer.Close();for(const auto point:{gp_Pnt(-10,-10,0),gp_Pnt(10,-10,0),gp_Pnt(10,10,0),gp_Pnt(-10,10,0)})hole.Add(point);hole.Close();auto inner=hole.Wire();inner.Reverse();BRepBuilderAPI_MakeFace face(outer.Wire());face.Add(inner);return iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(face.Face(),"square opening plate","intent-field-connector",1e-7);
}
class FiveAxisIntentOperations:public ::testing::Test
{
protected:
    iCAX::Services::CServiceProvider Provider;
    std::shared_ptr<const iCAX::Geometry::ICadReferenceService> Cad;
    std::shared_ptr<const IBoundaryLeadService> Boundary;
    std::shared_ptr<const ICurveOffset3DService> Offset;
    std::shared_ptr<const IToolpathGeometryServices> Geometry;
    std::shared_ptr<const CFiveAxisIntentOperationContext> Snapshot;
    std::shared_ptr<iCAX::Database::IRepository> Db;
    std::shared_ptr<CToolpathOperationRegistry> Registry;
    std::unique_ptr<CEditableToolpathDocument> Document;
    IntentId Plan;std::thread::id Owner;std::atomic_int FreezeCalls=0;
    void SetUp()override
    {
        static const auto paths=BackendPaths();iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(Provider,paths);
        Cad=Provider.Resolve<iCAX::Geometry::ICadReferenceService>();Boundary=Provider.Resolve<IBoundaryLeadService>();Offset=Provider.Resolve<ICurveOffset3DService>();Geometry=CreateFiveAxisIntentGeometryServices(Cad,Offset);
        Owner=std::this_thread::get_id();Registry=std::make_shared<CToolpathOperationRegistry>();RegisterStandardToolpathOperations(*Registry);
        RegisterFiveAxisIntentOperations(*Registry,[this](const Map&){if(std::this_thread::get_id()!=Owner)throw std::runtime_error("Worker resolved live context");++FreezeCalls;return Snapshot;});
        Db=Repository();Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry,Geometry);Plan=Document->CreatePlan("registered native pipeline");
    }
    Map Command(const std::string& kind)const{return {{"kind",kind},{"commandId",iCAX::Data::GenerateNewUUID()},{"planId",Plan}};}
    bool Generate()
    {
        auto task=Document->GenerateAsync(Plan,2);const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(!task->IsReady()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!task->IsReady()){ADD_FAILURE()<<"Test generation timed out";return false;}
        if(!Document->Publish(task)){ADD_FAILURE()<<"Test generation could not be published";return false;}
        const auto result=Document->Resolve(Plan);for(const auto& diagnostic:result.Diagnostics)ADD_FAILURE()<<diagnostic.Message;EXPECT_EQ(EOutputCompleteness::Complete,result.Status);return result.Status==EOutputCompleteness::Complete;
    }
    std::vector<SResolvedPath> Paths()const{return Document->Resolve(Plan).Paths;}
    IntentId Add(const std::string& type,const Map& parameters)
    {
        const auto paths=Paths();if(paths.size()!=1)throw std::runtime_error("Single current path required by test");const auto& path=paths.front();auto command=Command("AddOperation");command["typeId"]=type;command["parameters"]=parameters;command["context"]=Map{{"assetVersion",std::string("geometry-1")}};
        command["inputs"]=Array{Map{{"slot",std::string("input")},{"source",ToData(path.Port)},{"replace",true},{"ranges",Array{}}}};command["expectedInputs"]=Map{{ToolpathPortKey(path.Port),path.Fingerprint}};
        const auto reply=Document->Execute(command);if(!Generate())throw std::runtime_error("Test operation generation failed: "+type);return reply.at("operationId").To<IntentId>();
    }
    SToolpathValue CircleSource(BRepModel model,bool cylindrical=false)
    {
        std::optional<SFiveAxisIntentCadSpan> binding;CurveSegment3 curve;
        for(const auto& face:model.Faces){const auto& support=Record(model.Surfaces3,face.Surface3Id).Geometry;if(cylindrical!=std::holds_alternative<CylindricalSurface3>(support))continue;
            for(auto wireId:face.WireIds){const auto& wire=Record(model.Wires,wireId);for(std::size_t i=0;i<wire.Coedges.size();++i){const auto& coedge=wire.Coedges[i];const auto& edge=Record(model.Edges,coedge.EdgeId);if(!edge.Curve3Id)continue;const auto& geometry=Record(model.Curves3,edge.Curve3Id).Geometry;
                if(std::holds_alternative<Circle3>(geometry)){binding=SFiveAxisIntentCadSpan{face.Id,wire.Id,edge.Id,i};curve={geometry,edge.Range};curve.Range.Reversed=coedge.Orientation==ETopologyOrientation::Reversed;break;}}
                if(binding)break;}if(binding)break;}
        if(!binding)throw std::runtime_error("Test CAD circular boundary unavailable");
        auto value=MakeFiveAxisIntentValue(CompositeCurve3{{curve},true},{"source/circle"});SFiveAxisIntentMetadata metadata;metadata.SetupID=iCAX::Data::GenerateNewUUID();metadata.PlacementID=iCAX::Data::GenerateNewUUID();metadata.SourceKey="native/circle";
        metadata.Feature={{"machiningKind",std::string("Cut")},{"boundaryRole",std::string(cylindrical?"OuterTrim":"Opening")},{"retainedSide",std::string(cylindrical?"Interior":"Exterior")}};metadata.Pose=SurfacePose();
        metadata.SourceSpans={{"source/circle",Map{{"faceId",std::to_string(binding->FaceId)},{"wireId",std::to_string(binding->WireId)},{"edgeId",std::to_string(binding->EdgeId)},{"coedgeIndex",std::to_string(binding->CoedgeIndex)},{"first",curve.Range.First},{"last",curve.Range.Last},{"reversed",curve.Range.Reversed}}}};
        Snapshot=std::make_shared<CFiveAxisIntentOperationContext>(std::move(model),BuildFiveAxisIntentCadBindings(metadata.SourceSpans),"geometry-1","cad-0.14.8601/offset-1","Workpiece",1,Boundary,Offset);
        value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);return value;
    }
    void Source(SToolpathValue value)
    {auto command=Command("CreateSource");command["value"]=ToData(value);Document->Execute(command);if(!Generate())throw std::runtime_error("Test source generation failed");}
    SToolpathValue SquareSource(bool opening=false)
    {
        auto model=opening?PlateWithSquareHole():PlateWithHole();CompositeCurve3 curve;curve.Closed=true;std::vector<std::string> ids;CFiveAxisIntentOperationContext::Bindings bindings;SFiveAxisIntentMetadata metadata;metadata.SetupID=iCAX::Data::GenerateNewUUID();metadata.PlacementID=iCAX::Data::GenerateNewUUID();metadata.SourceKey="native/square";metadata.Feature={{"machiningKind",std::string("Cut")},{"boundaryRole",std::string(opening?"Opening":"OuterTrim")},{"retainedSide",std::string("Interior")}};metadata.Pose=SurfacePose();const auto& face=model.Faces.front();for(auto wireId:face.WireIds){if(opening&&wireId!=face.WireIds.back())continue;const auto& wire=Record(model.Wires,wireId);if(wire.Coedges.size()!=4)continue;for(std::size_t i=0;i<wire.Coedges.size();++i){const auto& coedge=wire.Coedges[i];const auto& edge=Record(model.Edges,coedge.EdgeId);const auto id="square/"+std::to_string(i);CurveSegment3 segment{Record(model.Curves3,edge.Curve3Id).Geometry,edge.Range};segment.Range.Reversed=coedge.Orientation==ETopologyOrientation::Reversed;curve.Segments.push_back(segment);ids.push_back(id);bindings[id]={face.Id,wire.Id,edge.Id,i};metadata.SourceSpans[id]=Map{{"faceId",std::to_string(face.Id)},{"wireId",std::to_string(wire.Id)},{"edgeId",std::to_string(edge.Id)},{"coedgeIndex",std::to_string(i)},{"first",segment.Range.First},{"last",segment.Range.Last},{"reversed",segment.Range.Reversed}};}break;}if(curve.Segments.size()!=4)throw std::runtime_error("Native square boundary unavailable");auto value=MakeFiveAxisIntentValue(curve,ids);value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);Snapshot=std::make_shared<CFiveAxisIntentOperationContext>(std::move(model),bindings,"geometry-square-1","native-services-1","Workpiece",1,Boundary,Offset);return value;
    }
    void ReopenAndCompare()
    {
        const auto before=Paths();auto db=Repository();const auto bytes=iCAX::Data::VariantSerializer::Serialize(Document->ExportDocument());const auto saved=iCAX::Data::VariantSerializer::Deserialize(bytes).To<Map>();
        const auto id=CEditableToolpathDocument::ImportDocument(db,saved,Geometry);CEditableToolpathDocument reopened(db,id,Registry,Geometry);const auto after=reopened.Resolve(Plan);ASSERT_EQ(EOutputCompleteness::Complete,after.Status);ASSERT_EQ(before.size(),after.Paths.size());
        for(const auto& original:before){const auto found=std::find_if(after.Paths.begin(),after.Paths.end(),[&](const auto& candidate){return candidate.PathId==original.PathId;});ASSERT_NE(after.Paths.end(),found);EXPECT_EQ(ToData(original.Value),ToData(found->Value));}
    }
    double Length(const SToolpathValue& value)
    {
        CompositeOffsetCurve3 exact;exact.Closed=value.Geometry.Closed;for(const auto& segment:value.Geometry.Segments){OffsetCurvePiece3 piece;std::visit([&](const auto& curve){if constexpr(!std::is_same_v<std::decay_t<decltype(curve)>,CurveSegment2>)piece.Geometry=curve;},segment.Curve);exact.Segments.push_back(std::move(piece));}
        OffsetTraversal::Evaluator evaluator(*Offset);return evaluator.Length(exact);
    }
};
}

TEST_F(FiveAxisIntentOperations, PoseKerfGapAndLineArcLeadsRemainEditableAndPersistent)
{
    Source(CircleSource(PlateWithHole()));const auto identity=Paths()[0].PathId;
    Add("fiveaxis.pose",{{"policy",SurfacePose()}});Add("fiveaxis.kerf",{{"policy",KerfPolicy()}});
    ASSERT_EQ(1u,Paths().size());const auto compensated=Paths()[0];ASSERT_TRUE(std::holds_alternative<DirectionalCurveOffset3>(compensated.Value.Geometry.Segments[0].Curve));
    for(double u:{.2,1.1,3.8}){const auto point=Geometry->Evaluate(compensated.Value.Geometry,compensated.Value.Geometry.Segments[0],u);EXPECT_NEAR(9,std::hypot(point.X,point.Y),1e-9);EXPECT_NEAR(0,point.Z,1e-9);}
    Add("fiveaxis.trace",{{"gapLengthMm",3.},{"overcutLengthMm",0.}});EXPECT_NEAR(18*std::numbers::pi-3,Length(Paths()[0].Value),1e-7);
    Add("fiveaxis.leads",{{"leadIn",Lead("Line",1.5)},{"leadOut",Lead("Arc",.6)}});const auto result=Paths()[0];EXPECT_EQ(identity,result.PathId);ASSERT_EQ(3u,result.Value.Geometry.Segments.size());
    EXPECT_EQ("LeadIn",result.Value.Geometry.Segments.front().ConstructionRole);EXPECT_EQ("LeadOut",result.Value.Geometry.Segments.back().ConstructionRole);EXPECT_TRUE(std::holds_alternative<Circle3>(std::get<CurveSegment3>(result.Value.Geometry.Segments.back().Curve).Curve));
    for(std::size_t i=0;i<result.Value.Geometry.Segments.size();++i){const auto range=Range(result.Value.Geometry.Segments[i]);const auto axis=EvaluateFiveAxisIntentDirection(*Snapshot,result.Value,i,(range.First+range.Last)/2);EXPECT_NEAR(0,axis.X,1e-10);EXPECT_NEAR(0,axis.Y,1e-10);EXPECT_NEAR(1,axis.Z,1e-10);}
    const auto cutRange=Range(result.Value.Geometry.Segments[1]);const SPathAnchor anchor{result.Port,result.Value.Geometry.Segments[1].Id,(cutRange.First+cutRange.Last)/2};
    const Array cuts{Map{{"id",std::string("continued-edit")},{"anchor",ToData(anchor)}}};
    Add("icax.edit.split",{{"cuts",cuts}});ASSERT_EQ(2u,Paths().size());ReopenAndCompare();EXPECT_GT(FreezeCalls.load(),0);
}

TEST_F(FiveAxisIntentOperations, OvercutPreservesExactRepeatedTraversalOccurrences)
{
    Source(CircleSource(PlateWithHole()));Add("fiveaxis.pose",{{"policy",SurfacePose()}});Add("fiveaxis.kerf",{{"policy",KerfPolicy()}});Add("fiveaxis.trace",{{"gapLengthMm",0.},{"overcutLengthMm",5.}});
    const auto value=Paths()[0].Value;ASSERT_EQ(2u,value.Geometry.Segments.size());EXPECT_NEAR(18*std::numbers::pi+5,Length(value),1e-7);EXPECT_FALSE(value.Geometry.Closed);
    EXPECT_NE(value.Geometry.Segments[0].Id,value.Geometry.Segments[1].Id);EXPECT_TRUE(std::all_of(value.Geometry.Segments.begin(),value.Geometry.Segments.end(),[](const auto& segment){return std::holds_alternative<DirectionalCurveOffset3>(segment.Curve);}));
    EXPECT_TRUE(std::any_of(value.Geometry.Segments[1].Sources.begin(),value.Geometry.Segments[1].Sources.end(),[](const auto& mapping){return mapping.Occurrence==1;}));ReopenAndCompare();
}
TEST_F(FiveAxisIntentOperations, IsolatedRoundConnectorRetainsItsSavedExactNormalBasis)
{
    Source(SquareSource());Add("fiveaxis.pose",{{"policy",SurfacePose()}});auto policy=KerfPolicy();policy["cornerMode"]=std::string("Round");Add("fiveaxis.kerf",{{"policy",policy}});const auto parent=Paths()[0];const auto connector=std::find_if(parent.Value.Geometry.Segments.begin(),parent.Value.Geometry.Segments.end(),[](const auto& segment){return !segment.ConstructionRole.empty();});ASSERT_NE(parent.Value.Geometry.Segments.end(),connector);const auto range=Range(*connector);const auto a=range.First+(range.Last-range.First)/3,b=range.First+2*(range.Last-range.First)/3;Add("icax.edit.split",{{"cuts",Array{Map{{"id",std::string("connector-a")},{"anchor",ToData(SPathAnchor{parent.Port,connector->Id,a})}},Map{{"id",std::string("connector-b")},{"anchor",ToData(SPathAnchor{parent.Port,connector->Id,b})}}}}});const auto paths=Paths();ASSERT_EQ(2u,paths.size());const auto isolated=std::find_if(paths.begin(),paths.end(),[](const auto& path){return path.Value.Geometry.Segments.size()==1&&!path.Value.Geometry.Segments[0].ConstructionRole.empty();});ASSERT_NE(paths.end(),isolated);CFiveAxisIntentPoseField field(*Snapshot,isolated->Value);for(const auto u:{a,(a+b)/2,b}){const auto axis=field.At(0,u),normal=field.NormalAt(0,u);EXPECT_NEAR(0,axis.X,1e-10);EXPECT_NEAR(0,axis.Y,1e-10);EXPECT_NEAR(1,axis.Z,1e-10);EXPECT_NEAR(1,normal.Z,1e-10);}ReopenAndCompare();
}
TEST_F(FiveAxisIntentOperations, SquareOpeningRoundKerfRetainsFieldThroughTraceLeadsAndAllMotionStations)
{
    Source(SquareSource(true));Add("fiveaxis.pose",{{"policy",SurfacePose()}});auto policy=KerfPolicy();policy["cornerMode"]=std::string("Round");policy["widthMm"]=.4;Add("fiveaxis.kerf",{{"policy",policy}});Add("fiveaxis.trace",{{"gapLengthMm",0.},{"overcutLengthMm",0.}});Add("fiveaxis.leads",{{"leadIn",Lead("None")},{"leadOut",Lead("None")}});const auto path=Paths()[0];CompositeOffsetCurve3 exact;exact.Closed=path.Value.Geometry.Closed;for(std::size_t i=0;i<path.Value.Geometry.Segments.size();++i){OffsetCurvePiece3 piece;const auto& segment=path.Value.Geometry.Segments[i];if(const auto* offset=std::get_if<DirectionalCurveOffset3>(&segment.Curve))piece.Geometry=*offset;else piece.Geometry=std::get<CurveSegment3>(segment.Curve);exact.Segments.push_back(piece);}OffsetTraversal::Evaluator evaluator(*Offset);const auto samples=evaluator.Sample(exact);CFiveAxisIntentPoseField field(*Snapshot,path.Value);for(const auto& sample:samples.Points){SCOPED_TRACE(path.Value.Geometry.Segments[sample.PieceIndex].Id+" u="+std::to_string(sample.Value.SourceParameter));EXPECT_NEAR(1,field.At(sample.PieceIndex,sample.Value.SourceParameter).Z,1e-10);}for(const auto& item:path.Value.Channels.at("poseField").To<Map>().at("entries").To<Array>()){const auto range=RangeFromData(item.To<Map>().at("range").To<Map>());for(const auto& anchor:{range.Start,range.End}){const auto found=std::find_if(path.Value.Geometry.Segments.begin(),path.Value.Geometry.Segments.end(),[&](const auto& segment){return segment.Id==anchor.SegmentId;});ASSERT_NE(path.Value.Geometry.Segments.end(),found);const auto i=std::size_t(found-path.Value.Geometry.Segments.begin());EXPECT_NEAR(1,field.At(i,anchor.Parameter).Z,1e-10);}}ReopenAndCompare();
}
TEST_F(FiveAxisIntentOperations, RestoringStandardHoleContourCoversAlreadyConstructedRoundConnectors)
{
    auto source=SquareSource(true);auto metadata=ReadFiveAxisIntentMetadata(source);const Map fixed{{"mode",std::string("FixedDirection")},{"frame",std::string("Workpiece")},{"direction",Array{0.,0.,1.}}};metadata.Pose=fixed;metadata.Feature["standardHole"]=Map{{"kind",std::string("Rectangle")}};metadata.Feature["contourPoseData"]=Map{{"basis",SurfacePose()},{"controlsSet",std::string("contour")}};source.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);Source(source);Add("fiveaxis.pose",{{"policy",fixed}});auto policy=KerfPolicy();policy["widthMm"]=.4;policy["cornerMode"]=std::string("Round");Add("fiveaxis.kerf",{{"policy",policy}});Add("fiveaxis.trace",{{"gapLengthMm",0.},{"overcutLengthMm",0.}});Add("fiveaxis.leads",{{"leadIn",Lead("None")},{"leadOut",Lead("None")}});Add("fiveaxis.cutType",{{"cutType",std::string("Contour")}});const auto path=Paths()[0];ASSERT_EQ(8u,path.Value.Geometry.Segments.size());CFiveAxisIntentPoseField field(*Snapshot,path.Value);for(std::size_t i=0;i<path.Value.Geometry.Segments.size();++i){SCOPED_TRACE(path.Value.Geometry.Segments[i].Id);const auto range=Range(path.Value.Geometry.Segments[i]);for(int j=0;j<=8;++j)EXPECT_NEAR(1,field.At(i,range.First+(range.Last-range.First)*j/8.).Z,1e-10);}ReopenAndCompare();
}
TEST_F(FiveAxisIntentOperations, ManualKerfEditIsRejectedBeforeDefinitionsHistoryOrResultsChange)
{
    Source(CircleSource(PlateWithHole()));Add("fiveaxis.pose",{{"policy",SurfacePose()}});Add("fiveaxis.kerf",{{"policy",KerfPolicy()}});const auto path=Paths()[0];const auto before=Document->ExportDocument();const auto history=Db->GetUndoArray();const auto range=Range(path.Value.Geometry.Segments[0]);const Map control{{"id",std::string("unsupported-manual")},{"kind",std::string("Control")},{"set",std::string("active")},{"boundaryPolicy",std::string("Duplicate")},{"anchor",ToData(SPathAnchor{path.Port,path.Value.Geometry.Segments[0].Id,(range.First+range.Last)/2})},{"direction",Array{.6,0.,.8}},{"transition",std::string("ArcLength")}};
    try{(void)Document->Preview("fiveaxis.pose.delta",{{"controls",Array{control}}},{path.Port});FAIL()<<"Unsupported manual + nonzero kerf accepted";}catch(const std::invalid_argument& error){EXPECT_NE(std::string::npos,std::string(error.what()).find("CAM_POSE_MANUAL_KERF_UNSUPPORTED"));}EXPECT_EQ(before,Document->ExportDocument());EXPECT_EQ(history,Db->GetUndoArray());const auto after=Paths();ASSERT_EQ(1u,after.size());EXPECT_EQ(path.Fingerprint,after[0].Fingerprint);EXPECT_EQ(ToData(path.Value),ToData(after[0].Value));
}
TEST_F(FiveAxisIntentOperations, SdkNumericVariantsRunTheNativePoseKerfTraceLeadPipeline)
{
    const auto run=[&](auto typed){using N=decltype(typed);Plan=Document->CreatePlan("SDK numeric variant");Source(CircleSource(PlateWithHole()));auto pose=SurfacePose();pose["initialization"]=std::string("FixedDirection");pose["direction"]=Array{N(0),N(0),N(1)};pose["leadAngleRad"]=N(0);pose["sideAngleRad"]=N(0);Add("fiveaxis.pose",{{"policy",pose}});auto kerf=KerfPolicy();kerf["widthMm"]=N(2);kerf["miterLimit"]=N(4);Add("fiveaxis.kerf",{{"policy",kerf}});Add("fiveaxis.trace",{{"gapLengthMm",N(0)},{"overcutLengthMm",N(0)}});auto none=Lead("None");none["lengthMm"]=N(0);none["angleRad"]=N(0);Add("fiveaxis.leads",{{"leadIn",none},{"leadOut",none}});const auto path=Paths()[0];ASSERT_EQ(1u,path.Value.Geometry.Segments.size());const auto& curve=std::get<DirectionalCurveOffset3>(path.Value.Geometry.Segments[0].Curve);EXPECT_DOUBLE_EQ(1,std::abs(curve.SignedDistance));const auto axis=CFiveAxisIntentPoseField(*Snapshot,path.Value).At(0,1.);EXPECT_NEAR(1,axis.Z,1e-12);};
    run(int(0));run(unsigned(0));run(0LL);run(0ULL);run(float(0));run(double(0));const auto before=Document->ExportDocument();const auto path=Paths()[0];auto invalid=SurfacePose();invalid["leadAngleRad"]=true;EXPECT_THROW(Document->Preview("fiveaxis.pose",{{"policy",invalid}},{path.Port}),std::invalid_argument);EXPECT_EQ(before,Document->ExportDocument());
}

TEST_F(FiveAxisIntentOperations, CurvedSupportRetainsTheExactSurfaceNormalDirectionField)
{
    auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakeCylinder(10,30).Shape(),"cylinder","intent-field",1e-7);Source(CircleSource(std::move(model),true));
    const auto poseOperation=Add("fiveaxis.pose",{{"policy",SurfacePose()}});Add("fiveaxis.kerf",{{"policy",KerfPolicy()}});const auto path=Paths()[0];const auto& segment=path.Value.Geometry.Segments[0];
    ASSERT_TRUE(std::holds_alternative<DirectionalCurveOffset3>(segment.Curve));const auto law=std::get<DirectionalCurveOffset3>(segment.Curve);ASSERT_TRUE(std::holds_alternative<SurfaceNormalField3>(law.ReferenceDirection));
    for(double u:{.2,1.5,4.5}){const auto evaluated=Offset->Evaluate(law,{u});ASSERT_EQ(ECurveOffsetStatus::SamplesEvaluated,evaluated.Status);const auto& point=evaluated.Samples[0];EXPECT_NEAR(10,std::hypot(point.OffsetPoint.X,point.OffsetPoint.Y),1e-8);EXPECT_NEAR(1,std::abs(point.OffsetPoint.Z-point.SourcePoint.Z),1e-8);}
    auto reverse=SurfacePose();reverse["axisReversed"]=true;auto command=Command("UpdateOperation");command["operationId"]=poseOperation;command["parameters"]=Map{{"policy",reverse}};command["expectedOperationFingerprint"]=Document->ReadOperation(poseOperation).at("definitionFingerprint");Document->Execute(command);ASSERT_TRUE(Generate());
    const auto reversed=std::get<DirectionalCurveOffset3>(Paths()[0].Value.Geometry.Segments[0].Curve);EXPECT_DOUBLE_EQ(-law.SignedDistance,reversed.SignedDistance);
    EXPECT_DOUBLE_EQ(-std::get<SurfaceNormalField3>(law.ReferenceDirection).Orientation,std::get<SurfaceNormalField3>(reversed.ReferenceDirection).Orientation);
    for(double u:{.2,1.5,4.5}){const auto a=Offset->Evaluate(law,{u}),b=Offset->Evaluate(reversed,{u});ASSERT_EQ(ECurveOffsetStatus::SamplesEvaluated,a.Status);ASSERT_EQ(ECurveOffsetStatus::SamplesEvaluated,b.Status);EXPECT_NEAR(0,OffsetTraversal::Distance(a.Samples[0].OffsetPoint,b.Samples[0].OffsetPoint),1e-9);}ReopenAndCompare();
}

TEST_F(FiveAxisIntentOperations, FrozenContextOwnsGeometryAndFingerprintsItsActualContent)
{
    auto model=PlateWithHole();const auto source=CircleSource(model);const auto original=Snapshot;const auto fingerprint=original->Fingerprint();const auto name=model.Metadata.Name;model.Metadata.Name="changed-after-freeze";
    EXPECT_EQ(name,original->Model().Metadata.Name);EXPECT_EQ(fingerprint,original->Fingerprint());
    const auto shared=std::make_shared<CFiveAxisIntentOperationContext>(original->FrozenModel(),original->SourceBindings(),"geometry-1","cad-0.14.8601/offset-1","Workpiece",1,Boundary,Offset);
    EXPECT_EQ(&original->Model(),&shared->Model());EXPECT_EQ(original->FrozenModel(),shared->FrozenModel());EXPECT_EQ(fingerprint,shared->Fingerprint());
    const auto changed=std::make_shared<CFiveAxisIntentOperationContext>(model,original->SourceBindings(),"geometry-1","cad-0.14.8601/offset-1","Workpiece",1,Boundary,Offset);EXPECT_NE(fingerprint,changed->Fingerprint());
    auto invalid=ReadFiveAxisIntentMetadata(source).SourceSpans;auto bad=invalid.at("source/circle").To<Map>();bad["faceId"]=-1;invalid["source/circle"]=bad;EXPECT_THROW(BuildFiveAxisIntentCadBindings(invalid),std::invalid_argument);
    bad["faceId"]=9007199254740992.;invalid["source/circle"]=bad;EXPECT_THROW(BuildFiveAxisIntentCadBindings(invalid),std::invalid_argument);
}
TEST_F(FiveAxisIntentOperations, ZeroCategoriesPublishComputedPoliciesForOpenAndClosedInputs)
{
    for(const bool closed:{false,true}){
        auto value=CircleSource(PlateWithHole());value.Geometry.Closed=closed;auto metadata=ReadFiveAxisIntentMetadata(value);metadata.Process={{"kerf",KerfPolicy()},{"gapLengthMm",4.},{"overcutLengthMm",0.},{"leadIn",Lead("Line",2.)},{"leadOut",Lead("Arc",1.)}};value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);Source(value);Add("fiveaxis.pose",{{"policy",SurfacePose()}});auto disabled=KerfPolicy();disabled["widthMm"]=0.;Add("fiveaxis.kerf",{{"policy",disabled}});Add("fiveaxis.trace",{{"gapLengthMm",0.},{"overcutLengthMm",0.}});Add("fiveaxis.leads",{{"leadIn",Lead("None")},{"leadOut",Lead("None")}});const auto path=Paths().back();const auto effective=ReadFiveAxisIntentMetadata(path.Value).Process;EXPECT_DOUBLE_EQ(0,effective.at("kerf").To<Map>().at("widthMm").To<double>());EXPECT_DOUBLE_EQ(0,effective.at("gapLengthMm").To<double>());EXPECT_DOUBLE_EQ(0,effective.at("overcutLengthMm").To<double>());EXPECT_EQ("None",effective.at("leadIn").To<Map>().at("kind").To<std::string>());EXPECT_EQ("None",effective.at("leadOut").To<Map>().at("kind").To<std::string>());ASSERT_EQ(1u,path.Value.Geometry.Segments.size());EXPECT_TRUE(std::holds_alternative<CurveSegment3>(path.Value.Geometry.Segments[0].Curve));
        if(!closed)Plan=Document->CreatePlan("closed zero category policies");
    }
}

TEST_F(FiveAxisIntentOperations, SurfaceMeasurementRejectsAnAxisMatchingOnlyTheMiddleOfAnArc)
{
    auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakeCylinder(10,30).Shape(),"cylinder","intent-fixed-surface",1e-7);
    auto value=CircleSource(std::move(model),true);auto& source=std::get<CurveSegment3>(value.Geometry.Segments[0].Curve);source.Range={std::numbers::pi-.2,std::numbers::pi+.2,false};value.Geometry.Closed=false;
    const auto binding=Snapshot->SourceBindings().at("source/circle");BoundaryLeadInput query;query.FaceId=binding.FaceId;query.WireId=binding.WireId;query.EdgeId=binding.EdgeId;query.CoedgeIndex=binding.CoedgeIndex;query.NativeParameter=std::numbers::pi;
    const auto frame=Boundary->Frame(Snapshot->Model(),query);const Map pose{{"mode",std::string("FixedDirection")},{"frame",std::string("Workpiece")},{"direction",Array{frame.Normal.X,frame.Normal.Y,frame.Normal.Z}}};
    Source(std::move(value));Add("fiveaxis.pose",{{"policy",pose}});const auto path=Paths()[0];
    try{(void)Document->Preview("fiveaxis.kerf",{{"policy",KerfPolicy()}},{path.Port},{{"assetVersion",std::string("geometry-1")}});FAIL()<<"Surface measurement accepted a non-normal tool axis away from the middle";}
    catch(const std::invalid_argument& error){EXPECT_EQ("CAM_GENERATE_SURFACE_KERF_REQUIRES_NORMAL_AXIS",std::string(error.what()));}
}

TEST_F(FiveAxisIntentOperations, DisabledProcessCategoriesDoNotEvaluateUnsupportedDraftChoices)
{
    Source(CircleSource(PlateWithHole()));const auto original=Paths()[0].PathId;auto kerf=KerfPolicy();kerf["widthMm"]=0.;kerf["owner"]=std::string("Executor");kerf["measurementSection"]=std::string("ThicknessSection");Add("fiveaxis.kerf",{{"policy",kerf}});
    auto none=Lead("None");none["placement"]=std::string("OutsideStock");none["angleRad"]=std::numbers::pi;Add("fiveaxis.leads",{{"leadIn",none},{"leadOut",none}});
    const auto result=Paths()[0];EXPECT_EQ(original,result.PathId);ASSERT_EQ(1u,result.Value.Geometry.Segments.size());EXPECT_TRUE(std::holds_alternative<CurveSegment3>(result.Value.Geometry.Segments[0].Curve));EXPECT_EQ("NominalBoundary",result.Value.Geometry.Meaning);EXPECT_NEAR(20*std::numbers::pi,Length(result.Value),1e-8);
}

TEST_F(FiveAxisIntentOperations, RejectsUnsupportedActivePoliciesAndAuthoredPoseFields)
{
    auto kerf=KerfPolicy();kerf["owner"]=std::string("Executor");EXPECT_THROW(Registry->Find("fiveaxis.kerf")->ValidateParameters({{"policy",kerf}}),std::invalid_argument);
    kerf=KerfPolicy();kerf["measurementSection"]=std::string("ThicknessSection");EXPECT_THROW(Registry->Find("fiveaxis.kerf")->ValidateParameters({{"policy",kerf}}),std::invalid_argument);
    auto outside=Lead("Line",1);outside["placement"]=std::string("OutsideStock");EXPECT_THROW(Registry->Find("fiveaxis.leads")->ValidateParameters({{"leadIn",outside},{"leadOut",Lead("None")}}),std::invalid_argument);
    auto manual=SurfacePose();manual["keypoints"]=Array{Map{{"id",std::string("authored")}}};EXPECT_THROW(Registry->Find("fiveaxis.pose")->ValidateParameters({{"policy",manual}}),std::invalid_argument);
}
