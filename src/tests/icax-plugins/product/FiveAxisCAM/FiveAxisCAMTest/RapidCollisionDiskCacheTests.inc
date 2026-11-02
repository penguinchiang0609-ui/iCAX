namespace rapid_collision_disk_cache_tests {
    constexpr const wchar_t* CacheEnvironment=L"ICAX_CAM_COLLISION_CACHE_DIR";
    class CacheDirectory final {
        bool HadPrevious=false;
        std::wstring Previous;
    public:
        const std::filesystem::path Root=std::filesystem::temp_directory_path()/("icax-collision-disk-test-"+iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()));
        CacheDirectory(){
            const auto count=GetEnvironmentVariableW(CacheEnvironment,nullptr,0);HadPrevious=count!=0;
            if(count){Previous.resize(count);const auto written=GetEnvironmentVariableW(CacheEnvironment,Previous.data(),count);if(!written||written>=count)throw std::runtime_error("Cannot capture collision-cache test environment");Previous.resize(written);}
            std::filesystem::create_directories(Root);Use(Root/"cache");
        }
        void Use(const std::filesystem::path& path){if(!SetEnvironmentVariableW(CacheEnvironment,path.c_str()))throw std::runtime_error("Cannot isolate collision-cache test directory");}
        ~CacheDirectory(){
            SetEnvironmentVariableW(CacheEnvironment,HadPrevious?Previous.c_str():nullptr);
            // Only this UUID-owned temporary directory is eligible for cleanup.
            std::error_code error;const auto absolute=std::filesystem::weakly_canonical(Root,error),parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
            if(!error&&absolute.parent_path()==parent&&absolute.filename().wstring().starts_with(L"icax-collision-disk-test-"))std::filesystem::remove_all(absolute,error);
        }
        std::vector<std::filesystem::path> Files()const{
            std::vector<std::filesystem::path> files;const auto cache=Root/"cache";if(!std::filesystem::is_directory(cache))return files;
            for(const auto& file:std::filesystem::directory_iterator(cache))if(file.is_regular_file()&&file.path().extension()==L".icaxcm1")files.push_back(file.path());
            return files;
        }
    };
    struct SavedFixture final {
        ID Project,SceneID;std::filesystem::path Path;Text FixtureID;
        SavedFixture(const std::filesystem::path& path):Path(path){
            Scene source;PrepareOrderPart(source);Project=source.Project;SceneID=source.GetSceneID();
            File fixture(BRepPrimAPI_MakeBox(gp_Pnt(1000.,1000.,1000.),2.,2.,2.).Shape());FixtureID=ImportedFixture(source,fixture);
            const auto generated=Wait(source,Invoke(&source,"CamPlan","Generate",GenerateRequest(source)));
            if(generated.at("status").To<Text>()!="Completed")throw std::runtime_error("Disk-cache fixture generation failed");
            fast_rapid_tests::ExpectUncheckedGeometry(fast_rapid_tests::Batch(source));SaveProductPlanFixture(source,Path);
        }
        std::unique_ptr<Scene> Open()const{
            auto scene=std::make_unique<Scene>(Project,SceneID,false);
            iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
            file.Open(Path,scene->Database(),scene->Resources());InitializeScene(*scene);return scene;
        }
    };
    auto Setup(Scene& scene){return std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(*ID::from_string(Read(scene).at("activeSetupId").To<Text>()))->GetComponent(CFiveAxisSetupComponent::S_ClassName));}
    struct AuthoredRoutes final {
        Map Rapids,Overrides,Cuts;
        explicit AuthoredRoutes(Scene& scene){const auto setup=Setup(scene);if(!setup)throw std::runtime_error("Disk-cache setup missing");Rapids=setup->GetGeneratedRapidPaths();Overrides=setup->GetRapidOverrides();Cuts=setup->GetGeneratedToolpaths();}
        void ExpectUnchanged(Scene& scene)const{const auto setup=Setup(scene);ASSERT_TRUE(setup);EXPECT_EQ(Rapids,setup->GetGeneratedRapidPaths());EXPECT_EQ(Overrides,setup->GetRapidOverrides());EXPECT_EQ(Cuts,setup->GetGeneratedToolpaths());}
    };
    void ExpectDiskPreparation(const Map& result,double hits,double misses,bool noRefinement){
        const auto performance=result.at("performance").To<Map>();
        EXPECT_DOUBLE_EQ(hits,performance.at("meshDiskCacheHits").To<double>());EXPECT_DOUBLE_EQ(misses,performance.at("meshDiskCacheMisses").To<double>());
        const auto refine=performance.at("meshRefinementMs").To<double>();EXPECT_TRUE(std::isfinite(refine));EXPECT_GE(refine,0.);if(noRefinement)EXPECT_DOUBLE_EQ(0.,refine);else EXPECT_GT(refine,0.);
    }
    void ExpectSameRoute(const Map& before,const Map& after){
        for(const auto* field:{"validation","geometryStatus","reason","points","directions"})EXPECT_EQ(before.at(field),after.at(field));
        EXPECT_NEAR(before.at("certifiedClearanceMm").To<double>(),after.at("certifiedClearanceMm").To<double>(),1e-8);
    }
    void CorruptFiniteCoordinateWithoutUpdatingChecksum(const std::filesystem::path& path){
        // File schema 1: 116-byte header, then uint64 vertex count and doubles.
        // Flipping only the low mantissa bit leaves a finite, structurally valid
        // mesh. The payload checksum must reject it before it can be trusted.
        constexpr std::streamoff firstCoordinate=116+8;
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);if(!file)throw std::runtime_error("Cannot open disk-cache corruption fixture");
        file.seekg(firstCoordinate);char value{};file.read(&value,1);if(!file)throw std::runtime_error("Disk-cache corruption fixture is truncated");
        value^=1;file.seekp(firstCoordinate);file.write(&value,1);if(!file)throw std::runtime_error("Cannot alter disk-cache checksum fixture");
    }
}

TEST(FiveAxisRapidCollisionDiskCache, ReopenedScenesReuseExactMeshesAndRejectChangedVersionsOrCorruptedPayloads)
{
    namespace D=rapid_collision_disk_cache_tests;namespace C=rapid_collision_cache_tests;
    D::CacheDirectory cache;D::SavedFixture fixture(cache.Root/"fixture.i5cam");Map coldRoute;
    {
        auto scene=fixture.Open();D::AuthoredRoutes authored(*scene);const auto result=C::CheckStoredRoute(*scene);coldRoute=C::CheckedRoute(result);
        ASSERT_EQ("Clear",coldRoute.at("validation").To<Text>());D::ExpectDiskPreparation(result,0.,2.,false);authored.ExpectUnchanged(*scene);
    }
    const auto initialFiles=cache.Files();ASSERT_EQ(2u,initialFiles.size());
    {
        // This is a distinct native Scene and resource library, so no pointer or
        // scene-owned memory cache can account for the reuse being tested.
        auto scene=fixture.Open();D::AuthoredRoutes authored(*scene);const auto result=C::CheckStoredRoute(*scene);
        C::ExpectMeshPreparation(result,0.,2.);D::ExpectDiskPreparation(result,2.,0.,true);D::ExpectSameRoute(coldRoute,C::CheckedRoute(result));authored.ExpectUnchanged(*scene);
        const auto collider=std::dynamic_pointer_cast<CFiveAxisFixtureComponent>(scene->Database().GetEntity(*ID::from_string(fixture.FixtureID))->GetComponent(CFiveAxisFixtureComponent::S_ClassName));ASSERT_TRUE(collider);
        const auto reference=iCAX::ProjectFile::TryGetResourceReferenceValue(collider->GetGeometryRef());ASSERT_TRUE(reference);
        const auto original=scene->Resources().Get<iCAX::GeometryData::BRepModel>(reference->URL,reference->nVersion);ASSERT_TRUE(original);
        auto info=scene->Resources().GetInfo(reference->URL,reference->nVersion).value();++info.nVersion;
        scene->Resources().Set(reference->URL,std::make_shared<iCAX::GeometryData::BRepModel>(*original),info);
        ASSERT_TRUE(collider->SetProperty("GeometryRef",iCAX::ProjectFile::MakeResourceReferenceValue({reference->URL,info.nVersion}).To<Map>()));
        const auto changed=C::CheckStoredRoute(*scene);C::ExpectMeshPreparation(changed,1.,1.);D::ExpectDiskPreparation(changed,0.,1.,false);
        D::ExpectSameRoute(coldRoute,C::CheckedRoute(changed));authored.ExpectUnchanged(*scene);
    }
    D::CorruptFiniteCoordinateWithoutUpdatingChecksum(initialFiles.front());
    {
        auto scene=fixture.Open();D::AuthoredRoutes authored(*scene);const auto result=C::CheckStoredRoute(*scene);
        D::ExpectDiskPreparation(result,1.,1.,false);D::ExpectSameRoute(coldRoute,C::CheckedRoute(result));authored.ExpectUnchanged(*scene);
    }
    {
        // A successful rebuild repairs the disposable derivative, so the next
        // reopened Scene can again reuse both exact collision meshes.
        auto scene=fixture.Open();const auto result=C::CheckStoredRoute(*scene);D::ExpectDiskPreparation(result,2.,0.,true);D::ExpectSameRoute(coldRoute,C::CheckedRoute(result));
    }
}

TEST(FiveAxisRapidCollisionDiskCache, UnavailableCacheDirectoryFallsBackToTheSameRealCollisionCheck)
{
    namespace D=rapid_collision_disk_cache_tests;namespace C=rapid_collision_cache_tests;
    D::CacheDirectory cache;D::SavedFixture fixture(cache.Root/"fixture.i5cam");Map cachedRoute;
    {
        auto scene=fixture.Open();const auto result=C::CheckStoredRoute(*scene);cachedRoute=C::CheckedRoute(result);ASSERT_EQ("Clear",cachedRoute.at("validation").To<Text>());
    }
    const auto blocked=cache.Root/"ordinary-file";{std::ofstream file(blocked,std::ios::binary);file<<"Not a directory; cache reads and writes must be optional.";}
    cache.Use(blocked);
    {
        auto scene=fixture.Open();D::AuthoredRoutes authored(*scene);const auto result=C::CheckStoredRoute(*scene);
        C::ExpectMeshPreparation(result,0.,2.);EXPECT_GT(result.at("performance").To<Map>().at("meshRefinementMs").To<double>(),0.);
        D::ExpectSameRoute(cachedRoute,C::CheckedRoute(result));authored.ExpectUnchanged(*scene);
    }
    {
        // Unavailable cache storage must not turn a real obstruction into an
        // empty/clear collider. Put an actual fixture around a displayed TCP.
        auto scene=fixture.Open();auto read=RapidRequest(*scene);read.erase("commandId");const auto route=Invoke(scene.get(),"CamRapid","ReadEditPath",read);
        const auto p=route.at("points").To<Array>().at(1).To<Array>();
        File blocker(BRepPrimAPI_MakeBox(gp_Pnt(p[0].To<double>()-10,p[1].To<double>()-10,p[2].To<double>()-10),20.,20.,20.).Shape());
        ImportedFixture(*scene,blocker);
        ASSERT_EQ("Completed",Wait(*scene,Invoke(scene.get(),"CamPlan","Generate",GenerateRequest(*scene))).at("status").To<Text>());
        fast_rapid_tests::Batch(*scene);D::AuthoredRoutes authored(*scene);const auto result=C::CheckStoredRoute(*scene),blockedRoute=C::CheckedRoute(result);
        EXPECT_NE("Clear",blockedRoute.at("validation").To<Text>());EXPECT_EQ("Failed",blockedRoute.at("geometryStatus").To<Text>());
        EXPECT_GT(result.at("performance").To<Map>().at("meshRefinementMs").To<double>(),0.);authored.ExpectUnchanged(*scene);
    }
    EXPECT_TRUE(std::filesystem::is_regular_file(blocked));
}
