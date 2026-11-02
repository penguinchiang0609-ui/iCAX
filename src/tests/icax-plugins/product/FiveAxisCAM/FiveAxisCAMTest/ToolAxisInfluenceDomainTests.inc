namespace
{
    Array InfluenceSamples(Scene& scene,const Variant& id){return Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",Array{id}}}).at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();}
    Map InfluenceRequest(Scene& scene,const Variant& id,const Variant& point,const Map& anchor,const Array& targets,const Text& interpolation="Smooth",const Array& direction=Array{0.,.6,.8})
    {auto r=AxisPointRequest(scene,id,point,anchor,direction);r["influence"]=Map{{"interpolation",interpolation},{"targets",targets}};return r;}
    Map InfluenceTarget(const Variant& sample,double weight){return {{"anchor",sample.To<Map>().at("anchor")},{"weight",weight}};}
}
TEST(FiveAxisToolAxisInfluence, ExplicitSelectionWeightsAreExactAndDoNotAccumulateAcrossApplyOrRegeneration)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});const auto original=Read(scene).at("workpieces");
    const auto samples=InfluenceSamples(scene,id);ASSERT_GT(samples.size(),10u);const auto center=samples.size()/2;
    const auto at=samples[center].To<Map>().at("anchor").To<Map>(),left=samples[center-2].To<Map>().at("anchor").To<Map>(),right=samples[center+2].To<Map>().at("anchor").To<Map>();const auto point=NewAxisPoint();
    const Array targets{InfluenceTarget(samples[center-2],.5),InfluenceTarget(samples[center+2],1.)};const double half=.5*std::atan2(.6,.8);
    const auto request=InfluenceRequest(scene,id,point,at,targets);const auto result=Invoke(&scene,"CamCut","EditToolAxisPoint",request);EXPECT_EQ(result,Invoke(&scene,"CamCut","EditToolAxisPoint",request));
    AxisNear(AxisPointRead(scene,id,at).at("direction"),{0,.6,.8});AxisNear(AxisPointRead(scene,id,left).at("direction"),{0,std::sin(half),std::cos(half)});AxisNear(AxisPointRead(scene,id,right).at("direction"),{0,.6,.8});
    for(std::size_t i=0;i<samples.size();i++){if(i==center||i==center-2||i==center+2)continue;const auto s=samples[i].To<Map>();EXPECT_EQ(s.at("direction"),AxisPointRead(scene,id,s.at("anchor").To<Map>()).at("direction"));}
    const auto repeat=Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,point,at,targets));EXPECT_FALSE(repeat.at("changed").To<bool>());
    GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,left,{0,std::sin(half),std::cos(half)});CheckGeneratedAxisPoint(scene,id,right,{0,.6,.8});GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,left,{0,std::sin(half),std::cos(half)});
    EXPECT_EQ(original,Read(scene).at("workpieces"));
}
TEST(FiveAxisToolAxisInfluence, CurveInterpolationAndWeightChangesAffectOnlySelectedControlIntervals)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});const auto samples=InfluenceSamples(scene,id);const auto point=NewAxisPoint();const auto n=samples.size()/2;
    const auto at=samples[n].To<Map>().at("anchor").To<Map>(),target=samples[n+1].To<Map>().at("anchor").To<Map>();Array targets{InfluenceTarget(samples[n+1],.5)};
    auto probe=at;probe["nativeParameter"]=at.at("nativeParameter").To<double>()*.75+target.at("nativeParameter").To<double>()*.25;
    Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,point,at,targets,"Linear"));const auto linear=AxisPointRead(scene,id,probe).at("direction").To<Array>();
    Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,point,at,targets,"Smooth"));const auto smooth=AxisPointRead(scene,id,probe).at("direction").To<Array>();EXPECT_GT(std::abs(linear[1].To<double>()-smooth[1].To<double>()),.005);
    GenerateAxis(scene);CheckGeneratedAxisPoint(scene,id,probe,{smooth[0].To<double>(),smooth[1].To<double>(),smooth[2].To<double>()});
    targets={InfluenceTarget(samples[n+1],0.)};Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,point,at,targets));AxisNear(AxisPointRead(scene,id,target).at("direction"),{0,0,1});
    ASSERT_TRUE(scene.Database().Undo());const double a=.5*std::atan2(.6,.8);AxisNear(AxisPointRead(scene,id,target).at("direction"),{0,std::sin(a),std::cos(a)});ASSERT_TRUE(scene.Database().Redo());AxisNear(AxisPointRead(scene,id,target).at("direction"),{0,0,1});
}
TEST(FiveAxisToolAxisInfluence, InvalidSelectionAndOverlapsFailAtomicallyAndResetRestoresFollowers)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();const auto samples=InfluenceSamples(scene,id);const auto point=NewAxisPoint();const auto n=samples.size()/2;
    const auto at=samples[n].To<Map>().at("anchor").To<Map>(),target=samples[n+1].To<Map>().at("anchor").To<Map>();
    for(const Array targets:{Array{InfluenceTarget(samples[n+1],-1.)},Array{InfluenceTarget(samples[n+1],1.1)},Array{InfluenceTarget(samples[n],.5)},Array{InfluenceTarget(samples[n+1],.5),InfluenceTarget(samples[n+1],.5)}}){
        const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,point,at,targets)),std::exception);EXPECT_EQ(before,Read(scene));
    }
    const Array targets{InfluenceTarget(samples[n+1],.5)};Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,point,at,targets));
    const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),target)),std::invalid_argument);EXPECT_EQ(before,Read(scene));
    const auto other=samples[n+3].To<Map>().at("anchor").To<Map>();EXPECT_THROW(Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,NewAxisPoint(),other,targets)),std::invalid_argument);EXPECT_EQ(before,Read(scene));
    auto reset=AxisPointRequest(scene,id,point,at);reset["action"]=Text("Reset");Invoke(&scene,"CamCut","EditToolAxisPoint",reset);AxisNear(AxisPointRead(scene,id,target).at("direction"),{0,0,1});
    ASSERT_TRUE(scene.Database().Undo());const double a=.5*std::atan2(.6,.8);AxisNear(AxisPointRead(scene,id,target).at("direction"),{0,std::sin(a),std::cos(a)});
}
TEST(FiveAxisToolAxisInfluence, ReversalAndSplitPreservePhysicalFollowerDirections)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});const auto samples=InfluenceSamples(scene,id);const auto point=NewAxisPoint();const auto n=samples.size()/3;
    const auto at=samples[n].To<Map>().at("anchor").To<Map>(),target=samples[n+1].To<Map>().at("anchor").To<Map>();
    Invoke(&scene,"CamCut","EditToolAxisPoint",InfluenceRequest(scene,id,point,at,{InfluenceTarget(samples[n+1],.5)}));const double a=.5*std::atan2(.6,.8);
    Invoke(&scene,"CamProcess","UpdatePose",EditRequest(scene,{id},{{"patch",Map{{"reverseAxes",true}}}}));AxisNear(AxisPointRead(scene,id,target).at("direction"),{0,-std::sin(a),-std::cos(a)});
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{id},{}));AxisNear(AxisPointRead(scene,id,target).at("direction"),{0,-std::sin(a),-std::cos(a)});
    const auto pieces=Invoke(&scene,"CamCut","Split",SplitRequest(scene,{CircleSplitSelection(scene,id,0,3)})).at("intentIds").To<Array>();GenerateAxis(scene);bool found=false;
    for(const auto& fragment:pieces)for(const auto& p:ProcessItem(scene,fragment).at("posePolicy").To<Map>().at("keypoints").To<Array>()){
        const auto k=p.To<Map>(),anchor=k.at("anchor").To<Map>();if(std::abs(anchor.at("nativeParameter").To<double>()-target.at("nativeParameter").To<double>())<1e-10){CheckGeneratedAxisPoint(scene,fragment,anchor,{0,-std::sin(a),-std::cos(a)});found=true;}
    }EXPECT_TRUE(found);
}
