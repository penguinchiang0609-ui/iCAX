// Included after CollisionRepairDomainTests.inc: real imported head and CAD
// fixture meshes, real product collision reports, no mocked certificates.
namespace collision_repair_block_tests {
    using namespace collision_repair_tests;
    struct Fixture {Variant Intent;Map Operation;std::array<Map,4> Controls;std::array<std::size_t,4> Frames{};Array BlockPointIDs;};
    bool SameAnchor(const Map& a,const Map& b){return a.at("sourceSpanId")==b.at("sourceSpanId")&&std::abs(a.at("nativeParameter").To<double>()-b.at("nativeParameter").To<double>())<1e-10;}
    std::size_t SourceFrame(Scene& scene,const Variant& intent,const Map& anchor,bool last=false){
        std::size_t offset=0;std::optional<std::size_t> matched;
        for(const auto& value:Generated(scene).at("operations").To<Array>()){
            const auto op=value.To<Map>();for(const auto& p:op.at("parts").To<Array>()){
                const auto part=p.To<Map>();const auto points=part.at("points").To<Array>();
                if(op.at("intentId")==intent&&part.contains("sourceSamples")){
                    const auto samples=part.at("sourceSamples").To<Array>();for(std::size_t i=0;i<samples.size();++i){const auto sample=samples[i].To<Map>();
                        if(sample.contains("sourceSpanId")&&sample.contains("nativeParameter")&&SameAnchor(anchor,{{"sourceSpanId",sample.at("sourceSpanId")},{"nativeParameter",sample.at("nativeParameter")}})){if(!last)return offset+i;matched=offset+i;}
                    }
                }offset+=points.size();
            }
        }if(matched)return *matched;throw std::runtime_error("Block control is missing from the real generated samples");
    }
    Text StaticStatus(const Map& report,std::size_t frame){
        for(const auto& value:report.at("keypoints").To<Array>()){const auto row=value.To<Map>();if(std::stoull(row.at("frameIndex").To<Text>())==frame)return row.at("status").To<Text>();}
        throw std::runtime_error("Full native check did not visit the block control");
    }
    Fixture Prepare(Scene& scene,bool manualBlock){
        PrepareOrderPart(scene);UseContours(scene);GenerateAxis(scene);Fixture out;bool found=false;
        for(const auto& value:Generated(scene).at("operations").To<Array>()){
            const auto op=value.To<Map>();auto samples=InfluenceSamples(scene,op.at("intentId"));
            std::sort(samples.begin(),samples.end(),[](const auto& a,const auto& b){return a.template To<Map>().at("pathDistanceMm").template To<double>()<b.template To<Map>().at("pathDistanceMm").template To<double>();});
            // A straight part of the 40x40 outer rectangle has controls about
            // 4.44 mm apart. Four interior controls avoid all system endpoints.
            for(std::size_t i=2;i+5<samples.size()&&!found;++i){
                bool suitable=true;for(std::size_t j=0;j<4;++j)out.Controls[j]=samples[i+j].To<Map>();
                const auto a=out.Controls[0].at("point").To<Array>(),b=out.Controls[3].at("point").To<Array>();
                for(std::size_t j=0;j<4;++j){const auto p=out.Controls[j].at("point").To<Array>();
                    suitable&=out.Controls[j].at("anchor").To<Map>().at("sourceSpanId")==out.Controls[0].at("anchor").To<Map>().at("sourceSpanId");
                    suitable&=std::abs(p[2].To<double>()-a[2].To<double>())<1e-9;
                    suitable&=(std::abs(p[0].To<double>()-a[0].To<double>())<1e-9||std::abs(p[1].To<double>()-a[1].To<double>())<1e-9);
                    if(j){const auto prev=out.Controls[j-1].at("point").To<Array>();suitable&=std::hypot(p[0].To<double>()-prev[0].To<double>(),p[1].To<double>()-prev[1].To<double>())>1.;}
                }
                suitable&=(std::abs(b[0].To<double>()-a[0].To<double>())>3.||std::abs(b[1].To<double>()-a[1].To<double>())>3.);
                if(suitable){std::size_t previous=0;for(std::size_t j=0;j<4;++j){const auto index=SourceFrame(scene,op.at("intentId"),out.Controls[j].at("anchor").To<Map>());if(j&&index<=previous)suitable=false;previous=index;}}
                if(suitable){out.Intent=op.at("intentId");out.Operation=op;found=true;}
            }if(found)break;
        }
        if(!found)throw std::runtime_error("Block fixture needs four adjacent straight-edge controls");
        // Nonidentity setup placement exercises local Workpiece directions and
        // Setup obstacle/actual quaternion handling, without tilting the plate.
        auto placement=GenerateRequest(scene);placement["placementId"]=out.Operation.at("placementId");
        placement["transform"]=Array{0.,-1.,0.,100.,1.,0.,0.,-50.,0.,0.,1.,0.,0.,0.,0.,1.};
        Invoke(&scene,"CamPlacement","SetTransform",placement);GenerateAxis(scene);
        // Existing safe manual controls must remain byte-for-byte unchanged.
        for(const auto j:{0u,3u})Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,out.Intent,NewAxisPoint(),out.Controls[j].at("anchor").To<Map>(),Array{0.,0.,1.}));
        if(manualBlock)for(const auto j:{1u,2u}){const auto id=NewAxisPoint();out.BlockPointIDs.push_back(id);Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,out.Intent,id,out.Controls[j].at("anchor").To<Map>(),Array{0.,0.,1.}));}
        GenerateAxis(scene);out.Operation=AxisOperation(Generated(scene),out.Intent);
        File head(BRepPrimAPI_MakeBox(gp_Pnt(-.1,-.1,2.),.2,.2,20.).Shape());
        const auto imported=Wait(scene,Invoke(&scene,"CamHead","Import",ImportTestHead(scene,head)));if(imported.at("status").To<Text>()!="Completed")throw std::runtime_error("Block head import failed");
        auto calibration=CalibrateTestHead(scene);calibration["tcpLocalPosition"]=Array{0.,0.,0.};Invoke(&scene,"CamHead","Configure",calibration);
        for(const auto j:{1u,2u})ImportClamp(scene,SetupPoint(scene,out.Operation,out.Controls[j].at("point").To<Array>()),.15);
        // Retain a real unrelated alarm and the fixture's ungenerated rapid.
        for(const auto& value:Generated(scene).at("operations").To<Array>()){const auto op=value.To<Map>();if(op.at("intentId")!=out.Intent){ImportClamp(scene,SetupPoint(scene,op,MiddleSource(op).second));break;}}
        GenerateAxis(scene);RunCollision(scene);
        for(std::size_t i=0;i<4;++i)out.Frames[i]=SourceFrame(scene,out.Intent,out.Controls[i].at("anchor").To<Map>(),i==0);
        return out;
    }
    void ExpectBlockProof(const Fixture& fixture,const Map& before,const Map& after,const Map& result){
        const auto policy=after.at("localRepairPolicy").To<Map>();EXPECT_TRUE(policy.at("clearBoundaryKeypointsPreserved").To<bool>());EXPECT_FALSE(policy.contains("neighborKeypointsPreserved"));
        const auto search=result.at("poseSearch").To<Map>();EXPECT_EQ("KeypointBlock",search.at("kind").To<Text>());
        EXPECT_EQ(std::to_string(fixture.Frames[0]),search.at("fixedEntryFrameIndex").To<Text>());EXPECT_EQ(std::to_string(fixture.Frames[3]),search.at("fixedExitFrameIndex").To<Text>());
        const auto controls=search.at("collisionControls").To<Array>();ASSERT_EQ(2u,controls.size());
        for(std::size_t j=0;j<2;++j){const auto c=controls[j].To<Map>();EXPECT_EQ(std::to_string(fixture.Frames[j+1]),c.at("sourceFrameIndex").To<Text>());EXPECT_EQ(fixture.Controls[j+1].at("anchor"),c.at("sourceAnchor"));}
        ExpectFixedKeypointBoundaries(before,after,result);
        const auto originalFrames=before.at("frames").To<Array>(),actualFrames=after.at("frames").To<Array>();
        const auto toolAxis=[](const Array& q){const auto x=q[0].To<double>(),y=q[1].To<double>(),z=q[2].To<double>(),w=q[3].To<double>();return std::array<double,3>{2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)};};
        for(std::size_t i=0;i<actualFrames.size();++i){const auto a=toolAxis(originalFrames[i].To<Map>().at("rotation").To<Array>()),b=toolAxis(actualFrames[i].To<Map>().at("rotation").To<Array>());double dot=0;for(int j=0;j<3;++j)dot+=a[j]*b[j];EXPECT_GE(dot,std::cos(15.*3.14159265358979323846/180.)-1e-10)<<"Actual sample deviation at "<<i;}
        const auto checked=after.at("localCheckedSegmentIndices").To<Array>(),oldSegments=before.at("segments").To<Array>(),segments=after.at("segments").To<Array>();std::set<std::size_t> local;
        for(const auto& value:checked){const auto i=std::stoull(value.To<Text>());local.insert(i);EXPECT_EQ("Clear",segments.at(i).To<Map>().at("status").To<Text>());}
        // The whole block is checked even when a shared rotor happens to leave
        // an individual direction unchanged; old Collision rows cannot leak in.
        for(std::size_t i=fixture.Frames[0];i<fixture.Frames[3];++i)EXPECT_TRUE(local.contains(i))<<i;
        ASSERT_EQ(oldSegments.size(),segments.size());EXPECT_LT(local.size(),segments.size());bool collision=false,unchecked=false;
        for(std::size_t i=0;i<segments.size();++i)if(!local.contains(i)){EXPECT_EQ(oldSegments[i],segments[i]);collision|=segments[i].To<Map>().at("status").To<Text>()=="Collision";unchecked|=segments[i].To<Map>().at("status").To<Text>()=="Unchecked";}
        EXPECT_TRUE(collision);EXPECT_TRUE(unchecked);EXPECT_LE(result.at("maxAppliedAngleDeg").To<double>(),15.+1e-8);EXPECT_GT(result.at("maxAppliedAngleDeg").To<double>(),0.);
    }
}
TEST(FiveAxisCollisionRepairBlock, AdjacentCollidingManualControlsKeepIDsAnchorsAndClearBoundariesThroughOneHistoryAndReopen)
{
    using namespace collision_repair_block_tests;const auto id=iCAX::Data::GenerateNewUUID();Scene scene(id,id);const auto f=Prepare(scene,true);
    const auto old=SimulationReport(scene),oldGeometry=Generated(scene),oldPose=ProcessItem(scene,f.Intent).at("posePolicy").To<Map>();
    // This is the exact product gap: the former one-control implementation
    // refused at candidate 0 because the selected control's neighbor collided.
    ASSERT_EQ("Clear",StaticStatus(old,f.Frames[0]));ASSERT_EQ("Collision",StaticStatus(old,f.Frames[1]));ASSERT_EQ("Collision",StaticStatus(old,f.Frames[2]));ASSERT_EQ("Clear",StaticStatus(old,f.Frames[3]));
    const auto history=scene.Database().GetUndoArray().size();auto request=Request(scene,f.Frames[1],15.);request["localPoseDeviationAuthorized"]=true;
    const auto task=WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto result=task.at("result").To<Map>();ASSERT_EQ("Repaired",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    const auto after=SimulationReport(scene),geometry=Generated(scene),pose=ProcessItem(scene,f.Intent).at("posePolicy").To<Map>();ExpectBlockProof(f,old,after,result);EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    const auto beforeOps=oldGeometry.at("operations").To<Array>(),afterOps=geometry.at("operations").To<Array>();ASSERT_EQ(beforeOps.size(),afterOps.size());
    for(std::size_t i=0;i<beforeOps.size();++i){const auto a=beforeOps[i].To<Map>(),b=afterOps[i].To<Map>();EXPECT_EQ(a.at("curveData"),b.at("curveData"));const auto ap=a.at("parts").To<Array>(),bp=b.at("parts").To<Array>();ASSERT_EQ(ap.size(),bp.size());for(std::size_t j=0;j<ap.size();++j)EXPECT_EQ(ap[j].To<Map>().at("points"),bp[j].To<Map>().at("points"));}
    const auto original=oldPose.at("keypoints").To<Array>(),points=pose.at("keypoints").To<Array>();ASSERT_EQ(original.size(),points.size());
    for(const auto& value:original){auto before=value.To<Map>();const auto found=std::find_if(points.begin(),points.end(),[&](const auto& p){return p.template To<Map>().at("id")==before.at("id");});ASSERT_NE(points.end(),found);auto actual=found->To<Map>();
        if(std::find(f.BlockPointIDs.begin(),f.BlockPointIDs.end(),before.at("id"))!=f.BlockPointIDs.end()){EXPECT_NE(before.at("direction"),actual.at("direction"));before.erase("direction");actual.erase("direction");EXPECT_EQ(before,actual);}else EXPECT_EQ(value,*found);
    }
    const auto edits=result.at("adjustments").To<Array>().front().To<Map>().at("keypointEdits").To<Array>();ASSERT_EQ(2u,edits.size());for(const auto& value:edits){const auto e=value.To<Map>();EXPECT_EQ("Updated",e.at("action").To<Text>());EXPECT_NE(f.BlockPointIDs.end(),std::find(f.BlockPointIDs.begin(),f.BlockPointIDs.end(),e.at("pointId")));}
    ASSERT_TRUE(scene.Database().Undo());PumpTasks(scene);EXPECT_EQ(oldPose,ProcessItem(scene,f.Intent).at("posePolicy").To<Map>());EXPECT_EQ(oldGeometry,Generated(scene));EXPECT_EQ(old,SimulationReport(scene));
    ASSERT_TRUE(scene.Database().Redo());PumpTasks(scene);EXPECT_EQ(pose,ProcessItem(scene,f.Intent).at("posePolicy").To<Map>());EXPECT_EQ(geometry,Generated(scene));EXPECT_EQ(after,SimulationReport(scene));
    const auto folder=std::filesystem::path("C:/icax-machine-kinematics/product-fixtures");std::filesystem::create_directories(folder);const auto path=folder/(iCAX::Data::to_string(id)+"-local-block.i5cam");
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=id;info.MainSceneID=id;info.ProjectName="Continuous collision control block";file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(id,id,false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(geometry,Generated(reopened));EXPECT_EQ(after,SimulationReport(reopened));EXPECT_EQ(pose,ProcessItem(reopened,f.Intent).at("posePolicy").To<Map>());std::filesystem::remove(path);
}
TEST(FiveAxisCollisionRepairBlock, AdjacentAutomaticControlsInsertOnlyTheirOverridesAndPreserveAllSafeManualControls)
{
    using namespace collision_repair_block_tests;Scene scene;const auto f=Prepare(scene,false);const auto before=SimulationReport(scene),oldPose=ProcessItem(scene,f.Intent).at("posePolicy").To<Map>();
    ASSERT_EQ("Clear",StaticStatus(before,f.Frames[0]));ASSERT_EQ("Collision",StaticStatus(before,f.Frames[1]));ASSERT_EQ("Collision",StaticStatus(before,f.Frames[2]));ASSERT_EQ("Clear",StaticStatus(before,f.Frames[3]));
    auto request=Request(scene,f.Frames[1],15.);request["localPoseDeviationAuthorized"]=true;const auto task=WaitRepair(scene,Invoke(&scene,"CamSimulation","AutoRepairCollision",request));ASSERT_EQ("Completed",task.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(task);
    const auto result=task.at("result").To<Map>();ASSERT_EQ("Repaired",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);ExpectBlockProof(f,before,SimulationReport(scene),result);
    const auto points=ProcessItem(scene,f.Intent).at("posePolicy").To<Map>().at("keypoints").To<Array>(),old=oldPose.at("keypoints").To<Array>();ASSERT_EQ(old.size()+2,points.size());for(const auto& p:old)EXPECT_NE(points.end(),std::find(points.begin(),points.end(),p));
    const auto edits=result.at("adjustments").To<Array>().front().To<Map>().at("keypointEdits").To<Array>();ASSERT_EQ(2u,edits.size());for(const auto& value:edits)EXPECT_EQ("Inserted",value.To<Map>().at("action").To<Text>());
    for(const auto& value:result.at("poseSearch").To<Map>().at("collisionControls").To<Array>())EXPECT_EQ("",value.To<Map>().at("existingPointId").To<Text>());
}
