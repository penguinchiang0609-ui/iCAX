#include <gtest/gtest.h>
#include "FiveAxisCAM/FiveAxisIntentProjection.h"


#include "IntentToolpath/EditableToolpathInternal.h"
#include "Database/IMetaRegistry.h"
#include "Database/IRepository.h"
#include <chrono>
#include <thread>

namespace
{
using namespace iCAX::FiveAxisCAM;
using namespace iCAX::CAM::Intent;
using namespace iCAX::CAM::Intent::Editable;
using namespace iCAX::GeometryData;
using namespace iCAX::Database;
using namespace iCAX::Data;
struct Fixture final
{
    IntentId Setup=GenerateNewUUID(),Placement=GenerateNewUUID(),Plan,Source;
    std::shared_ptr<IRepository> Database;
    std::shared_ptr<CToolpathOperationRegistry> Registry=std::make_shared<CToolpathOperationRegistry>();
    std::unique_ptr<CEditableToolpathDocument> Document;
    SToolpathValue Value;
    Map Metadata;
    Array SourceSpans;
    Fixture()
    {
        auto meta=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*meta);Database=GenerateRepository(GenerateNewUUID(),meta);
        RegisterStandardToolpathOperations(*Registry);Document=std::make_unique<CEditableToolpathDocument>(Database,IntentId{},Registry);Plan=Document->CreatePlan("projection");
        Value.Geometry=MakeEditableGeometry(CompositeCurve3{{{Segment3{{0,0,0},{10,0,0}},{0,1,false}}},false},"cad/edge-7");
        SourceSpans={Map{{"sourceSpanId",std::string("span-7")},{"edgeId",std::string("7")},{"curveId",std::string("8")},{"faceId",std::string("9")},
            {"first",0.},{"last",1.},{"reversed",false}}};
        Metadata={{"setupId",Setup},{"placementId",Placement},{"sourceKey",std::string("boundary-7")},
            {"beforeCommands",std::string("M10")},{"afterCommands",std::string("M11")},
            {"sourceGeometryRef",Map{{"url",std::string("cad.source")},{"version",1ull}}},
            {"sourceData",Map{{"kind",std::string("BoundaryRecognition")}}},
            {"feature",Map{{"machiningKind",std::string("Contour")},{"curveData",Map{{"must",std::string("be removed")}}}}},
            {"pose",Map{{"mode",std::string("Continuous")},{"transitionMetric",std::string("ArcLength")},{"keypoints",Array{}}}},
            {"process",Map{{"technology",std::string("Steel")}}},{"inputRevision",3ull}};
        Value.Channels["fiveaxis"]=BuildFiveAxisIntentChannel(Metadata,Value.Geometry,SourceSpans);
        auto command=Command("CreateSource");command["value"]=ToData(Value);command["name"]=std::string("core label");command["userNumber"]=std::string("65");
        Source=iCAX::CAM::Intent::Editable::ID(At(Document->Execute(command),"operationId"));
    }
    Map Command(std::string kind)const{return {{"kind",std::move(kind)},{"commandId",GenerateNewUUID()},{"planId",Plan}};}
    void Generate()
    {
        auto task=Document->GenerateAsync(Plan);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!task->IsReady()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ASSERT_TRUE(task->IsReady());ASSERT_TRUE(Document->Publish(task));
    }
    void Add(const std::string& type,const SResolvedPath& path,const Map& parameters)
    {
        auto command=Command("AddOperation");command["typeId"]=type;command["parameters"]=parameters;
        command["inputs"]=Array{Map{{"slot",std::string("input/0")},{"source",ToData(path.Port)},{"replace",true},{"ranges",Array{}}}};
        command["expectedInputs"]=Map{{ToolpathPortKey(path.Port),path.Fingerprint}};Document->Execute(command);
    }
    void Split(const SResolvedPath& path)
    {Add("icax.edit.split",path,{{"cuts",Array{Map{{"id",std::string("cut")},{"anchor",ToData(SPathAnchor{path.Port,path.Value.Geometry.Segments[0].Id,.5})}}}}});}
};
}

TEST(FiveAxisIntentProjection, ReadsOnlyCoreLabelsOrganizationAndEmbeddedMetadata)
{
    Fixture host;
    const auto pending=ReadFiveAxisIntentMetadata(ToolpathFromData(host.Document->ReadOperation(host.Source).at("parameters").To<Map>().at("value").To<Map>()));
    EXPECT_EQ(host.Setup,pending.SetupID);EXPECT_EQ(host.Placement,pending.PlacementID);EXPECT_FALSE(pending.Feature.contains("curveData"));
    host.Generate();const auto source=host.Document->Resolve(host.Plan).Paths.at(0);
    auto group=host.Command("Group");group["groupId"]=std::string("core group");group["members"]=Array{source.PathId};host.Document->Execute(group);
    const auto paths=ProjectFiveAxisIntentPlan(*host.Document,host.Plan);ASSERT_EQ(1u,paths.size());
    EXPECT_EQ("core label",paths[0].Name);EXPECT_EQ("65",paths[0].UserNumber);EXPECT_EQ("core group",paths[0].GroupId);
    EXPECT_EQ(host.Setup,paths[0].Metadata.SetupID);EXPECT_EQ(host.Placement,paths[0].Metadata.PlacementID);EXPECT_EQ("M10",paths[0].Metadata.BeforeCommands);
    EXPECT_FALSE(paths[0].Value.Channels.at("fiveaxis").To<Map>().contains("locked"));EXPECT_EQ(3u,paths[0].Metadata.InputRevision);
    EXPECT_EQ(source.Value.Geometry.Segments[0].Id,paths[0].SourceSpans.begin()->first);
    EXPECT_EQ(host.Setup,ReadFiveAxisIntentMetadata(host.Document->ReadPath(host.Plan,source.Port).Value).SetupID);
}

TEST(FiveAxisIntentProjection, SplitAndReverseClipCADSpansAndRetainProductValues)
{
    Fixture host;host.Generate();host.Split(host.Document->Resolve(host.Plan).Paths.at(0));host.Generate();
    auto paths=host.Document->Resolve(host.Plan).Paths;ASSERT_EQ(2u,paths.size());
    for(const auto& path:paths)
    {
        const auto projected=ProjectFiveAxisIntentPath(path);const auto& segment=path.Value.Geometry.Segments[0];
        const auto evidence=projected.SourceSpans.at(segment.Id).To<Array>();ASSERT_EQ(1u,evidence.size());
        const auto record=evidence[0].To<Map>(),range=record.at("range").To<Map>();
        EXPECT_EQ("span-7",record.at("sourceSpanId").To<std::string>());EXPECT_EQ("7",record.at("edgeId").To<std::string>());
        EXPECT_DOUBLE_EQ(Range(segment).First,range.at("first").To<double>());EXPECT_DOUBLE_EQ(Range(segment).Last,range.at("last").To<double>());
        EXPECT_EQ(host.Setup,projected.Metadata.SetupID);EXPECT_EQ("Steel",projected.Metadata.Process.at("technology").To<std::string>());
    }
    host.Add("icax.edit.reverse",paths[0],{});host.Generate();
    paths=host.Document->Resolve(host.Plan).Paths;ASSERT_EQ(2u,paths.size());bool reversed=false;
    for(const auto& path:paths)if(Range(path.Value.Geometry.Segments[0]).Reversed)
    {
        reversed=true;const auto projected=ProjectFiveAxisIntentPath(path);
        const auto record=projected.SourceSpans.at(path.Value.Geometry.Segments[0].Id).To<Array>()[0].To<Map>();
        EXPECT_TRUE(record.at("reversed").To<bool>());EXPECT_EQ("span-7",record.at("sourceSpanId").To<std::string>());
        EXPECT_EQ(host.Placement,projected.Metadata.PlacementID);
    }
    EXPECT_TRUE(reversed);
}

TEST(FiveAxisIntentProjection, ConstructionSegmentsNeverAcquireFabricatedCADSpans)
{
    Fixture host;host.Generate();auto path=host.Document->Resolve(host.Plan).Paths.at(0);
    auto lead=path.Value.Geometry.Segments[0];lead.Id="constructed lead";lead.ConstructionRole="Lead";
    lead.Sources.push_back({path.Port,path.Value.Geometry.Segments[0].Id,{0,1,false},{0,1,false}});
    path.Value.Geometry.Segments.insert(path.Value.Geometry.Segments.begin(),lead);
    const auto projected=ProjectFiveAxisIntentPath(path);
    EXPECT_TRUE(projected.SourceSpans.at("constructed lead").To<Array>().empty());
    EXPECT_FALSE(projected.Metadata.Feature.contains("start"));
}

TEST(FiveAxisIntentProjection, ManualPosePointsUseCoreAnchorsAndOnlyFollowTheirActualSplitOutput)
{
    Fixture host;host.Generate();const auto original=host.Document->Resolve(host.Plan).Paths.at(0);
    auto input=host.Metadata,pose=input.at("pose").To<Map>();
    pose["keypoints"]=Array{Map{{"id",std::string("manual point")},{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",.25}}},
        {"direction",Array{0.,0.,1.}},{"transition",std::string("ArcLength")}}};input["pose"]=pose;
    EXPECT_THROW(BuildFiveAxisIntentChannel(input,host.Value.Geometry,host.SourceSpans),std::invalid_argument);
    auto value=host.Value;value.Channels=BuildFiveAxisIntentChannels(input,value.Geometry,host.SourceSpans,original.Port);
    EXPECT_FALSE(value.Channels.at("fiveaxis").To<Map>().at("pose").To<Map>().contains("keypoints"));
    auto update=host.Command("UpdateSource");update["operationId"]=host.Source;update["value"]=ToData(value);
    update["expectedOperationFingerprint"]=host.Document->ReadOperation(host.Source).at("definitionFingerprint");host.Document->Execute(update);host.Generate();
    host.Split(host.Document->Resolve(host.Plan).Paths.at(0));host.Generate();
    const auto paths=host.Document->Resolve(host.Plan).Paths;ASSERT_EQ(2u,paths.size());int retained=0;
    for(const auto& path:paths)
    {
        const auto entries=path.Value.Channels.at("poseControls").To<Map>().at("entries").To<Array>();retained+=static_cast<int>(entries.size());
        if(!entries.empty())
        {
            EXPECT_LT(Range(path.Value.Geometry.Segments[0]).First,.25);const auto entry=entries[0].To<Map>();
            const auto anchor=AnchorFromData(entry.at("anchor").To<Map>());
            EXPECT_EQ(path.Port,anchor.Port);EXPECT_DOUBLE_EQ(.25,anchor.Parameter);EXPECT_EQ("manual point",entry.at("id").To<std::string>());
            host.Add("icax.edit.reverse",path,{});
        }
    }
    EXPECT_EQ(1,retained);host.Generate();int afterReverse=0;
    for(const auto& path:host.Document->Resolve(host.Plan).Paths)
    {const auto entries=path.Value.Channels.at("poseControls").To<Map>().at("entries").To<Array>();afterReverse+=static_cast<int>(entries.size());if(!entries.empty())EXPECT_EQ(path.Port,AnchorFromData(entries[0].To<Map>().at("anchor").To<Map>()).Port);}
    EXPECT_EQ(1,afterReverse);
}

TEST(FiveAxisIntentProjection, IntegerNumericPoseInputsPreserveTheirValuesAndRejectBooleanCoordinates)
{
    Fixture host;host.Generate();const auto source=host.Document->Resolve(host.Plan).Paths.at(0);
    auto spans=host.SourceSpans;auto span=spans.front().To<Map>();span["first"]=0;span["last"]=1u;spans[0]=span;
    auto input=host.Metadata,pose=input.at("pose").To<Map>();
    const Map influence{{"interpolation",std::string("Linear")},{"referenceDirection",Array{0,0u,1ll}},
        {"targets",Array{Map{{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",1u}}},
            {"weight",1},{"referenceDirection",Array{0,1,0}}}}}};
    const Map control{{"id",std::string("integer pose")},{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",0}}},
        {"direction",Array{0,0u,1ll}},{"transition",std::string("ArcLength")},{"influence",influence}};
    pose["keypoints"]=Array{control};input["pose"]=pose;const auto before=input;
    auto value=host.Value;value.Channels=BuildFiveAxisIntentChannels(input,value.Geometry,spans,source.Port);
    const auto entries=ProjectFiveAxisPoseControls(value).at("active").To<Array>();ASSERT_EQ(2u,entries.size());
    EXPECT_EQ(before,input);
    for(const auto& item:entries)
    {
        const auto entry=item.To<Map>();const bool target=entry.at("kind").To<std::string>()=="InfluenceTarget";
        EXPECT_DOUBLE_EQ(target?1:0,AnchorFromData(entry.at("anchor").To<Map>()).Parameter);
        if(target)EXPECT_DOUBLE_EQ(1,entry.at("weight").To<double>());
    }
    auto invalid=control;invalid["direction"]=Array{false,0,1};pose["keypoints"]=Array{invalid};input["pose"]=pose;
    EXPECT_THROW(BuildFiveAxisIntentChannels(input,value.Geometry,spans,source.Port),std::invalid_argument);
}

TEST(FiveAxisIntentProjection, InvalidPoseInfluenceInputsRejectWithoutMutatingOriginalData)
{
    Fixture host;host.Generate();auto input=host.Metadata,pose=input.at("pose").To<Map>();
    const Map target{{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",.75}}},{"weight",1.}};
    const Map influence{{"targets",Array{target}}};
    pose["keypoints"]=Array{Map{{"id",std::string("manual")},{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",.25}}},
        {"direction",Array{0.,0.,1.}},{"transition",std::string("ArcLength")},{"influence",influence}}};
    input["pose"]=pose;const auto before=input;
    EXPECT_THROW(BuildFiveAxisIntentChannels(input,host.Value.Geometry,host.SourceSpans,host.Document->Resolve(host.Plan).Paths[0].Port),std::invalid_argument);
    EXPECT_EQ(before,input);
}

TEST(FiveAxisIntentProjection, CreationCanonicalizesMetadataWithoutCopyingCurveAuthority)
{
    Fixture host;auto input=host.Metadata;
    const Map curve{{"schema",std::string("cam.intent-curve-data")},{"bytes",std::string("exact curve bytes")}};
    const Map oldSourceMap{{"revision",std::string("old")},{"value",Map{{"curveData",Map{{"revision",std::string("geometry")},{"value",curve}}},{"spans",host.SourceSpans}}}};
    input["sourceData"]=Map{{"sourceMapData",oldSourceMap},{"parentSourceData",Map{{"curveData",curve}}}};
    auto feature=input.at("feature").To<Map>();feature["start"]=Map{{"sourceSpanId",std::string("old start")},{"nativeParameter",.8}};input["feature"]=feature;
    const auto before=input;
    const auto channel=BuildFiveAxisIntentChannel(input,host.Value.Geometry,host.SourceSpans);
    EXPECT_EQ(before,input);EXPECT_FALSE(channel.at("feature").To<Map>().contains("start"));
    const auto data=channel.at("sourceData").To<Map>();
    EXPECT_FALSE(data.at("sourceMapData").To<Map>().at("value").To<Map>().contains("curveData"));
    EXPECT_FALSE(data.at("parentSourceData").To<Map>().contains("curveData"));
    auto direct=input;direct["sourceData"]=curve;
    EXPECT_THROW(BuildFiveAxisIntentChannel(direct,host.Value.Geometry,host.SourceSpans),std::invalid_argument);
    auto metadata=FiveAxisIntentMetadataFromChannel(channel);metadata.Feature["start"]=Map{{"old",true}};
    EXPECT_FALSE(ToFiveAxisIntentChannel(metadata).at("feature").To<Map>().contains("start"));
}

TEST(FiveAxisIntentProjection, InvalidNestedContourPosePointsRemainIntact)
{
    Fixture host;auto input=host.Metadata;auto feature=input.at("feature").To<Map>();
    feature["contourPoseData"]=Map{{"revision",std::string("old pose")},{"value",Map{{"keypoints",Array{Map{{"id",std::string("do not lose")}}}}}}};
    input["feature"]=feature;const auto before=input;
    EXPECT_THROW(BuildFiveAxisIntentChannel(input,host.Value.Geometry,host.SourceSpans),std::invalid_argument);
    EXPECT_EQ(before,input);
}

TEST(FiveAxisIntentProjection, InfluenceOwnersTargetsAndContourControlsMapIndependentlyAcrossSplitReverseAndReopen)
{
    Fixture host;host.Generate();const auto source=host.Document->Resolve(host.Plan).Paths.at(0);
    auto input=host.Metadata,pose=input.at("pose").To<Map>();
    const Map influence{{"interpolation",std::string("Smooth")},{"referenceDirection",Array{0.,0.,1.}},
        {"targets",Array{Map{{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",.75}}},
            {"weight",.625},{"referenceDirection",Array{0.,1.,0.}}}}}};
    pose["keypoints"]=Array{Map{{"id",std::string("owner")},{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",.25}}},
        {"direction",Array{1.,0.,0.}},{"transition",std::string("DirectionChange")},{"influence",influence}}};input["pose"]=pose;
    auto feature=input.at("feature").To<Map>();
    feature["contourPoseData"]=Map{{"revision",std::string("original contour revision")},{"value",pose}};input["feature"]=feature;
    const auto before=input;auto value=host.Value;value.Channels=BuildFiveAxisIntentChannels(input,value.Geometry,host.SourceSpans,source.Port);
    EXPECT_EQ(before,input);const auto metadata=ReadFiveAxisIntentMetadata(value);
    EXPECT_FALSE(metadata.Pose.contains("keypoints"));const auto contour=metadata.Feature.at("contourPoseData").To<Map>();
    ASSERT_EQ(2u,contour.size());EXPECT_EQ("contour",contour.at("controlsSet").To<std::string>());
    auto basis=pose;basis.erase("keypoints");EXPECT_EQ(basis,contour.at("basis").To<Map>());
    EXPECT_FALSE(contour.at("basis").To<Map>().contains("keypoints"));
    auto update=host.Command("UpdateSource");update["operationId"]=host.Source;update["value"]=ToData(value);
    update["expectedOperationFingerprint"]=host.Document->ReadOperation(host.Source).at("definitionFingerprint");host.Document->Execute(update);host.Generate();
    host.Split(host.Document->Resolve(host.Plan).Paths.at(0));host.Generate();
    auto paths=host.Document->Resolve(host.Plan).Paths;ASSERT_EQ(2u,paths.size());
    for(const auto& path:paths)
    {
        const auto controls=ProjectFiveAxisPoseControls(path.Value);ASSERT_EQ(2u,controls.size());
        for(const auto& set:{std::string("active"),std::string("contour")})
        {
            const auto entries=controls.at(set).To<Array>();ASSERT_EQ(1u,entries.size());const auto entry=entries[0].To<Map>();
            const auto anchor=AnchorFromData(entry.at("anchor").To<Map>());EXPECT_EQ(path.Port,anchor.Port);
            EXPECT_EQ("Duplicate",entry.at("boundaryPolicy").To<std::string>());EXPECT_FALSE(entry.contains("influence"));
            const auto driver=entry.at("driver").To<Map>();EXPECT_FALSE(driver.contains("anchor"));
            EXPECT_EQ(Array({1.,0.,0.}),driver.at("direction").To<Array>());EXPECT_EQ(Array({0.,0.,1.}),driver.at("referenceDirection").To<Array>());
            EXPECT_EQ("Smooth",driver.at("interpolation").To<std::string>());
            if(Range(path.Value.Geometry.Segments[0]).Last<=.5)
            {EXPECT_EQ("Control",entry.at("kind").To<std::string>());EXPECT_DOUBLE_EQ(.25,anchor.Parameter);}
            else
            {
                EXPECT_EQ("InfluenceTarget",entry.at("kind").To<std::string>());EXPECT_DOUBLE_EQ(.75,anchor.Parameter);
                EXPECT_DOUBLE_EQ(.625,entry.at("weight").To<double>());EXPECT_EQ(Array({0.,1.,0.}),entry.at("referenceDirection").To<Array>());
            }
        }
        if(Range(path.Value.Geometry.Segments[0]).Last<=.5)host.Add("icax.edit.reverse",path,{});
    }
    host.Generate();const auto saved=host.Document->ExportDocument();
    auto meta=CreateMetaRegistry();CMetaRegistrationCatalog::ReplayAll(*meta);auto database=GenerateRepository(GenerateNewUUID(),meta);
    const auto imported=CEditableToolpathDocument::ImportDocument(database,saved);CEditableToolpathDocument reopened(database,imported,host.Registry);
    const auto result=reopened.Resolve(host.Plan);ASSERT_EQ(EOutputCompleteness::Complete,result.Status);ASSERT_EQ(2u,result.Paths.size());
    bool reversed=false;for(const auto& path:result.Paths)
    {
        const auto projection=ProjectFiveAxisIntentPath(path);EXPECT_EQ(2u,projection.PoseControls.size());
        if(Range(path.Value.Geometry.Segments[0]).Reversed)reversed=true;
        for(const auto& [set,entries]:projection.PoseControls)EXPECT_EQ(1u,entries.To<Array>().size());
    }
    EXPECT_TRUE(reversed);EXPECT_EQ(saved,reopened.ExportDocument());
}

TEST(FiveAxisIntentProjection, SplitBoundaryControlsHaveAnExplicitDuplicatePolicy)
{
    Fixture host;host.Generate();auto input=host.Metadata,pose=input.at("pose").To<Map>();
    pose["keypoints"]=Array{Map{{"id",std::string("boundary")},{"anchor",Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",.5}}},
        {"direction",Array{0.,0.,1.}},{"transition",std::string("ArcLength")}}};input["pose"]=pose;
    auto value=host.Value;value.Channels=BuildFiveAxisIntentChannels(input,value.Geometry,host.SourceSpans,host.Document->Resolve(host.Plan).Paths[0].Port);
    auto update=host.Command("UpdateSource");update["operationId"]=host.Source;update["value"]=ToData(value);
    update["expectedOperationFingerprint"]=host.Document->ReadOperation(host.Source).at("definitionFingerprint");host.Document->Execute(update);host.Generate();
    host.Split(host.Document->Resolve(host.Plan).Paths.at(0));host.Generate();const auto paths=host.Document->Resolve(host.Plan).Paths;ASSERT_EQ(2u,paths.size());
    for(const auto& path:paths)
    {const auto entries=ProjectFiveAxisPoseControls(path.Value).at("active").To<Array>();ASSERT_EQ(1u,entries.size());EXPECT_DOUBLE_EQ(.5,AnchorFromData(entries[0].To<Map>().at("anchor").To<Map>()).Parameter);}
}

TEST(FiveAxisIntentProjection, TheOldStartBecomesAReferenceStationForActiveAndContourPoseFields)
{
    Fixture host;host.Generate();auto input=host.Metadata,feature=input.at("feature").To<Map>();
    feature["start"]=Map{{"sourceSpanId",std::string("span-7")},{"nativeParameter",.2}};
    feature["contourPoseData"]=Map{{"revision",std::string("saved contour")},{"value",input.at("pose")}};input["feature"]=feature;const auto before=input;
    auto value=host.Value;value.Channels=BuildFiveAxisIntentChannels(input,value.Geometry,host.SourceSpans,host.Document->Resolve(host.Plan).Paths[0].Port);
    const auto controls=ProjectFiveAxisPoseControls(value);ASSERT_EQ(2u,controls.size());
    for(const auto& set:{std::string("active"),std::string("contour")})
    {
        const auto entries=controls.at(set).To<Array>();ASSERT_EQ(1u,entries.size());const auto entry=entries[0].To<Map>();
        EXPECT_EQ("Reference",entry.at("kind").To<std::string>());EXPECT_FALSE(entry.contains("direction"));EXPECT_FALSE(entry.contains("driver"));
        EXPECT_DOUBLE_EQ(.2,AnchorFromData(entry.at("anchor").To<Map>()).Parameter);
    }
    EXPECT_EQ(before,input);EXPECT_FALSE(ReadFiveAxisIntentMetadata(value).Feature.contains("start"));
}

TEST(FiveAxisIntentProjection, ControlsAtAnOldClosedStartKeepOneExplicitAddressWhenTheCoreRotatesTheSeam)
{
    Fixture host;host.Generate();auto input=host.Metadata,feature=input.at("feature").To<Map>(),pose=input.at("pose").To<Map>();
    feature["start"]=Map{{"sourceSpanId",std::string("a")},{"nativeParameter",.25}};input["feature"]=feature;
    pose["keypoints"]=Array{Map{{"id",std::string("at seam")},{"anchor",feature.at("start")},{"direction",Array{0.,0.,1.}},{"transition",std::string("ArcLength")}}};input["pose"]=pose;
    SToolpathValue value;value.Geometry=MakeEditableGeometry(CompositeCurve3{{
        {Segment3{{0,0,0},{10,0,0}},{0,1,false}},
        {Segment3{{10,0,0},{0,10,0}},{0,1,false}},
        {Segment3{{0,10,0},{0,0,0}},{0,1,false}}},true},"closed triangle");
    const Array spans{Map{{"sourceSpanId",std::string("a")}},Map{{"sourceSpanId",std::string("b")}},Map{{"sourceSpanId",std::string("c")}}};
    const auto sourcePort=host.Document->Resolve(host.Plan).Paths[0].Port;value.Channels=BuildFiveAxisIntentChannels(input,value.Geometry,spans,sourcePort);
    auto update=host.Command("UpdateSource");update["operationId"]=host.Source;update["value"]=ToData(value);
    update["expectedOperationFingerprint"]=host.Document->ReadOperation(host.Source).at("definitionFingerprint");host.Document->Execute(update);host.Generate();
    const auto source=host.Document->Resolve(host.Plan).Paths.at(0);
    host.Add("icax.edit.start",source,{{"anchor",ToData(SPathAnchor{source.Port,source.Value.Geometry.Segments.front().Id,.25})}});host.Generate();
    const auto path=host.Document->Resolve(host.Plan).Paths.at(0);EXPECT_TRUE(path.Value.Geometry.Closed);
    const auto entries=ProjectFiveAxisPoseControls(path.Value).at("active").To<Array>();ASSERT_EQ(2u,entries.size());
    for(const auto& item:entries){const auto anchor=AnchorFromData(item.To<Map>().at("anchor").To<Map>());EXPECT_EQ(path.Port,anchor.Port);EXPECT_DOUBLE_EQ(.25,anchor.Parameter);}
}

TEST(FiveAxisIntentProjection, CommonDisplayConstraintsOrderDisabledPathsAndKeepNestedAtomicBlocksContiguous)
{
    std::vector<SResolvedPath> paths(4);for(std::size_t i=0;i<paths.size();++i){paths[i].PathId=GenerateNewUUID();paths[i].Name=std::to_string(i);}
    paths[1].Participating=false;
    const Map constraints{{"blocks",Array{
        Map{{"id",std::string("derived pair")},{"members",Array{paths[0].PathId,paths[1].PathId}},{"atomic",true}},
        Map{{"id",std::string("derived outer")},{"members",Array{paths[2].PathId,paths[0].PathId,paths[1].PathId}},{"atomic",true}},
        Map{{"id",std::string("scene.manual-order")},{"members",Array{paths[2].PathId,paths[0].PathId,paths[1].PathId,paths[3].PathId}},{"atomic",false}}}}};
    const auto ordered=OrderFiveAxisIntentPaths(paths,constraints);ASSERT_EQ(4u,ordered.size());
    EXPECT_EQ("2",ordered[0].Name);EXPECT_EQ("0",ordered[1].Name);EXPECT_EQ("1",ordered[2].Name);EXPECT_FALSE(ordered[2].Participating);EXPECT_EQ("3",ordered[3].Name);
    const Map precedence{{"blocks",Array{Map{{"id",std::string("pair")},{"members",Array{paths[1].PathId,paths[2].PathId}},{"atomic",true}}}},
        {"precedence",Array{Map{{"before",Array{paths[2].PathId}},{"after",Array{paths[0].PathId}}}}}};
    const auto constrained=OrderFiveAxisIntentPaths(paths,precedence);ASSERT_EQ(4u,constrained.size());
    EXPECT_EQ("1",constrained[0].Name);EXPECT_EQ("2",constrained[1].Name);EXPECT_EQ("0",constrained[2].Name);EXPECT_EQ("3",constrained[3].Name);
    const Map conflict{{"blocks",Array{Map{{"id",std::string("pair")},{"members",Array{paths[0].PathId,paths[2].PathId}},{"atomic",true}}}},
        {"precedence",Array{Map{{"before",Array{paths[0].PathId}},{"after",Array{paths[1].PathId}}},Map{{"before",Array{paths[1].PathId}},{"after",Array{paths[2].PathId}}}}}};
    EXPECT_THROW(OrderFiveAxisIntentPaths(paths,conflict),std::invalid_argument);
}

TEST(FiveAxisIntentProjection, EffectivePoseAndProcessingPoliciesOverlayTheSourceMetadataWithoutChangingIt)
{
    Fixture host;auto value=host.Value;const auto source=value.Channels.at("fiveaxis");
    const Map pose{{"mode",std::string("FixedDirection")},{"frame",std::string("Workpiece")},{"direction",Array{1.,0.,0.}}};
    value.Channels["pose"]=Map{{"schema",std::string("fiveaxis.pose/1")},{"attachment",std::string("Path")},{"policy",pose}};
    const Map kerf{{"owner",std::string("Cam")},{"widthMm",.35}};
    value.Channels["fiveaxis.kerf"]=Map{{"schema",std::string("fiveaxis.kerf/1")},{"attachment",std::string("Path")},{"policy",kerf}};
    value.Channels["fiveaxis.trace"]=Map{{"schema",std::string("fiveaxis.trace/1")},{"attachment",std::string("Path")},{"gapLengthMm",.4},{"overcutLengthMm",0.}};
    const Map in{{"kind",std::string("Line")} },out{{"kind",std::string("Arc")}};
    value.Channels["fiveaxis.leads"]=Map{{"schema",std::string("fiveaxis.leads/1")},{"attachment",std::string("Path")},{"leadIn",in},{"leadOut",out}};
    const auto projected=ReadFiveAxisIntentMetadata(value);EXPECT_EQ(pose,projected.Pose);EXPECT_EQ(kerf,projected.Process.at("kerf").To<Map>());
    EXPECT_DOUBLE_EQ(.4,projected.Process.at("gapLengthMm").To<double>());EXPECT_EQ(in,projected.Process.at("leadIn").To<Map>());EXPECT_EQ(out,projected.Process.at("leadOut").To<Map>());
    EXPECT_EQ(source,value.Channels.at("fiveaxis"));
}

TEST(FiveAxisIntentProjection, RetiringASetupPlanClearsRootsAndOperationsWithoutDisposingCorePathIdentities)
{
    Fixture host;host.Generate();const auto source=host.Document->Resolve(host.Plan).Paths.at(0);host.Add("icax.edit.reverse",source,{});host.Generate();
    const auto plan=host.Document->ReadPlan(host.Plan);const auto operation=plan.at("operations").To<Array>().back().To<IntentId>();
    auto roots=host.Command("SetPlanRoots");roots["roots"]=Array{};roots["expectedPlanFingerprint"]=plan.at("definitionFingerprint");
    auto disable=host.Command("SetOperationEnabled");disable["operationId"]=operation;disable["enabled"]=false;disable["dependencyPolicy"]=std::string("KeepInvalid");
    disable["expectedOperationFingerprint"]=host.Document->ReadOperation(operation).at("definitionFingerprint");
    const auto history=host.Database->GetUndoArray();host.Document->ExecuteBatch({roots,disable});
    EXPECT_EQ(history.size()+1,host.Database->GetUndoArray().size());const auto retired=host.Document->Resolve(host.Plan);
    EXPECT_EQ(EOutputCompleteness::Complete,retired.Status);EXPECT_TRUE(retired.IntentionalEmpty);EXPECT_TRUE(retired.Paths.empty());
    EXPECT_TRUE(host.Database->HasEntity(source.PathId));EXPECT_NO_THROW(ValidateToolpathImport(host.Document->ExportDocument(),*CreateToolpathGeometryServices()));
    ASSERT_TRUE(host.Database->Undo());host.Generate();EXPECT_EQ(source.PathId,host.Document->Resolve(host.Plan).Paths.at(0).PathId);
}
