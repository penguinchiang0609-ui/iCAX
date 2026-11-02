// Include after the recognition and fixture-domain helpers in DomainTests.cpp.
namespace recognition_location_domain_tests
{
    void ApplyCapturedRoundoff(iCAX::GeometryData::BRepShapeRef& ref)
    {
        auto& m=ref.Location.Matrix.Values;
        m[0][0]=0.9999999999999991;m[1][1]=1.0000000000000002;m[2][2]=1.0000000000000002;
        m[0][1]=m[1][0]=-6.157398269790351e-18;
        m[0][2]=m[2][0]=2.117582368135751e-22;
        m[1][2]=m[2][1]=2.710505431213761e-20;
        m[0][3]=-9.094947017729282e-13;m[1][3]=-1.1368683772161603e-13;m[2][3]=-7.105427357601002e-15;
    }
    auto PersistentBodies(Scene& scene)
    {
        std::map<iCAX::Resource::CResourceReference,std::vector<std::uint8_t>> result;
        for(const auto& info:scene.Resources().GetManifest(false)){
            const iCAX::Resource::CResourceReference ref{info.Key.Source,info.nVersion};
            result.emplace(ref,scene.Resources().SerializePersistentVersion(ref).Body);
        }
        return result;
    }
}

TEST(FiveAxisRecognitionDomain, NestedRoundoffLocationsPreserveAdoptedFixtureAndPersistentResources)
{
    using namespace recognition_location_domain_tests;
    Scene scene;File file(RecognitionPart());const auto ids=Imported(scene,file);
    auto recipe=PlateRecipe(scene);recipe["stations"]=Array{};
    for(double coordinate:{-12.,0.,12.}){
        auto stations=recipe.at("stations").To<Array>();stations.emplace_back(Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"coordinate",coordinate}});recipe["stations"]=stations;
    }
    SetPlateRecipe(scene,recipe);const auto fixture=GeneratePlates(scene);AdoptPlates(scene,fixture);
    ASSERT_FALSE(Read(scene).at("fixtures").To<Array>().empty());

    // Author the old frame-reset roundoff as test preparation in a new owned
    // resource. The imported CAD and every adopted fixture resource stay intact.
    const auto state=Read(scene),work=state.at("workpieces").To<Array>().front().To<Map>(),wire=work.at("geometryRef").To<Map>();
    const iCAX::Resource::CResourceReference original{wire.at("url").To<Text>(),std::stoull(wire.at("version").To<Text>())};
    const auto source=scene.Resources().Get<iCAX::GeometryData::BRepModel>(original.URL,original.nVersion);ASSERT_TRUE(source);
    auto residual=std::make_shared<iCAX::GeometryData::BRepModel>(*source);ASSERT_FALSE(residual->RootShapes.empty());
    for(auto& root:residual->RootShapes)ApplyCapturedRoundoff(root);
    std::uint64_t compoundID=1;for(const auto& compound:residual->Compounds)compoundID=std::max(compoundID,compound.Id+1);
    residual->Compounds.push_back({compoundID,residual->RootShapes});
    iCAX::GeometryData::BRepShapeRef outer{iCAX::GeometryData::EBRepShapeKind::Compound,compoundID};ApplyCapturedRoundoff(outer);residual->RootShapes={outer};
    const auto residualBytes=iCAX::GeometryData::Persistence::Serialize(*residual),sourceBytes=iCAX::GeometryData::Persistence::Serialize(*source);
    const auto originalInfo=scene.Resources().GetInfo(original.URL,original.nVersion);ASSERT_TRUE(originalInfo);
    iCAX::Resource::CResourceInfo info;info.Name="Captured nested frame-reset roundoff";info.ResourceTypeID=originalInfo->ResourceTypeID;info.nVersion=1;
    info.nSchemaVersion=originalInfo->nSchemaVersion;info.nMinimumReaderVersion=originalInfo->nMinimumReaderVersion;info.Persistence=iCAX::Resource::EResourcePersistenceMode::Embedded;info.Dependencies={original};
    const iCAX::Resource::CResourceReference replacement{scene.Resources().AllocateResourceURL(),1};scene.Resources().Set(replacement.URL,residual,info);
    auto& tx=scene.Database().BeginTransaction("Prepare nested frame-reset roundoff regression");
    tx.ModifyComponent(ID::from_string(ids.at("workpieceId").To<Text>()).value(),CFiveAxisWorkpieceComponent::S_ClassName,
        {{"GeometryRef",iCAX::ProjectFile::MakeResourceReferenceValue({replacement.URL,replacement.nVersion}).To<Map>()}});
    ASSERT_TRUE(scene.Database().CommitTransaction(tx));

    const auto before=Read(scene);const auto persistentBefore=PersistentBodies(scene);const auto undoBefore=scene.Database().GetUndoArray(),redoBefore=scene.Database().GetRedoArray();
    const Map query{{"expected",before.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}};
    Map sources;ASSERT_NO_THROW(sources=Invoke(&scene,"CamCut","QuerySources",query));
    EXPECT_EQ(source->Faces.size(),sources.at("faceUses").To<Array>().size());
    const auto request=ExtractRequest(scene,ids);const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",request));
    ASSERT_EQ("Completed",terminal.at("status").To<Text>())<<terminal.at("error").To<Text>();
    const auto page=CandidatePage(scene,terminal);EXPECT_TRUE(page.at("coverageComplete").To<bool>());EXPECT_EQ(2u,page.at("candidates").To<Array>().size());
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(undoBefore,scene.Database().GetUndoArray());EXPECT_EQ(redoBefore,scene.Database().GetRedoArray());
    EXPECT_EQ(residualBytes,iCAX::GeometryData::Persistence::Serialize(*residual));EXPECT_EQ(sourceBytes,iCAX::GeometryData::Persistence::Serialize(*source));
    EXPECT_EQ(persistentBefore,PersistentBodies(scene));RecordProperty("roundoff_occurrence_depth",2);RecordProperty("adopted_fixture_parts",static_cast<int>(before.at("fixtures").To<Array>().size()));
}
