namespace
{
    Map AxisSamplesRead(Scene& scene,const Array* ids=nullptr)
    {Map r{{"expected",Read(scene).at("expected")}};if(ids)r["intentIds"]=*ids;return Invoke(&scene,"CamCut","ReadToolAxisSamples",r);}
    std::array<double,3> AxisSampleVector(const Variant& value){const auto a=value.To<Array>();return {a.at(0).To<double>(),a.at(1).To<double>(),a.at(2).To<double>()};}
}
TEST(FiveAxisToolAxisDisplay, EveryEnabledCutIsSampledBeforeGenerationAndTheReadDoesNotChangeTheProject)
{
    Scene scene;PrepareOrderPart(scene,false);const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto response=AxisSamplesRead(scene);EXPECT_EQ("Workpiece",response.at("frame").To<Text>());EXPECT_EQ("NominalCutDefinition",response.at("geometry").To<Text>());EXPECT_TRUE(response.at("errors").To<Array>().empty());
    const auto cuts=response.at("cuts").To<Array>();std::set<Text> sampled;
    for(const auto& value:cuts){const auto cut=value.To<Map>();EXPECT_TRUE(sampled.insert(cut.at("intentId").To<Text>()).second);const auto samples=cut.at("samples").To<Array>();ASSERT_GT(samples.size(),4u);
        unsigned displayable=0;for(const auto& s:samples){const auto sample=s.To<Map>();EXPECT_EQ(3u,sample.at("point").To<Array>().size());AxisNear(sample.at("surfaceNormal"),{0,0,1});AxisNear(sample.at("direction"),{0,0,1});EXPECT_TRUE(sample.at("anchor").To<Map>().contains("sourceSpanId"));if(!sample.at("keypointIds").To<Array>().empty())++displayable;}EXPECT_GT(displayable,0u);EXPECT_LT(displayable,samples.size());}
    for(const auto& value:before.at("intentToolpaths").To<Array>()){const auto cut=value.To<Map>();if(cut.at("enabled").To<bool>()&&cut.at("machiningKind").To<Text>()=="Cut")EXPECT_TRUE(sampled.contains(cut.at("id").To<Text>()));}
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    ASSERT_FALSE(cuts.empty());const auto id=cuts.front().To<Map>().at("intentId");auto disable=GenerateRequest(scene);disable["action"]=Text("Disable");disable["intentIds"]=Array{id};Invoke(&scene,"CamCut","ManageTree",disable);
    const auto enabled=AxisSamplesRead(scene).at("cuts").To<Array>();EXPECT_EQ(cuts.size()-1,enabled.size());for(const auto& cut:enabled)EXPECT_NE(id,cut.To<Map>().at("intentId"));
    const Array onlyDisabled{id};EXPECT_TRUE(AxisSamplesRead(scene,&onlyDisabled).at("cuts").To<Array>().empty());
}
TEST(FiveAxisToolAxisDisplay, FixedAndLocalDirectionsUseExactAnchorsAndInheritedLocksRemainReadable)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});
    const auto anchor=AxisAnchorAt(scene,id,1.23456789);Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),anchor));
    const Array only{id};const auto source=Read(scene).at("workpieces");auto response=AxisSamplesRead(scene,&only);ASSERT_TRUE(response.at("errors").To<Array>().empty());ASSERT_EQ(1u,response.at("cuts").To<Array>().size());
    const auto samples=response.at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();bool exact=false;
    for(const auto& value:samples){const auto s=value.To<Map>(),at=s.at("anchor").To<Map>();const auto truth=AxisPointRead(scene,id,at);EXPECT_EQ(truth.at("point"),s.at("point"));EXPECT_EQ(truth.at("surfaceNormal"),s.at("surfaceNormal"));AxisNear(s.at("direction"),AxisSampleVector(truth.at("direction")));
        if(at==anchor){exact=true;AxisNear(s.at("direction"),{0,.6,.8});EXPECT_FALSE(s.at("keypointIds").To<Array>().empty());}}
    EXPECT_TRUE(exact);Invoke(&scene,"CamCut","SetToolAxis",AxisRequest(scene,id,"Fixed",{.2,0,1.},"Workpiece"));const auto fixed=AxisSamplesRead(scene,&only);EXPECT_TRUE(fixed.at("errors").To<Array>().empty());
    for(const auto& v:fixed.at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>()){const auto s=v.To<Map>();AxisNear(s.at("direction"),AxisSampleVector(AxisPointRead(scene,id,s.at("anchor").To<Map>()).at("direction")));}
    auto lock=GenerateRequest(scene);lock["action"]=Text("Lock");lock["intentIds"]=Array{id};Invoke(&scene,"CamCut","ManageTree",lock);const auto locked=Read(scene);EXPECT_EQ(fixed,AxisSamplesRead(scene,&only));EXPECT_EQ(locked,Read(scene));EXPECT_EQ(source,Read(scene).at("workpieces"));
}
TEST(FiveAxisToolAxisDisplay, BatchScopeAndExpectedRevisionAreValidated)
{
    Scene scene;PrepareOrderPart(scene,false);const auto state=Read(scene);const auto id=state.at("intentToolpaths").To<Array>().front().To<Map>().at("id");
    Map request{{"expected",state.at("expected")},{"intentIds",Array{id,id}}};EXPECT_THROW(Invoke(&scene,"CamCut","ReadToolAxisSamples",request),std::invalid_argument);
    request["intentIds"]=Array{Text("bad")};EXPECT_THROW(Invoke(&scene,"CamCut","ReadToolAxisSamples",request),std::exception);
    request["intentIds"]=Array{id};auto stale=state.at("expected").To<Map>();stale["mutationSequence"]=Text("0");request["expected"]=stale;EXPECT_THROW(Invoke(&scene,"CamCut","ReadToolAxisSamples",request),std::invalid_argument);
    const Array empty;EXPECT_TRUE(AxisSamplesRead(scene,&empty).at("cuts").To<Array>().empty());EXPECT_EQ(state,Read(scene));
}

TEST(FiveAxisToolAxisDisplay, PerforatedCoverSplineMeasuresResolveAcrossEveryKnot)
{
    Scene scene;const auto path=std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()
        / u8"../../../../../../TestData/FA/刀路/刀路/整理测试刀路/多槽圆盖-3-7b202.igs").u8string();
    const auto initial=Read(scene);const auto imported=Wait(scene,Invoke(&scene,"CamWorkpiece","Import",{
        {"commandId",NewAxisPoint()},{"expected",initial.at("expected")},{"setupId",initial.at("activeSetupId")},
        {"sourcePath",Text(reinterpret_cast<const char*>(path.data()),path.size())}}));
    ASSERT_EQ("Completed",imported.at("status").To<Text>());
    const auto ref=Read(scene).at("workpieces").To<Array>().front().To<Map>().at("geometryRef").To<Map>();
    const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto editing=scene.Services().Resolve<iCAX::Geometry::ICurveEditingService>();std::size_t checked=0;
    for(const auto& edge:geometry->Edges){
        if(edge.Degenerated||!edge.Curve3Id)continue;SCOPED_TRACE(edge.Id);
        const auto curve=std::find_if(geometry->Curves3.begin(),geometry->Curves3.end(),[&](const auto& c){return c.Id==edge.Curve3Id;});ASSERT_NE(curve,geometry->Curves3.end());
        iCAX::GeometryData::CurveSegment3 segment{curve->Geometry,edge.Range};segment.Range.Reversed=false;
        iCAX::Geometry::SCurveMeasure full;ASSERT_NO_THROW(full=editing->Measure(segment));EXPECT_GT(full.LengthMm,0.);
        // The same exact spline measured in differently trimmed intervals must
        // give the same length and total turning, including an intervening corner.
        auto left=segment,right=segment;left.Range.Last=right.Range.First=segment.Range.First+.371*(segment.Range.Last-segment.Range.First);
        const auto a=editing->Measure(left),b=editing->Measure(right);double dot=0;for(int i=0;i<3;++i)dot+=a.LastTangent[i]*b.FirstTangent[i];
        EXPECT_NEAR(full.LengthMm,a.LengthMm+b.LengthMm,2e-7);
        EXPECT_NEAR(full.TurnRad,a.TurnRad+b.TurnRad+std::acos(std::clamp(dot,-1.,1.)),2e-7);
        segment.Range.Reversed=true;const auto reverse=editing->Measure(segment);EXPECT_NEAR(full.TurnRad,reverse.TurnRad,1e-10);
        for(int i=0;i<3;++i)EXPECT_NEAR(full.FirstTangent[i],-reverse.LastTangent[i],1e-10);
        ++checked;
    }
    EXPECT_EQ(536u,geometry->Edges.size());EXPECT_EQ(534u,checked);RecordProperty("measured_edges",static_cast<int>(checked));
}
