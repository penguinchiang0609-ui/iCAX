namespace
{
    Map Lead(const char* kind="Line",double length=1.)
    {Map lead{{"kind",Text(kind)},{"placement",Text("ScrapSide")},{"lengthMm",length},{"angleRad",0.}};if(Text(kind)=="Arc")lead["radiusMm"]=2.;return lead;}
    Map LeadPreview(Scene& scene,const Array& ids,Map patch={})
    {Map q{{"expected",Read(scene).at("expected")},{"intentIds",ids}};if(!patch.empty())q["patch"]=patch;return Invoke(&scene,"CamProcess","PreviewLeads",q);}
}
TEST(FiveAxisLeads, PreviewUsesNativeAnchorAndDoesNotMutate)
{
    Scene scene;const auto ids=ConfirmWithPolicies(scene).at("createdIntentIds").To<Array>();const auto before=Read(scene);
    const auto undo=scene.Database().GetUndoArray().size();
    const auto preview=LeadPreview(scene,ids,{{"leadIn",Lead()},{"leadOut",Lead("Arc")}});
    const auto leads=preview.at("items").To<Array>().front().To<Map>().at("leads").To<Array>();ASSERT_EQ(2u,leads.size());
    for(const auto& value:leads){const auto lead=value.To<Map>();const auto points=lead.at("points").To<Array>();
        const auto anchor=lead.at("anchor").To<Array>(),endpoint=(lead.at("role").To<Text>()=="LeadIn"?points.back():points.front()).To<Array>();
        for(int i=0;i<3;++i)EXPECT_NEAR(anchor[i].To<double>(),endpoint[i].To<double>(),1e-10);
        const auto start=before.at("intentToolpaths").To<Array>().front().To<Map>().at("startPoint").To<Array>();
        for(int i=0;i<3;++i)EXPECT_NEAR(start[i].To<double>(),anchor[i].To<double>(),1e-9);
        if(lead.at("role").To<Text>()=="LeadIn"){
            // Entry starts one millimeter inside the compensated R3.9 cut.
            const auto p=points.front().To<Array>();EXPECT_NEAR(2.9,std::hypot(p[0].To<double>(),p[1].To<double>()),1e-8);
        }
    }
    EXPECT_THROW(LeadPreview(scene,ids,{{"leadIn",Lead("Line",10.)}}),std::exception);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisLeads, BatchValidationPrecedesPublicationAndOneUndoRestores)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    auto outside=Lead();outside["placement"]=Text("OutsideStock");
    EXPECT_THROW(Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"leadIn",outside}}}})),std::exception);
    auto invalid=Lead("Arc",8);EXPECT_THROW(LeadPreview(scene,ids,{{"leadIn",invalid}}),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0],Text("00000000-0000-0000-0000-000000000001")},{{"patch",Map{{"leadIn",Lead()}}}})),std::exception);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    const auto request=EditRequest(scene,ids,{{"patch",Map{{"leadIn",Lead("Arc")},{"leadOut",Lead()}}}});
    const auto result=Invoke(&scene,"CamProcess","Update",request);EXPECT_EQ(result,Invoke(&scene,"CamProcess","Update",request));
    EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
    for(const auto& id:ids)EXPECT_EQ(Lead("Arc"),ProcessItem(scene,id).at("assignment").To<Map>().at("leadIn").To<Map>());
    const auto saved=LeadPreview(scene,ids);ASSERT_TRUE(scene.Database().Undo());
    EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(saved,LeadPreview(scene,ids));
}
TEST(FiveAxisLeads, FailedLeadResultCanBePreviewedAndCorrectedFromAuthoredGraph)
{
    Scene scene;const auto ids=ConfirmWithPolicies(scene).at("createdIntentIds").To<Array>();
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"leadIn",Lead("Line",10.)}}}}));
    const auto failed=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    try{(void)LeadPreview(scene,ids);FAIL()<<"Invalid saved lead unexpectedly evaluated";}
    catch(const std::exception& error){EXPECT_NE(Text::npos,Text(error.what()).find("LEAD_CROSSES_MATERIAL"))<<error.what();}
    const auto preview=LeadPreview(scene,ids,{{"leadIn",Lead("Line",1.)},{"leadOut",Lead("Line",1.)}});
    ASSERT_EQ(ids.size(),preview.at("items").To<Array>().size());
    for(const auto& item:preview.at("items").To<Array>())EXPECT_EQ(2u,item.To<Map>().at("leads").To<Array>().size());
    EXPECT_EQ(failed,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    EXPECT_NO_THROW(Invoke(&scene,"CamProcess","PreviewCompensation",{{"expected",Read(scene).at("expected")},{"intentIds",ids}}));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"leadIn",Lead("Line",1.)},{"leadOut",Lead("Line",1.)}}}}));
    (void)Read(scene);EXPECT_EQ(preview,LeadPreview(scene,ids));
    ASSERT_TRUE(scene.Database().Undo());(void)Read(scene);
    EXPECT_NO_THROW(LeadPreview(scene,ids,{{"leadIn",Lead("Line",1.)},{"leadOut",Lead("Line",1.)}}));
    ASSERT_TRUE(scene.Database().Redo());(void)Read(scene);EXPECT_EQ(preview,LeadPreview(scene,ids));
}
TEST(FiveAxisLeads, NeutralServiceRespectsSpatialSupportAndReversal)
{
    using namespace iCAX::GeometryData;using namespace iCAX::GeometryServices;
    Scene scene;const auto service=scene.Services().Resolve<IBoundaryLeadService>();BRepModel model;
    // Vertical YZ support: input u travels along Y; waste lies along -Z.
    PlaneSurface3 plane;plane.Placement.Location={10,20,30};plane.Placement.XDirection={0,1,0};plane.Placement.YDirection={0,0,1};plane.Placement.ZDirection={1,0,0};
    model.Surfaces3.push_back({1,plane});
    const Point2 uv[4]={{0,0},{10,0},{10,10},{0,10}};BRepWire wire;wire.Id=1;
    for(std::uint64_t i=0;i<4;++i){const auto a=uv[i],b=uv[(i+1)%4];const Direction2 d{(b.X-a.X)/10,(b.Y-a.Y)/10};
        const Point3 point{10,20+a.X,30+a.Y};model.Vertices.push_back({i+1,point,1e-8});
        model.Curves3.push_back({i+1,Line3{{point,{0,d.X,d.Y}}}});model.Curves2.push_back({i+1,Line2{{a,d}}});
        BRepEdge edge;edge.Id=i+1;edge.Curve3Id=i+1;edge.StartVertexId=i+1;edge.EndVertexId=(i+1)%4+1;edge.Range={0,10,false};edge.Tolerance=1e-8;model.Edges.push_back(edge);
        BRepCoedge coedge;coedge.EdgeId=i+1;coedge.Curve2Id=i+1;coedge.Range=edge.Range;wire.Coedges.push_back(coedge);
    }
    model.Wires.push_back(wire);BRepFace face;face.Id=1;face.Surface3Id=1;face.WireIds={1};face.Tolerance=1e-8;model.Faces.push_back(face);
    BoundaryLeadInput q;q.FaceId=q.WireId=q.EdgeId=1;q.NativeParameter=4;q.Length=2;
    const auto result=service->Build(model,q);EXPECT_DOUBLE_EQ(10,result.Points.front().X);EXPECT_DOUBLE_EQ(24,result.Points.front().Y);EXPECT_DOUBLE_EQ(28,result.Points.front().Z);EXPECT_DOUBLE_EQ(30,result.Points.back().Z);
    q.Reversed=true;const auto reversed=service->Build(model,q);EXPECT_DOUBLE_EQ(result.Points.front().Z,reversed.Points.front().Z);EXPECT_DOUBLE_EQ(-result.Tangent.Y,reversed.Tangent.Y);
    q.Arc=true;q.Radius=2;const auto arc=service->Build(model,q);ASSERT_GT(arc.Points.size(),2u);for(const auto& point:arc.Points)EXPECT_NEAR(10,point.X,1e-12);
    EXPECT_NEAR(24,arc.Points.back().Y,1e-12);EXPECT_NEAR(30,arc.Points.back().Z,1e-12);
    q.NativeParameter=11;EXPECT_THROW(service->Build(model,q),std::exception);
    q.NativeParameter=4;q.Arc=false;q.Reversed=false;
    for(const auto endpoint:{0.,10.}){
        auto at=q;at.NativeParameter=endpoint;const auto exact=service->Frame(model,at);
        at.NativeParameter=std::nextafter(endpoint,endpoint==0?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity());
        const auto rounded=service->Frame(model,at);EXPECT_DOUBLE_EQ(exact.Point.Y,rounded.Point.Y);EXPECT_DOUBLE_EQ(exact.Point.Z,rounded.Point.Z);
        EXPECT_NO_THROW(service->Build(model,at));
        EXPECT_NO_THROW(service->Frames(model,{at,at}));
        at.NativeParameter=endpoint+(endpoint==0?-1e-6:1e-6);
        EXPECT_THROW(service->Frame(model,at),std::exception);
        EXPECT_THROW(service->Frames(model,{at}),std::exception);
        EXPECT_THROW(service->Build(model,at),std::exception);
    }
    // Native curve/surface reuse is local to this read, and must preserve
    // traversal, face orientation and each occurrence's support validation.
    auto batchModel=model;auto secondPlane=plane;secondPlane.Placement.YDirection={0,0,-1};secondPlane.Placement.ZDirection={-1,0,0};
    batchModel.Surfaces3.push_back({2,secondPlane});auto secondFace=face;secondFace.Id=2;secondFace.Surface3Id=2;secondFace.Orientation=ETopologyOrientation::Reversed;batchModel.Faces.push_back(secondFace);
    std::vector<BoundaryLeadInput> queries;
    for(std::size_t i=0;i<4;++i)for(bool reverse:{false,true})for(const auto faceId:{1ull,2ull}){
        auto at=q;at.FaceId=faceId;at.EdgeId=i+1;at.CoedgeIndex=i;at.NativeParameter=i+1;at.Reversed=reverse;queries.push_back(at);
    }
    const auto frames=service->Frames(batchModel,queries);ASSERT_EQ(queries.size(),frames.size());
    for(std::size_t i=0;i<queries.size();++i){const auto single=service->Frame(batchModel,queries[i]);const auto& batched=frames[i];
        EXPECT_DOUBLE_EQ(single.Point.X,batched.Point.X);EXPECT_DOUBLE_EQ(single.Point.Y,batched.Point.Y);EXPECT_DOUBLE_EQ(single.Point.Z,batched.Point.Z);
        EXPECT_DOUBLE_EQ(single.Tangent.X,batched.Tangent.X);EXPECT_DOUBLE_EQ(single.Tangent.Y,batched.Tangent.Y);EXPECT_DOUBLE_EQ(single.Tangent.Z,batched.Tangent.Z);
        EXPECT_DOUBLE_EQ(single.Normal.X,batched.Normal.X);EXPECT_DOUBLE_EQ(single.Normal.Y,batched.Normal.Y);EXPECT_DOUBLE_EQ(single.Normal.Z,batched.Normal.Z);
    }
    EXPECT_TRUE(service->Frames(batchModel,{}).empty());auto invalidQueries=queries;invalidQueries.back().NativeParameter=11;
    EXPECT_THROW(service->Frames(batchModel,invalidQueries),std::exception);
    auto approximate=model;
    approximate.Curves2.front().Geometry=Line2{{{.001,0},{1,0}}};
    const auto refined=service->Frame(approximate,q);
    EXPECT_NEAR(24,refined.Point.Y,1e-12);EXPECT_NEAR(1,refined.Normal.X,1e-12);
    // Refining UV must not admit a curve which lies off its source surface.
    auto detached=model;plane.Placement.Location.X+=.01;detached.Surfaces3.front().Geometry=plane;
    EXPECT_THROW(service->Frame(detached,q),std::exception);
    EXPECT_THROW(service->Frames(detached,{q,q}),std::exception);
    EXPECT_THROW(service->Build(detached,q),std::exception);
}
TEST(FiveAxisLeads, EveryOpeningLeadStaysInItsOwnLoopIncludingNarrowConcaveNotches)
{
    using namespace iCAX::GeometryData;using namespace iCAX::GeometryServices;
    Scene scene;const auto service=scene.Services().Resolve<IBoundaryLeadService>();
    const auto modelFor=[](const std::vector<Point2>& hole){
        BRepModel model;model.Surfaces3.push_back({1,PlaneSurface3{}});
        const auto add=[&](const std::vector<Point2>& points){BRepWire wire;wire.Id=model.Wires.size()+1;wire.Closed=true;
            const auto first=model.Vertices.size()+1;
            for(std::size_t i=0;i<points.size();++i){const auto a=points[i],b=points[(i+1)%points.size()];const auto length=std::hypot(b.X-a.X,b.Y-a.Y);const Direction2 d{(b.X-a.X)/length,(b.Y-a.Y)/length};const auto id=first+i;
                model.Vertices.push_back({id,{a.X,a.Y,0},1e-8});model.Curves3.push_back({id,Line3{{{a.X,a.Y,0},{d.X,d.Y,0}}}});model.Curves2.push_back({id,Line2{{a,d}}});
                BRepEdge edge;edge.Id=edge.Curve3Id=id;edge.StartVertexId=id;edge.EndVertexId=first+(i+1)%points.size();edge.Range={0,length,false};edge.Tolerance=1e-8;model.Edges.push_back(edge);
                BRepCoedge coedge;coedge.EdgeId=coedge.Curve2Id=id;coedge.Range=edge.Range;wire.Coedges.push_back(coedge);
            }model.Wires.push_back(wire);};
        add({{-20,-20},{20,-20},{20,20},{-20,20}});add(hole);
        BRepFace face;face.Id=face.Surface3Id=1;face.WireIds={1,2};face.WireOrientations={ETopologyOrientation::Forward,ETopologyOrientation::Reversed};face.Tolerance=1e-8;model.Faces.push_back(face);return model;
    };
    auto rectangle=modelFor({{0,0},{10,0},{10,10},{0,10}});
    BoundaryLeadInput q;q.FaceId=1;q.WireId=2;q.EdgeId=5;q.NativeParameter=5;q.Length=9;q.RequireInsideWire=true;
    // Deliberately wrong direction and retained-side flip must not put a hole
    // lead in the surrounding plate. Both entry and exit use the same region.
    q.Anchor=BoundaryLeadAnchor{{5,0,0},{1,0,0},{0,-1,0}};
    for(bool entry:{true,false})for(bool flip:{true,false}){q.Entry=entry;q.FlipSide=flip;
        const auto line=service->Build(rectangle,q);for(const auto& p:line.Points){EXPECT_GE(p.X,0);EXPECT_LE(p.X,10);EXPECT_GE(p.Y,0);EXPECT_LE(p.Y,10);}
        q.Length=11;EXPECT_THROW(service->Build(rectangle,q),std::exception);q.Length=2;q.Arc=true;q.Radius=2;EXPECT_NO_THROW(service->Build(rectangle,q));q.Arc=false;q.Length=9;
    }
    auto concave=modelFor({{0,0},{10,0},{10,10},{0,10},{0,5.015},{8,5.015},{8,5.005},{0,5.005}});
    q.Entry=true;q.FlipSide=false;q.Length=9;
    EXPECT_THROW(service->Build(concave,q),std::exception)<<"A 0.01 mm notch between the old 0.05 mm samples must reject the entire lead";
    auto reversed=rectangle;auto& wire=reversed.Wires[1];std::reverse(wire.Coedges.begin(),wire.Coedges.end());for(auto& edge:wire.Coedges)edge.Orientation=ETopologyOrientation::Reversed;
    reversed.Faces[0].WireOrientations[1]=ETopologyOrientation::Forward;reversed.Faces[0].Orientation=ETopologyOrientation::Reversed;q.CoedgeIndex=3;
    EXPECT_NO_THROW(service->Build(reversed,q));
    // Imported round holes are often rational spline pieces. A valid tangent
    // arc can produce a small tolerance-overlap at its anchor in OCCT.
    auto circle=rectangle;const Point2 cardinal[4]={{10,0},{0,10},{-10,0},{0,-10}};
    for(std::size_t i=0;i<4;++i){const auto a=cardinal[i],b=cardinal[(i+1)%4];NURBS2 c2;NURBS3 c3;
        c2.Degree=c3.Degree=2;c2.Poles={a,{a.X+b.X,a.Y+b.Y},b};c3.Poles={{a.X,a.Y,0},{a.X+b.X,a.Y+b.Y,0},{b.X,b.Y,0}};
        c2.Weights=c3.Weights={1,std::sqrt(.5),1};c2.Knots=c3.Knots={0,1};c2.Multiplicities=c3.Multiplicities={3,3};
        circle.Vertices[4+i].Position={a.X,a.Y,0};circle.Curves2[4+i].Geometry=c2;circle.Curves3[4+i].Geometry=c3;
        circle.Edges[4+i].Range={0,1,false};circle.Wires[1].Coedges[i].Range={0,1,false};
    }
    q.CoedgeIndex=0;q.NativeParameter=0;q.Length=q.Radius=1;q.Arc=true;q.Anchor=BoundaryLeadAnchor{{10,0,0},{0,1,0},{-1,0,0}};
    for(bool entry:{true,false}){q.Entry=entry;const auto arc=service->Build(circle,q);for(const auto& p:arc.Points)EXPECT_LE(std::hypot(p.X,p.Y),10+1e-7);}
    q.Radius=10;EXPECT_THROW(service->Build(circle,q),std::exception)<<"An arc coincident with the hole boundary has no interior lead";
    q.Radius=30;q.Length=4;EXPECT_THROW(service->Build(circle,q),std::exception);
}
