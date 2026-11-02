#include <gtest/gtest.h>
#include <FiveAxisCAM/FiveAxisIntentPoseField.h>
#include <FiveAxisCAM/FiveAxisIntentProjection.h>
#include <OpenCascadeResourceImport/CadReferenceService.h>
#include <OpenCascadeResourceImport/CurveEditingService.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
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
#include <gp_Elips.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <set>
#include <thread>

namespace
{
using namespace iCAX::FiveAxisCAM;using namespace iCAX::CAM::Intent;using namespace iCAX::GeometryData;using namespace iCAX::GeometryServices;
using Map=IntentMap;using Array=IntentArray;using Variant=iCAX::Data::Variant;using Text=std::string;
Text String(const Map& map,const char* key){const auto found=map.find(key);return found==map.end()?Text{}:found->second.To<Text>();}
double RegistrationNumber(const Variant& value){return value.To<double>();}
std::array<double,3> RegistrationPoint(const Variant& value){const auto data=value.To<Array>();return {data[0].To<double>(),data[1].To<double>(),data[2].To<double>()};}
bool FixedToolAxis(const Map& policy){return String(policy,"mode")=="FixedDirection"||String(policy,"initialization")=="FixedDirection";}
template<class T> const T& Record(const std::vector<T>& values,std::uint64_t id){for(const auto& value:values)if(value.Id==id)return value;throw std::runtime_error("Missing native test record");}
struct CurveHolder{CompositeCurve3 Curve3;};
struct CutDefinition{Map Pose,Feature;std::shared_ptr<CurveHolder> Curve;std::vector<SFiveAxisIntentCadSpan> Bindings;};
iCAX::Geometry::SCurveLocation SplitLocation(const CutDefinition& cut,const Map& anchor)
{const auto id=String(anchor,"sourceSpanId");if(id!="source/0")throw std::invalid_argument("Test source anchor unknown");return {0,RegistrationNumber(anchor.at("nativeParameter"))};}
iCAX::Geometry::SCurveLocation CutEndpoint(const CutDefinition& cut,bool first)
{const auto i=first?0:cut.Curve->Curve3.Segments.size()-1;const auto r=cut.Curve->Curve3.Segments[i].Range;return {i,first?(r.Reversed?r.Last:r.First):(r.Reversed?r.First:r.Last)};}
BoundaryLeadInput CutBoundaryQuery(const CutDefinition& cut,iCAX::Geometry::SCurveLocation at)
{const auto& binding=cut.Bindings.at(at.SegmentIndex);BoundaryLeadInput input;input.FaceId=binding.FaceId;input.WireId=binding.WireId;input.EdgeId=binding.EdgeId;input.CoedgeIndex=binding.CoedgeIndex;input.NativeParameter=at.NativeParameter;return input;}
BoundaryFrame CutBoundaryFrame(const CutDefinition& cut,const iCAX::Geometry::SBoundaryPath& path,iCAX::Geometry::SCurveLocation at,const BRepModel& model,const IBoundaryLeadService& boundary)
{return boundary.Frame(model,CutBoundaryQuery(cut,{*path.SourceSegments.at(at.SegmentIndex),at.NativeParameter}));}
}
// Compile the unchanged legacy field with a read-only native-geometry shim.
// This is a behavioral oracle, independent of the replacement field code.
#include "PoseFieldReference.inc"

namespace
{
class FiveAxisIntentPoseField:public ::testing::Test
{
protected:
    iCAX::Services::CServiceProvider Provider;std::shared_ptr<const iCAX::Geometry::ICadReferenceService> Cad;
    std::shared_ptr<const iCAX::Geometry::ICurveEditingService> Editing;std::shared_ptr<const IBoundaryLeadService> Boundary;std::shared_ptr<const ICurveOffset3DService> Offset;
    std::shared_ptr<const CFiveAxisIntentOperationContext> Context;CutDefinition Cut;SToolpathValue Value;SPortRef SourcePort{iCAX::Data::GenerateNewUUID(),iCAX::Data::GenerateNewUUID()};
    std::shared_ptr<iCAX::Database::IRepository> Db;std::shared_ptr<CToolpathOperationRegistry> Registry;std::unique_ptr<CEditableToolpathDocument> Document;IntentId Plan;
    void SetUp()override
    {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);const auto directory=std::filesystem::path(executable).parent_path();std::vector<Text> paths;
        for(const auto* name:{L"OpenCascadeResourceImport.dll",L"OpenCascadeGeometryServices.dll"}){const auto path=directory/name;if(!LoadLibraryW(path.c_str()))throw std::runtime_error("Native pose backend cannot load");paths.push_back(path.string());}
        iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(Provider,paths);Cad=Provider.Resolve<iCAX::Geometry::ICadReferenceService>();Editing=Provider.Resolve<iCAX::Geometry::ICurveEditingService>();Boundary=Provider.Resolve<IBoundaryLeadService>();Offset=Provider.Resolve<ICurveOffset3DService>();
        BRepBuilderAPI_MakePolygon polygon;for(auto point:{gp_Pnt(-30,-30,0),gp_Pnt(30,-30,0),gp_Pnt(30,30,0),gp_Pnt(-30,30,0)})polygon.Add(point);polygon.Close();
        auto hole=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2({},gp::DZ()),12,5)).Edge()).Wire();hole.Reverse();BRepBuilderAPI_MakeFace face(polygon.Wire());face.Add(hole);auto model=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(face.Face(),"pose ellipse","pose-field-test",1e-7);
        CurveSegment3 curve;SFiveAxisIntentCadSpan binding;bool found=false;
        for(const auto& face:model.Faces)for(auto wireId:face.WireIds){const auto& wire=Record(model.Wires,wireId);for(std::size_t i=0;i<wire.Coedges.size();++i){const auto& coedge=wire.Coedges[i];const auto& edge=Record(model.Edges,coedge.EdgeId);if(!edge.Curve3Id)continue;const auto& shape=Record(model.Curves3,edge.Curve3Id).Geometry;if(!std::holds_alternative<Ellipse3>(shape))continue;curve={shape,edge.Range};curve.Range.Reversed=coedge.Orientation==ETopologyOrientation::Reversed;binding={face.Id,wire.Id,edge.Id,i};found=true;}}
        if(!found)throw std::runtime_error("Native ellipse not preserved");
        Context=std::make_shared<CFiveAxisIntentOperationContext>(std::move(model),CFiveAxisIntentOperationContext::Bindings{{"source/0",binding}},"ellipse-1","native-editing-1","Workpiece",1,Boundary,Offset,Editing);
        Cut.Curve=std::make_shared<CurveHolder>();Cut.Curve->Curve3={{curve},true};Cut.Bindings={binding};Cut.Feature={{"start",LegacyAnchor(.143)}};
        Cut.Pose={{"mode",Text("Continuous")},{"initialization",Text("SurfaceNormal")},{"transitionMetric",Text("ArcLength")},{"leadAngleRad",0.},{"sideAngleRad",0.},{"keypoints",Array{}}};
        Value=MakeFiveAxisIntentValue(Cut.Curve->Curve3,{"source/0"});SFiveAxisIntentMetadata metadata;metadata.SetupID=iCAX::Data::GenerateNewUUID();metadata.PlacementID=iCAX::Data::GenerateNewUUID();metadata.SourceKey="pose/ellipse";metadata.Feature={{"boundaryRole",Text("Opening")},{"retainedSide",Text("Exterior")}};metadata.Pose=Cut.Pose;metadata.Pose.erase("keypoints");
        metadata.SourceSpans={{"source/0",Map{{"sourceSpanId",Text("source/0")},{"faceId",std::to_string(binding.FaceId)},{"wireId",std::to_string(binding.WireId)},{"edgeId",std::to_string(binding.EdgeId)},{"coedgeIndex",std::to_string(binding.CoedgeIndex)},{"first",curve.Range.First},{"last",curve.Range.Last},{"reversed",curve.Range.Reversed}}}};Value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);
    }
    static Map LegacyAnchor(double u){return {{"sourceSpanId",Text("source/0")},{"nativeParameter",u}};}
    Map Control(const Text& id,double u,Array direction,const Text& metric="ArcLength")const{return {{"id",id},{"kind",Text("Control")},{"set",Text("active")},{"boundaryPolicy",Text("Duplicate")},{"anchor",ToData(SPathAnchor{SourcePort,"source/0",u})},{"direction",direction},{"transition",metric}};}
    void Compile()
    {
        auto policy=Cut.Pose;policy.erase("keypoints");Value.Channels["pose"]=Map{{"schema",Text("fiveaxis.pose/1")},{"attachment",Text("Path")},{"policy",policy}};
        auto channel=BuildFiveAxisPoseControls(Cut.Pose,Value.Geometry,ReadFiveAxisIntentMetadata(Value).SourceSpans,SourcePort);auto entries=channel.at("entries").To<Array>();entries.emplace_back(Map{{"id",Text("reference/start")},{"kind",Text("Reference")},{"set",Text("active")},{"boundaryPolicy",Text("Duplicate")},{"anchor",ToData(SPathAnchor{SourcePort,"source/0",.143})}});channel["entries"]=entries;Value.Channels["poseControls"]=channel;Value.Channels["poseField"]=BuildFiveAxisIntentPoseField(*Context,Value,SourcePort);
    }
    void Compare()
    {
        iCAX::Geometry::SBoundaryPath path;path.Curve=Cut.Curve->Curve3;path.SourceSegments={0};ToolAxisField old(Cut,path,Context->Model(),1,*Editing,*Boundary);CFiveAxisIntentPoseField current(*Context,Value);
        for(int i=0;i<=240;++i){const auto r=Cut.Curve->Curve3.Segments[0].Range;const auto u=r.First+(r.Last-r.First)*i/240.;const auto frame=Boundary->Frame(Context->Model(),CutBoundaryQuery(Cut,{0,u}));const auto expected=old.AtSource({0,u},frame);const auto actual=current.At(0,u);EXPECT_NEAR(expected[0],actual.X,1e-10)<<u;EXPECT_NEAR(expected[1],actual.Y,1e-10)<<u;EXPECT_NEAR(expected[2],actual.Z,1e-10)<<u;}
    }
    Map Command(const Text& kind)const{return {{"kind",kind},{"commandId",iCAX::Data::GenerateNewUUID()},{"planId",Plan}};}
    void Generate(){auto task=Document->GenerateAsync(Plan,2);for(int i=0;i<30000&&!task->IsReady();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));ASSERT_TRUE(task->IsReady());ASSERT_TRUE(Document->Publish(task));const auto result=Document->Resolve(Plan);for(const auto& diagnostic:result.Diagnostics)ADD_FAILURE()<<diagnostic.Message;ASSERT_EQ(EOutputCompleteness::Complete,result.Status);}
    void OpenDocument(std::function<void()> prepare = {})
    {
        auto meta=iCAX::Database::CreateMetaRegistry();iCAX::Database::CMetaRegistrationCatalog::ReplayAll(*meta);Db=iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID(),meta);Registry=std::make_shared<CToolpathOperationRegistry>();RegisterStandardToolpathOperations(*Registry);RegisterFiveAxisIntentOperations(*Registry,[this](const Map&){return Context;});Document=std::make_unique<CEditableToolpathDocument>(Db,IntentId{},Registry,CreateFiveAxisIntentGeometryServices(Cad,Offset));Plan=Document->CreatePlan("pose interval persistence");auto source=Command("CreateSource");auto plain=Value;plain.Channels.erase("poseControls");plain.Channels.erase("poseField");source["value"]=ToData(plain);const auto receipt=Document->Execute(source);Generate();const auto path=Document->Resolve(Plan).Paths.at(0);SourcePort=path.Port;if(prepare)prepare();else Compile();auto update=Command("UpdateSource");update["operationId"]=path.Port.OperationId;update["value"]=ToData(Value);const auto definition=Document->ReadOperation(path.Port.OperationId);update["sourceBinding"]=definition.at("sourceBinding");update["expectedOperationFingerprint"]=definition.at("definitionFingerprint");Document->Execute(update);Generate();
    }
    IntentId Add(const SResolvedPath& path,const Text& type,Map parameters)
    {auto command=Command("AddOperation");command["typeId"]=type;command["parameters"]=parameters;command["context"]=Map{};command["inputs"]=Array{Map{{"slot",Text("input")},{"source",ToData(path.Port)},{"replace",true},{"ranges",Array{}}}};command["expectedInputs"]=Map{{ToolpathPortKey(path.Port),path.Fingerprint}};const auto receipt=Document->Execute(command);Generate();return receipt.at("operationId").To<IntentId>();}
};
}

TEST_F(FiveAxisIntentPoseField, NativeEllipseMatchesLegacyForBothMetricsAndInfluenceInterpolation)
{
    for(const auto metric:{"ArcLength","DirectionChange"})for(const auto interpolation:{"Linear","Smooth"}){
        Cut.Pose["transitionMetric"]=Text(metric);Map influence{{"referenceDirection",Array{0.,0.,1.}},{"interpolation",Text(interpolation)},{"targets",Array{Map{{"anchor",LegacyAnchor(1.13)},{"weight",.4},{"referenceDirection",Array{0.,0.,1.}}},Map{{"anchor",LegacyAnchor(2.1)},{"weight",.75},{"referenceDirection",Array{0.,.6,.8}}}}}};
        Cut.Pose["keypoints"]=Array{Map{{"id",Text("owner")},{"anchor",LegacyAnchor(.4)},{"direction",Array{.6,0.,.8}},{"transition",Text(metric)},{"influence",influence}},Map{{"id",Text("neighbor")},{"anchor",LegacyAnchor(4.2)},{"direction",Array{0.,-.6,.8}},{"transition",Text(metric)}}};Compile();Compare();
    }
}
TEST_F(FiveAxisIntentPoseField, ClosedSeamAndReferenceStartMatchLegacy)
{
    Cut.Pose["keypoints"]=Array{Map{{"id",Text("seam")},{"anchor",LegacyAnchor(0)},{"direction",Array{.6,0.,.8}},{"transition",Text("DirectionChange")}}};Compile();Compare();
}
TEST_F(FiveAxisIntentPoseField, FullNativeFieldSamplingRetainsExactDirectionsAndMeasuresReadCost)
{
    Cut.Pose["transitionMetric"]=Text("DirectionChange");
    Cut.Pose["keypoints"]=Array{
        Map{{"id",Text("first")},{"anchor",LegacyAnchor(.4)},{"direction",Array{.6,0.,.8}},{"transition",Text("DirectionChange")}},
        Map{{"id",Text("last")},{"anchor",LegacyAnchor(4.2)},{"direction",Array{0.,-.6,.8}},{"transition",Text("ArcLength")}}};
    Compile();
    iCAX::Geometry::SBoundaryPath path;path.Curve=Cut.Curve->Curve3;path.SourceSegments={0};
    ToolAxisField reference(Cut,path,Context->Model(),1,*Editing,*Boundary);
    const auto range=Cut.Curve->Curve3.Segments.front().Range;
    struct Sample{double Parameter;std::array<double,3> Direction;};std::vector<Sample> samples;
    for(int i=0;i<=1024;++i){const auto u=range.First+(range.Last-range.First)*i/1024.;const auto frame=Boundary->Frame(Context->Model(),CutBoundaryQuery(Cut,{0,u}));samples.push_back({u,reference.AtSource({0,u},frame)});}
    CFiveAxisIntentPoseField field(*Context,Value);
    // Every read must use the evaluator's owned snapshot, including its index.
    Value.Channels.clear();Value.Geometry.Segments.clear();
    const auto started=std::chrono::steady_clock::now();
    for(const auto& sample:samples){const auto actual=field.At(0,sample.Parameter),base=field.BaseAt(0,sample.Parameter),normal=field.NormalAt(0,sample.Parameter);
        EXPECT_NEAR(sample.Direction[0],actual.X,1e-10);EXPECT_NEAR(sample.Direction[1],actual.Y,1e-10);EXPECT_NEAR(sample.Direction[2],actual.Z,1e-10);
        EXPECT_NEAR(0,base.X,1e-12);EXPECT_NEAR(0,base.Y,1e-12);EXPECT_NEAR(1,base.Z,1e-12);EXPECT_NEAR(0,normal.X,1e-12);EXPECT_NEAR(0,normal.Y,1e-12);EXPECT_NEAR(1,normal.Z,1e-12);}
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
    RecordProperty("sampleCount",std::to_string(samples.size()));RecordProperty("exactFieldReadCount",std::to_string(samples.size()*3));RecordProperty("samplingMilliseconds",std::to_string(elapsed));
    std::cout<<"Exact pose field: "<<samples.size()*3<<" reads across the complete native ellipse domain in "<<elapsed<<" ms"<<std::endl;
}
TEST_F(FiveAxisIntentPoseField, SplitKeepsOriginalFieldAndLocalDeltaDoesNotTouchSibling)
{
    Cut.Pose["keypoints"]=Array{Map{{"id",Text("original")},{"anchor",LegacyAnchor(1.25)},{"direction",Array{.6,0.,.8}},{"transition",Text("ArcLength")}}};Compile();OpenDocument();auto basis=Cut.Pose;basis.erase("keypoints");Add(Document->Resolve(Plan).Paths[0],"fiveaxis.pose",{{"policy",basis}});Add(Document->Resolve(Plan).Paths[0],"fiveaxis.kerf",{{"policy",Map{{"owner",Text("Cam")},{"widthMm",0.},{"miterLimit",4.},{"measurementSection",Text("Surface")},{"cornerMode",Text("Miter")}}}});Add(Document->Resolve(Plan).Paths[0],"fiveaxis.trace",{{"gapLengthMm",0.},{"overcutLengthMm",0.}});const Map none{{"kind",Text("None")},{"placement",Text("ScrapSide")},{"lengthMm",0.},{"angleRad",0.}};Add(Document->Resolve(Plan).Paths[0],"fiveaxis.leads",{{"leadIn",none},{"leadOut",none}});const auto original=Document->Resolve(Plan).Paths.at(0);const auto u=1.3;Add(original,"fiveaxis.split",{{"retainFirstIdentity",true},{"cuts",Array{Map{{"id",Text("middle")},{"anchor",ToData(SPathAnchor{original.Port,original.Value.Geometry.Segments[0].Id,u})}}}}});auto children=Document->Resolve(Plan).Paths;ASSERT_EQ(2u,children.size());
    CFiveAxisIntentPoseField before(*Context,original.Value);for(const auto& child:children){CFiveAxisIntentPoseField field(*Context,child.Value);for(std::size_t i=0;i<child.Value.Geometry.Segments.size();++i){const auto r=std::get<CurveSegment3>(child.Value.Geometry.Segments[i].Curve).Range;for(int j=0;j<=20;++j){const auto parameter=r.First+(r.Last-r.First)*j/20.;const auto expected=before.At(0,parameter),actual=field.At(i,parameter);EXPECT_NEAR(expected.X,actual.X,1e-10);EXPECT_NEAR(expected.Y,actual.Y,1e-10);EXPECT_NEAR(expected.Z,actual.Z,1e-10);}}}
    const auto selected=children[0],sibling=children[1];const auto& segment=selected.Value.Geometry.Segments.front();const auto range=std::get<CurveSegment3>(segment.Curve).Range;const auto at=(range.First+range.Last)/2;auto point=Control("local",at,Array{0.,.6,.8});point["anchor"]=ToData(SPathAnchor{selected.Port,segment.Id,at});Add(selected,"fiveaxis.pose.delta",{{"controls",Array{point}}});const auto edited=Document->Resolve(Plan).Paths;const auto unchanged=std::find_if(edited.begin(),edited.end(),[&](const auto& path){return path.PathId==sibling.PathId;});ASSERT_NE(edited.end(),unchanged);EXPECT_EQ(ToData(sibling.Value),ToData(unchanged->Value));
    const auto changed=std::find_if(edited.begin(),edited.end(),[&](const auto& path){return path.PathId==selected.PathId;});ASSERT_NE(edited.end(),changed);const auto actual=CFiveAxisIntentPoseField(*Context,changed->Value).At(0,at);EXPECT_NEAR(0,actual.X,1e-10);EXPECT_NEAR(.6,actual.Y,1e-10);EXPECT_NEAR(.8,actual.Z,1e-10);
    const auto saved=iCAX::Data::VariantSerializer::Deserialize(iCAX::Data::VariantSerializer::Serialize(Document->ExportDocument())).To<Map>();auto meta=iCAX::Database::CreateMetaRegistry();iCAX::Database::CMetaRegistrationCatalog::ReplayAll(*meta);auto reopenedDb=iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID(),meta);auto geometry=CreateFiveAxisIntentGeometryServices(Cad,Offset);const auto id=CEditableToolpathDocument::ImportDocument(reopenedDb,saved,geometry);CEditableToolpathDocument reopened(reopenedDb,id,Registry,geometry);const auto restored=reopened.Resolve(Plan);ASSERT_EQ(EOutputCompleteness::Complete,restored.Status);ASSERT_EQ(edited.size(),restored.Paths.size());for(const auto& path:edited){const auto found=std::find_if(restored.Paths.begin(),restored.Paths.end(),[&](const auto& candidate){return candidate.PathId==path.PathId;});ASSERT_NE(restored.Paths.end(),found);EXPECT_EQ(ToData(path.Value),ToData(found->Value));}
}
TEST_F(FiveAxisIntentPoseField, ManualControlsRequireNativeMeasureCapability)
{
    Cut.Pose["keypoints"]=Array{Map{{"id",Text("point")},{"anchor",LegacyAnchor(1.25)},{"direction",Array{.6,0.,.8}},{"transition",Text("ArcLength")}}};Compile();auto missing=CFiveAxisIntentOperationContext(Context->FrozenModel(),Context->SourceBindings(),"ellipse-1","native-editing-1","Workpiece",1,Boundary,Offset);Value.Channels.erase("poseField");EXPECT_THROW(BuildFiveAxisIntentPoseField(missing,Value,SourcePort),std::invalid_argument);
}
TEST_F(FiveAxisIntentPoseField, RemovingInheritedControlRecompilesOriginalNeighborDomain)
{
    Cut.Pose["keypoints"]=Array{Map{{"id",Text("remove")},{"anchor",LegacyAnchor(1.25)},{"direction",Array{.6,0.,.8}},{"transition",Text("ArcLength")}},Map{{"id",Text("keep")},{"anchor",LegacyAnchor(2.9)},{"direction",Array{0.,.6,.8}},{"transition",Text("DirectionChange")}}};Compile();const auto removed=RemoveFiveAxisIntentPoseControls(*Context,Value,SourcePort,{"remove"});auto candidate=Value;candidate.Channels["poseField"]=removed;auto points=Cut.Pose.at("keypoints").To<Array>();points.erase(points.begin());Cut.Pose["keypoints"]=points;Compile();CFiveAxisIntentPoseField expected(*Context,Value),actual(*Context,candidate);for(int i=0;i<=200;++i){const auto u=2*std::numbers::pi*i/200.;const auto a=expected.At(0,u),b=actual.At(0,u);EXPECT_NEAR(a.X,b.X,1e-10);EXPECT_NEAR(a.Y,b.Y,1e-10);EXPECT_NEAR(a.Z,b.Z,1e-10);}
}
TEST_F(FiveAxisIntentPoseField, ReverseNegatesTheEntireAuthoredField)
{
    Cut.Pose["keypoints"]=Array{Map{{"id",Text("reverse")},{"anchor",LegacyAnchor(1.25)},{"direction",Array{.6,0.,.8}},{"transition",Text("ArcLength")}}};Compile();auto reversed=Value;reversed.Channels["poseField"]=ReverseFiveAxisIntentPoseField(*Context,Value,SourcePort);auto pose=reversed.Channels.at("pose").To<Map>();auto policy=pose.at("policy").To<Map>();policy["axisReversed"]=true;pose["policy"]=policy;reversed.Channels["pose"]=pose;CFiveAxisIntentPoseField before(*Context,Value),after(*Context,reversed);for(int i=0;i<=160;++i){const auto u=2*std::numbers::pi*i/160.;const auto a=before.At(0,u),b=after.At(0,u);EXPECT_NEAR(-a.X,b.X,1e-10);EXPECT_NEAR(-a.Y,b.Y,1e-10);EXPECT_NEAR(-a.Z,b.Z,1e-10);}
}
TEST_F(FiveAxisIntentPoseField, DefaultMetricEditPreservesAuthoredConstraintsAndMatchesLegacy)
{
    Cut.Pose["keypoints"]=Array{Map{{"id",Text("manual")},{"anchor",LegacyAnchor(1.25)},{"direction",Array{.6,0.,.8}},{"transition",Text("ArcLength")}}};Compile();auto updated=Value;auto policy=updated.Channels.at("pose").To<Map>().at("policy").To<Map>();policy["transitionMetric"]=Text("DirectionChange");updated.Channels["poseField"]=UpdateFiveAxisIntentPoseFieldPolicy(*Context,Value,SourcePort,policy);auto pose=updated.Channels.at("pose").To<Map>();pose["policy"]=policy;updated.Channels["pose"]=pose;Cut.Pose["transitionMetric"]=Text("DirectionChange");Compile();CFiveAxisIntentPoseField expected(*Context,Value),actual(*Context,updated);for(int i=0;i<=160;++i){const auto u=2*std::numbers::pi*i/160.;const auto a=expected.At(0,u),b=actual.At(0,u);EXPECT_NEAR(a.X,b.X,1e-10);EXPECT_NEAR(a.Y,b.Y,1e-10);EXPECT_NEAR(a.Z,b.Z,1e-10);}
}
TEST_F(FiveAxisIntentPoseField, RepeatedFrozenControlDoesNotReviveItsRemovedPriorLayer)
{
    Compile();const auto a=Control("repeat-a",1.25,Array{.6,0.,.8});auto once=Value;once.Channels["poseField"]=BuildFiveAxisIntentPoseDelta(*Context,Value,SourcePort,Array{a},"active");auto twice=once;twice.Channels["poseField"]=BuildFiveAxisIntentPoseDelta(*Context,once,SourcePort,Array{a},"active");const auto b=Control("other-b",4.2,Array{0.,.6,.8});auto after=twice;after.Channels["poseField"]=BuildFiveAxisIntentPoseDelta(*Context,twice,SourcePort,Array{b},"active");const auto expected=CFiveAxisIntentPoseField(*Context,twice).At(0,1.25),actual=CFiveAxisIntentPoseField(*Context,after).At(0,1.25);EXPECT_NEAR(expected.X,actual.X,1e-10);EXPECT_NEAR(expected.Y,actual.Y,1e-10);EXPECT_NEAR(expected.Z,actual.Z,1e-10);
}
TEST_F(FiveAxisIntentPoseField, StandardHoleSplitRestoresSavedContourFieldWithoutItsOffFragmentOwner)
{
    auto metadata=ReadFiveAxisIntentMetadata(Value);metadata.Feature["standardHole"]=Map{{"diameterMm",10.}};Value.Channels["fiveaxis"]=ToFiveAxisIntentChannel(metadata);Cut.Pose["keypoints"]=Array{Map{{"id",Text("owner")},{"anchor",LegacyAnchor(1.25)},{"direction",Array{.6,0.,.8}},{"transition",Text("ArcLength")}}};Compile();OpenDocument();const auto contour=Document->Resolve(Plan).Paths[0];const Map fixed{{"mode",Text("FixedDirection")},{"frame",Text("Workpiece")},{"direction",Array{0.,0.,1.}}};Add(contour,"fiveaxis.cutType",{{"cutType",Text("StandardHole")},{"policy",fixed}});const auto standard=Document->Resolve(Plan).Paths[0];Add(standard,"fiveaxis.split",{{"retainFirstIdentity",true},{"cuts",Array{Map{{"id",Text("separate")},{"anchor",ToData(SPathAnchor{standard.Port,"source/0",1.3})}}}}});const auto children=Document->Resolve(Plan).Paths;ASSERT_EQ(2u,children.size());CFiveAxisIntentPoseField reference(*Context,contour.Value);for(const auto& child:children){EXPECT_EQ("Continuous",String(ReadFiveAxisIntentMetadata(child.Value).Pose,"mode"));EXPECT_FALSE(ReadFiveAxisIntentMetadata(child.Value).Feature.contains("standardHole"));CFiveAxisIntentPoseField field(*Context,child.Value);for(std::size_t i=0;i<child.Value.Geometry.Segments.size();++i){const auto r=std::get<CurveSegment3>(child.Value.Geometry.Segments[i].Curve).Range;for(int j=0;j<=20;++j){const auto u=r.First+(r.Last-r.First)*j/20.;const auto a=reference.At(0,u),b=field.At(i,u);EXPECT_NEAR(a.X,b.X,1e-10);EXPECT_NEAR(a.Y,b.Y,1e-10);EXPECT_NEAR(a.Z,b.Z,1e-10);}}}
}
TEST_F(FiveAxisIntentPoseField, InitialFixedSourceCompilesItsHiddenManualContourBeforeAnySplit)
{
    Cut.Pose["keypoints"]=Array{Map{{"id",Text("hidden-owner")},{"anchor",LegacyAnchor(1.25)},{"direction",Array{.6,0.,.8}},{"transition",Text("ArcLength")}}};const Map fixed{{"mode",Text("FixedDirection")},{"frame",Text("Workpiece")},{"direction",Array{0.,0.,1.}}};auto input=Value.Channels.at("fiveaxis").To<Map>();auto feature=input.at("feature").To<Map>();feature["standardHole"]=Map{{"diameterMm",10.}};feature["contourPoseData"]=Cut.Pose;feature["start"]=LegacyAnchor(.143);input["feature"]=feature;input["pose"]=fixed;OpenDocument([&]{Value.Channels=BuildFiveAxisIntentChannels(input,Value.Geometry,{},SourcePort);});Add(Document->Resolve(Plan).Paths[0],"fiveaxis.pose",{{"policy",fixed}});const auto standard=Document->Resolve(Plan).Paths[0];ASSERT_TRUE(standard.Value.Channels.contains("contourPoseField"));Add(standard,"fiveaxis.split",{{"retainFirstIdentity",true},{"cuts",Array{Map{{"id",Text("cut")},{"anchor",ToData(SPathAnchor{standard.Port,standard.Value.Geometry.Segments[0].Id,1.3})}}}}});const auto children=Document->Resolve(Plan).Paths;ASSERT_EQ(2u,children.size());iCAX::Geometry::SBoundaryPath path;path.Curve=Cut.Curve->Curve3;path.SourceSegments={0};ToolAxisField legacy(Cut,path,Context->Model(),1,*Editing,*Boundary);for(const auto& child:children){CFiveAxisIntentPoseField field(*Context,child.Value);for(std::size_t i=0;i<child.Value.Geometry.Segments.size();++i){const auto r=std::get<CurveSegment3>(child.Value.Geometry.Segments[i].Curve).Range;for(int j=0;j<=16;++j){const auto u=r.First+(r.Last-r.First)*j/16.;const auto frame=Boundary->Frame(Context->Model(),CutBoundaryQuery(Cut,{0,u}));const auto expected=legacy.AtSource({0,u},frame);const auto actual=field.At(i,u);EXPECT_NEAR(expected[0],actual.X,1e-10);EXPECT_NEAR(expected[1],actual.Y,1e-10);EXPECT_NEAR(expected[2],actual.Z,1e-10);}}}
}
