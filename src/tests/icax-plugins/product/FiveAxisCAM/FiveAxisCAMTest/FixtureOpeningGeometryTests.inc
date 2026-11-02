#include <BRepAlgoAPI_Fuse.hxx>
// Direct neutral-service tests: no CAM intent or toolpath exists in these cases.
// Include after FixtureClearanceTests.inc for its exact BRep verification helpers.
namespace {
iCAX::Geometry::SectionPlateInput OpeningPerforatedInput(double x,double y,double radius){
    auto in=ClearanceInput();
    const auto face=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),-60,60,-60,60).Face();
    const auto cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,y,-10),gp_Dir(0,0,1)),radius,20).Shape();
    BRepAlgoAPI_Cut perforated(face,cylinder);
    if(!perforated.IsDone()||!BRepCheck_Analyzer(perforated.Shape()).IsValid())throw std::runtime_error("Opening test workpiece invalid");
    in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(perforated.Shape(),"Exact circular opening","generated",.05));
    in.FaceIDs.clear();for(const auto& item:in.Geometry->Faces)in.FaceIDs.push_back(item.Id);
    return in;
}
TopoDS_Wire OpeningPlateWire(const iCAX::Geometry::SectionPlate& plate,const std::vector<std::array<double,2>>& loop){
    BRepBuilderAPI_MakePolygon polygon;const auto& m=plate.Frame;const double depth=plate.Thickness/2;
    for(const auto p:loop)polygon.Add(gp_Pnt(m[0]*p[0]+m[1]*p[1]+m[2]*depth+m[3],m[4]*p[0]+m[5]*p[1]+m[6]*depth+m[7],m[8]*p[0]+m[9]*p[1]+m[10]*depth+m[11]));
    polygon.Close();if(!polygon.IsDone())throw std::runtime_error("Opening test plate wire invalid");return polygon.Wire();
}
void CheckOpeningWeb(const iCAX::Geometry::SectionPlate& plate,double minimum){
    const auto outline=OpeningPlateWire(plate,plate.Outline);std::vector<TopoDS_Wire> previous;
    for(const auto& loop:plate.Holes){
        const auto hole=OpeningPlateWire(plate,loop);BRepExtrema_DistShapeShape outside(hole,outline);
        ASSERT_TRUE(outside.IsDone());EXPECT_GE(outside.Value(),minimum-1e-5);
        for(const auto& earlier:previous){BRepExtrema_DistShapeShape separation(hole,earlier);ASSERT_TRUE(separation.IsDone());EXPECT_GE(separation.Value(),minimum-1e-5);}
        previous.push_back(hole);
    }
}
double OpeningIntersectionVolume(const iCAX::Geometry::SectionPlate& plate,const TopoDS_Shape& corridor){
    BRepAlgoAPI_Common common(ClearanceShape(plate),corridor);if(!common.IsDone())throw std::runtime_error("Opening test common failed");
    GProp_GProps properties;BRepGProp::VolumeProperties(common.Shape(),properties);return std::abs(properties.Mass());
}
}
TEST(FiveAxisFixtureOpeningGeometry, AutomaticWindowsPerforateBaseAndSupportsWithoutLosingBearingBands){
    Scene scene;auto in=ClearanceInput();const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    in.BaseDrop=100;in.AreaWidth=180;in.AreaDepth=180;in.AvoidOpeningBoundaries=true;const auto solid=service->Build(in,{});
    EXPECT_EQ(solid.back().OpeningBoundaryCount,0u);EXPECT_EQ(solid.back().UnclassifiedOpeningFaceCount,0u);
    in.WindowMode="Automatic";in.WindowWeb=8;const auto perforated=service->Build(in,{});
    ASSERT_EQ(perforated.size(),solid.size());ASSERT_GT(perforated.size(),2u);ASSERT_TRUE(perforated.back().Base);EXPECT_TRUE(perforated.back().Supported);
    EXPECT_GE(perforated.back().Holes.size(),2u);size_t supportOpenings=0;
    for(size_t i=0;i<perforated.size();++i){
        const auto& plate=perforated[i];CheckClearancePlate(plate,{});CheckOpeningWeb(plate,in.WindowWeb);
        EXPECT_EQ(plate.Outline,solid[i].Outline);EXPECT_EQ(plate.Joints.size(),solid[i].Joints.size());
        if(!plate.Base){EXPECT_EQ(plate.Contacts,solid[i].Contacts);EXPECT_GE(plate.Holes.size(),2u);supportOpenings+=plate.Holes.size();}
    }
    // Check actual support footprints against the finished base, separately
    // from the contact-hull result. Every original full foot retains material.
    const auto base=ClearanceShape(perforated.back());
    for(const auto& plate:perforated)if(!plate.Base){const auto& m=plate.Frame;
        for(double fraction:{.1,.5,.9}){const double u=plate.Span[0]+fraction*(plate.Span[1]-plate.Span[0]);
            const gp_Pnt point(m[0]*u+m[2]*plate.Thickness/2+m[3],m[4]*u+m[6]*plate.Thickness/2+m[7],perforated.back().Frame[11]+perforated.back().Thickness/2);
            EXPECT_EQ(JointState(base,point),TopAbs_IN);
        }
    }
    RecordProperty("base_windows",static_cast<int>(perforated.back().Holes.size()));RecordProperty("support_windows",static_cast<int>(supportOpenings));
}
TEST(FiveAxisFixtureOpeningGeometry, ExactCircularBoundaryReservesItsWholeVerticalEnvelopeBeforeAnyToolpath){
    Scene scene;auto in=OpeningPerforatedInput(0,18,8);const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    const auto baseline=service->Build(in,{});in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;
    // Analytic bounds of the source circle, including the requested margin.
    // This independent solid spans every support and the complete base depth.
    const auto corridor=BRepPrimAPI_MakeBox(gp_Pnt(-9,9,-in.BaseDrop-in.BaseThickness),18,18,in.BaseDrop+in.BaseThickness+1).Shape();
    double before=0;for(const auto& plate:baseline)before+=OpeningIntersectionVolume(plate,corridor);ASSERT_GT(before,1.);
    const auto protectedParts=service->Build(in,{});ASSERT_GT(protectedParts.size(),2u);EXPECT_TRUE(protectedParts.back().Supported);EXPECT_GT(protectedParts.back().KeepOutReliefCount,0u);
    EXPECT_EQ(protectedParts.back().OpeningBoundaryCount,1u);EXPECT_EQ(protectedParts.back().UnclassifiedOpeningFaceCount,0u);
    size_t relievedSupports=0;for(const auto& plate:protectedParts){CheckClearancePlate(plate,{corridor});if(!plate.Base&&plate.KeepOutReliefCount)++relievedSupports;}
    EXPECT_GT(relievedSupports,0u);EXPECT_EQ(JointState(ClearanceShape(protectedParts.back()),gp_Pnt(0,18,-in.BaseDrop-in.BaseThickness/2)),TopAbs_OUT);
    RecordProperty("baseline_forbidden_volume_mm3",std::to_string(before));RecordProperty("relieved_supports",static_cast<int>(relievedSupports));
}
TEST(FiveAxisFixtureOpeningGeometry, TiltedCircleEnvelopeAlsoClearsAPlateWhoseCentrePlaneMissesTheOpening){
    Scene scene;auto in=OpeningPerforatedInput(0,0,4);const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    const double angle=.3,c=std::cos(angle),s=std::sin(angle),tx=15,ty=7,tz=20,margin=1;
    in.Transform={c,0,s,tx,0,1,0,ty,-s,0,c,tz,0,0,0,1};
    // The analytic world-X bound is tx +/- 4*cos(angle). The middle plate's
    // centre misses its dilated corridor, but 0.75 mm of its thickness enters.
    const double first=tx-4*c-margin,last=tx+4*c+margin,edgeStation=first-.75;
    in.Stations={{tx-35,-100,100,true},{edgeStation,-100,100,true},{tx+35,-100,100,true}};
    const double bottom=tz-60*s-in.BaseDrop-in.BaseThickness,top=tz+4*s+margin;
    const auto corridor=BRepPrimAPI_MakeBox(gp_Pnt(first,ty-5,bottom),last-first,10,top-bottom).Shape();
    const auto baseline=service->Build(in,{});auto prior=std::find_if(baseline.begin(),baseline.end(),[](const auto& plate){return !plate.Base&&plate.StationIndex==1;});
    ASSERT_NE(prior,baseline.end());EXPECT_LT(edgeStation,first);EXPECT_GT(edgeStation+in.Thickness/2,first);ASSERT_GT(OpeningIntersectionVolume(*prior,corridor),.1);
    in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=margin;const auto protectedParts=service->Build(in,{});
    ASSERT_GT(protectedParts.size(),2u);EXPECT_TRUE(protectedParts.back().Supported);
    for(const auto& plate:protectedParts)CheckClearancePlate(plate,{corridor});
    const auto protectedEdge=std::find_if(protectedParts.begin(),protectedParts.end(),[](const auto& plate){return !plate.Base&&plate.StationIndex==1;});
    ASSERT_NE(protectedEdge,protectedParts.end());EXPECT_GT(protectedEdge->KeepOutReliefCount,0u);EXPECT_FALSE(protectedEdge->Contacts.empty());
    RecordProperty("centre_plane_outside_corridor_mm",std::to_string(first-edgeStation));RecordProperty("plate_thickness_overlap_mm",std::to_string(edgeStation+in.Thickness/2-first));
}
TEST(FiveAxisFixtureOpeningGeometry, RationalSplineRepresentationKeepsTheExactPlanarOpeningPreclearance){
    Scene scene;auto in=OpeningPerforatedInput(0,18,8);const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();
    const auto source=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);ASSERT_TRUE(source.bOK);
    BRepBuilderAPI_NurbsConvert nurbs(source.Shape,true);ASSERT_TRUE(nurbs.IsDone());
    in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(nurbs.Shape(),"Exact NURBS opening","generated",.05));
    in.FaceIDs.clear();for(const auto& face:in.Geometry->Faces)in.FaceIDs.push_back(face.Id);
    const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);ASSERT_TRUE(rebuilt.bOK);ASSERT_EQ(rebuilt.Faces.size(),1u);
    ASSERT_EQ(BRepAdaptor_Surface(rebuilt.Faces.begin()->second).GetType(),GeomAbs_BSplineSurface);
    in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;const auto parts=service->Build(in,{});
    ASSERT_GT(parts.size(),2u);EXPECT_EQ(parts.back().OpeningBoundaryCount,1u);EXPECT_EQ(parts.back().UnclassifiedOpeningFaceCount,0u);
    EXPECT_LE(parts.back().OpeningPlaneDeviationUpper,1e-7);EXPECT_TRUE(parts.back().Supported);
    const auto corridor=BRepPrimAPI_MakeBox(gp_Pnt(-9,9,-in.BaseDrop-in.BaseThickness),18,18,in.BaseDrop+in.BaseThickness+1).Shape();
    for(const auto& plate:parts)CheckClearancePlate(plate,{corridor});
    RecordProperty("complete_control_net_plane_deviation_upper_mm",std::to_string(parts.back().OpeningPlaneDeviationUpper));
}
TEST(FiveAxisFixtureOpeningGeometry, ClosedInvertedTrayWithSidePortPreservesRealHoleClearanceAndSingleFaceSupport){
    // A downward-facing tray mouth is not a through hole: the remote ceiling
    // is real material. Its three bores remain true vertical passages. The
    // lateral port connects the cavity walls to the outside, preventing a
    // proof that merely follows an isolated annular wall shell.
    const auto outer=BRepPrimAPI_MakeBox(gp_Pnt(-60,-50,0),120,100,22).Shape();
    const auto cavity=BRepPrimAPI_MakeBox(gp_Pnt(-56,-46,-1),112,92,21).Shape();BRepAlgoAPI_Cut hollow(outer,cavity);ASSERT_TRUE(hollow.IsDone());TopoDS_Shape source=hollow.Shape();
    const std::array<std::array<double,2>,3> holes{{{-20,-18},{0,18},{20,-18}}};
    for(const auto p:holes){BRepAlgoAPI_Cut bore(source,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(p[0],p[1],19),gp_Dir(0,0,1)),4,4).Shape());ASSERT_TRUE(bore.IsDone());source=bore.Shape();}
    BRepAlgoAPI_Cut sidePort(source,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(54,0,10),gp_Dir(1,0,0)),3,8).Shape());ASSERT_TRUE(sidePort.IsDone());source=sidePort.Shape();ASSERT_TRUE(BRepCheck_Analyzer(source).IsValid());
    auto in=ClearanceInput();in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(source,"Inverted perforated tray with lateral port","generated",.05));
    const auto before=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(*in.Geometry);ASSERT_TRUE(built.bOK);in.FaceIDs.clear();
    for(const auto& [id,shape]:built.Faces){const auto face=TopoDS::Face(shape);if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane)continue;Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);
        if(std::abs(z0-20)<1e-7&&std::abs(z1-20)<1e-7)in.FaceIDs.push_back(id);
    }
    ASSERT_EQ(in.FaceIDs.size(),1u);in.Stations={{-40,-46,46,true},{0,-46,46,true},{40,-46,46,true}};
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();const auto baseline=service->Build(in,{});ASSERT_EQ(baseline.size(),4u);ASSERT_TRUE(baseline.back().Supported);
    // Analytic original bore bounds plus the declared 1 mm margin span the
    // full base and every support; no production opening list is reused.
    const double bottom=-in.BaseDrop-in.BaseThickness;std::vector<TopoDS_Shape> corridors;
    for(const auto p:holes)corridors.push_back(BRepPrimAPI_MakeBox(gp_Pnt(p[0]-5,p[1]-5,bottom),10,10,23-bottom).Shape());
    double priorOverlap=0;for(const auto& plate:baseline)for(const auto& corridor:corridors)priorOverlap+=OpeningIntersectionVolume(plate,corridor);ASSERT_GT(priorOverlap,1.);
    in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;std::vector<iCAX::Geometry::SectionPlate> parts;ASSERT_NO_THROW(parts=service->Build(in,{}));ASSERT_EQ(parts.size(),4u);EXPECT_TRUE(parts.back().Supported);
    EXPECT_EQ(parts.back().OpeningBoundaryCount,holes.size());EXPECT_GE(parts.back().OpeningNonExitBoundaryCount,4u);
    for(const auto& plate:parts)CheckClearancePlate(plate,corridors);
    EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));RecordProperty("real_vertical_bores",3);RecordProperty("lateral_wall_ports",1);RecordProperty("selected_support_faces",1);
}
TEST(FiveAxisFixtureOpeningGeometry, DownwardClosedBeadKeepsBothSupportSidesAndTrueBoreRemainsClear){
    const auto sheet=BRepPrimAPI_MakeBox(gp_Pnt(-60,-60,0),120,120,1).Shape();
    const auto bead=BRepPrimAPI_MakeBox(gp_Pnt(-50,-22,-8),100,18,9).Shape();
    BRepAlgoAPI_Fuse formed(sheet,bead);ASSERT_TRUE(formed.IsDone());
    BRepAlgoAPI_Cut drilled(formed.Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20,-13,-10),gp_Dir(0,0,1)),4,20).Shape());ASSERT_TRUE(drilled.IsDone());
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=ClearanceInput();
    in.Geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(drilled.Shape(),"Downward closed bead and real bore","generated",.05));
    in.FaceIDs=service->FindLowerEnvelopeFaces(in,{});in.AvoidOpeningBoundaries=true;in.OpeningBoundaryMargin=1;
    const auto before=iCAX::GeometryData::Persistence::Serialize(*in.Geometry);const auto parts=service->Build(in,{});
    ASSERT_EQ(parts.size(),4u);EXPECT_TRUE(parts.back().Supported);EXPECT_EQ(parts.back().OpeningBoundaryCount,1u);
    const auto corridor=BRepPrimAPI_MakeBox(gp_Pnt(15,-18,-in.BaseDrop-8-in.BaseThickness),10,10,in.BaseDrop+in.BaseThickness+10).Shape();
    for(const auto& part:parts){CheckClearancePlate(part,{corridor});if(!part.Base){EXPECT_LT(part.Span[0],-59.);EXPECT_GT(part.Span[1],59.);EXPECT_FALSE(part.Contacts.empty());}}
    EXPECT_EQ(before,iCAX::GeometryData::Persistence::Serialize(*in.Geometry));
}
