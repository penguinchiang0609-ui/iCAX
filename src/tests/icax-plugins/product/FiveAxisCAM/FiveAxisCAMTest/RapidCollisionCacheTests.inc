namespace rapid_collision_cache_tests {
    Map CheckStoredRoute(Scene& scene) {
        auto request=RapidRequest(scene);auto read=request;read.erase("commandId");
        const auto route=Invoke(&scene,"CamRapid","ReadEditPath",read);
        const auto points=route.at("points").To<Array>(),directions=route.at("directions").To<Array>();
        if(points.size()<2||points.size()!=directions.size())throw std::runtime_error("Cache regression route is incomplete");
        request["manualPath"]=Map{{"points",Array(points.begin()+1,points.end()-1)},{"directions",Array(directions.begin()+1,directions.end()-1)}};
        const auto task=Wait(scene,Invoke(&scene,"CamRapid","CheckPath",request));
        if(task.at("status").To<Text>()!="Completed")throw std::runtime_error(iCAX::Data::VariantSerializer::Serialize(task));
        return task.at("result").To<Map>();
    }
    Map CheckedRoute(const Map& result) {
        const auto connections=result.at("connections").To<Map>();
        if(connections.size()!=1)throw std::runtime_error("A single-route check must return exactly one connection");
        return connections.begin()->second.To<Map>().at("result").To<Map>();
    }
    void ExpectMeshPreparation(const Map& result,double hits,double misses) {
        ASSERT_TRUE(result.contains("performance"));const auto performance=result.at("performance").To<Map>();
        EXPECT_DOUBLE_EQ(hits,performance.at("meshCacheHits").To<double>());
        EXPECT_DOUBLE_EQ(misses,performance.at("meshCacheMisses").To<double>());
        for(const auto* name:{"captureMs","meshPreparationMs","calculationMs"}) {
            ASSERT_TRUE(performance.contains(name));EXPECT_TRUE(std::isfinite(performance.at(name).To<double>()));EXPECT_GE(performance.at(name).To<double>(),0.);
        }
        if(misses==0)EXPECT_DOUBLE_EQ(0.,performance.at("meshPreparationMs").To<double>());
    }
}

TEST(FiveAxisRapidCollisionCache, ThirtyFiveFixtureMeshesRemainWarmAcrossSingleChecksAndOnlyChangedVersionsRebuild)
{
    namespace C=rapid_collision_cache_tests;namespace F=fast_rapid_tests;
    Scene scene;PrepareOrderPart(scene);std::vector<Text> fixtures;
    // One real workpiece plus 35 separately owned STEP fixture resources exceeds
    // the old 32-entry whole-cache reset. The remote solids do not change the
    // selected route's actual collision verdict.
    for(int index=0;index<35;++index) {
        File fixture(BRepPrimAPI_MakeBox(gp_Pnt(1000.+index*10,1000.,1000.),2.,2.,2.).Shape());
        fixtures.push_back(ImportedFixture(scene,fixture));
    }
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    F::ExpectUncheckedGeometry(F::Batch(scene));
    const auto before=Read(scene);ASSERT_EQ(35u,before.at("fixtures").To<Array>().size());ASSERT_EQ(1u,before.at("placements").To<Array>().size());
    std::set<Text> resources;
    for(const auto& value:before.at("workpieces").To<Array>())resources.insert(value.To<Map>().at("geometryRef").To<Map>().at("url").To<Text>());
    for(const auto& value:before.at("fixtures").To<Array>())resources.insert(value.To<Map>().at("geometryRef").To<Map>().at("url").To<Text>());
    ASSERT_EQ(36u,resources.size());
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(before.at("activeSetupId").To<Text>()))->GetComponent(CFiveAxisSetupComponent::S_ClassName));
    ASSERT_TRUE(setup);const auto savedRoutes=setup->GetGeneratedRapidPaths(),savedOverrides=setup->GetRapidOverrides(),savedCuts=setup->GetGeneratedToolpaths();

    const auto cold=C::CheckStoredRoute(scene),coldRoute=C::CheckedRoute(cold);C::ExpectMeshPreparation(cold,0.,36.);
    ASSERT_EQ("Clear",coldRoute.at("validation").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(coldRoute);
    const auto warm=C::CheckStoredRoute(scene),warmRoute=C::CheckedRoute(warm);C::ExpectMeshPreparation(warm,36.,0.);
    EXPECT_EQ(coldRoute.at("validation"),warmRoute.at("validation"));EXPECT_EQ(coldRoute.at("reason"),warmRoute.at("reason"));
    EXPECT_EQ(coldRoute.at("points"),warmRoute.at("points"));EXPECT_EQ(coldRoute.at("directions"),warmRoute.at("directions"));
    EXPECT_NEAR(coldRoute.at("certifiedClearanceMm").To<double>(),warmRoute.at("certifiedClearanceMm").To<double>(),1e-8);

    // Resource content versions invalidate just that mesh, while every other
    // obstacle retains the same immutable source and prepared collision index.
    const auto fixture=std::dynamic_pointer_cast<CFiveAxisFixtureComponent>(scene.Database().GetEntity(*ID::from_string(fixtures.front()))->GetComponent(CFiveAxisFixtureComponent::S_ClassName));
    ASSERT_TRUE(fixture);const auto reference=iCAX::ProjectFile::TryGetResourceReferenceValue(fixture->GetGeometryRef());ASSERT_TRUE(reference);
    const auto original=scene.Resources().Get<iCAX::GeometryData::BRepModel>(reference->URL,reference->nVersion);ASSERT_TRUE(original);
    auto info=scene.Resources().GetInfo(reference->URL,reference->nVersion).value();++info.nVersion;
    scene.Resources().Set(reference->URL,std::make_shared<iCAX::GeometryData::BRepModel>(*original),info);
    ASSERT_TRUE(fixture->SetProperty("GeometryRef",iCAX::ProjectFile::MakeResourceReferenceValue({reference->URL,info.nVersion}).To<Map>()));
    const auto changedVersion=C::CheckStoredRoute(scene),versionRoute=C::CheckedRoute(changedVersion);C::ExpectMeshPreparation(changedVersion,35.,1.);
    EXPECT_EQ(coldRoute.at("validation"),versionRoute.at("validation"));EXPECT_EQ(coldRoute.at("points"),versionRoute.at("points"));EXPECT_EQ(coldRoute.at("directions"),versionRoute.at("directions"));

    // A rigid placement belongs to the query. It must not remesh the unchanged
    // fixture or flush the other 35 obstacle resources.
    MoveFixture(scene,fixtures.front(),FixtureTransform(123.,0.,0.));
    const auto transformed=C::CheckStoredRoute(scene),transformedRoute=C::CheckedRoute(transformed);C::ExpectMeshPreparation(transformed,36.,0.);
    EXPECT_EQ(coldRoute.at("validation"),transformedRoute.at("validation"));EXPECT_EQ(coldRoute.at("points"),transformedRoute.at("points"));EXPECT_EQ(coldRoute.at("directions"),transformedRoute.at("directions"));
    EXPECT_EQ(savedRoutes,setup->GetGeneratedRapidPaths());EXPECT_EQ(savedOverrides,setup->GetRapidOverrides());EXPECT_EQ(savedCuts,setup->GetGeneratedToolpaths());
}
