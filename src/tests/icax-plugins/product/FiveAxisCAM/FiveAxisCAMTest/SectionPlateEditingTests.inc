namespace {
using EditLoop=iCAX::GeometryData::Loop2;
EditLoop EditingRect(double x0,double y0,double x1,double y1){
    EditLoop result;const std::array<std::array<double,2>,4> points{{{x0,y0},{x1,y0},{x1,y1},{x0,y1}}};
    for(size_t i=0;i<4;++i){const auto a=points[i],b=points[(i+1)%4];result.Segments.push_back({iCAX::GeometryData::Segment2{{a[0],a[1]},{b[0],b[1]}},{0,1,false}});}return result;
}
EditLoop EditingCircle(double x,double y,double radius,bool split=false){
    iCAX::GeometryData::Circle2 circle;circle.Placement.Location={x,y};circle.Radius=radius;const double pi=std::acos(-1.);EditLoop result;
    if(!split)result.Segments.push_back({circle,{0,2*pi,false}});
    else for(int i=0;i<2;++i)result.Segments.push_back({iCAX::GeometryData::Arc2{circle,i*pi,(i+1)*pi,true},{i*pi,(i+1)*pi,false}});
    return result;
}
EditLoop EditingLoop(const iCAX::GeometryData::Tube::SRegionLoop2& loop){EditLoop result;for(const auto& curve:loop.Curves)result.Segments.push_back(curve.Segment);return result;}
iCAX::Geometry::SectionPlateHoleEditInput EditingInput(const iCAX::Geometry::SectionPlate& plate){
    iCAX::Geometry::SectionPlateHoleEditInput result;result.OriginalGeometry=plate.Geometry;result.Frame=plate.Frame;result.Thickness=plate.Thickness;result.MinimumWeb=8;return result;
}
iCAX::Geometry::SectionPlate EditingThinEdgeMortise(){
    BRepBuilderAPI_MakePolygon outer;for(auto p:std::array<std::array<double,2>,4>{{{-50,-50},{50,-50},{50,50},{-50,50}}})outer.Add(gp_Pnt(p[0],p[1],0));outer.Close();BRepBuilderAPI_MakeFace face(outer.Wire());
    BRepBuilderAPI_MakePolygon hole;for(auto p:std::array<std::array<double,2>,4>{{{45,-5},{48,-5},{48,5},{45,5}}})hole.Add(gp_Pnt(p[0],p[1],0));hole.Close();face.Add(TopoDS::Wire(hole.Wire().Reversed()));
    iCAX::Geometry::SectionPlate result;result.Thickness=3;result.Frame={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    result.Geometry=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(BRepPrimAPI_MakePrism(face.Face(),gp_Vec(0,0,3)).Shape(),"Existing narrow mortise","generated",.05);return result;
}
void EditingReject(const std::shared_ptr<iCAX::Geometry::ISectionPlateEditingService>& service,const iCAX::Geometry::SectionPlateHoleEditInput& input,const Text& code){
    try{service->EditHoles(input);ADD_FAILURE()<<"Expected "<<code;}catch(const std::exception& error){EXPECT_NE(Text(error.what()).find(code),Text::npos)<<error.what();}
}
}
TEST(FiveAxisSectionPlateEditing, CirclesAndNativeArcsRemainExactThroughSinglePlateReadback){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();auto in=ClearanceInput();in.AreaWidth=180;in.AreaDepth=180;in.WindowMode="Automatic";in.WindowWeb=8;
    const auto parts=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>()->Build(in,{});const auto original=iCAX::GeometryData::Persistence::Serialize(parts.front().Geometry);auto edit=EditingInput(parts.back());
    edit.Holes={EditingCircle(20,30,4),EditingCircle(-20,-30,5,true)};edit.ProtectedMaterial=service->ReadProtection(parts.back(),parts,8);
    const auto result=service->EditHoles(edit);const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(result.Geometry);ASSERT_TRUE(built.bOK);ASSERT_TRUE(BRepCheck_Analyzer(built.Shape).IsValid());
    GProp_GProps properties;BRepGProp::VolumeProperties(built.Shape,properties);EXPECT_NEAR(std::abs(properties.Mass()),(180.*180.-std::acos(-1.)*41)*in.BaseThickness,1e-5);
    ASSERT_EQ(result.Profile.Boundaries.size(),3u);EXPECT_EQ(result.Profile.Boundaries[0].ID,"O0");EXPECT_EQ(result.Profile.Boundaries[1].ID,"H0");EXPECT_EQ(result.Profile.Boundaries[2].ID,"H1");
    size_t analytic=0;for(size_t i=1;i<result.Profile.Boundaries.size();++i)for(const auto& curve:result.Profile.Boundaries[i].Curves){analytic+=std::holds_alternative<iCAX::GeometryData::Circle2>(curve.Segment.Curve)||std::holds_alternative<iCAX::GeometryData::Arc2>(curve.Segment.Curve);EXPECT_GT(curve.Segment.Range.Last,curve.Segment.Range.First);}EXPECT_GE(analytic,3u);
    const auto reopened=service->ReadProfile(result.Geometry,edit.Frame,edit.Thickness);ASSERT_EQ(reopened.Boundaries.size(),3u);
    for(size_t i=0;i<3;++i)EXPECT_EQ(result.Profile.Boundaries[i].ID,reopened.Boundaries[i].ID);
    const auto& first=reopened.Boundaries[1].Curves.front().Segment.Curve;const auto* circle=std::get_if<iCAX::GeometryData::Circle2>(&first);ASSERT_NE(circle,nullptr);EXPECT_NEAR(circle->Placement.Location.X,20,1e-10);EXPECT_NEAR(circle->Placement.Location.Y,30,1e-10);EXPECT_NEAR(circle->Radius,4,1e-10);
    EXPECT_EQ(original,iCAX::GeometryData::Persistence::Serialize(parts.front().Geometry));EXPECT_EQ(parts.back().Geometry.Faces.size(),edit.OriginalGeometry.Faces.size());RecordProperty("exact_circle_arc_spans",static_cast<int>(analytic));
}
TEST(FiveAxisSectionPlateEditing, UnchangedNarrowManufacturingHoleRemainsValidAndCannotBeFilled){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();const auto plate=EditingThinEdgeMortise();auto edit=EditingInput(plate);const auto profile=service->ReadProfile(plate.Geometry,plate.Frame,plate.Thickness);ASSERT_EQ(profile.Boundaries.size(),2u);
    edit.Holes={EditingLoop(profile.Boundaries[1])};edit.ProtectedVoids=edit.Holes;EXPECT_NO_THROW(service->EditHoles(edit));
    edit.Holes.clear();EditingReject(service,edit,"PLATE_EDIT_PROTECTED_VOID");
    edit.ProtectedVoids.clear();EXPECT_NO_THROW(service->EditHoles(edit));
}
TEST(FiveAxisSectionPlateEditing, NestedOverlappingCutoutsAndEdgeNotchesSubtractTheirUnion){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();auto in=ClearanceInput();in.AreaWidth=180;in.AreaDepth=180;
    const auto parts=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>()->Build(in,{});auto edit=EditingInput(parts.back());
    const auto area=[&](const auto& result){const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(result.Geometry);EXPECT_TRUE(built.bOK);EXPECT_TRUE(BRepCheck_Analyzer(built.Shape).IsValid());GProp_GProps properties;BRepGProp::VolumeProperties(built.Shape,properties);return std::abs(properties.Mass())/in.BaseThickness;};
    edit.Holes={EditingCircle(0,0,4),EditingRect(-20,-20,20,20)};auto nested=service->EditHoles(edit);EXPECT_NEAR(area(nested),180.*180.-1600.,1e-5);ASSERT_EQ(nested.Profile.Boundaries.size(),2u);ASSERT_EQ(nested.HoleInputIndices.size(),1u);EXPECT_EQ(nested.HoleInputIndices[0].size(),2u);EXPECT_EQ(nested.EqualInput[0],1u);
    edit.Holes={EditingRect(-20,-20,10,10),EditingRect(-10,-10,20,20)};auto overlap=service->EditHoles(edit);EXPECT_NEAR(area(overlap),180.*180.-1400.,1e-5);EXPECT_EQ(overlap.Profile.Boundaries.size(),2u);
    edit.Holes={EditingRect(-20,70,20,110)};auto notch=service->EditHoles(edit);EXPECT_NEAR(area(notch),180.*180.-800.,1e-5);EXPECT_TRUE(notch.OutlineChanged);EXPECT_EQ(notch.Profile.Boundaries.size(),1u);EXPECT_TRUE(notch.PreviewHoles.empty());
    edit.Holes={EditingCircle(0,0,4),EditingRect(-20,-20,20,110)};auto open=service->EditHoles(edit);EXPECT_NEAR(area(open),180.*180.-4400.,1e-5);EXPECT_TRUE(open.OutlineChanged);EXPECT_EQ(open.Profile.Boundaries.size(),1u);
    edit.Holes={EditingCircle(0,90,10)};auto roundNotch=service->EditHoles(edit);EXPECT_NEAR(area(roundNotch),180.*180.-50.*std::acos(-1.),1e-5);EXPECT_TRUE(roundNotch.OutlineChanged);size_t arcs=0;for(const auto& curve:roundNotch.Profile.Boundaries.front().Curves)arcs+=std::holds_alternative<iCAX::GeometryData::Circle2>(curve.Segment.Curve)||std::holds_alternative<iCAX::GeometryData::Arc2>(curve.Segment.Curve);EXPECT_GT(arcs,0u);
    edit.ProtectedMaterial={EditingRect(-10,75,10,90)};EditingReject(service,edit,"PLATE_EDIT_PROTECTED_MATERIAL");
}
TEST(FiveAxisSectionPlateEditing, WeldBaseFootprintAndMinimumBandsPreventAnInteriorBreak){
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();auto in=ClearanceInput();in.AreaWidth=180;in.AreaDepth=180;
    const auto parts=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>()->Build(in,{});auto edit=EditingInput(parts.back());edit.Holes={EditingCircle(0,20,4)};
    EXPECT_NO_THROW(service->EditHoles(edit));edit.ProtectedMaterial=service->ReadProtection(parts.back(),parts,8);ASSERT_FALSE(edit.ProtectedMaterial.empty());EditingReject(service,edit,"PLATE_EDIT_PROTECTED_MATERIAL");
    edit.MinimumWeb=0;edit.ProtectedMaterial=service->ReadProtection(parts.back(),parts,0,false);EditingReject(service,edit,"PLATE_EDIT_PROTECTED_MATERIAL");edit.MinimumWeb=8;
    edit.ProtectedMaterial.clear();edit.Holes={EditingCircle(84,20,4)};EditingReject(service,edit,"PLATE_EDIT_MINIMUM_WEB");
    edit.MinimumWeb=0;EXPECT_NO_THROW(service->EditHoles(edit));
    edit.Holes={EditingRect(-100,-1,100,1)};EditingReject(service,edit,"PLATE_EDIT_MATERIAL_DISCONNECTED");
    EXPECT_THROW(service->EditHoles(edit,[]{return true;}),std::exception);
}
TEST(FiveAxisSectionPlateEditing, WindowIndicesRetainExactHoleOrderAndOriginalTabSlotFitBands){
    Scene scene;const auto editor=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();const auto plates=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=ClearanceInput();
    in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.BaseDrop=80;in.WindowMode="Automatic";in.WindowWeb=8;in.Stations=plates->SuggestStations(in,50);const auto assembly=plates->Build(in,{});const auto& plate=assembly.back();ASSERT_TRUE(plate.Base);ASSERT_FALSE(plate.WindowHoleIndices.empty());
    const auto profile=editor->ReadProfile(plate.Geometry,plate.Frame,plate.Thickness);ASSERT_EQ(profile.Boundaries.size(),plate.Holes.size()+1);auto edit=EditingInput(plate);edit.ProtectedMaterial=editor->ReadProtection(plate,assembly,8);
    for(size_t i=0;i<plate.Holes.size();++i){EXPECT_EQ(profile.Boundaries[i+1].ID,"H"+std::to_string(i));const auto loop=EditingLoop(profile.Boundaries[i+1]);edit.Holes.push_back(loop);
        if(std::find(plate.WindowHoleIndices.begin(),plate.WindowHoleIndices.end(),i)==plate.WindowHoleIndices.end())edit.ProtectedVoids.push_back(loop);
        double x0=1e100,y0=1e100,x1=-1e100,y1=-1e100;for(const auto& point:plate.Holes[i]){x0=std::min(x0,point[0]);x1=std::max(x1,point[0]);y0=std::min(y0,point[1]);y1=std::max(y1,point[1]);}
        std::vector<std::array<double,2>> points;for(const auto& segment:loop.Segments){const auto* line=std::get_if<iCAX::GeometryData::Segment2>(&segment.Curve);ASSERT_NE(line,nullptr);points.push_back({line->Start.X,line->Start.Y});}
        double rx0=1e100,ry0=1e100,rx1=-1e100,ry1=-1e100;for(auto point:points){rx0=std::min(rx0,point[0]);rx1=std::max(rx1,point[0]);ry0=std::min(ry0,point[1]);ry1=std::max(ry1,point[1]);}EXPECT_NEAR(x0,rx0,1e-10);EXPECT_NEAR(x1,rx1,1e-10);EXPECT_NEAR(y0,ry0,1e-10);EXPECT_NEAR(y1,ry1,1e-10);
    }
    EXPECT_NO_THROW(editor->EditHoles(edit));
    const auto index=plate.WindowHoleIndices.front();edit.Holes.erase(edit.Holes.begin()+index);
    EXPECT_NO_THROW(editor->EditHoles(edit));
    RecordProperty("editable_windows",static_cast<int>(plate.WindowHoleIndices.size()));RecordProperty("immutable_holes",static_cast<int>(edit.ProtectedVoids.size()));
}
TEST(FiveAxisSectionPlateEditing, CrossingPostRemainsProtectedBeyondTheMinimumHoleToEdgeDistance){
    Scene scene;const auto editor=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();const auto plates=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();auto in=ClearanceInput();in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.BaseDrop=60;in.Stations=plates->SuggestStations(in,50);const auto assembly=plates->Build(in,{});
    const auto found=std::find_if(assembly.begin(),assembly.end(),[](const auto& p){return !p.Base&&std::any_of(p.Joints.begin(),p.Joints.end(),[](const auto& j){return j.Type=="LapBottom";});});ASSERT_NE(found,assembly.end());
    const auto joint=std::find_if(found->Joints.begin(),found->Joints.end(),[](const auto& j){return j.Type=="LapBottom";});double first=1e100,last=-1e100;for(auto point:joint->Boundary){first=std::min(first,point[0]);last=std::max(last,point[0]);}
    auto edit=EditingInput(*found);edit.Holes={EditingCircle((first+last)/2,45,2)};EXPECT_NO_THROW(editor->EditHoles(edit));edit.ProtectedMaterial=editor->ReadProtection(*found,assembly,8);EditingReject(editor,edit,"PLATE_EDIT_PROTECTED_MATERIAL");
    edit.MinimumWeb=0;edit.ProtectedMaterial=editor->ReadProtection(*found,assembly,0,false);EXPECT_NO_THROW(editor->EditHoles(edit));
    double jointTop=-1e100;for(auto point:joint->Boundary)jointTop=std::max(jointTop,point[1]);edit.Holes={EditingCircle(first-.5,jointTop-1,2)};EditingReject(editor,edit,"PLATE_EDIT_PROTECTED_MATERIAL");
}
TEST(FiveAxisFixtureJoinery, TenonRadiusRoundsOnlyInsertionCornersWithExactArcs){
    Scene scene;auto in=ClearanceInput();in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.TenonRadius=1.;
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();in.Stations=service->SuggestStations(in,50);const auto parts=service->Build(in,{});size_t tenons=0,mortises=0,arcs=0;
    for(const auto& plate:parts){const auto solid=ClearanceShape(plate);
        for(TopExp_Explorer e(solid,TopAbs_EDGE);e.More();e.Next()){BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));if(c.GetType()!=GeomAbs_Circle)continue;const auto center=c.Circle().Location();const auto& f=plate.Frame;const double y=(center.X()-f[3])*f[1]+(center.Y()-f[7])*f[5]+(center.Z()-f[11])*f[9];
            if(std::abs(y+in.BaseThickness-in.TenonRadius)<1e-6){EXPECT_NEAR(c.Circle().Radius(),in.TenonRadius,1e-7);++arcs;}}
        for(const auto& joint:plate.Joints){if(joint.Type=="BaseMortise"){++mortises;EXPECT_EQ(joint.Boundary.size(),4u);continue;}if(joint.Type!="BaseTenon")continue;++tenons;
            double first=INFINITY,last=-INFINITY;for(auto p:joint.Boundary){first=std::min(first,p[0]);last=std::max(last,p[0]);}EXPECT_NEAR(last-first,in.TenonWidth,1e-10);const auto& m=plate.Frame;
            const auto point=[&](double u,double v){return gp_Pnt(m[0]*u+m[1]*v+m[2]*plate.Thickness/2+m[3],m[4]*u+m[5]*v+m[6]*plate.Thickness/2+m[7],m[8]*u+m[9]*v+m[10]*plate.Thickness/2+m[11]);};
            const double r=in.TenonRadius,bottom=-in.BaseThickness;
            EXPECT_EQ(JointState(solid,point(first+.1*r,bottom+.1*r)),TopAbs_OUT);
            EXPECT_EQ(JointState(solid,point(first+.4*r,bottom+.4*r)),TopAbs_IN);
            EXPECT_EQ(JointState(solid,point(first+.01*r,bottom+r+.1)),TopAbs_IN);
            EXPECT_EQ(JointState(solid,point((first+last)/2,bottom+.1*r)),TopAbs_IN);
        }
    }
    EXPECT_GT(tenons,0u);EXPECT_EQ(tenons,mortises);EXPECT_GE(arcs,tenons*4);
    auto bad=in;bad.TenonRadius=std::min(in.TenonWidth/2,in.BaseThickness/2)+.1;EXPECT_THROW(service->Build(bad,{}),std::exception);
    in.TenonRadius=0;const auto square=service->Build(in,{});EXPECT_EQ(square.size(),parts.size());
}
TEST(FiveAxisFixtureJoinery, ConstructedRoundedWindowProfilesMatchIndependentSolidReadback){
    Scene scene;auto in=ClearanceInput();in.Automatic=true;in.Pattern="Grid";in.JointMode="TabSlot";in.TenonRadius=1.;in.BaseDrop=80;in.WindowMode="Automatic";in.WindowWeb=8;
    const auto service=scene.Services().Resolve<iCAX::Geometry::ISectionPlateService>();const auto editor=scene.Services().Resolve<iCAX::Geometry::ISectionPlateEditingService>();in.Stations=service->SuggestStations(in,50);
    const auto parts=service->Build(in,{});size_t rounded=0,windows=0;
    for(const auto& plate:parts){if(plate.Base)continue;++rounded;windows+=plate.WindowHoleIndices.size();
        ASSERT_FALSE(plate.ExactProfile.Boundaries.empty());EXPECT_EQ(plate.ExactProfile.Boundaries.size(),plate.Holes.size()+1);
        const auto independent=editor->ReadProfile(plate.Geometry,plate.Frame,plate.Thickness);
        EXPECT_EQ(iCAX::CAD::TubeCadJson::Encode(plate.ExactProfile),iCAX::CAD::TubeCadJson::Encode(independent));
        EXPECT_TRUE(BRepCheck_Analyzer(ClearanceShape(plate)).IsValid());
    }
    EXPECT_GT(rounded,0u);EXPECT_GT(windows,0u);
}
