#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
namespace fixture_skeleton_tests {
iCAX::Geometry::SectionPlateInput Input(const std::shared_ptr<iCAX::Geometry::ISectionPlateService>& service,double length=800,double width=160){
    auto in=ClearanceInput();in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakeBox(gp_Pnt(-length/2,-width/2,0),length,width,1).Shape(),"Skeleton test","generated",.05));
    in.SolidBase=true;in.Automatic=true;in.Pattern="Skeleton";in.JointMode="TabSlot";in.MaximumSupportSpan=200;in.BaseDrop=50;in.WindowMode="Automatic";in.WindowWeb=8;
    in.FaceIDs=service->FindLowerEnvelopeFaces(in,{});in.Stations=service->SuggestStations(in,in.MaximumSupportSpan);return in;
}
std::vector<double> Bearings(const iCAX::Geometry::SectionPlateInput& in,const std::vector<iCAX::Geometry::SectionPlate>& parts){std::vector<double> result;for(const auto& p:parts)if(!p.Base&&!p.Connection)result.push_back(in.Stations[p.StationIndex].Coordinate);return result;}
}
TEST(FiveAxisFixtureSkeleton, SparseFrameHasRealContactsAndLowerNonContactConnections){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service);const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto parts=service->Build(in,{});
    EXPECT_TRUE(parts.back().Supported);EXPECT_LE(parts.size(),10u);EXPECT_LT(parts.size(),in.Stations.size()+1);size_t bearings=0,connections=0;
    for(const auto& part:parts){const auto native=iCAX::OpenCascade::BuildOpenCascadeShape(part.Geometry);ASSERT_TRUE(native.bOK);EXPECT_TRUE(BRepCheck_Analyzer(native.Shape).IsValid());if(part.Base)continue;
        if(part.Connection){++connections;EXPECT_TRUE(part.Contacts.empty());EXPECT_EQ(part.ContactLength,0);EXPECT_GE(part.Distance+1e-6,in.Gap);size_t crossing=0;for(const auto& j:part.Joints)if(j.Type=="LapTop"||j.Type=="LapBottom")++crossing;EXPECT_GE(crossing,2u);
            for(auto p:part.Outline)EXPECT_LE(part.Frame[11]+p[1],-in.Gap+1e-7);
        }else{++bearings;EXPECT_FALSE(part.Contacts.empty());EXPECT_GT(part.ContactLength,0);}
    }
    EXPECT_GE(bearings,2u);EXPECT_EQ(connections,2u);EXPECT_LE(parts.back().MaximumUnsupportedSpan,in.MaximumSupportSpan+1e-6);EXPECT_LE(parts.back().MaximumEndOverhang,in.MaximumSupportSpan/2+1e-6);
    EXPECT_TRUE(parts.back().WindowHoleIndices.empty());EXPECT_EQ(parts.back().Holes.size(),parts.back().Joints.size());
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
}
TEST(FiveAxisFixtureSkeleton, MissingContactUsesSpareStationsAndUnbridgeableGapFails){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service);const auto before=service->Build(in,{});const auto initial=fixture_skeleton_tests::Bearings(in,before);ASSERT_GE(initial.size(),3u);const double blocked=initial[initial.size()/2];
    in.KeepOuts={ClearanceBox(blocked-10,-100,-3,blocked+10,100,3,.2)};const auto after=service->Build(in,{});const auto revised=fixture_skeleton_tests::Bearings(in,after);EXPECT_NE(initial,revised);EXPECT_TRUE(after.back().Supported);EXPECT_LE(after.back().MaximumUnsupportedSpan,in.MaximumSupportSpan+1e-6);
    for(auto x:revised)EXPECT_GT(std::abs(x-blocked),10.);const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);
    in.KeepOuts={ClearanceBox(-220,-100,-3,220,100,3,.2)};EXPECT_THROW(service->Build(in,{}),std::runtime_error);EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
}
TEST(FiveAxisFixtureSkeleton, LongAxisAndSupportDirectionRotateTogether){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service,160,800);const auto baseline=service->Build(in,{});
    for(const auto& s:in.Stations)if(!s.Connection)EXPECT_FALSE(s.AlongX);
    gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,0,0)),.43);auto rotated=in;rotated.Transform=DirectionMatrix(rotation);const auto axis=gp_Dir(0,0,1).Transformed(rotation);rotated.SupportDirection={axis.X(),axis.Y(),axis.Z()};rotated.Stations=service->SuggestStations(rotated,in.MaximumSupportSpan);const auto parts=service->Build(rotated,{});ASSERT_EQ(parts.size(),baseline.size());
    for(size_t i=0;i<parts.size();++i){EXPECT_EQ(parts[i].Connection,baseline[i].Connection);const auto frame=DirectionMatrix(rotation*DirectionTransform(baseline[i].Frame));for(size_t j=0;j<16;++j)EXPECT_NEAR(frame[j],parts[i].Frame[j],1e-5);}
    EXPECT_TRUE(parts.back().Supported);
}
TEST(FiveAxisFixtureSkeleton, WeldedCrossingsHaveNoInterpenetratingMaterial){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service,500);in.JointMode="Weld";const auto parts=service->Build(in,{});EXPECT_TRUE(parts.back().Supported);
    for(const auto& rail:parts)if(rail.Connection)for(const auto& support:parts)if(!support.Base&&!support.Connection){const auto a=iCAX::OpenCascade::BuildOpenCascadeShape(rail.Geometry),b=iCAX::OpenCascade::BuildOpenCascadeShape(support.Geometry);GProp_GProps common;BRepGProp::VolumeProperties(BRepAlgoAPI_Common(a.Shape,b.Shape).Shape(),common);EXPECT_LT(std::abs(common.Mass()),1e-5);}
}
TEST(FiveAxisFixtureSkeleton, SolidBaseIgnoresBeamReservationsButKeepsPhysicalObstacles){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service);auto reservation=ClearanceBox(-399,-79,-54,-395,-75,-51,.01);reservation.AffectsBase=false;in.KeepOuts={reservation};const auto parts=service->Build(in,{});
    EXPECT_TRUE(parts.back().WindowHoleIndices.empty());EXPECT_EQ(parts.back().Holes.size(),parts.back().Joints.size());
    in.KeepOuts.front().AffectsBase=true;EXPECT_THROW(service->Build(in,{}),std::runtime_error);
}
TEST(FiveAxisFixtureSkeleton, DraftConfirmationHistoryAndReopenPreserveConnections){
    Scene scene;File source(BRepPrimAPI_MakeBox(gp_Pnt(-400,-80,0),800,160,1).Shape());Imported(scene,source);auto recipe=PlateRecipe(scene);recipe["layout"]=Text("Automatic");recipe["pattern"]=Text("Skeleton");recipe["stations"]=Array{};recipe["spacing"]=200.;recipe["jointMode"]=Text("TabSlot");recipe["windowMode"]=Text("Automatic");recipe["windowWeb"]=8.;
    auto request=FixtureRequest(scene);request["recipe"]=recipe;request["newUnit"]=true;const auto before=Read(scene);const auto task=Invoke(&scene,"CamFixture","Generate",request),done=Wait(scene,task);ASSERT_EQ(done.at("status").To<Text>(),"Completed")<<iCAX::Data::VariantSerializer::Serialize(done);EXPECT_EQ(Read(scene),before);
    const auto candidate=FixtureQueryDesign(scene,Map{{"taskId",task.at("taskId")}}).at("candidate").To<Map>();size_t connections=0;for(auto v:candidate.at("parts").To<Array>()){const auto p=v.To<Map>();if(p.at("role").To<Text>()=="Connection"){++connections;EXPECT_TRUE(p.at("contactPoints").To<Array>().empty());EXPECT_GE(p.at("minimumDistance").To<double>(),.2-1e-6);}}EXPECT_EQ(connections,2u);
    EXPECT_LE(candidate.at("supportCheck").To<Map>().at("maximumSpanMm").To<double>(),200.+1e-6);auto confirm=FixtureRequest(scene);confirm["taskId"]=task.at("taskId");confirm["candidateId"]=candidate.at("id");confirm["supportReviewed"]=true;Invoke(&scene,"CamFixture","Confirm",confirm);const auto committed=Read(scene);EXPECT_EQ(committed.at("workpieces").To<Array>().front().To<Map>().at("geometryRef"),before.at("workpieces").To<Array>().front().To<Map>().at("geometryRef"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(Read(scene).at("fixtures"),before.at("fixtures"));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(Read(scene).at("fixtures"),committed.at("fixtures"));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Sparse skeleton";auto path=source.Path;path.replace_extension(".skeleton.i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(Read(reopened).at("fixtureUnits"),committed.at("fixtureUnits"));std::filesystem::remove(path);
}
TEST(FiveAxisFixtureSkeleton, DISABLED_RealHatRailBenchmark){
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path("TestData/FA/Video_HatRail_1200x220x70_t1p2.step");while(!root.empty()&&!std::filesystem::exists(root/relative)){const auto parent=root.parent_path();if(parent==root)break;root=parent;}ASSERT_TRUE(std::filesystem::exists(root/relative));Scene scene;auto request=FixtureRequest(scene);const auto path=(root/relative).u8string();request["sourcePath"]=Text(reinterpret_cast<const char*>(path.data()),path.size());ASSERT_EQ(Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request)).at("status").To<Text>(),"Completed");
    const auto state=Read(scene),ref=state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();iCAX::Geometry::SectionPlateInput in;in.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));const auto transform=state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();for(size_t i=0;i<16;++i)in.Transform[i]=transform[i].To<double>();
    in.SolidBase=true;in.Automatic=true;in.Pattern="Skeleton";in.JointMode="TabSlot";in.TenonRadius=1;in.MaximumSupportSpan=220;in.WindowMode="Automatic";in.WindowWeb=8;in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();in.FaceIDs=service->FindLowerEnvelopeFaces(in,{});in.Stations=service->SuggestStations(in,in.MaximumSupportSpan);const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto started=std::chrono::steady_clock::now();const auto parts=service->Build(in,{});const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();ASSERT_TRUE(parts.back().Supported);EXPECT_LE(parts.size(),12u);EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));Array snapshots;size_t supports=0,connections=0;for(const auto& part:parts){auto snapshot=fixture_parallel_tests::Snapshot(part);snapshot["connection"]=part.Connection;snapshots.emplace_back(snapshot);if(!part.Base){if(part.Connection)++connections;else ++supports;}}
    Array faces;const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);
    for(const auto& [id,face]:built.Faces){Bnd_Box box;BRepBndLib::AddOptimal(face.Moved(TopLoc_Location(DirectionTransform(in.Transform))),box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);faces.emplace_back(Map{{"id",id},{"selected",std::find(in.FaceIDs.begin(),in.FaceIDs.end(),id)!=in.FaceIDs.end()},{"bounds",Array{x0,y0,z0,x1,y1,z1}}});}
    for(const auto& part:parts)if(!part.Base&&!part.Connection){EXPECT_LT(part.Span[0],-100)<<part.StationIndex;EXPECT_GT(part.Span[1],100)<<part.StationIndex;}
    const Map report{{"passed",!HasFailure()},{"faces",faces},{"buildMs",elapsed},{"parts",snapshots},{"supportCount",static_cast<UInt>(supports)},{"connectionCount",static_cast<UInt>(connections)},{"maximumSpan",parts.back().MaximumUnsupportedSpan},{"maximumOverhang",parts.back().MaximumEndOverhang},{"sourceDigest",fixture_parallel_tests::Digest(original)}};const auto json=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);const auto destination=fixture_parallel_tests::Environment("ICAX_FIXTURE_PARALLEL_REPORT");if(!destination.empty()){std::ofstream file(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(destination.data()),destination.size())),std::ios::binary);file<<json;ASSERT_TRUE(file.good());}std::cout<<"fixture skeleton benchmark "<<elapsed<<" ms supports="<<supports<<" connections="<<connections<<std::endl;
}
TEST(FiveAxisFixtureCounts, GeometryRecommendsCountsAndRequestedCountsAreExact){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service);in.CountLayout=true;
    auto suggested=service->SuggestStations(in,220);size_t supports=0,connections=0;
    for(const auto& s:suggested){if(s.Connection)connections=std::max(connections,s.LayoutIndex+1);else supports=std::max(supports,s.LayoutIndex+1);}
    EXPECT_EQ(supports,5u);EXPECT_EQ(connections,2u);
    in.SupportCount=3;in.ConnectionCount=3;in.Stations=service->SuggestStations(in,220);const auto parts=service->Build(in,{});
    EXPECT_EQ(std::count_if(parts.begin(),parts.end(),[](const auto& p){return !p.Base&&!p.Connection;}),3);EXPECT_EQ(std::count_if(parts.begin(),parts.end(),[](const auto& p){return p.Connection;}),3);
    EXPECT_TRUE(parts.back().Supported);EXPECT_GT(parts.back().MaximumUnsupportedSpan,220.);
}
TEST(FiveAxisFixtureCounts, FixedPositionConflictCannotSilentlyReduceCounts){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service);in.CountLayout=true;in.SupportCount=4;in.ConnectionCount=2;
    in.SupportPositions={-300,-100,100,300};in.ConnectionPositions={-55,55};in.Stations=service->SuggestStations(in,220);
    auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),7u);
    auto forbidden=ClearanceBox(-105,-100,-51,-95,100,5,.2);forbidden.AffectsBase=false;in.KeepOuts={forbidden};
    EXPECT_THROW(service->Build(in,{}),std::runtime_error);
    in.KeepOuts.clear();in.SupportPositions={-300,-100};EXPECT_THROW(service->SuggestStations(in,220),std::invalid_argument);
}
TEST(FiveAxisFixtureCounts, TaperedPartKeepsEveryFixedStationAndConnectsRailsBelowTheWorkpiece){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    auto in=fixture_skeleton_tests::Input(service);BRepBuilderAPI_MakePolygon boundary;
    for(const auto p:std::vector<std::array<double,2>>{{-400,-30},{-100,-100},{100,-100},{400,-30},{400,30},{100,100},{-100,100},{-400,30}})boundary.Add(gp_Pnt(p[0],p[1],0));boundary.Close();
    const auto work=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(boundary.Wire()).Face(),gp_Vec(0,0,1)).Shape();
    in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(work,"Tapered bearing","generated",.05));
    in.CountLayout=true;in.SupportCount=5;in.ConnectionCount=2;in.WindowMode="None";in.FaceIDs=service->FindLowerEnvelopeFaces(in,{});in.Stations=service->SuggestStations(in,220);
    const auto before=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto parts=service->Build(in,{});ASSERT_EQ(8u,parts.size());EXPECT_TRUE(parts.back().Supported);
    size_t bearings=0,rails=0;
    for(const auto& part:parts){if(part.Base){EXPECT_TRUE(part.WindowHoleIndices.empty());continue;}const auto& station=in.Stations[part.StationIndex];EXPECT_DOUBLE_EQ(station.NominalCoordinate,station.Coordinate);
        const auto shape=ClearanceShape(part);EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());GProp_GProps common;BRepGProp::VolumeProperties(BRepAlgoAPI_Common(shape,work).Shape(),common);EXPECT_LT(std::abs(common.Mass()),1e-6);
        size_t crossings=0;for(const auto& joint:part.Joints)if(joint.Type=="LapTop"||joint.Type=="LapBottom")++crossings;
        if(part.Connection){++rails;EXPECT_EQ(5u,crossings);EXPECT_TRUE(part.Contacts.empty());}
        else {++bearings;EXPECT_EQ(2u,crossings);EXPECT_FALSE(part.Contacts.empty());EXPECT_LT(part.Span[0],-99);EXPECT_GT(part.Span[1],99);}
    }
    EXPECT_EQ(5u,bearings);EXPECT_EQ(2u,rails);EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
}
TEST(FiveAxisFixtureCounts, CurvedSheetUsesExactMassAndBaseBoundInRotatedSupportFrame){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service);
    const auto work=FixtureCurvedSheet();in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(work,"Curved sheet count","generated",.05));
    in.FaceIDs.clear();for(const auto& face:in.Geometry->Faces)in.FaceIDs.push_back(face.Id);in.CountLayout=true;in.SupportCount=3;in.ConnectionCount=2;in.WindowMode="None";
    const auto before=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);GProp_GProps properties;BRepGProp::SurfaceProperties(work,properties);const auto centroid=properties.CentreOfMass();
    for(double angle:{0.,.47}){gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)),angle);in.Transform=DirectionMatrix(rotation);const auto direction=gp_Dir(0,0,1).Transformed(rotation);in.SupportDirection={direction.X(),direction.Y(),direction.Z()};
        in.Stations=service->SuggestStations(in,220);const auto parts=service->Build(in,{});ASSERT_EQ(6u,parts.size());const auto& base=parts.back();EXPECT_TRUE(base.Supported);EXPECT_EQ("UniformSheet",base.CenterMethod);EXPECT_EQ(0.,base.MeshDeflection);EXPECT_EQ(in.BaseDrop,base.Distance);
        const auto center=centroid.Transformed(rotation);for(int k=0;k<3;++k)EXPECT_NEAR(center.Coord(k+1),base.CenterOfMass[k],1e-6);
        const auto world=work.Moved(TopLoc_Location(rotation));for(const auto& part:parts){const auto shape=ClearanceShape(part);EXPECT_TRUE(BRepCheck_Analyzer(shape).IsValid());BRepExtrema_DistShapeShape distance(shape,world);ASSERT_TRUE(distance.IsDone());
            if(part.Base)EXPECT_GE(distance.Value()+1e-6,in.BaseDrop);else if(part.Connection)EXPECT_GE(distance.Value()+1e-6,in.Gap);else {EXPECT_LE(distance.Value(),1e-6);EXPECT_DOUBLE_EQ(part.TopRetreat,0.);EXPECT_FALSE(part.Contacts.empty());}}
    }
    EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
}
TEST(FiveAxisFixtureCounts, DISABLED_RealPillarCountLayoutHasValidNonIntersectingPlates){
    auto root=std::filesystem::current_path();const auto relative=std::filesystem::path(u8"TestData/FA/汽车/04_Pillar_Reinforcement_900x300_t1p4.step");
    while(!root.empty()&&!std::filesystem::exists(root/relative)){const auto parent=root.parent_path();if(parent==root)break;root=parent;}
    ASSERT_TRUE(std::filesystem::exists(root/relative));Scene scene;auto request=FixtureRequest(scene);const auto source=(root/relative).u8string();
    request["sourcePath"]=Text(reinterpret_cast<const char*>(source.data()),source.size());ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamWorkpiece","Import",request)).at("status").To<Text>());
    const auto state=Read(scene),workpiece=state.at("workpieces").To<Array>().front().To<Map>(),ref=workpiece.at("geometryRef").To<Map>();
    iCAX::Geometry::SectionPlateInput in;in.Geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto transform=state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();for(size_t i=0;i<16;++i)in.Transform[i]=transform[i].To<double>();
    in.FaceIDs={3,4,7,8,19,23,26,27,28,29,30,32,34,36,38,39,43,44,45,46,47,48,49,51,53,54,56,58,60,62,63,64,65,66};
    if(!fixture_parallel_tests::Environment("ICAX_FIXTURE_PILLAR_SINGLE_FACE").empty())in.FaceIDs={66};
    in.Automatic=true;in.Pattern="Skeleton";in.CountLayout=true;in.SupportCount=5;in.ConnectionCount=2;in.SolidBase=true;in.JointMode="TabSlot";
    in.WindowMode="Automatic";in.WindowWeb=10;in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;in.OpeningReliefDepth=5;
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();in.Stations=service->SuggestStations(in,220);
    const auto before=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto started=std::chrono::steady_clock::now();const auto parts=service->Build(in,{});
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();ASSERT_EQ(8u,parts.size());EXPECT_TRUE(parts.back().Supported);
    const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);ASSERT_TRUE(built.bOK);const auto work=built.Shape.Moved(TopLoc_Location(DirectionTransform(in.Transform)));
    Array audit;size_t bearings=0,rails=0;std::vector<TopoDS_Shape> auditedSolids;
    for(const auto& part:parts){
        std::cout<<"Pillar BRep audit "<<(part.Base?"base":part.Connection?"connection":"support")<<' '<<part.StationIndex<<std::endl;
        const auto shape=ClearanceShape(part);const bool valid=BRepCheck_Analyzer(shape).IsValid();EXPECT_TRUE(valid)<<part.StationIndex;
        BRepExtrema_DistShapeShape distance;distance.LoadS1(shape);distance.LoadS2(work);distance.SetMultiThread(false);ASSERT_TRUE(distance.Perform());EXPECT_FALSE(distance.InnerSolution());if(part.Base||part.Connection)EXPECT_GT(distance.Value(),1e-7)<<part.StationIndex;else EXPECT_LE(distance.Value(),1e-5)<<part.StationIndex;
        const auto intersectionVolume=[](const TopoDS_Shape& a,const TopoDS_Shape& b){BRepAlgoAPI_Common overlap;NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(a);tools.Append(b);overlap.SetArguments(arguments);overlap.SetTools(tools);overlap.SetNonDestructive(true);overlap.SetRunParallel(false);overlap.Build();if(!overlap.IsDone())throw std::runtime_error("Independent intersection failed");GProp_GProps volume;BRepGProp::VolumeProperties(overlap.Shape(),volume);return std::abs(volume.Mass());};
        const double workpieceOverlap=intersectionVolume(shape,work);EXPECT_LE(workpieceOverlap,1e-7)<<part.StationIndex;
        for(const auto& other:auditedSolids)EXPECT_LE(intersectionVolume(shape,other),1e-7)<<part.StationIndex;auditedSolids.push_back(shape);
        size_t crossings=0;for(const auto& joint:part.Joints)if(joint.Type=="LapTop"||joint.Type=="LapBottom")++crossings;
        if(part.Base){EXPECT_TRUE(part.WindowHoleIndices.empty());EXPECT_EQ(part.Holes.size(),part.Joints.size());EXPECT_DOUBLE_EQ(part.Distance,in.BaseDrop);EXPECT_GE(distance.Value()+1e-6,in.BaseDrop);}
        else {const auto& station=in.Stations[part.StationIndex];EXPECT_DOUBLE_EQ(station.NominalCoordinate,station.Coordinate);EXPECT_DOUBLE_EQ(part.MeshDeflection,0.);
            if(part.Connection){++rails;EXPECT_EQ(5u,crossings);EXPECT_TRUE(part.Contacts.empty());EXPECT_GE(part.Distance,in.Gap);EXPECT_GE(distance.Value()+1e-6,in.Gap);}
            else {++bearings;EXPECT_EQ(2u,crossings);EXPECT_FALSE(part.Contacts.empty());EXPECT_LE(part.Distance,2*part.TopRetreat+.001);EXPECT_LE(distance.Value(),2*part.TopRetreat+.001);}}
        audit.emplace_back(Map{{"station",static_cast<UInt>(part.StationIndex)},{"base",part.Base},{"connection",part.Connection},{"validBRep",valid},{"exactDistanceMm",distance.Value()},{"workpieceIntersectionVolumeMm3",workpieceOverlap},{"innerSolution",distance.InnerSolution()},{"crossingCount",static_cast<UInt>(crossings)}});
        std::cout<<"Pillar BRep distance "<<distance.Value()<<" mm"<<std::endl;
    }
    EXPECT_EQ(5u,bearings);EXPECT_EQ(2u,rails);EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
    const Map report{{"passed",!HasFailure()},{"buildMs",elapsed},{"parts",audit},{"sourceDigest",fixture_parallel_tests::Digest(before)}};
    const auto destination=fixture_parallel_tests::Environment("ICAX_FIXTURE_PARALLEL_REPORT");if(!destination.empty()){std::ofstream file(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(destination.data()),destination.size())),std::ios::binary);file<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);ASSERT_TRUE(file.good());}
    std::cout<<"Real Pillar count layout build "<<elapsed<<" ms; exact BRep audit "<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report)<<std::endl;
}
TEST(FiveAxisFixtureCounts, NativeRecipeRoundTripsNumericGroupsAndHasNoSpacingInput){
    Scene scene;File source(BRepPrimAPI_MakeBox(gp_Pnt(-400,-80,0),800,160,1).Shape());const auto imported=Imported(scene,source);
    auto facesRequest=FixtureRequest(scene);facesRequest["placementId"]=imported.at("placementId");facesRequest["supportDirection"]=Array{0.,0.,1.};
    const auto faces=Wait(scene,Invoke(&scene,"CamFixture","FindSupportFaces",facesRequest));ASSERT_EQ(faces.at("status").To<Text>(),"Completed");
    auto recipe=PlateRecipe(scene);recipe["supportFaces"]=Invoke(&scene,"CamFixture","ReadSupportSelection",{{"epoch",faces.at("epoch")},{"taskId",faces.at("taskId")}}).at("supportFaces");recipe["layout"]=Text("Automatic");recipe["pattern"]=Text("Skeleton");recipe["stations"]=Array{};recipe.erase("spacing");recipe["supportCount"]=4.;recipe["connectionCount"]=3.;recipe["supportPositions"]=Array{};recipe["connectionPositions"]=Array{};recipe["jointMode"]=Text("TabSlot");recipe["windowMode"]=Text("None");
    recipe["tenonRadius"]=1.;auto request=FixtureRequest(scene);request["recipe"]=recipe;const auto before=Read(scene);const auto info=Invoke(&scene,"CamFixture","SuggestLayout",request);EXPECT_EQ(info.at("supportCount").To<UInt>(),4u);EXPECT_EQ(info.at("connectionCount").To<UInt>(),3u);EXPECT_EQ(Read(scene),before);
    request["newUnit"]=true;const auto done=Wait(scene,Invoke(&scene,"CamFixture","Generate",request));ASSERT_EQ(done.at("status").To<Text>(),"Completed")<<iCAX::Data::VariantSerializer::Serialize(done);
    const auto design=FixtureQueryDesign(scene,Map{{"taskId",done.at("taskId")}});EXPECT_FALSE(design.at("recipe").To<Map>().contains("spacing"));EXPECT_EQ(design.at("candidate").To<Map>().at("parts").To<Array>().size(),8u);EXPECT_EQ(Read(scene),before);
}
TEST(FiveAxisFixtureCounts, CommandReceiptsAndQueriesHaveSeparateResponsibilities){
    Scene scene;File source(BRepPrimAPI_MakeBox(gp_Pnt(-200,-60,0),400,120,1).Shape());Imported(scene,source);auto recipe=PlateRecipe(scene);
    recipe["layout"]=Text("Automatic");recipe["pattern"]=Text("Skeleton");recipe["supportCount"]=3.;recipe["connectionCount"]=2.;recipe["stations"]=Array{};recipe["jointMode"]=Text("TabSlot");recipe["tenonRadius"]=1.;
    auto request=FixtureRequest(scene);request["recipe"]=recipe;request["newUnit"]=true;const auto task=Wait(scene,Invoke(&scene,"CamFixture","Generate",request));ASSERT_EQ("Completed",task.at("status").To<Text>());
    const auto generated=task.at("result").To<Map>();EXPECT_EQ(generated.size(),3u);EXPECT_FALSE(generated.contains("design"));EXPECT_FALSE(generated.contains("parts"));EXPECT_LT(iCAX::Data::VariantSerializer::Serialize(generated).size(),1024u);
    auto confirm=FixtureRequest(scene);confirm["taskId"]=task.at("taskId");confirm["candidateId"]=generated.at("candidateId");confirm["supportReviewed"]=true;const auto receipt=Invoke(&scene,"CamFixture","Confirm",confirm);EXPECT_EQ(receipt.size(),3u);EXPECT_FALSE(receipt.contains("parts"));EXPECT_LT(iCAX::Data::VariantSerializer::Serialize(receipt).size(),1024u);
    const auto before=Read(scene),projection=Invoke(&scene,"CamScene","ReadState",{}),unit=projection.at("fixtureUnits").To<Array>().front().To<Map>();const auto assembly=unit.at("design").To<Map>().at("assembly").To<Map>();EXPECT_FALSE(assembly.contains("parts"));EXPECT_EQ(assembly.at("partCount").To<UInt>(),6u);
    auto query=FixtureRequest(scene);query["unitId"]=unit.at("id");const auto design=Invoke(&scene,"CamFixture","ReadDesign",query);query["snapshot"]=design.at("snapshot");const auto rows=Invoke(&scene,"CamFixture","QueryParts",query).at("items").To<Array>();ASSERT_EQ(rows.size(),6u);
    for(const auto& value:rows){const auto row=value.To<Map>();for(const auto* heavy:{"outline","holes","exactProfile","contactPoints"})EXPECT_FALSE(row.contains(heavy));EXPECT_TRUE(row.contains("geometryRef"));EXPECT_TRUE(row.contains("previewRef"));}
    query["partId"]=rows.front().To<Map>().at("partId");const auto metadata=Invoke(&scene,"CamFixture","PartProfile",query);EXPECT_FALSE(metadata.contains("profile"));EXPECT_TRUE(metadata.contains("profileResourceRef"));const auto body=FixtureQueryDocument(scene,metadata.at("profileResourceRef").To<Map>());EXPECT_FALSE(body.at("profile").To<Map>().at("Boundaries").To<Array>().empty());
    EXPECT_EQ(before,Read(scene));query["snapshot"]=Text("stale");EXPECT_THROW(Invoke(&scene,"CamFixture","QueryParts",query),std::exception);
}
TEST(FiveAxisFixtureCounts, DISABLED_SavedPillarFirstBearingDistanceDiagnostic){
    const auto path=fixture_parallel_tests::Environment("ICAX_FIXTURE_REPLAY_PROJECT");ASSERT_FALSE(path.empty());iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(std::filesystem::path(path));const auto info=prepared.Result().Info;Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);const auto state=Read(scene);
    const auto shape=[&](const Map& ref){const auto& version=ref.at("version");const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),version.Is<Text>()?std::stoull(version.To<Text>()):version.To<UInt>());return iCAX::OpenCascade::BuildOpenCascadeShape(*geometry).Shape;};
    const auto raw=shape(state.at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>());const auto matrix=state.at("placements").To<Array>().front().To<Map>().at("transform").To<Array>();std::array<double,16> transform;for(size_t i=0;i<16;++i)transform[i]=matrix[i].To<double>();const auto work=raw.Moved(TopLoc_Location(DirectionTransform(transform)));
    GProp_GProps mass;BRepGProp::VolumeProperties(work,mass);BRepClass3d_SolidClassifier classifier(work);classifier.PerformInfinitePoint(1e-7);std::cout<<"Pillar source signed volume "<<mass.Mass()<<" infinite state "<<classifier.State()<<std::endl;
    const auto part=state.at("fixtureUnits").To<Array>().front().To<Map>().at("design").To<Map>().at("assembly").To<Map>().at("parts").To<Array>().front().To<Map>();const auto plate=shape(part.at("geometryRef").To<Map>());
    for(double drop:{0.,.005,.01}){gp_Trsf shift;shift.SetTranslation(gp_Vec(0,0,-drop));BRepExtrema_DistShapeShape distance;distance.LoadS1(plate.Moved(TopLoc_Location(shift)));distance.LoadS2(work);distance.SetMultiThread(true);ASSERT_TRUE(distance.Perform());const auto p=distance.PointOnShape1(1);std::cout<<"Pillar diagnostic drop "<<drop<<" distance "<<distance.Value()<<" inner "<<distance.InnerSolution()<<" point "<<p.X()<<','<<p.Y()<<','<<p.Z()<<std::endl;}
}
TEST(FiveAxisFixtureCounts, OpeningRecessKeepsBoardPositionsAndLowerConnectingMaterial){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service,120,120);const auto perforated=OpeningPerforatedInput(0,18,8);
    in.Geometry=perforated.Geometry;in.FaceIDs=perforated.FaceIDs;in.CountLayout=true;in.SupportCount=3;in.ConnectionCount=2;in.WindowMode="None";in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;in.OpeningReliefDepth=5;
    in.Stations=service->SuggestStations(in,220);ASSERT_EQ(in.Stations.size(),5u);const auto original=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),6u);
    for(const auto& p:parts)if(!p.Base)EXPECT_DOUBLE_EQ(in.Stations[p.StationIndex].Coordinate,in.Stations[p.StationIndex].NominalCoordinate);
    const auto middle=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return !p.Base&&!p.Connection&&std::abs(in.Stations[p.StationIndex].Coordinate)<1e-6;});ASSERT_NE(middle,parts.end());
    EXPECT_GT(middle->KeepOutReliefCount,0u);EXPECT_FALSE(middle->Contacts.empty());EXPECT_LT(middle->Span[0],-59);EXPECT_GT(middle->Span[1],59);
    const auto shape=ClearanceShape(*middle);EXPECT_EQ(JointState(shape,gp_Pnt(0,18,-2)),TopAbs_OUT);EXPECT_EQ(JointState(shape,gp_Pnt(0,18,-15)),TopAbs_IN);EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
}
TEST(FiveAxisFixtureCounts, VentOpeningsPreserveSolidBaseJointsAndAllCounts){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service);in.CountLayout=true;in.SupportCount=4;in.ConnectionCount=2;in.WindowMode="None";in.Stations=service->SuggestStations(in,220);const auto baseline=service->Build(in,{});
    in.WindowMode="Automatic";in.WindowWeb=10;const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),baseline.size());ASSERT_EQ(parts.size(),7u);EXPECT_TRUE(parts.back().Supported);
    for(size_t i=0;i<parts.size();++i){const auto& p=parts[i];if(p.Base){EXPECT_TRUE(p.WindowHoleIndices.empty());EXPECT_EQ(p.Holes,baseline[i].Holes);}else {EXPECT_FALSE(p.WindowHoleIndices.empty())<<i;CheckOpeningWeb(p,in.WindowWeb);}EXPECT_EQ(p.Outline,baseline[i].Outline);EXPECT_EQ(p.Contacts,baseline[i].Contacts);EXPECT_EQ(p.Joints.size(),baseline[i].Joints.size());const auto native=ClearanceShape(p);EXPECT_TRUE(BRepCheck_Analyzer(native).IsValid());
        if(!p.Base)EXPECT_EQ(p.WindowHoleIndices.size(),1u);for(auto index:p.WindowHoleIndices){ASSERT_LT(index,p.Holes.size());const auto& hole=p.Holes[index];ASSERT_GE(hole.size(),4u);double u=0,v=0;for(auto q:hole){u+=q[0]/hole.size();v+=q[1]/hole.size();}const auto& m=p.Frame;EXPECT_EQ(JointState(native,gp_Pnt(m[0]*u+m[1]*v+m[2]*p.Thickness/2+m[3],m[4]*u+m[5]*v+m[6]*p.Thickness/2+m[7],m[8]*u+m[9]*v+m[10]*p.Thickness/2+m[11])),TopAbs_OUT);}
    }
    const auto base=ClearanceShape(parts.back()),priorBase=ClearanceShape(baseline.back());size_t checkedFeet=0;for(const auto& p:parts)if(!p.Base){const auto& m=p.Frame;for(double fraction:{.11,.37,.63,.89}){const double u=p.Span[0]+fraction*(p.Span[1]-p.Span[0]);const gp_Pnt point(m[0]*u+m[2]*p.Thickness/2+m[3],m[4]*u+m[6]*p.Thickness/2+m[7],parts.back().Frame[11]+parts.back().Thickness/2);if(JointState(priorBase,point)==TopAbs_IN){++checkedFeet;EXPECT_EQ(JointState(base,point),TopAbs_IN);}}}EXPECT_GT(checkedFeet,0u);
    in.WindowWeb=2;EXPECT_THROW(service->Build(in,{}),std::invalid_argument);
}
TEST(FiveAxisFixtureCounts, LongAxisYConnectionsAlsoUseOneHorizontalSlot){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=fixture_skeleton_tests::Input(service,160,800);in.BaseDrop=40;in.CountLayout=true;in.SupportCount=4;in.ConnectionCount=2;in.WindowMode="Automatic";in.WindowWeb=10;in.Stations=service->SuggestStations(in,220);const auto parts=service->Build(in,{});ASSERT_EQ(parts.size(),7u);
    for(const auto& p:parts){if(p.Base){EXPECT_TRUE(p.WindowHoleIndices.empty());continue;}ASSERT_EQ(p.WindowHoleIndices.size(),1u);CheckOpeningWeb(p,in.WindowWeb);if(p.Connection){const auto& hole=p.Holes[p.WindowHoleIndices[0]];double x0=1e100,x1=-1e100,y0=1e100,y1=-1e100;for(auto q:hole){x0=std::min(x0,q[0]);x1=std::max(x1,q[0]);y0=std::min(y0,q[1]);y1=std::max(y1,q[1]);}EXPECT_GT(x1-x0,100);EXPECT_LE(x1-x0,120.001);EXPECT_GE((x1-x0)/(y1-y0),9.5);}}
}
