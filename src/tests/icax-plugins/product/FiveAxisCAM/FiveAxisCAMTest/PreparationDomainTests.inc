namespace
{
    std::pair<std::set<ID>,std::map<Text,std::size_t>> PreparationGraphShape(Scene& scene,const Variant& setupId)
    {
        const auto setup=scene.Database().GetEntity(ID::from_string(setupId.To<Text>()).value())->GetComponent<CFiveAxisSetupComponent>();
        const auto plan=scene.Database().GetEntity(setup->GetIntentPlanID())->GetComponent("CEditablePlanComponent")->GetProperties().at("Definition").To<Map>();
        std::pair<std::set<ID>,std::map<Text,std::size_t>> result;
        for(const auto& value:plan.at("operations").To<Array>())
        {
            const auto operation=value.To<ID>();result.first.insert(operation);
            const auto definition=scene.Database().GetEntity(operation)->GetComponent("CEditableOperationComponent")->GetProperties().at("Definition").To<Map>();
            ++result.second[definition.at("typeId").To<Text>()];
        }
        return result;
    }
}
TEST(FiveAxisPreparation, RenameIsMetadataOnlyIdempotentUndoableAndValidatesNames) {
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene),w=before.at("workpieces").To<Array>().front().To<Map>();
    Map request{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",before.at("expected")},{"workpieceId",w.at("id")},{"name",Text("冲压件")}};
    const auto result=Invoke(&scene,"CamWorkpiece","Rename",request);EXPECT_EQ(result,Invoke(&scene,"CamWorkpiece","Rename",request));
    const auto after=Read(scene);auto changed=after.at("workpieces").To<Array>().front().To<Map>();EXPECT_EQ("冲压件",changed.at("name").To<Text>());changed["name"]=w.at("name");EXPECT_EQ(w,changed);
    for(const auto* key:{"placements","setups","intentToolpaths","rapidConnections"})if(before.contains(key))EXPECT_EQ(before.at(key),after.at(key));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after.at("workpieces"),Read(scene).at("workpieces"));
    for(const auto& name:{Text(),Text("   "),Text("bad\nname"),Text(513,'a')}){request["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());request["expected"]=Read(scene).at("expected");request["name"]=name;EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Rename",request),std::exception);}
    EXPECT_THROW(Invoke(nullptr,"CamWorkpiece","Rename",request),std::exception);
}
TEST(FiveAxisPreparation, CopyTreeIsAtomicIndependentAndClearsQualification) {
    Scene scene;PrepareOrderPart(scene);const auto state=Read(scene),first=state.at("placements").To<Array>().front().To<Map>();Map part{{"placementId",first.at("id")}};
    File support(BRepPrimAPI_MakeBox(10.,20.,5.).Shape());auto import=FixtureRequest(scene);import["sourcePath"]=support.Utf8();ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",import)).at("status").To<Text>());
    const auto before=Read(scene);const auto sourceGraph=PreparationGraphShape(scene,before.at("activeSetupId"));auto request=ActivateRequest(scene,part);
    const auto copy=Invoke(&scene,"CamWorkpiece","Copy",request);EXPECT_EQ(copy,Invoke(&scene,"CamWorkpiece","Copy",request));
    const auto after=Read(scene);ASSERT_EQ(2u,after.at("workpieces").To<Array>().size());EXPECT_NE(copy.at("workpieceId"),first.at("workpieceId"));
    const auto originalCuts=before.at("intentToolpaths").To<Array>();size_t copied=0;
    for(const auto& v:after.at("intentToolpaths").To<Array>()){const auto c=v.To<Map>();if(c.at("setupId")!=copy.at("setupId"))continue;copied++;EXPECT_EQ(copy.at("placementId"),c.at("placementId"));EXPECT_EQ("Current",c.at("sourceState").To<Text>());}
    EXPECT_EQ(originalCuts.size(),copied);
    const auto copiedGraph=PreparationGraphShape(scene,copy.at("setupId"));EXPECT_EQ(sourceGraph.second,copiedGraph.second);
    for(const auto& operation:copiedGraph.first)EXPECT_FALSE(sourceGraph.first.contains(operation));
    EXPECT_EQ(sourceGraph,PreparationGraphShape(scene,before.at("activeSetupId")));
    auto setup=SetupState(scene,copy.at("setupId"));EXPECT_FALSE(setup.at("locked").To<bool>());EXPECT_EQ("Missing",setup.at("registrationPlan").To<Map>().at("status").To<Text>());EXPECT_NE("Current",setup.at("generatedGeometry").To<Map>().at("status").To<Text>());
    for(const auto& v:before.at("intentToolpaths").To<Array>()){const auto c=v.To<Map>();auto existing=after.at("intentToolpaths").To<Array>();EXPECT_TRUE(std::find(existing.begin(),existing.end(),v)!=existing.end());}
    ASSERT_EQ(2u,after.at("fixtureUnits").To<Array>().size());for(const auto& value:after.at("fixtureUnits").To<Array>()){const auto u=value.To<Map>();if(u.at("setupId")!=copy.at("setupId"))continue;for(const auto& id:u.at("partIds").To<Array>()){const auto fixture=scene.Database().GetEntity(*ID::from_string(id.To<Text>()))->GetComponent<CFiveAxisFixtureComponent>();EXPECT_EQ(*ID::from_string(u.at("id").To<Text>()),fixture->GetUnitID());}}
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("intentToolpaths"),Read(scene).at("intentToolpaths"));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after.at("fixtureUnits"),Read(scene).at("fixtureUnits"));
    auto invalid=request;invalid["commandId"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamWorkpiece","Copy",invalid),std::exception);
    EXPECT_THROW(Invoke(nullptr,"CamWorkpiece","Copy",request),std::exception);
}
TEST(FiveAxisPreparation, CopySetupAndFixtureKeepParentAndDoNotChangeSources) {
    Scene scene;PrepareOrderPart(scene);const auto state=Read(scene),first=state.at("placements").To<Array>().front().To<Map>();Map part{{"placementId",first.at("id")}};
    File support(BRepPrimAPI_MakeBox(10.,20.,5.).Shape());auto import=FixtureRequest(scene);import["sourcePath"]=support.Utf8();ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamFixture","Import",import)).at("status").To<Text>());
    const auto before=Read(scene);auto request=ActivateRequest(scene,part);const auto copy=Invoke(&scene,"CamSetup","Copy",request);
    EXPECT_EQ(first.at("workpieceId"),copy.at("workpieceId"));EXPECT_EQ(1u,Read(scene).at("workpieces").To<Array>().size());EXPECT_EQ(before.at("intentToolpaths").To<Array>().size()*2,Read(scene).at("intentToolpaths").To<Array>().size());
    const auto units=Read(scene).at("fixtureUnits").To<Array>();Map unit;for(const auto& u:units)if(u.To<Map>().at("setupId")==copy.at("setupId"))unit=u.To<Map>();ASSERT_FALSE(unit.empty());
    auto duplicate=FixtureRequest(scene);duplicate["unitId"]=unit.at("id");const auto result=Invoke(&scene,"CamFixture","UnitCopy",duplicate);EXPECT_EQ(result,Invoke(&scene,"CamFixture","UnitCopy",duplicate));
    EXPECT_EQ(3u,Read(scene).at("fixtureUnits").To<Array>().size());EXPECT_NE(unit.at("id"),result.at("unitId"));
    auto lock=ActivateRequest(scene,part);lock["locked"]=true;Invoke(&scene,"CamWorkpiece","SetLocked",lock);for(const auto& s:Read(scene).at("setups").To<Array>())EXPECT_TRUE(s.To<Map>().at("locked").To<bool>());
    lock=ActivateRequest(scene,part);lock["locked"]=false;Invoke(&scene,"CamWorkpiece","SetLocked",lock);for(const auto& s:Read(scene).at("setups").To<Array>())EXPECT_FALSE(s.To<Map>().at("locked").To<bool>());
}
