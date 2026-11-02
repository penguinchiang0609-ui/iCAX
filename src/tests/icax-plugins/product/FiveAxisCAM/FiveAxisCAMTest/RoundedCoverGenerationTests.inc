TEST(FiveAxisToolAxis, RoundedCoverSingleCutWithLocalPointsGeneratesWithoutTravel)
{
    for(const bool outer:{true,false}){
    SCOPED_TRACE(outer?"outside machining skin":"inside machining skin");
    Scene scene;File file;
    const auto source=std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path()
        / u8"../../../../../../TestData/FA/刀路/刀路/整理测试刀路/圆弧多义线盖-无孔-9696.stp.STEP");
    std::filesystem::copy_file(source,file.Path,std::filesystem::copy_options::overwrite_existing);
    const auto ids=Imported(scene,file);const auto state=Read(scene);
    const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});
    Array faces;for(const auto& v:sources.at("faceUses").To<Array>()){
        const auto face=std::stoull(v.To<Map>().at("localId").To<Text>());if(outer?face>=53:face<=46)faces.push_back(v);}
    const Map request{{"commandId",NewAxisPoint()},{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},
        {"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},
        {"selection",Map{{"mode",Text("Faces")},{"faceUses",faces}}},{"geometryOnly",true},{"representation",Text("SkinSurface")},{"workpieceFamily",Text("SheetSurface")},
        {"side",Map{{"outwardSign",1.}}},{"requestedKinds",Array{Text("GeneralContour")}},{"draftId",NewAxisPoint()},{"draftRevision",Text("1")},{"draftDigest",Text("rounded-cover")}};
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",request));
    ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(terminal);
    const auto created=Invoke(&scene,"CamCut","Create",CreateRequest(scene,request,terminal)).at("createdIntentIds").To<Array>();
    ASSERT_EQ(1u,created.size());const auto id=created.front();ZeroGenerationPolicies(scene,created);
    const auto baseline=DefinitionItem(scene,id);
    const auto samples=Invoke(&scene,"CamCut","ReadToolAxisSamples",{{"expected",Read(scene).at("expected")},{"intentIds",created}}).at("cuts").To<Array>().front().To<Map>().at("samples").To<Array>();
    ASSERT_GT(samples.size(),8u);
    std::vector<std::pair<Map,std::array<double,3>>> edits;
    for(const auto index:{samples.size()/3,2*samples.size()/3}){
        const auto row=samples[index].To<Map>(),anchor=row.at("anchor").To<Map>();const auto d=row.at("direction").To<Array>();
        std::array<double,3> direction{d[0].To<double>()+.1,d[1].To<double>()+.05,d[2].To<double>()};
        const auto norm=std::hypot(direction[0],direction[1],direction[2]);for(auto& x:direction)x/=norm;
        Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,NewAxisPoint(),anchor,{direction[0],direction[1],direction[2]}));edits.push_back({anchor,direction});
    }
    GenerateAxis(scene);
    const auto generated=Generated(scene);ASSERT_EQ("Current",generated.at("status").To<Text>());
    ASSERT_EQ(1u,generated.at("operations").To<Array>().size());EXPECT_TRUE(generated.at("transitions").To<Array>().empty());
    for(const auto& [anchor,direction]:edits)CheckGeneratedAxisPoint(scene,id,anchor,direction);
    EXPECT_EQ(baseline.at("sourceSpans"),DefinitionItem(scene,id).at("sourceSpans"));
    EXPECT_EQ(state.at("workpieces"),Read(scene).at("workpieces"));
    }
}
