TEST(FiveAxisLeadMaterialSide, OuterTrimCompensationAndLeadsStayOutsideTheRetainedPlate)
{
    Scene scene;File file(RecognitionPart());const auto placement=Imported(scene,file);auto extraction=ExtractRequest(scene,placement);extraction["geometryOnly"]=true;
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
    Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal));Array ids;
    for(const auto& row:Read(scene).at("intentToolpaths").To<Array>())if(row.To<Map>().at("boundaryRole").To<Text>()=="OuterTrim")ids.push_back(row.To<Map>().at("id"));
    ASSERT_EQ(1u,ids.size());
    ZeroGenerationPolicies(scene,ids);
    const auto start=Invoke(&scene,"CamCut","ResolveStart",{{"expected",Read(scene).at("expected")},{"intentId",ids.front()},
        {"nearPoint",Array{0.,-20.,10.}},{"maximumDistanceMm",1.},{"mode",Text("ClosestPoint")},{"frame",Text("Workpiece")}});
    Invoke(&scene,"CamCut","SetStart",EditRequest(scene,ids,{{"start",start.at("start")}}));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"kerf",Map{{"widthMm",.4}}},{"leadIn",Lead("Line",1.)},{"leadOut",Lead("Line",1.)}}}}));
    for(int reverse=0;reverse<2;++reverse)
    {
        const auto leads=LeadPreview(scene,ids).at("items").To<Array>().front().To<Map>();
        ASSERT_EQ(2u,leads.at("leads").To<Array>().size());
        for(const auto& lead:leads.at("leads").To<Array>())for(const auto& point:lead.To<Map>().at("points").To<Array>())
        {const auto p=point.To<Array>();EXPECT_GE(std::max(std::abs(p[0].To<double>()),std::abs(p[1].To<double>())),20.-1e-7);}
        const auto preview=Invoke(&scene,"CamProcess","PreviewCompensation",{{"expected",Read(scene).at("expected")},{"intentIds",ids}}).at("items").To<Array>().front().To<Map>();
        ASSERT_FALSE(preview.contains("error"))<<iCAX::Data::VariantSerializer::Serialize(preview);
        for(const auto& part:preview.at("parts").To<Array>())if(part.To<Map>().at("role").To<Text>()=="Cut")for(const auto& point:part.To<Map>().at("points").To<Array>())
        {const auto p=point.To<Array>();EXPECT_NEAR(20.2,std::max(std::abs(p[0].To<double>()),std::abs(p[1].To<double>())),1e-6);}
        EXPECT_EQ("Interior",DefinitionItem(scene,ids.front()).at("retainedSide").To<Text>());
        Invoke(&scene,"CamCut","Reverse",EditRequest(scene,ids,{}));
    }
}
