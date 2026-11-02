namespace
{
    Map AxisRequest(Scene& scene,const Variant& id,const Text& mode="Fixed",const Array& direction=Array{0.,3.,4.},const Text& frame="Setup")
    {
        auto r=GenerateRequest(scene);r.erase("setupId");r["intentId"]=id;r["mode"]=mode;r["frame"]=frame;r["scope"]=Text("WholePath");if(mode=="Fixed")r["direction"]=direction;return r;
    }
    Map AxisRead(Scene& scene,const Variant& id){return Invoke(&scene,"CamCut","ReadToolAxis",{{"expected",Read(scene).at("expected")},{"intentId",id}});}
    Map AxisOperation(const Map& generated,const Variant& id){for(const auto& v:generated.at("operations").To<Array>())if(v.To<Map>().at("intentId")==id)return v.To<Map>();throw std::logic_error("missing operation");}
    void AxisNear(const Variant& value,const std::array<double,3>& expected){const auto a=value.To<Array>();ASSERT_EQ(3u,a.size());for(int i=0;i<3;++i)EXPECT_NEAR(expected[i],a[i].To<double>(),1e-9);}
}
TEST(FiveAxisToolAxis, NormalizesSetupInputAndGeneratesTheStoredAxisWithoutChangingSourceGeometry)
{
    Scene scene;PrepareOrderPart(scene);auto state=Read(scene);const auto id=OrderedIDs(Generated(scene)).front();const auto original=AxisOperation(Generated(scene),id);
    const auto p=state.at("placements").To<Array>().front().To<Map>();Array transform{0.,-1.,0.,11.,1.,0.,0.,22.,0.,0.,1.,33.,0.,0.,0.,1.};
    auto move=GenerateRequest(scene);move["placementId"]=p.at("id");move["transform"]=transform;Invoke(&scene,"CamPlacement","SetTransform",move);
    const auto source=Read(scene).at("workpieces");const auto normal=AxisRead(scene,id);ASSERT_EQ("Start",normal.at("sample").To<Text>());
    const auto request=AxisRequest(scene,id);const auto result=Invoke(&scene,"CamCut","SetToolAxis",request);EXPECT_TRUE(result.at("changed").To<bool>());
    EXPECT_EQ(result,Invoke(&scene,"CamCut","SetToolAxis",request));AxisNear(AxisRead(scene,id).at("direction"),{.6,0,.8});AxisNear(AxisRead(scene,id).at("setupDirection"),{0,.6,.8});
    EXPECT_EQ(source,Read(scene).at("workpieces"));EXPECT_EQ("Stale",Generated(scene).at("status").To<Text>());
    const auto noOp=Read(scene);EXPECT_FALSE(Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id)).at("changed").To<bool>());EXPECT_EQ(noOp,Read(scene));
    const auto generated=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));ASSERT_EQ("Completed",generated.at("status").To<Text>());
    const auto op=AxisOperation(Generated(scene),id);const auto parts=op.at("parts").To<Array>();ASSERT_EQ(original.at("parts").To<Array>().size(),parts.size());
    for(std::size_t i=0;i<parts.size();++i){const auto part=parts[i].To<Map>();EXPECT_EQ(original.at("parts").To<Array>()[i].To<Map>().at("points"),part.at("points"));for(const auto& v:part.at("directions").To<Array>())AxisNear(v,{.6,0,.8});}
    AxisNear(op.at("setupStartDirection"),{0,.6,.8});AxisNear(op.at("setupEndDirection"),{0,.6,.8});
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"SurfaceNormal"));EXPECT_EQ("SurfaceNormal",AxisRead(scene,id).at("mode").To<Text>());EXPECT_EQ(normal.at("surfaceNormal"),AxisRead(scene,id).at("direction"));
}
TEST(FiveAxisToolAxis, ValidationAndInheritedLocksAreAtomicAndUndoRedoRestorePose)
{
    Scene scene;PrepareOrderPart(scene);const auto ids=OrderedIDs(Generated(scene));const auto id=ids.front();const auto initial=AxisRead(scene,id);
    for(const Array bad:{Array{0.,0.,0.},Array{1.,2.},Array{Text("x"),0.,1.}}){const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",bad)),std::exception);EXPECT_EQ(before,Read(scene));}
    auto bad=AxisRequest(scene,id);bad["frame"]=Text("Screen");EXPECT_THROW(Invoke(&scene,"CamCut","SetToolAxis",bad),std::invalid_argument);
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id));const auto applied=AxisRead(scene,id);ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(initial,AxisRead(scene,id));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(applied,AxisRead(scene,id));
    auto group=GenerateRequest(scene);group["action"]=Text("Group");group["intentIds"]=Array{ids[0],ids[1]};const auto created=Invoke(&scene,"CamCut","ManageTree",group);
    auto lock=GenerateRequest(scene);lock["action"]=Text("Lock");lock["intentIds"]=Array{};lock["groupIds"]=Array{created.at("groupId")};Invoke(&scene,"CamCut","ManageTree",lock);
    const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id)),std::invalid_argument);EXPECT_EQ(before,Read(scene));
}
namespace
{
    Map AxisPointRead(Scene& scene,const Variant& id,const Map& anchor)
    {return Invoke(&scene,"CamCut","ReadToolAxis",{{"expected",Read(scene).at("expected")},{"intentId",id},{"anchor",anchor}});}
    Map AxisPointRequest(Scene& scene,const Variant& id,const Variant& point,const Map& anchor,const Array& direction=Array{0.,3.,4.},const Text& transition="ArcLength")
    {auto r=GenerateRequest(scene);r.erase("setupId");r["intentId"]=id;r["pointId"]=point;r["action"]=Text("Upsert");r["anchor"]=anchor;r["direction"]=direction;r["frame"]=Text("Workpiece");r["transition"]=transition;return r;}
    Variant NewAxisPoint(){return Text(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));}
    Map AxisAnchorAt(Scene& scene,const Variant& id,double u,std::size_t segment=0)
    {const auto spans=DefinitionItem(scene,id).at("sourceSpans").To<Array>();return {{"sourceSpanId",spans.at(segment).To<Map>().at("sourceSpanId")},{"nativeParameter",u}};}
    void GenerateAxis(Scene& scene)
    {const auto done=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));ASSERT_EQ("Completed",done.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(done);}
    void CheckGeneratedAxisPoint(Scene& scene,const Variant& id,const Map& anchor,const std::array<double,3>& expected)
    {
        const auto op=AxisOperation(Generated(scene),id);unsigned hits=0;
        for(const auto& p:op.at("parts").To<Array>()){
            const auto part=p.To<Map>();if(!part.contains("sourceSamples"))continue;
            const auto samples=part.at("sourceSamples").To<Array>(),directions=part.at("directions").To<Array>();
            for(std::size_t i=0;i<samples.size();++i){const auto sample=samples[i].To<Map>();
                if(sample.contains("sourceSpanId")&&sample.at("sourceSpanId")==anchor.at("sourceSpanId")&&std::abs(sample.at("nativeParameter").To<double>()-anchor.at("nativeParameter").To<double>())<1e-10){AxisNear(directions[i],expected);++hits;}
                double n=0;for(const auto& v:directions[i].To<Array>())n+=v.To<double>()*v.To<double>();EXPECT_NEAR(1,n,1e-10);
            }
        }EXPECT_GT(hits,0u)<<"The exact hard-point anchor must be sampled";
    }
}
TEST(FiveAxisToolAxis, LocalHardPointIsExactUndoableAndSurvivesReverseStartAndWholePathEdits)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});
    const auto before=DefinitionItem(scene,id);const auto anchor=AxisAnchorAt(scene,id,1.23456789);const auto point=NewAxisPoint();const auto request=AxisPointRequest(scene,id,point,anchor);
    const auto result=Invoke(&scene,"CamCut","EditToolAxisPoint",request);EXPECT_EQ(result,Invoke(&scene,"CamCut","EditToolAxisPoint",request));
    AxisNear(AxisPointRead(scene,id,anchor).at("direction"),{0,.6,.8});EXPECT_EQ(before.at("feature"),DefinitionItem(scene,id).at("feature"));EXPECT_EQ(before.at("sourceSpans"),DefinitionItem(scene,id).at("sourceSpans"));
    auto repeat=AxisPointRequest(scene,id,point,anchor);EXPECT_FALSE(Invoke(&scene,"CamCut","EditToolAxisPoint",repeat).at("changed").To<bool>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(Text("0"),AxisRead(scene,id).at("keypointCount").To<Text>());ASSERT_TRUE(scene.Database().Redo());GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,anchor,{0,.6,.8});
    const auto length=AxisOperation(Generated(scene),id).at("lengthMm");
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));auto start=anchor;start["nativeParameter"]=2.3;Invoke(&scene,"CamCut","SetStart",EditRequest(scene,{id},{{"start",start}}));GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,anchor,{0,.6,.8});EXPECT_NEAR(length.To<double>(),AxisOperation(Generated(scene),id).at("lengthMm").To<double>(),1e-8);
    Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",{.2,0.,1.},"Workpiece"));GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,anchor,{0,.6,.8});
    auto reset=AxisPointRequest(scene,id,point,anchor);reset["action"]=Text("Delete");Invoke(&scene,"CamCut","EditToolAxisPoint",reset);EXPECT_EQ("0",AxisRead(scene,id).at("keypointCount").To<Text>());
}
TEST(FiveAxisToolAxis, EditingOneVectorPreservesEveryOtherReferenceAndManualVectorInReadAndGeneratedData)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});
    const auto sample=[&](){return Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();};
    const auto baseline=sample();ASSERT_GT(baseline.size(),10u);const auto index=baseline.size()/2;
    const auto anchor=baseline[index].To<Map>().at("anchor").To<Map>(),neighbor=baseline[index+1].To<Map>().at("anchor").To<Map>();
    const auto point=NewAxisPoint(),nextPoint=NewAxisPoint();const auto original=Read(scene).at("workpieces");
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,point,anchor,{.3,0.,.4}));
    for(std::size_t i=0;i<baseline.size();++i){const auto row=baseline[i].To<Map>();if(i==index)continue;EXPECT_EQ(row.at("direction"),AxisPointRead(scene,id,row.at("anchor").To<Map>()).at("direction"));}
    // The interval is continuous while the next reference remains fixed.
    auto between=anchor;between["nativeParameter"]=(anchor.at("nativeParameter").To<double>()+neighbor.at("nativeParameter").To<double>())/2;
    const auto middle=AxisPointRead(scene,id,between).at("direction").To<Array>();EXPECT_GT(middle[0].To<double>(),0);EXPECT_LT(middle[0].To<double>(),.6);
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,nextPoint,neighbor,{0.,.3,.4}));
    AxisNear(AxisPointRead(scene,id,anchor).at("direction"),{.6,0,.8});
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,point,anchor,{.15,0.,.4}));
    AxisNear(AxisPointRead(scene,id,neighbor).at("direction"),{0,.6,.8});GenerateAxis(scene);
    for(std::size_t i=0;i<baseline.size();++i){if(i==index||i==index+1)continue;const auto row=baseline[i].To<Map>();const auto d=row.at("direction").To<Array>();CheckGeneratedAxisPoint(scene,id,row.at("anchor").To<Map>(),{d[0].To<double>(),d[1].To<double>(),d[2].To<double>()});}
    CheckGeneratedAxisPoint(scene,id,neighbor,{0,.6,.8});const auto updated=AxisPointRead(scene,id,anchor).at("direction");
    ASSERT_TRUE(scene.Database().Undo());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(updated,AxisPointRead(scene,id,anchor).at("direction"));
    auto erase=AxisPointRequest(scene,id,point,anchor);erase["action"]=Text("Delete");Invoke(&scene,"CamCut","EditToolAxisPoint",erase);
    EXPECT_EQ(baseline[index].To<Map>().at("direction"),AxisPointRead(scene,id,anchor).at("direction"));AxisNear(AxisPointRead(scene,id,neighbor).at("direction"),{0,.6,.8});
    EXPECT_EQ(original,Read(scene).at("workpieces"));
}
TEST(FiveAxisToolAxis, EndpointsGapProtectedDeletionInvalidAnchorsOpposedDirectionsAndLocksAreAtomic)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"gapLengthMm",2.}}}}));
    const auto list=Invoke(&scene,"CamCut","ListToolAxisPoints",{{"expected",Read(scene).at("expected")},{"intentId",id}}).at("points").To<Array>();ASSERT_EQ(2u,list.size());
    auto end=list[1].To<Map>().at("anchor").To<Map>();Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,Text("end"),end));GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,end,{0,.6,.8});
    for(const auto& p:{Text("start"),Text("end")}){auto r=AxisPointRequest(scene,id,p,end);r["action"]=Text("Delete");const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","EditToolAxisPoint",r),std::invalid_argument);EXPECT_EQ(before,Read(scene));}
    for(const auto& direction:{Array{0.,0.,0.}}){const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),AxisAnchorAt(scene,id,1.2),direction)),std::invalid_argument);EXPECT_EQ(before,Read(scene));}
    auto outside=end;outside["nativeParameter"]=1000.;const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),outside)),std::exception);EXPECT_EQ(before,Read(scene));
    auto lock=GenerateRequest(scene);lock["action"]=Text("Lock");lock["intentIds"]=Array{id};Invoke(&scene,"CamCut","ManageTree",lock);const auto locked=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,Text("end"),end)),std::invalid_argument);EXPECT_EQ(locked,Read(scene));
}
TEST(FiveAxisToolAxis, SplitRemapsPhysicalKeypointToItsFragmentAndRegenerates)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});
    const auto anchor=AxisAnchorAt(scene,id,1.2);Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),anchor));
    const auto pieces=Invoke(&scene,"CamCut","Split",SplitRequest(scene,{CircleSplitSelection(scene,id,0,3)})).at("intentIds").To<Array>();unsigned count=0;GenerateAxis(scene);
    for(const auto& fragment:pieces){const auto pose=ProcessItem(scene,fragment).at("posePolicy").To<Map>();for(const auto& p:pose.at("keypoints").To<Array>()){++count;CheckGeneratedAxisPoint(scene,fragment,p.To<Map>().at("anchor").To<Map>(),{0,.6,.8});}}
    EXPECT_EQ(1u,count);
}
TEST(FiveAxisToolAxis, IntrinsicMeasuresUseArcLengthAndIncludePolylineCornerTurn)
{
    using namespace iCAX::GeometryData;Scene scene;const auto editing=scene.Services().Resolve<iCAX::Geometry::ICurveEditingService>();
    Circle3 circle;circle.Radius=7;auto m=editing->Measure({circle,{.3,2.3,false}});EXPECT_NEAR(14,m.LengthMm,1e-8);EXPECT_NEAR(2,m.TurnRad,1e-8);
    const auto reverse=editing->Measure({circle,{.3,2.3,true}});EXPECT_NEAR(m.LengthMm,reverse.LengthMm,1e-9);for(int i=0;i<3;++i)EXPECT_NEAR(-m.FirstTangent[i],reverse.LastTangent[i],1e-9);
    Polyline3 poly;poly.Points={{0,0,0},{4,0,0},{4,3,0}};m=editing->Measure({poly,{0,2,false}});EXPECT_NEAR(7,m.LengthMm,1e-8);EXPECT_NEAR(1.5707963267948966,m.TurnRad,1e-7);
    NURBS3 spline;spline.Degree=2;spline.Poles={{0,0,0},{2,0,0},{10,0,0}};spline.Weights={1,1,1};spline.Knots={2,7};spline.Multiplicities={3,3};
    const auto half=editing->Measure({spline,{2,4.5,false}});EXPECT_NEAR(3.5,half.LengthMm,1e-8);EXPECT_NEAR(0,half.TurnRad,1e-8);
}
TEST(FiveAxisToolAxis, CurvedSupportNormalsRemainVariableAndLocalTransitionsPreserveOtherControls)
{
    Scene scene;File file(BRepBuilderAPI_MakeFace(gp_Cylinder(gp_Ax3(),20),0,1.5707963267948966,0,15).Face());const auto ids=Imported(scene,file);
    
    const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",Read(scene).at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});
    Map extraction{{"commandId",NewAxisPoint()},{"expected",Read(scene).at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
        {"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},{"selection",Map{{"mode",Text("Faces")},{"faceUses",sources.at("faceUses")}}},
        {"representation",Text("SkinSurface")},{"workpieceFamily",Text("SheetSurface")},{"declaredThicknessMm",2.},{"side",Map{{"outwardSign",1.}}},{"geometryOnly",true},
        {"requestedKinds",Array{Text("Circle"),Text("Rectangle"),Text("Slot"),Text("GeneralContour")}},{"draftId",NewAxisPoint()},{"draftRevision",Text("1")},{"draftDigest",Text("cylinder-axis")}};
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(terminal);
    const auto created=Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal)).at("createdIntentIds").To<Array>();ASSERT_EQ(1u,created.size());const auto id=created.front();ZeroGenerationPolicies(scene,created);GenerateAxis(scene);
    const auto spans=DefinitionItem(scene,id).at("sourceSpans").To<Array>();ASSERT_GE(spans.size(),4u);
    const auto mid=[&](std::size_t index){const auto s=spans[index].To<Map>();return AxisAnchorAt(scene,id,(s.at("first").To<double>()+s.at("last").To<double>())/2,index);};
    const auto a=mid(0),b=mid(2);auto na=AxisPointRead(scene,id,a).at("direction").To<Array>(),nb=AxisPointRead(scene,id,b).at("direction").To<Array>();na[2]=.35;nb[2]=-.2;
    const auto point=NewAxisPoint();Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,point,a,na));Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),b,nb));
    std::vector<Map> probes;for(std::size_t i=0;i<spans.size();++i){const auto span=spans[i].To<Map>();for(double t:{.23,.61})probes.push_back(AxisAnchorAt(scene,id,span.at("first").To<double>()*(1-t)+span.at("last").To<double>()*t,i));}
    std::vector<Array> arc;for(const auto& at:probes)arc.push_back(AxisPointRead(scene,id,at).at("direction").To<Array>());
    const auto controls=Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,point,a,na,"DirectionChange"));
    for(const auto& v:controls){const auto control=v.To<Map>();EXPECT_EQ(control.at("direction"),AxisPointRead(scene,id,control.at("anchor").To<Map>()).at("direction"));}
    // Constant-curvature arcs give the same local law for both metrics. They
    // must not smear a correction over intervening straight/corner controls.
    for(std::size_t i=0;i<probes.size();++i){const auto direction=AxisPointRead(scene,id,probes[i]).at("direction").To<Array>();for(int j=0;j<3;++j)EXPECT_NEAR(direction[j].To<double>(),arc[i][j].To<double>(),1e-8);}
    GenerateAxis(scene);
    const auto normalize=[](const Array& values){std::array<double,3> result;double norm=0;for(int i=0;i<3;++i){result[i]=values[i].To<double>();norm+=result[i]*result[i];}for(auto& x:result)x/=std::sqrt(norm);return result;};
    CheckGeneratedAxisPoint(scene,id,a,normalize(na));CheckGeneratedAxisPoint(scene,id,b,normalize(nb));
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{id},{{"patch",Map{{"reverseAxes",true}}}}));
    for(std::size_t i=0;i<probes.size();++i){const auto d=AxisPointRead(scene,id,probes[i]).at("direction").To<Array>();for(int j=0;j<3;++j)EXPECT_NEAR(-arc[i][j].To<double>(),d[j].To<double>(),1e-8);}
    GenerateAxis(scene);
    const auto op=AxisOperation(Generated(scene),id);double range=0;const auto parts=op.at("parts").To<Array>();const auto directions=parts.front().To<Map>().at("directions").To<Array>();
    for(const auto& v:directions)for(int j=0;j<3;++j)range=std::max(range,std::abs(v.To<Array>()[j].To<double>()-directions.front().To<Array>()[j].To<double>()));EXPECT_GT(range,.5);
}

TEST(FiveAxisToolAxis, ReverseAllAndSingleAntipodalPointReachGeneratedData)
{
    Scene scene;PrepareOrderPart(scene);const auto ids=OrderedIDs(Generated(scene));const auto id=ids.front();
    const auto original=AxisRead(scene,id);const auto source=Read(scene).at("workpieces");
    auto reverse=EditRequest(scene,ids,{{"patch",Map{{"reverseAxes",true}}}});
    const auto receipt=Invoke(&scene,"CamProcess","UpdatePose",reverse);EXPECT_EQ(receipt,Invoke(&scene,"CamProcess","UpdatePose",reverse));
    const auto before=original.at("direction").To<Array>();AxisNear(AxisRead(scene,id).at("direction"),{-before[0].To<double>(),-before[1].To<double>(),-before[2].To<double>()});
    GenerateAxis(scene);EXPECT_EQ(source,Read(scene).at("workpieces"));
    for(const auto& part:AxisOperation(Generated(scene),id).at("parts").To<Array>())for(const auto& d:part.To<Map>().at("directions").To<Array>())AxisNear(d,{-before[0].To<double>(),-before[1].To<double>(),-before[2].To<double>()});
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,ids,{{"patch",Map{{"reverseAxes",true}}}}));AxisNear(AxisRead(scene,id).at("direction"),{before[0].To<double>(),before[1].To<double>(),before[2].To<double>()});
    const auto samples=Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();
    const auto at=samples[samples.size()/2].To<Map>().at("anchor").To<Map>();
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),at,{-before[0].To<double>(),-before[1].To<double>(),-before[2].To<double>()}));
    GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,at,{-before[0].To<double>(),-before[1].To<double>(),-before[2].To<double>()});
    for(std::size_t i=0;i<samples.size();++i){if(i==samples.size()/2)continue;const auto row=samples[i].To<Map>();const auto d=row.at("direction").To<Array>();AxisNear(AxisPointRead(scene,id,row.at("anchor").To<Map>()).at("direction"),{d[0].To<double>(),d[1].To<double>(),d[2].To<double>()});}
}
