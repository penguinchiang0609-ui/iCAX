namespace
{
    Map SplitRequest(Scene& scene,const Array& cuts)
    {return {{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"cuts",cuts}};}
    Map CircleSplitSelection(Scene& scene,const iCAX::Data::Variant& id,double a=0.,double b=3.)
    {
        auto anchor=DefinitionItem(scene,id).at("start").To<Map>();anchor["nativeParameter"]=a;auto second=anchor;second["nativeParameter"]=b;
        return {{"intentId",id},{"points",Array{anchor,second}}};
    }
}
TEST(FiveAxisCurveSplitting, ReversedSpatialNurbsAndPolylineKeepNativePayloadAndPartition)
{
    using namespace iCAX::GeometryData;
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ICurveEditingService>();
    NURBS3 spline;spline.Degree=2;spline.Poles={{0,0,0},{8,3,5},{15,10,-3}};spline.Weights={1,.7,1.2};spline.Knots={2,7};spline.Multiplicities={3,3};
    CompositeCurve3 curve;curve.Segments.push_back({spline,{2,7,true}});
    const auto pieces=service->Split(curve,{{0,3},{0,6}});ASSERT_EQ(3u,pieces.size());
    const double bounds[][2]={{6,7},{3,6},{2,3}};
    for(std::size_t i=0;i<pieces.size();++i){
        ASSERT_EQ(1u,pieces[i].Curve.Segments.size());const auto& s=pieces[i].Curve.Segments[0];const auto& copy=std::get<NURBS3>(s.Curve);
        EXPECT_EQ(spline.Weights,copy.Weights);EXPECT_EQ(spline.Knots,copy.Knots);EXPECT_EQ(spline.Multiplicities,copy.Multiplicities);
        EXPECT_DOUBLE_EQ(5,copy.Poles[1].Z);EXPECT_TRUE(s.Range.Reversed);EXPECT_FALSE(pieces[i].Curve.Closed);
        EXPECT_NEAR(bounds[i][0],s.Range.First,1e-12);EXPECT_NEAR(bounds[i][1],s.Range.Last,1e-12);
        EXPECT_GT(pieces[i].PreviewPoints.size(),1u);
        if(i)for(int k=0;k<3;++k)EXPECT_NEAR(pieces[i-1].PreviewPoints.back()[k],pieces[i].PreviewPoints.front()[k],1e-9);
    }
    Polyline3 poly;poly.Points={{0,0,0},{3,0,4},{3,5,4}};curve.Segments={{poly,{0,2,false}}};
    const auto lines=service->Split(curve,{{0,.5},{0,1}});ASSERT_EQ(3u,lines.size());
    for(const auto& p:lines)EXPECT_TRUE(std::holds_alternative<Polyline3>(p.Curve.Segments[0].Curve));
    EXPECT_NEAR(2,lines[0].PreviewPoints.back()[2],1e-8);EXPECT_NEAR(5,lines.back().PreviewPoints.back()[1],1e-8);
    EXPECT_THROW(service->Split(curve,{{0,0}}),std::exception);
    EXPECT_THROW(service->Split(curve,{{0,2}}),std::exception);
    EXPECT_THROW(service->Split(curve,{{0,.5},{0,.5}}),std::exception);
    EXPECT_THROW(service->Split(curve,{{0,3}}),std::exception);
    EXPECT_THROW(service->Split(curve,{{3,.5}}),std::exception);
}
TEST(FiveAxisCurveSplitting, ClosedSeamAndSharedVerticesDoNotAddZeroLengthPieces)
{
    using namespace iCAX::GeometryData;
    Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ICurveEditingService>();
    CompositeCurve3 curve;curve.Closed=true;
    curve.Segments={{Line3{{{0,0,0},{1,0,0}}},{0,10,false}}, {Line3{{{10,0,0},{-1,0,0}}},{0,10,false}}};
    const auto atSeam=service->Split(curve,{{0,0}});ASSERT_EQ(1u,atSeam.size());EXPECT_EQ(2u,atSeam[0].Curve.Segments.size());EXPECT_FALSE(atSeam[0].Curve.Closed);
    const auto cuts=service->Split(curve,{{0,10},{1,5}});ASSERT_EQ(2u,cuts.size());EXPECT_EQ(1u,cuts[0].Curve.Segments.size());EXPECT_EQ(2u,cuts[1].Curve.Segments.size());
    EXPECT_THROW(service->Split(curve,{{0,10},{1,0}}),std::exception);
    EXPECT_THROW(service->Split(curve,{{0,0},{1,10}}),std::exception);
    EXPECT_THROW(service->Split(curve,{{0,5},{0,5+1e-10}}),std::exception);
    Circle3 circle;circle.Radius=5;curve.Segments={{circle,{0,6.283185307179586,false}}};
    const auto single=service->Split(curve,{{0,0}});ASSERT_EQ(1u,single.size());
    EXPECT_DOUBLE_EQ(6.283185307179586,single[0].Curve.Segments[0].Range.Last);
    const auto interior=service->Split(curve,{{0,1.234567890123}});ASSERT_EQ(1u,interior.size());
    ASSERT_EQ(2u,interior[0].Curve.Segments.size());
    EXPECT_DOUBLE_EQ(1.234567890123,interior[0].Curve.Segments[0].Range.First);
    EXPECT_DOUBLE_EQ(1.234567890123,interior[0].Curve.Segments[1].Range.Last);
}
TEST(FiveAxisCutSplitting, ExactAnchorsAtomicSplitUndoRedoAndEmbeddedReopen)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));
    const auto before=Read(scene),old=before.at("intentToolpaths").To<Array>().front().To<Map>();
    const auto resolved=Invoke(&scene,"CamCut","ResolveSplitPoint",{{"expected",before.at("expected")},{"intentId",id},
        {"nearPoint",Array{0.,4.05,10.}},{"maximumDistanceMm",.2},{"mode",Text("Auto")},{"frame",Text("Workpiece")}});
    EXPECT_NEAR(4.,resolved.at("point").To<Array>()[1].To<double>(),1e-9);EXPECT_EQ(before,Read(scene));
    auto selection=CircleSplitSelection(scene,id);selection["points"]=Array{selection.at("points").To<Array>()[1]};
    const auto request=SplitRequest(scene,{selection});const auto undo=scene.Database().GetUndoArray().size();
    const auto result=Invoke(&scene,"CamCut","Split",request);EXPECT_EQ(result,Invoke(&scene,"CamCut","Split",request));
    EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());const auto ids=result.at("intentIds").To<Array>();ASSERT_EQ(2u,ids.size());EXPECT_EQ(id,ids[0]);
    const auto after=Read(scene).at("intentToolpaths").To<Array>();ASSERT_EQ(2u,after.size());
    EXPECT_EQ(old.at("number"),after[0].To<Map>().at("number"));EXPECT_NE(after[0].To<Map>().at("number"),after[1].To<Map>().at("number"));
    double rangeSum=0;
    for(const auto& value:after){
        const auto c=value.To<Map>(),definition=DefinitionItem(scene,c.at("id"));EXPECT_FALSE(definition.at("closed").To<bool>());
        EXPECT_TRUE(definition.at("reversed").To<bool>());
        EXPECT_EQ(old.at("processRef"),c.at("processRef"));EXPECT_EQ(old.at("poseRef"),c.at("poseRef"));EXPECT_EQ(old.at("sourceKey"),c.at("sourceKey"));
        const auto feature=PolicyBody(scene,c.at("featureRef").To<Map>());EXPECT_EQ(old.at("featureRef").To<Map>().at("revision"),feature.at("splitFromData").To<Map>().at("revision"));
        if(c.at("id")==id){const auto delta=std::abs(old.at("start").To<Map>().at("nativeParameter").To<double>()-feature.at("start").To<Map>().at("nativeParameter").To<double>());
            EXPECT_TRUE(delta<1e-12||std::abs(delta-6.283185307179586)<1e-12);}
        const auto map=PolicyBody(scene,feature.at("sourceMapData").To<Map>());
        const auto curve=IntentCurveValue(scene,c);
        ASSERT_EQ(curve->Curve3.Segments.size(),map.at("spans").To<Array>().size());
        for(const auto& s:curve->Curve3.Segments){EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::Circle3>(s.Curve));rangeSum+=s.Range.Last-s.Range.First;}
        EXPECT_THROW(CompiledItem(scene,c.at("id")),std::exception);
    }
    EXPECT_NEAR(6.283185307179586,rangeSum,1e-12);
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after,Read(scene).at("intentToolpaths").To<Array>());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Split contours";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(after,Read(reopened).at("intentToolpaths").To<Array>());std::filesystem::remove(path);
}
TEST(FiveAxisCutSplitting, BatchValidationRollbackStaleAndOvercutRejection)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);const auto before=Read(scene),selection=CircleSplitSelection(scene,ids[0]);
    auto bad=CircleSplitSelection(scene,ids[1]);auto points=bad.at("points").To<Array>();auto anchor=points[0].To<Map>();anchor["sourceSpanId"]=Text("missing");bad["points"]=Array{anchor};
    EXPECT_THROW(Invoke(&scene,"CamCut","Split",SplitRequest(scene,{selection,bad})),std::exception);EXPECT_EQ(before,Read(scene));
    EXPECT_THROW(Invoke(nullptr,"CamCut","Split",SplitRequest(scene,{selection})),std::exception);
    auto stale=SplitRequest(scene,{selection});
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"overcutLengthMm",1.}}}}));
    EXPECT_THROW(Invoke(&scene,"CamCut","Split",stale),std::exception);
    EXPECT_THROW(Invoke(&scene,"CamCut","Split",SplitRequest(scene,{selection})),std::exception);
    ASSERT_TRUE(scene.Database().Undo());
    class RejectSplit final:public iCAX::Database::IChecker {
    public:bool Reject=true;
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text& error)override{if(Reject){Reject=false;error="Injected split attach failure";return false;}return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&)override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase&,const iCAX::Data::PropertySet&,Text&)override{return true;}
    };
    scene.Database().GetMetaRegistry()->RegistCheckerByName(std::make_shared<RejectSplit>(),"CEditableOperationComponent");
    const auto request=SplitRequest(scene,{selection,CircleSplitSelection(scene,ids[1])}),prior=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    EXPECT_THROW(Invoke(&scene,"CamCut","Split",request),std::exception);EXPECT_EQ(prior,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
    EXPECT_NO_THROW(Invoke(&scene,"CamCut","Split",request));EXPECT_EQ(4u,Read(scene).at("intentToolpaths").To<Array>().size());
    auto conflict=request;conflict["cuts"]=Array{selection};EXPECT_THROW(Invoke(&scene,"CamCut","Split",conflict),std::exception);
}

TEST(FiveAxisCutSplitting, NestedFragmentsKeepMaterialSupportAndPhysicalLeadSideAfterReopen)
{
    // Use the imported radius-4 hole and both retained-side choices. Tangential
    // travel may reverse; the physical scrap region must remain the same.
    for(const bool reverse:{false,true})for(const Text side:{Text("Exterior"),Text("Interior")}) {
        SCOPED_TRACE(side+(reverse?" reversed":" forward"));
        Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();
        if(side=="Interior")Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,{id},{{"retainedSide",side}}));
        if(reverse)Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));
        auto entry=Lead("Line",.8);entry["angleRad"]=.25;
        auto exit=Lead("Arc",.3);exit["radiusMm"]=.5;
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"leadIn",entry},{"leadOut",exit}}}}));
        const auto original=Read(scene).at("intentToolpaths").To<Array>().front().To<Map>();
        const auto root=PolicyBody(scene,original.at("featureRef").To<Map>());
        const auto originalMap=PolicyBody(scene,root.at("sourceMapData").To<Map>());
        const auto rootSpan=originalMap.at("spans").To<Array>().front().To<Map>();
        ASSERT_EQ(Text("Opening"),root.at("boundaryRole").To<Text>());
        Invoke(&scene,"CamCut","Split",SplitRequest(scene,{CircleSplitSelection(scene,id,0.,3.)}));
        const auto once=Read(scene).at("intentToolpaths");
        const auto fragment=DefinitionItem(scene,id),span=fragment.at("sourceSpans").To<Array>().front().To<Map>();
        const Map anchor{{"sourceSpanId",span.at("sourceSpanId")},{"nativeParameter",(span.at("first").To<double>()+span.at("last").To<double>())/2}};
        Invoke(&scene,"CamCut","Split",SplitRequest(scene,{Map{{"intentId",id},{"points",Array{anchor}}}}));
        const auto twice=Read(scene).at("intentToolpaths");ASSERT_EQ(3u,twice.To<Array>().size());

        const auto verify=[&](Scene& target)->Map {
            Array ids;bool foundNested=false;
            for(const auto& value:Read(target).at("intentToolpaths").To<Array>()) {
                const auto cut=value.To<Map>();ids.push_back(cut.at("id"));
                const auto definition=DefinitionItem(target,cut.at("id"));
                EXPECT_EQ(side,definition.at("retainedSide").To<Text>());
                EXPECT_EQ(reverse,definition.at("reversed").To<bool>());EXPECT_FALSE(definition.at("closed").To<bool>());
                EXPECT_EQ(original.at("processRef"),cut.at("processRef"));EXPECT_EQ(original.at("poseRef"),cut.at("poseRef"));
                const auto feature=PolicyBody(target,cut.at("featureRef").To<Map>());
                const auto map=PolicyBody(target,feature.at("sourceMapData").To<Map>());
                EXPECT_EQ(originalMap.at("sourceGeometry"),map.at("sourceGeometry"));
                auto parent=PolicyBody(target,feature.at("splitFromData").To<Map>());
                const auto parentMap=PolicyBody(target,parent.at("sourceMapData").To<Map>());
                for(const auto& v:map.at("spans").To<Array>()) {
                    const auto s=v.To<Map>();
                    for(const auto* key:{"faceId","wireId","edgeId","coedgeIndex","curveId"})EXPECT_EQ(rootSpan.at(key),s.at(key))<<key;
                    bool matched=false;
                    for(const auto& p:parentMap.at("spans").To<Array>()) {
                        const auto source=p.To<Map>();if(source.at("sourceSpanId")!=s.at("parentSourceSpanId"))continue;
                        matched=true;EXPECT_GE(s.at("first").To<double>(),source.at("first").To<double>());
                        EXPECT_LE(s.at("last").To<double>(),source.at("last").To<double>());EXPECT_EQ(source.at("reversed"),s.at("reversed"));
                    }
                    EXPECT_TRUE(matched);
                }
                int depth=1;
                while(parent.contains("splitFromData")&&depth<10){parent=PolicyBody(target,parent.at("splitFromData").To<Map>());++depth;}
                foundNested=foundNested||depth==2;EXPECT_EQ(root,parent);
            }
            EXPECT_TRUE(foundNested);
            const auto preview=LeadPreview(target,ids);
            for(const auto& value:preview.at("items").To<Array>()) {
                const auto leads=value.To<Map>().at("leads").To<Array>();EXPECT_EQ(2u,leads.size());
                for(const auto& v:leads) {
                    const auto lead=v.To<Map>();const auto points=lead.at("points").To<Array>();
                    const bool entering=lead.at("role").To<Text>()=="LeadIn";
                    const auto end=(entering?points.front():points.back()).To<Array>();
                    const auto anchor=lead.at("anchor").To<Array>();
                    EXPECT_NEAR(4.,std::hypot(anchor[0].To<double>(),anchor[1].To<double>()),1e-8);
                    const auto radius=std::hypot(end[0].To<double>(),end[1].To<double>());
                    if(side=="Exterior")EXPECT_LT(radius,4.);else EXPECT_GT(radius,4.);
                    EXPECT_NEAR(10.,end[2].To<double>(),1e-8);
                }
            }
            return preview;
        };
        const auto expectedLeads=verify(scene);
        ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(once,Read(scene).at("intentToolpaths"));
        ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(twice,Read(scene).at("intentToolpaths"));EXPECT_EQ(expectedLeads,verify(scene));
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Split material side";
        const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");
        file.Save(path,info,scene.Database(),scene.Resources());
        Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
        EXPECT_EQ(twice,Read(reopened).at("intentToolpaths"));EXPECT_EQ(expectedLeads,verify(reopened));std::filesystem::remove(path);
    }
}

TEST(FiveAxisCurveSplitting, PhysicalLengthTraversalAcrossSpatialPolylineNurbsAndSeam)
{
    using namespace iCAX::GeometryData;Scene scene;const auto service=scene.Services().Resolve<iCAX::Geometry::ICurveEditingService>();
    CompositeCurve3 poly;poly.Closed=true;Polyline3 p;p.Points={{0,0,0},{3,0,4},{3,10,4},{0,0,0}};poly.Segments={{p,{0,3,false}}};
    const auto trace=service->TraceClosed(poly,{0,.8},3,false);EXPECT_NEAR(1.2,trace.End.NativeParameter,1e-7);
    const auto end=trace.Piece.PreviewPoints.back();EXPECT_NEAR(3,end[0],1e-7);EXPECT_NEAR(2,end[1],1e-7);EXPECT_NEAR(4,end[2],1e-7);
    const auto back=service->TraceClosed(poly,trace.End,3,true);EXPECT_NEAR(.8,back.End.NativeParameter,1e-7);
    // Unequal rational weights: parameter fraction is not physical length fraction.
    NURBS3 n;n.Degree=2;n.Poles={{0,0,0},{8,3,5},{15,10,-3}};n.Weights={1,.7,1.2};n.Knots={2,7};n.Multiplicities={3,3};
    CompositeCurve3 curve;curve.Closed=true;curve.Segments={{n,{2,7,false}},{Line3{{{15,10,-3},{-15/std::sqrt(334.),-10/std::sqrt(334.),3/std::sqrt(334.)}}},{0,std::sqrt(334.),false}}};
    const double line=std::sqrt(334.);const auto seam=service->TraceClosed(curve,{1,line*.8},line*.2+2,false);
    EXPECT_EQ(0u,seam.End.SegmentIndex);EXPECT_GT(seam.End.NativeParameter,2);EXPECT_LT(seam.End.NativeParameter,7);
    ASSERT_EQ(2u,seam.Piece.Curve.Segments.size());EXPECT_EQ(n.Weights,std::get<NURBS3>(seam.Piece.Curve.Segments.back().Curve).Weights);
    const auto inverse=service->TraceClosed(curve,seam.End,line*.2+2,true);EXPECT_EQ(1u,inverse.End.SegmentIndex);EXPECT_NEAR(line*.8,inverse.End.NativeParameter,1e-7);
    const auto loop=service->TraceClosed(curve,{0,3.4},0,false);const auto full=service->TraceClosed(curve,{0,3.4},loop.PerimeterMm,false);
    EXPECT_EQ(0u,full.End.SegmentIndex);EXPECT_NEAR(3.4,full.End.NativeParameter,1e-7);
    curve.Segments[1].Range.Reversed=true;EXPECT_THROW(service->TraceClosed(curve,{0,3.4},1,false),std::exception);
}
TEST(FiveAxisCurveSplitting, CadJunctionsRetainExactSourcesAndRejectUnrelatedOrOutOfToleranceGaps)
{
    using namespace iCAX::GeometryData;using namespace iCAX::Geometry;
    Scene scene;const auto service=scene.Services().Resolve<ICurveEditingService>();BRepModel model;
    model.Vertices={{1,{0,0,0},.001},{2,{10,0,0},.001},{3,{10,10,0},.001}};
    CompositeCurve3 curve;curve.Closed=true;const double diagonal=std::sqrt(200.);
    curve.Segments={{Line3{{{0,0,0},{1,0,0}}},{0,10,false}},
        {Line3{{{10.0005,0,0},{0,1,0}}},{0,10,false}},
        {Line3{{{10,10,0},{-10/diagonal,-10/diagonal,0}}},{0,diagonal,false}}};
    std::vector<SBoundaryCurveSpan> spans;
    for(std::size_t i=0;i<3;++i){BRepEdge edge;edge.Id=i+1;edge.Curve3Id=i+1;edge.StartVertexId=i+1;edge.EndVertexId=(i+1)%3+1;edge.Range=curve.Segments[i].Range;model.Edges.push_back(edge);
        spans.push_back({i+1,i+1,edge.Range.First,edge.Range.Last,false});}
    const auto path=service->JoinBoundary(model,curve,spans,.01);ASSERT_EQ(2u,path.Junctions.size());ASSERT_EQ(5u,path.Curve.Segments.size());
    ASSERT_EQ(5u,path.SourceSegments.size());EXPECT_EQ(0u,*path.SourceSegments[0]);EXPECT_FALSE(path.SourceSegments[1]);EXPECT_EQ(1u,*path.SourceSegments[2]);EXPECT_FALSE(path.SourceSegments[3]);
    EXPECT_DOUBLE_EQ(10.0005,std::get<Line3>(path.Curve.Segments[2].Curve).Axis.Location.X);
    const auto loop=service->TraceClosed(path.Curve,{0,0},0,false),full=service->TraceClosed(path.Curve,{0,0},loop.PerimeterMm,false);
    EXPECT_NEAR(20+diagonal+.001,loop.PerimeterMm,1e-9);EXPECT_NEAR(0,full.Piece.PreviewPoints.back()[0],1e-9);
    EXPECT_NEAR(loop.PerimeterMm,service->TraceClosed(path.Curve,{0,0},loop.PerimeterMm,true).PerimeterMm,1e-9);
    EXPECT_THROW(service->JoinBoundary(model,curve,spans,.0001),std::invalid_argument);
    auto outside=model;outside.Vertices[1].Tolerance=outside.Vertices[2].Tolerance=1e-5;
    EXPECT_THROW(service->JoinBoundary(outside,curve,spans,.01),std::invalid_argument);
    auto unrelated=model;unrelated.Edges[1].StartVertexId=22;unrelated.Vertices.push_back({22,{10,0,0},.001});
    EXPECT_THROW(service->JoinBoundary(unrelated,curve,spans,.01),std::invalid_argument);
}

TEST(FiveAxisCutEditing, ReverseOpenFragmentsSwapsAnchorsKeepsMaterialAndSurvivesReopen)
{
    for(const bool initiallyReversed:{false,true})for(const Text side:{Text("Interior"),Text("Exterior")}){
        Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();
        Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,{id},{{"retainedSide",side}}));
        if(initiallyReversed)Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));
        auto entry=Lead("Line",.4);entry["angleRad"]=.2;
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"leadIn",entry},{"leadOut",Lead("Arc",.2)}}}}));
        const auto ids=Invoke(&scene,"CamCut","Split",SplitRequest(scene,{CircleSplitSelection(scene,id,.5,3.)})).at("intentIds").To<Array>();
        const auto before=Read(scene);const auto leads=LeadPreview(scene,ids).at("items").To<Array>();
        const auto undo=scene.Database().GetUndoArray().size();const auto request=EditRequest(scene,ids,{});
        const auto result=Invoke(&scene,"CamCut","Reverse",request);EXPECT_EQ(result,Invoke(&scene,"CamCut","Reverse",request));
        EXPECT_EQ(undo+1,scene.Database().GetUndoArray().size());
        const auto after=Read(scene);const auto reversedLeads=LeadPreview(scene,ids).at("items").To<Array>();
        for(std::size_t i=0;i<ids.size();++i){
            const auto old=before.at("intentToolpaths").To<Array>()[i].To<Map>(),cut=after.at("intentToolpaths").To<Array>()[i].To<Map>();
            const auto forward=leads[i].To<Map>(),backward=reversedLeads[i].To<Map>();
            EXPECT_EQ(forward.at("end"),backward.at("start"));EXPECT_EQ(forward.at("start"),backward.at("end"));
            EXPECT_EQ(backward.at("start"),cut.at("startPoint"));EXPECT_NE(old.at("reversed"),cut.at("reversed"));
            for(const auto key:{"curveRef","processRef","poseRef","retainedSide","sourceKey"})EXPECT_EQ(old.at(key),cut.at(key));
            const auto oldFeature=PolicyBody(scene,old.at("featureRef").To<Map>()),newFeature=PolicyBody(scene,cut.at("featureRef").To<Map>());
            EXPECT_EQ(oldFeature.at("sourceMapData"),newFeature.at("sourceMapData"));EXPECT_EQ(oldFeature.at("splitFromData"),newFeature.at("splitFromData"));
            for(const auto& item:backward.at("leads").To<Array>()){
                const auto lead=item.To<Map>();const auto points=lead.at("points").To<Array>();
                const auto end=(lead.at("role").To<Text>()=="LeadIn"?points.front():points.back()).To<Array>();
                if(side=="Exterior")EXPECT_LT(std::hypot(end[0].To<double>(),end[1].To<double>()),4.);
                else EXPECT_GT(std::hypot(end[0].To<double>(),end[1].To<double>()),4.);
            }
        }
        ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
        ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after.at("intentToolpaths"),Read(scene).at("intentToolpaths"));
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Reverse open curves";
        const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
        Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
        EXPECT_EQ(after.at("intentToolpaths"),Read(reopened).at("intentToolpaths"));EXPECT_EQ(reversedLeads,LeadPreview(reopened,ids).at("items").To<Array>());
        Invoke(&reopened,"CamCut","Reverse",EditRequest(reopened,ids,{}));EXPECT_EQ(leads,LeadPreview(reopened,ids).at("items").To<Array>());std::filesystem::remove(path);
    }
}
