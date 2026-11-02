namespace
{
    iCAX::CAM::Intent::SToolpathValue PublishedBranchValue(Scene&,const Variant&);
    void AssertUnifiedCutAuthority(Scene&);
}
namespace collision_repair_tests
{
    struct Station { std::size_t Index=0; Variant Intent; Map Anchor; Array Direction; };
    Map Request(Scene& scene,std::size_t index,double maximum=15.)
    {
        auto request=GenerateRequest(scene);request["segmentIndex"]=std::to_string(index);request["maxDeviationDeg"]=maximum;return request;
    }
    Map WaitRepairTask(Scene& scene,const Map& task,std::chrono::seconds timeout,const Text& stage)
    {
        const auto started=std::chrono::steady_clock::now(),deadline=started+timeout;Map result;
        const auto record=[&]{static std::size_t sequence=0;::testing::Test::RecordProperty("collisionTaskMs_"+std::to_string(++sequence),std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count()));};
        do {
            result=Invoke(&scene,"CamSimulation","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
            if(result.at("status").To<Text>()!="Running"){record();return result;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while(std::chrono::steady_clock::now()<deadline);
        record();throw std::runtime_error(stage+" timed out: "+iCAX::Data::VariantSerializer::Serialize(result));
    }
    Map WaitRepair(Scene& scene,const Map& task){return WaitRepairTask(scene,task,std::chrono::seconds(190),"Collision repair");}
    Map WaitRepairPreparation(Scene& scene,const Map& task)
    {const auto result=WaitRepairTask(scene,task,std::chrono::seconds(60),"Collision repair fixture preparation");if(result.at("status").To<Text>()!="Completed")throw std::runtime_error(iCAX::Data::VariantSerializer::Serialize(result));return result;}
    void GenerateRepairAxis(Scene& scene){(void)WaitRepairPreparation(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));}
    void CheckRepairCollision(Scene& scene){(void)WaitRepairPreparation(scene,Invoke(&scene,"CamSimulation","CheckCollision",GenerateRequest(scene)));}
    void UseContours(Scene& scene)
    {
        Array ids;for(const auto& value:Read(scene).at("intentToolpaths").To<Array>())ids.push_back(value.To<Map>().at("id"));
        Invoke(&scene,"CamCut","SetType",EditRequest(scene,ids,{{"cutType",Text("Contour")}}));
    }
    void PrepareBlocked(Scene& scene)
    {
        PrepareOrderPart(scene,false);UseContours(scene);
        // Real closed fixture volume encloses the whole head. No direction
        // within the allowed cone can remove an occupied endpoint from it.
        File enclosure(BRepPrimAPI_MakeBox(gp_Pnt(-1000,-1000,-1000),2000.,2000.,2000.).Shape());
        auto request=GenerateRequest(scene);request["sourcePath"]=enclosure.Utf8();
        const auto fixture=WaitRepairPreparation(scene,Invoke(&scene,"CamFixture","Import",request));
        if(fixture.at("status").To<Text>()!="Completed")throw std::runtime_error("Repair fixture import failed");
        GenerateRepairAxis(scene);CheckRepairCollision(scene);
    }
    Station FindStation(Scene& scene,const Map* anchor=nullptr)
    {
        const auto report=SimulationReport(scene);const auto segments=report.at("segments").To<Array>();
        std::size_t offset=0;
        // This fixture deliberately has no generated rapid. Missing connector
        // intervals are retained as Unchecked, not invented as clear motion.
        for(const auto& value:Generated(scene).at("operations").To<Array>()){
            const auto op=value.To<Map>();
            for(const auto& p:op.at("parts").To<Array>()){
                const auto part=p.To<Map>();const auto points=part.at("points").To<Array>();
                const auto samples=part.contains("sourceSamples")?part.at("sourceSamples").To<Array>():Array{};
                const auto directions=part.at("directions").To<Array>();
                for(std::size_t i=1;i+1<samples.size();++i){
                    const auto sample=samples[i].To<Map>();if(sample.at("kind").To<Text>()!="IntentSource")continue;
                    const auto at=sample.at("anchor").To<Map>();
                    if(anchor&&at!=*anchor)continue;
                    const auto index=offset+i;if(index>=segments.size())continue;
                    const auto segment=segments[index].To<Map>();if(segment.at("status").To<Text>()!="Collision")continue;
                    // The containment witness is at the actual first pose;
                    // choosing an interior source station avoids system ends.
                    if(segment.at("fraction").To<double>()>=.5)continue;
                    return {index,op.at("intentId"),at,directions[i].To<Array>()};
                }
                offset+=points.size();
            }
        }
        throw std::runtime_error("No contained editable cut station found");
    }
    Map RunRepair(Scene& scene,std::size_t index)
    {
        return WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",Request(scene,index)));
    }
    void ExpectUnchanged(Scene& scene,const Map& before,std::size_t resources,std::size_t history)
    {
        EXPECT_EQ(before,Read(scene));EXPECT_EQ(resources,scene.Resources().Count());EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    }
    void ExpectUnresolved(const Map& task,const Text& reason)
    {
        ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
        const auto result=task.at("result").To<Map>();EXPECT_EQ("Local",result.at("scope").To<Text>());EXPECT_EQ("Unresolved",result.at("status").To<Text>());EXPECT_FALSE(result.at("changed").To<bool>());
        ASSERT_EQ(1u,result.at("unresolved").To<Array>().size());EXPECT_EQ(reason,result.at("unresolved").To<Array>().front().To<Map>().at("reason").To<Text>());
    }
    std::pair<Map,Array> MiddleSource(const Map& operation)
    {
        for(const auto& value:operation.at("parts").To<Array>()){
            const auto part=value.To<Map>();if(!part.contains("sourceSamples"))continue;
            const auto samples=part.at("sourceSamples").To<Array>(),points=part.at("points").To<Array>();
            for(std::size_t i=samples.size()/2;i+1<samples.size();++i){const auto sample=samples[i].To<Map>();
                if(sample.at("kind").To<Text>()=="IntentSource")return {sample.at("anchor").To<Map>(),points[i].To<Array>()};
            }
        }
        throw std::runtime_error("No middle source station");
    }
    Array SetupPoint(Scene& scene,const Map& operation,const Array& point)
    {
        for(const auto& value:Read(scene).at("placements").To<Array>()){
            const auto placement=value.To<Map>();if(placement.at("id")!=operation.at("placementId"))continue;
            const auto transform=placement.at("transform").To<Array>();Array result;
            for(int i=0;i<3;++i){double x=transform[i*4+3].To<double>();for(int j=0;j<3;++j)x+=transform[i*4+j].To<double>()*point[j].To<double>();result.emplace_back(x);}return result;
        }
        throw std::runtime_error("Repair placement missing");
    }
    void ImportClamp(Scene& scene,const Array& p,double halfWidth=.15)
    {
        File clamp(BRepPrimAPI_MakeBox(gp_Pnt(p[0].To<double>()-halfWidth,p[1].To<double>()-halfWidth,p[2].To<double>()+20.5),2*halfWidth,2*halfWidth,.5).Shape());
        auto request=GenerateRequest(scene);request["sourcePath"]=clamp.Utf8();const auto result=WaitRepairPreparation(scene,Invoke(&scene,"CamFixture","Import",request));
        if(result.at("status").To<Text>()!="Completed")throw std::runtime_error("Local side-clamp import failed");
    }
    Station PrepareLocal(Scene& scene,bool zeroLimitsWithManual=false,std::size_t cutIndex=0,bool interpolation=false)
    {
        PrepareOrderPart(scene,false);UseContours(scene);GenerateRepairAxis(scene);
        if(zeroLimitsWithManual){
            const auto id=OrderedIDs(Generated(scene)).front();const auto samples=InfluenceSamples(scene,id);const auto anchor=samples.at(2).To<Map>().at("anchor").To<Map>();
            Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),anchor,Array{0.,0.,1.}));
            Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{id},{{"patch",Map{{"angularToleranceRad",0.},{"leadDeviationLimitRad",0.},{"sideDeviationLimitRad",0.}}}}));GenerateRepairAxis(scene);
        }
        const double halfWidth=interpolation?.01:.1;
        File head(BRepPrimAPI_MakeBox(gp_Pnt(-halfWidth,-halfWidth,2.),2*halfWidth,2*halfWidth,20.).Shape());
        const auto imported=WaitRepairPreparation(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,head)));
        if(imported.at("status").To<Text>()!="Completed")throw std::runtime_error("Repair test head import failed");
        auto calibration=CalibrateTestHead(scene);calibration["tcpLocalPosition"]=Array{0.,0.,0.};Invoke(&scene,"CamHead","Configure",calibration);
        const auto operations=Generated(scene).at("operations").To<Array>();if(operations.size()<2)throw std::runtime_error("Repair requires two test cuts");
        const auto first=operations.at(cutIndex).To<Map>(),remote=operations[(cutIndex+1)%operations.size()].To<Map>();auto [anchor,point]=MiddleSource(first);
        if(interpolation){
            const auto controls=InfluenceSamples(scene,first.at("intentId"));double best=0;
            for(const auto& p:first.at("parts").To<Array>()){const auto part=p.To<Map>();if(!part.contains("sourceSamples"))continue;const auto samples=part.at("sourceSamples").To<Array>(),positions=part.at("points").To<Array>();
                for(std::size_t i=1;i+1<samples.size();++i){const auto sample=samples[i].To<Map>();if(sample.at("kind").To<Text>()!="IntentSource")continue;
                    const auto at=positions[i].To<Array>();double nearest=1e100;for(const auto& value:controls){const auto other=value.To<Map>().at("point").To<Array>();double distance=0;for(int j=0;j<3;++j)distance+=std::pow(at[j].To<double>()-other[j].To<double>(),2);nearest=std::min(nearest,std::sqrt(distance));}
                    if(nearest>best){best=nearest;anchor=sample.at("anchor").To<Map>();point=at;}
                }
            }
            if(best<.1)throw std::runtime_error("Interpolation fixture needs a station separated from all keypoints: "+std::to_string(best));
        }
        ImportClamp(scene,SetupPoint(scene,first,point),interpolation?.01:.15);ImportClamp(scene,SetupPoint(scene,remote,MiddleSource(remote).second));
        GenerateRepairAxis(scene);CheckRepairCollision(scene);CheckRepairCollision(scene);const auto station=FindStation(scene,&anchor);
        if(station.Intent!=first.at("intentId"))throw std::runtime_error("Local repair station changed cut");return station;
    }
    Map HistoryFacts(Scene& scene)
    {
        const auto state=Read(scene);const auto setup=scene.Database().GetEntity(*ID::from_string(state.at("activeSetupId").To<Text>()))->GetComponent<CFiveAxisSetupComponent>();Map poses;
        for(const auto& value:state.at("intentToolpaths").To<Array>()){const auto row=value.To<Map>();poses[row.at("id").To<Text>()]=row.at("poseRef");}
        return {{"inputRevision",std::to_string(setup->GetInputRevision())},{"generatedData",setup->GetGeneratedToolpaths()},{"rapidData",setup->GetGeneratedRapidPaths()},{"simulationCheckRef",setup->GetSimulationCheckData()},{"poseRefs",poses},{"visibleGenerationResource",Generated(scene).at("revision")}};
    }
    Map CurrentRepairDefinitions(Scene& scene)
    {
        Map result;for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
            if(const auto component=entity->GetComponent("CEditableOperationComponent"))result[iCAX::Data::to_string(entity->GetID())]=component->GetProperties().at("Definition");
        return result;
    }
    Map RepairExactGeometry(Scene& scene,const Variant& id)
    {
        auto geometry=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,id)).at("geometry").To<Map>();auto segments=geometry.at("segments").To<Array>();
        // A pose operation adds exact provenance to its input; curve payloads,
        // domains, identities, traversal, frame and tolerance stay unchanged.
        for(auto& value:segments){auto segment=value.To<Map>();segment.erase("sources");value=std::move(segment);}geometry["segments"]=std::move(segments);return geometry;
    }
    struct NativeRepairFrame{Text Intent,Segment;double Parameter=0;};
    std::vector<NativeRepairFrame> NativeRepairFrames(const Map& generated)
    {
        std::vector<NativeRepairFrame> result;
        for(const auto& item:generated.at("operations").To<Array>())
        {
            const auto operation=item.To<Map>();
            for(const auto& value:operation.at("parts").To<Array>())
            {
                const auto part=value.To<Map>();const auto samples=part.at("sourceSamples").To<Array>();
                if(samples.size()!=part.at("points").To<Array>().size())throw std::runtime_error("Repair fixture has incomplete native frame addresses");
                for(const auto& sample:samples){const auto anchor=iCAX::CAM::Intent::AnchorFromData(sample.To<Map>().at("anchor").To<Map>());result.push_back({operation.at("intentId").To<Text>(),anchor.SegmentId,anchor.Parameter});}
            }
        }
        return result;
    }
    std::size_t MatchRepairFrame(const std::vector<NativeRepairFrame>& frames,const NativeRepairFrame& target)
    {
        std::optional<std::size_t> found;
        for(std::size_t i=0;i<frames.size();++i)if(frames[i].Intent==target.Intent&&frames[i].Segment==target.Segment&&std::abs(frames[i].Parameter-target.Parameter)<1e-12)
        {if(found)throw std::runtime_error("Ambiguous native repair frame address");found=i;}
        if(!found)throw std::runtime_error("An original repair frame address disappeared");return *found;
    }
    void CheckPublishedRepair(const Map& originalGeometry,const Map& geometry,const Map& originalReport,const Map& report,const Map& result)
    {
        ASSERT_EQ("Verified",result.at("verification").To<Text>());EXPECT_EQ("PublishedRepairFullRecheck",report.at("checkMode").To<Text>());EXPECT_EQ("Full",report.at("scopeOfLastCheck").To<Text>());
        const auto before=NativeRepairFrames(originalGeometry),after=NativeRepairFrames(geometry);
        const auto oldFrames=originalReport.at("frames").To<Array>(),frames=report.at("frames").To<Array>();ASSERT_EQ(before.size(),oldFrames.size());ASSERT_EQ(after.size(),frames.size());
        const auto policy=report.at("localRepairPolicy").To<Map>();const auto first=std::stoull(policy.at("fixedEntryFrameIndex").To<Text>()),last=std::stoull(policy.at("fixedExitFrameIndex").To<Text>());
        ASSERT_LT(first,last);ASSERT_LT(last,frames.size());const auto oldFirst=MatchRepairFrame(before,after.at(first)),oldLast=MatchRepairFrame(before,after.at(last));ASSERT_LT(oldFirst,oldLast);
        std::size_t changed=0;
        for(std::size_t i=0;i<before.size();++i)
        {
            const auto at=MatchRepairFrame(after,before[i]);
            if(i<=oldFirst||i>=oldLast)EXPECT_EQ(oldFrames[i],frames[at])<<"Original exterior full pose changed at native frame "<<i;
            else if(oldFrames[i]!=frames[at])++changed;
        }
        EXPECT_GT(changed,0u);
        const auto segments=report.at("segments").To<Array>(),oldSegments=originalReport.at("segments").To<Array>();
        EXPECT_EQ(std::to_string(segments.size()),report.at("checkedSegmentCount").To<Text>());EXPECT_EQ(segments.size(),report.at("localCheckedSegmentIndices").To<Array>().size());
        for(std::size_t i=first;i<last;++i)EXPECT_EQ("Clear",segments.at(i).To<Map>().at("status").To<Text>());
        bool otherCollision=false,otherUnchecked=false;
        for(std::size_t i=0;i<oldSegments.size();++i)if(i<oldFirst||i>=oldLast)
        {
            ASSERT_LT(i+1,before.size());const auto a=MatchRepairFrame(after,before[i]),b=MatchRepairFrame(after,before[i+1]);ASSERT_EQ(a+1,b);
            const auto expected=oldSegments[i].To<Map>().at("status"),actual=segments.at(a).To<Map>().at("status");EXPECT_EQ(expected,actual);
            otherCollision|=actual.To<Text>()=="Collision";otherUnchecked|=actual.To<Text>()=="Unchecked";
        }
        EXPECT_TRUE(otherCollision);EXPECT_TRUE(otherUnchecked);
    }
    void ExpectFixedKeypointBoundaries(const Map& before,const Map& after,const Map& result)
    {
        const auto search=result.at("poseSearch").To<Map>();const auto entry=std::stoull(search.at("fixedEntryFrameIndex").To<Text>()),exit=std::stoull(search.at("fixedExitFrameIndex").To<Text>());
        const auto original=before.at("frames").To<Array>(),actual=after.at("frames").To<Array>();ASSERT_EQ(original.size(),actual.size());ASSERT_LT(entry,exit);ASSERT_LT(exit,actual.size());
        for(std::size_t i=0;i<actual.size();++i)if(i<=entry||i>=exit)EXPECT_EQ(original[i],actual[i])<<"Fixed or unrelated full pose changed at frame "<<i;
        const auto trials=std::stoull(search.at("candidateCount").To<Text>()),steps=std::stoull(search.at("transportStepCount").To<Text>());
        EXPECT_GT(trials,0u);EXPECT_LE(steps,trials*(exit-entry));
    }
}

TEST(FiveAxisCollisionRepair, InvalidAndUnselectedRequestsDoNotPublishOrCreateHistory)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto station=FindStation(scene);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    auto request=Request(scene,station.Index,0.);EXPECT_THROW(Invoke(&scene,"CamSimulation","AutoRepairCollision",request),std::exception);
    request=Request(scene,station.Index,181.);EXPECT_THROW(Invoke(&scene,"CamSimulation","AutoRepairCollision",request),std::exception);
    request=Request(scene,SimulationReport(scene).at("segments").To<Array>().size());EXPECT_THROW(Invoke(&scene,"CamSimulation","AutoRepairCollision",request),std::exception);
    const auto segments=SimulationReport(scene).at("segments").To<Array>();bool tested=false;
    for(std::size_t i=0;i<segments.size();++i)if(segments[i].To<Map>().at("status").To<Text>()=="Unchecked"){
        request=Request(scene,i);EXPECT_THROW(Invoke(&scene,"CamSimulation","AutoRepairCollision",request),std::exception);tested=true;break;
    }
    EXPECT_TRUE(tested);ExpectUnchanged(scene,before,resources,history);
}

TEST(FiveAxisCollisionRepair, CancelledWorkerCannotPublishEvenWhenAResultIsAlreadyReady)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto station=FindStation(scene);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    const auto request=Request(scene,station.Index);const auto task=Invoke(&scene,"CamSimulation","AutoRepairCollision",request);
    EXPECT_EQ(task.at("taskId"),Invoke(&scene,"CamSimulation","AutoRepairCollision",request).at("taskId"));
    Invoke(&scene,"CamSimulation","CancelTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
    EXPECT_EQ("Cancelled",WaitRepair(scene,task).at("status").To<Text>());ExpectUnchanged(scene,before,resources,history);
}

TEST(FiveAxisCollisionRepair, InheritedGroupLockBlocksSelectedCutWithoutBlockingCaptureOfOtherMissingRapids)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto station=FindStation(scene);
    const auto ids=OrderedIDs(Generated(scene));ASSERT_GE(ids.size(),2u);const auto other=ids[0]==station.Intent?ids[1]:ids[0];
    auto group=GenerateRequest(scene);group["action"]=Text("Group");group["intentIds"]=Array{station.Intent,other};
    const auto created=Invoke(&scene,"CamCut","ManageTree",group);
    auto lock=GenerateRequest(scene);lock["action"]=Text("Lock");lock["intentIds"]=Array{};lock["groupIds"]=Array{created.at("groupId")};Invoke(&scene,"CamCut","ManageTree",lock);
    GenerateRepairAxis(scene);CheckRepairCollision(scene);const auto selected=FindStation(scene);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    ExpectUnresolved(RunRepair(scene,selected.Index),"CAM_REPAIR_LOCKED_CUT");ExpectUnchanged(scene,before,resources,history);
}

TEST(FiveAxisCollisionRepair, BlockedNeighborKeypointsAreNeverMovedToRepairAManualPoint)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto station=FindStation(scene);
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,station.Intent,NewAxisPoint(),station.Anchor,station.Direction));
    GenerateRepairAxis(scene);CheckRepairCollision(scene);const auto selected=FindStation(scene,&station.Anchor);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    const auto task=RunRepair(scene,selected.Index);EXPECT_EQ("Unresolved",task.at("result").To<Map>().at("status").To<Text>());ExpectUnchanged(scene,before,resources,history);
}

TEST(FiveAxisCollisionRepair, OriginalPoseDeviationZeroIsNotSilentlyRelaxed)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto station=FindStation(scene);
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{station.Intent},{{"patch",Map{{"angularToleranceRad",0.}}}}));
    GenerateRepairAxis(scene);CheckRepairCollision(scene);const auto selected=FindStation(scene);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    ExpectUnresolved(RunRepair(scene,selected.Index),"CAM_REPAIR_POSE_LIMIT_ZERO");ExpectUnchanged(scene,before,resources,history);
}

TEST(FiveAxisCollisionRepair, ExplicitInfluenceTargetCannotBeReplacedByANewRepairOwner)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto id=OrderedIDs(Generated(scene)).front();
    const auto samples=InfluenceSamples(scene,id);ASSERT_GT(samples.size(),8u);const auto n=samples.size()/2;
    const auto owner=samples[n-2].To<Map>().at("anchor").To<Map>(),target=samples[n].To<Map>().at("anchor").To<Map>();
    Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,NewAxisPoint(),owner,{InfluenceTarget(samples[n],.5)},"Linear",Array{0.,0.,1.}));
    GenerateRepairAxis(scene);CheckRepairCollision(scene);const auto selected=FindStation(scene,&target);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    const auto task=RunRepair(scene,selected.Index);EXPECT_EQ("Unresolved",task.at("result").To<Map>().at("status").To<Text>());ExpectUnchanged(scene,before,resources,history);
}

TEST(FiveAxisCollisionRepair, SceneChangeSupersedesWorkerWithoutReplacingTheOldGenerationOrReport)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto station=FindStation(scene);
    ASSERT_EQ(nullptr,GetModuleHandleW(L"CamRuntime.dll"));
    auto settings=Read(scene).at("setups").To<Array>().front().To<Map>().at("rapidSettings").To<Map>();settings["envelopeRadiusMm"]=2.;settings["envelopeLengthMm"]=10.;
    auto edit=GenerateRequest(scene);edit["settings"]=settings;const auto repair=Request(scene,station.Index);
    const auto task=Invoke(&scene,"CamSimulation","AutoRepairCollision",repair);
    // Submit the already prepared user edit before another scene read can
    // pump and publish a completed worker. This exercises the actual race.
    Invoke(&scene,"CamRapid","Configure",edit);const auto afterEdit=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    const auto superseded=WaitRepair(scene,task);EXPECT_EQ("Superseded",superseded.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(superseded);ExpectUnchanged(scene,afterEdit,resources,history);
    EXPECT_EQ(nullptr,GetModuleHandleW(L"CamRuntime.dll"));
}

TEST(FiveAxisCollisionRepair, EnclosedHeadRemainsUnresolvedAndUnrelatedCollisionAndUncheckedRowsAreUntouched)
{
    using namespace collision_repair_tests;Scene scene;PrepareBlocked(scene);const auto station=FindStation(scene);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
    const auto task=RunRepair(scene,station.Index);ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto result=task.at("result").To<Map>();EXPECT_EQ("Unresolved",result.at("status").To<Text>());EXPECT_FALSE(result.at("changed").To<bool>());
    ExpectUnchanged(scene,before,resources,history);
    EXPECT_GT(std::stoull(SimulationReport(scene).at("collisionCount").To<Text>()),0u);EXPECT_GT(std::stoull(SimulationReport(scene).at("uncheckedCount").To<Text>()),0u);
}

TEST(FiveAxisCollisionRepair, PlanarLocalRepairCommitsOnceRetainsOtherIssuesAndRestoresItsExactCertificates)
{
    using namespace collision_repair_tests;Scene scene;const auto station=PrepareLocal(scene);ASSERT_EQ(nullptr,GetModuleHandleW(L"CamRuntime.dll"));
    const auto before=Read(scene),original=Generated(scene),oldReport=SimulationReport(scene),definitions=CurrentRepairDefinitions(scene);
    const auto exactGeometry=RepairExactGeometry(scene,station.Intent),originalValue=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,station.Intent));
    const auto history=scene.Database().GetUndoArray().size();Map siblings;
    for(const auto& item:before.at("intentToolpaths").To<Array>()){const auto id=item.To<Map>().at("id");if(id!=station.Intent)siblings[id.To<Text>()]=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,id));}
    const auto task=RunRepair(scene,station.Index);ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto result=task.at("result").To<Map>();ASSERT_EQ("Repaired",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    EXPECT_TRUE(result.at("changed").To<bool>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());EXPECT_GT(result.at("maxAppliedAngleDeg").To<double>(),0.);EXPECT_LE(result.at("maxAppliedAngleDeg").To<double>(),15.+1e-8);
    const auto report=SimulationReport(scene),generated=Generated(scene);ASSERT_EQ("Current",report.at("validity").To<Text>());ASSERT_EQ("Current",generated.at("status").To<Text>());
    ASSERT_NO_FATAL_FAILURE(CheckPublishedRepair(original,generated,oldReport,report,result));EXPECT_EQ(exactGeometry,RepairExactGeometry(scene,station.Intent));
    const auto afterDefinitions=CurrentRepairDefinitions(scene);ASSERT_EQ(definitions.size()+1,afterDefinitions.size());std::size_t deltas=0;
    for(const auto& [id,definition]:afterDefinitions){if(definitions.contains(id))EXPECT_EQ(definitions.at(id),definition);else {EXPECT_EQ("fiveaxis.pose.delta",definition.To<Map>().at("typeId").To<Text>());++deltas;}}
    EXPECT_EQ(1u,deltas);for(const auto& [id,value]:siblings)EXPECT_EQ(value,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,Text(id))));
    EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("fixtures"),Read(scene).at("fixtures"));AssertUnifiedCutAuthority(scene);
    const auto repairedValue=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,station.Intent));
    ASSERT_TRUE(scene.Database().Undo());(void)Read(scene);EXPECT_EQ(definitions,CurrentRepairDefinitions(scene));EXPECT_EQ(originalValue,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,station.Intent)));
    const auto undoneGeometry=Generated(scene),undoneReport=SimulationReport(scene);
    if(undoneGeometry.at("status").To<Text>()=="Current")EXPECT_EQ(original,undoneGeometry);else EXPECT_EQ("Stale",undoneGeometry.at("status").To<Text>());
    if(undoneReport.at("validity").To<Text>()=="Current")EXPECT_EQ(oldReport,undoneReport);else EXPECT_EQ("Stale",undoneReport.at("validity").To<Text>());
    ASSERT_TRUE(scene.Database().Redo());(void)Read(scene);EXPECT_EQ(afterDefinitions,CurrentRepairDefinitions(scene));EXPECT_EQ(repairedValue,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,station.Intent)));
    ASSERT_EQ("Current",Generated(scene).at("status").To<Text>());EXPECT_EQ(generated,Generated(scene));EXPECT_EQ(report,SimulationReport(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Common pose delta verified repair";
    const auto saved=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(saved,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(saved,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(afterDefinitions,CurrentRepairDefinitions(reopened));EXPECT_EQ(repairedValue,iCAX::CAM::Intent::ToData(PublishedBranchValue(reopened,station.Intent)));EXPECT_EQ(generated,Generated(reopened));EXPECT_EQ(report,SimulationReport(reopened));
    EXPECT_EQ(exactGeometry,RepairExactGeometry(reopened,station.Intent));AssertUnifiedCutAuthority(reopened);EXPECT_EQ(nullptr,GetModuleHandleW(L"CamRuntime.dll"));std::filesystem::remove(saved);
}
TEST(FiveAxisCollisionRepair, ThirdCutCollidingManualKeypointMovesOnlyItselfBetweenFixedNeighbors)
{
    using namespace collision_repair_tests;Scene scene;const auto initial=PrepareLocal(scene,false,2);
    const auto pointId=NewAxisPoint();Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,initial.Intent,pointId,initial.Anchor,initial.Direction));
    GenerateRepairAxis(scene);CheckRepairCollision(scene);const auto selected=FindStation(scene,&initial.Anchor);const auto before=SimulationReport(scene),oldPose=ProcessItem(scene,selected.Intent).at("posePolicy").To<Map>();
    const auto task=RunRepair(scene,selected.Index);ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto result=task.at("result").To<Map>();ASSERT_EQ("Repaired",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    EXPECT_EQ("Keypoint",result.at("poseSearch").To<Map>().at("kind").To<Text>());EXPECT_GT(std::stoull(result.at("poseSearch").To<Map>().at("fixedEntryFrameIndex").To<Text>()),0u);
    ExpectFixedKeypointBoundaries(before,SimulationReport(scene),result);
    const auto points=ProcessItem(scene,selected.Intent).at("posePolicy").To<Map>().at("keypoints").To<Array>(),original=oldPose.at("keypoints").To<Array>();ASSERT_EQ(original.size(),points.size());
    for(const auto& value:original){const auto old=value.To<Map>();const auto found=std::find_if(points.begin(),points.end(),[&](const auto& p){return p.template To<Map>().at("id")==old.at("id");});ASSERT_NE(points.end(),found);
        if(old.at("id")==pointId){EXPECT_EQ(old.at("anchor"),found->To<Map>().at("anchor"));EXPECT_NE(old.at("direction"),found->To<Map>().at("direction"));}else EXPECT_EQ(value,*found);
    }
}

TEST(FiveAxisCollisionRepair, SafeKeypointsWithCollidingInterpolationKeepBothEndpointsAndOnlyChangeTheInterval)
{
    using namespace collision_repair_tests;Scene scene;const auto selected=PrepareLocal(scene,false,2,true);const auto before=SimulationReport(scene),pose=ProcessItem(scene,selected.Intent).at("posePolicy").To<Map>();
    const auto task=RunRepair(scene,selected.Index);ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);const auto result=task.at("result").To<Map>();
    ASSERT_EQ("Repaired",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);EXPECT_EQ("Interpolation",result.at("poseSearch").To<Map>().at("kind").To<Text>());
    ExpectFixedKeypointBoundaries(before,SimulationReport(scene),result);const auto points=ProcessItem(scene,selected.Intent).at("posePolicy").To<Map>().at("keypoints").To<Array>();
    EXPECT_EQ(pose.at("keypoints").To<Array>().size()+1,points.size());for(const auto& old:pose.at("keypoints").To<Array>())EXPECT_NE(points.end(),std::find(points.begin(),points.end(),old));
}

TEST(FiveAxisCollisionRepair, RejectedCommitRollsBackAllNewResourcesAndCanBeRetried)
{
    using namespace collision_repair_tests;Scene scene;const auto station=PrepareLocal(scene);
    ASSERT_EQ(nullptr,GetModuleHandleW(L"CamRuntime.dll"));
    class RejectRepair final : public iCAX::Database::IChecker {
    public:
        bool Reject=true;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet& values,Text& error)override{
            if(Reject&&values.contains("SimulationCheckData")){Reject=false;error="Injected local repair commit rejection";return false;}return true;
        }
    };
    const auto checker=std::make_shared<RejectRepair>();scene.Database().GetMetaRegistry()->RegistChecker<CFiveAxisSetupComponent>(checker);
    const auto before=Read(scene);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();const auto undo=scene.Database().GetUndoArray(),redo=scene.Database().GetRedoArray();
    const auto rejected=RunRepair(scene,station.Index);ASSERT_EQ("Failed",rejected.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(rejected);
    EXPECT_FALSE(checker->Reject);EXPECT_NE(Text::npos,rejected.at("error").To<Text>().find("Injected local repair commit rejection"));
    ASSERT_TRUE(rejected.at("result").To<Map>().at("candidateReverted").To<bool>())<<iCAX::Data::VariantSerializer::Serialize(rejected);
    auto expected=before,actual=Read(scene);expected.erase("expected");actual.erase("expected");EXPECT_EQ(expected,actual);
    EXPECT_EQ(resources,scene.Resources().Count());EXPECT_EQ(undo,scene.Database().GetUndoArray());EXPECT_EQ(redo,scene.Database().GetRedoArray());
    const auto retried=RunRepair(scene,station.Index);ASSERT_EQ("Completed",retried.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(retried);
    EXPECT_EQ("Repaired",retried.at("result").To<Map>().at("status").To<Text>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());EXPECT_EQ("Current",SimulationReport(scene).at("validity").To<Text>());
    EXPECT_EQ(nullptr,GetModuleHandleW(L"CamRuntime.dll"));
}

TEST(FiveAxisCollisionRepair, ExplicitLocalBudgetSupportsFractionalAnglesWithoutChangingZeroGlobalLimitsOrManualConstraints)
{
    using namespace collision_repair_tests;
    for(const auto budget:{15.,7.5}){
        SCOPED_TRACE(budget);Scene scene;const auto station=PrepareLocal(scene,true);
        const auto manualAnchor=ProcessItem(scene,station.Intent).at("posePolicy").To<Map>().at("keypoints").To<Array>().front().To<Map>().at("anchor").To<Map>();
        const auto before=Read(scene),pose=ProcessItem(scene,station.Intent).at("posePolicy").To<Map>(),manual=AxisPointRead(scene,station.Intent,manualAnchor);const auto resources=scene.Resources().Count(),history=scene.Database().GetUndoArray().size();
        ExpectUnresolved(RunRepair(scene,station.Index),"CAM_REPAIR_POSE_LIMIT_ZERO");ExpectUnchanged(scene,before,resources,history);
        auto request=Request(scene,station.Index,budget);request["localPoseDeviationAuthorized"]=true;
        const auto task=WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
        const auto result=task.at("result").To<Map>();ASSERT_EQ("Repaired",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
        EXPECT_DOUBLE_EQ(budget,result.at("maxDeviationDeg").To<double>());EXPECT_GT(result.at("maxAppliedAngleDeg").To<double>(),0.);EXPECT_LE(result.at("maxAppliedAngleDeg").To<double>(),budget+1e-8);EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
        const auto afterPose=ProcessItem(scene,station.Intent).at("posePolicy").To<Map>();for(const auto* key:{"angularToleranceRad","leadDeviationLimitRad","sideDeviationLimitRad"}){EXPECT_EQ(pose.at(key),afterPose.at(key));EXPECT_DOUBLE_EQ(0.,afterPose.at(key).To<double>());}
        const auto beforePoints=pose.at("keypoints").To<Array>(),afterPoints=afterPose.at("keypoints").To<Array>();ASSERT_EQ(beforePoints.size()+1,afterPoints.size());
        for(const auto& existing:beforePoints){const auto id=existing.To<Map>().at("id");const auto found=std::find_if(afterPoints.begin(),afterPoints.end(),[&](const Variant& p){return p.To<Map>().at("id")==id;});ASSERT_NE(afterPoints.end(),found);EXPECT_EQ(existing,*found);}
        EXPECT_EQ(manual.at("direction"),AxisPointRead(scene,station.Intent,manualAnchor).at("direction"));
        const auto report=SimulationReport(scene);EXPECT_EQ("Current",report.at("validity").To<Text>());EXPECT_GT(std::stoull(report.at("collisionCount").To<Text>()),0u);EXPECT_GT(std::stoull(report.at("uncheckedCount").To<Text>()),0u);
        const auto oldReport=before.at("setups").To<Array>().front().To<Map>().at("simulationCheck").To<Map>();
        const auto oldSegments=oldReport.at("segments").To<Array>(),segments=report.at("segments").To<Array>();
        std::set<std::size_t> checked;for(const auto& i:report.at("localCheckedSegmentIndices").To<Array>())checked.insert(std::stoull(i.To<Text>()));EXPECT_LT(checked.size(),segments.size());ASSERT_EQ(oldSegments.size(),segments.size());
        for(std::size_t i=0;i<segments.size();++i)if(!checked.contains(i))EXPECT_EQ(oldSegments[i],segments[i]);
    }
}

TEST(FiveAxisCollisionRepair, ManualRapidRepairsOnlyInteriorDirectionsAndRetainsItsExactSpatialPathThroughHistoryAndReopen)
{
    using namespace collision_repair_tests;Scene scene;PrepareLocal(scene);const auto ids=OrderedIDs(Generated(scene));
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,ids,{{"patch",Map{{"angularToleranceRad",0.},{"leadDeviationLimitRad",0.},{"sideDeviationLimitRad",0.}}}}));
    ImportClamp(scene,Array{60.,200.,100.});GenerateRepairAxis(scene);
    auto edit=RapidRequest(scene);edit.erase("commandId");const auto initial=Invoke(&scene,"CamRapid","ReadEditPath",edit);
    const auto start=initial.at("points").To<Array>().front().To<Array>(),end=initial.at("points").To<Array>().back().To<Array>();
    const Array innerPoints{Array{start[0],start[1],70.},Array{start[0],200.,100.},Array{50.,200.,100.},Array{60.,200.,100.},Array{70.,200.,100.},Array{end[0],200.,100.},Array{end[0],end[1],70.}};
    Array innerDirections;for(std::size_t i=0;i<innerPoints.size();++i)innerDirections.emplace_back(Array{0.,0.,1.});
    edit["manualPath"]=Map{{"points",innerPoints},{"directions",innerDirections}};edit["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());Invoke(&scene,"CamRapid","EditPath",edit);
    ASSERT_EQ("Draft",Generated(scene).at("transitions").To<Array>().front().To<Map>().at("geometryStatus").To<Text>());
    CheckRepairCollision(scene);const auto before=Read(scene),original=Generated(scene),oldReport=SimulationReport(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto frames=oldReport.at("frames").To<Array>(),oldSegments=oldReport.at("segments").To<Array>();std::optional<std::size_t> selected;
    for(std::size_t i=0;i<oldSegments.size();++i)if(i+1<frames.size()&&frames[i].To<Map>().at("role").To<Text>()=="Rapid"&&frames[i+1].To<Map>().at("role").To<Text>()=="Rapid"&&oldSegments[i].To<Map>().at("status").To<Text>()=="Collision"){selected=i;break;}
    ASSERT_TRUE(selected)<<"The actual authored Draft TCP path must enter full collision checking";
    const auto task=RunRepair(scene,*selected);ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto result=task.at("result").To<Map>();ASSERT_EQ("Repaired",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);EXPECT_EQ("1",result.at("changedRapidCount").To<Text>());EXPECT_TRUE(result.at("changedIntentIds").To<Array>().empty());EXPECT_GT(result.at("maxAppliedAngleDeg").To<double>(),0.);EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    const auto repaired=Generated(scene),report=SimulationReport(scene),oldRapid=original.at("transitions").To<Array>().front().To<Map>(),rapid=repaired.at("transitions").To<Array>().front().To<Map>();
    EXPECT_EQ(original.at("operations"),repaired.at("operations"));EXPECT_EQ(oldRapid.at("points"),rapid.at("points"));EXPECT_EQ("Generated",rapid.at("geometryStatus").To<Text>());EXPECT_EQ("LocalSmoothOrientationRepair",rapid.at("planningMethod").To<Text>());
    const auto oldDirections=oldRapid.at("directions").To<Array>(),directions=rapid.at("directions").To<Array>();EXPECT_EQ(oldDirections.front(),directions.front());EXPECT_EQ(oldDirections.back(),directions.back());EXPECT_NE(oldDirections,directions);EXPECT_EQ(innerPoints,rapid.at("manualPath").To<Map>().at("points").To<Array>());
    EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));for(const auto& id:ids){const auto pose=ProcessItem(scene,id).at("posePolicy").To<Map>();for(const auto* key:{"angularToleranceRad","leadDeviationLimitRad","sideDeviationLimitRad"})EXPECT_DOUBLE_EQ(0.,pose.at(key).To<double>());}
    EXPECT_EQ("Current",report.at("validity").To<Text>());EXPECT_GT(std::stoull(report.at("collisionCount").To<Text>()),0u);EXPECT_GT(std::stoull(report.at("uncheckedCount").To<Text>()),0u);
    const auto segments=report.at("segments").To<Array>();std::set<std::size_t> checked;for(const auto& i:report.at("localCheckedSegmentIndices").To<Array>())checked.insert(std::stoull(i.To<Text>()));EXPECT_TRUE(checked.contains(*selected));ASSERT_EQ(oldSegments.size(),segments.size());EXPECT_LT(checked.size(),segments.size());
    for(std::size_t i=0;i<segments.size();++i)if(!checked.contains(i))EXPECT_EQ(oldSegments[i],segments[i]);
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(original,Generated(scene));EXPECT_EQ(oldReport,SimulationReport(scene));ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(repaired,Generated(scene));EXPECT_EQ(report,SimulationReport(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Rapid local orientation repair";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(repaired,Generated(reopened));EXPECT_EQ(report,SimulationReport(reopened));auto read=RapidRequest(reopened);read.erase("commandId");read["policy"]=rapid.at("policy");read["manualPath"]=rapid.at("manualPath");const auto restored=Invoke(&reopened,"CamRapid","ReadEditPath",read);EXPECT_EQ(rapid.at("points"),restored.at("points"));EXPECT_EQ(rapid.at("directions"),restored.at("directions"));std::filesystem::remove(path);
}
