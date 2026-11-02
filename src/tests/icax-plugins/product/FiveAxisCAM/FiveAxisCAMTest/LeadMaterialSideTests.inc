TEST(FiveAxisLeadMaterialSide, RetainedRegionControlsBothLeadAndKerfAndSurvivesReverse)
{
    Scene scene;const auto ids=ConfirmWithPolicies(scene,.4).at("createdIntentIds").To<Array>();const auto id=ids.front();
    ZeroGenerationPolicies(scene,ids);
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"kerf",Map{{"widthMm",.4}}},{"leadIn",Lead("Line",.5)},{"leadOut",Lead("Arc",.5)}}}}));
    for(const auto& side:std::vector<Text>{"Exterior","Interior"})
    {
        Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,ids,{{"retainedSide",side}}));
        for(int reverse=0;reverse<2;++reverse)
        {
            const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
            const auto leads=LeadPreview(scene,ids).at("items").To<Array>().front().To<Map>();
            const auto preview=Invoke(&scene,"CamProcess","PreviewCompensation",{{"expected",before.at("expected")},{"intentIds",ids}}).at("items").To<Array>().front().To<Map>();
            ASSERT_FALSE(preview.contains("error"))<<iCAX::Data::VariantSerializer::Serialize(preview);
            ASSERT_EQ(2u,leads.at("leads").To<Array>().size());
            const auto radius=[](const Variant& p){const auto point=p.To<Array>();return std::hypot(point[0].To<double>(),point[1].To<double>());};
            for(const auto& p:preview.at("nominalPoints").To<Array>())EXPECT_NEAR(4.,radius(p),1e-6);
            for(const auto& part:preview.at("parts").To<Array>())
            {
                const auto data=part.To<Map>();
                if(data.at("role").To<Text>()=="Cut")for(const auto& p:data.at("points").To<Array>())EXPECT_NEAR(side=="Exterior"?3.8:4.2,radius(p),1e-6);
            }
            for(const auto& lead:leads.at("leads").To<Array>())for(const auto& p:lead.To<Map>().at("points").To<Array>())
                if(side=="Exterior"){EXPECT_LT(radius(p),4.000001);}else {EXPECT_GT(radius(p),3.999999);}
            EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
            EXPECT_EQ(side,DefinitionItem(scene,id).at("retainedSide").To<Text>());
            Invoke(&scene,"CamCut","Reverse",EditRequest(scene,ids,{}));
        }
    }
}
TEST(FiveAxisLeadMaterialSide, CompensationDisplayDoesNotRequireProductionTechnology)
{
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file),extraction=ExtractRequest(scene,ids);
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));auto create=CreateRequest(scene,extraction,terminal);
    create["candidateIds"]=Array{create.at("candidateIds").To<Array>().front()};
    const auto paths=Invoke(&scene,"CamCut","Create",create).at("createdIntentIds").To<Array>();
    const auto before=Read(scene);const auto undo=scene.Database().GetUndoArray().size();
    const auto item=Invoke(&scene,"CamProcess","PreviewCompensation",{{"expected",before.at("expected")},{"intentIds",paths}}).at("items").To<Array>().front().To<Map>();
    ASSERT_FALSE(item.contains("error"))<<iCAX::Data::VariantSerializer::Serialize(item);
    EXPECT_FALSE(item.at("nominalPoints").To<Array>().empty());EXPECT_FALSE(item.at("parts").To<Array>().empty());
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray().size());
}
