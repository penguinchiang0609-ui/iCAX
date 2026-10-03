namespace {
class AssemblyProcessProfileProduct final : public iCAX::Product::IProductContext {
public:
    AssemblyProcessProfileProduct() {
        definition.ProductID="icax.tube-designer";
        iCAX::Application::CUserDataRecordTypeDescriptor fixed;
        fixed.RecordType="fixed-section";fixed.SubjectTypes={"profile-definition"};
        iCAX::Application::CUserDataFeatureDescriptor profiles;
        profiles.FeatureID="profile";profiles.RecordTypes={fixed};
        iCAX::Application::CUserDataFeatureDescriptor tools;
        tools.FeatureID="punch-tool";
        fixed.RecordType="fixed-geometry";fixed.SubjectTypes={"punch-tool-definition"};
        tools.RecordTypes.push_back(fixed);fixed.RecordType="parametric-package";
        tools.RecordTypes.push_back(fixed);
        store=std::make_shared<iCAX::Application::CProductUserDataStore>(
            std::make_shared<iCAX::Application::CSqliteUserDataStore>(":memory:"),definition.ProductID,
            std::vector<iCAX::Application::CUserDataFeatureDescriptor>{profiles,tools});
    }
    const iCAX::Product::CProductDefinition& GetDefinition() const override {return definition;}
    const std::string& GetProductID() const override {return definition.ProductID;}
    std::shared_ptr<iCAX::Application::IProductUserDataStore> GetUserDataStore() const override {return store;}
    iCAX::Product::CProductData GetProductData() const override {return {};}
    iCAX::Services::CServiceProvider& GetServiceProvider() const override {throw std::logic_error("No product services");}
    iCAX::Database::IMetaRegistry& GetMetaRegistry() const override {throw std::logic_error("No product registry");}
    iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override {throw std::logic_error("No behaviors");}
    iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override {throw std::logic_error("No loaders");}
    iCAX::Interaction::CSDORegistry& GetSDORegistry() const override {throw std::logic_error("Use registered SDO");}
private:
    iCAX::Product::CProductDefinition definition;
    std::shared_ptr<iCAX::Application::IProductUserDataStore> store;
};
ObjectMap assemblyProcessPlanStock() {
    return {{"id",std::string("shared-stock")},
        {"profileRef",ObjectMap{{"scope",std::string("system")},{"id",std::string("rect")}}},
        {"parameters",ObjectMap{{"width",60.0},{"depth",40.0},{"wallThickness",2.0},
            {"cornerRadius",3.0},{"innerRadius",1.0}}},{"length",1000.0}};
}
ObjectMap assemblyProcessPlanLocalInput(double station,double rotation) {
    auto stock=assemblyProcessPlanStock();
    const double angle=rotation*3.14159265358979323846/180;
    stock["matrix"]=VariantArray{1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,
        0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0};
    return {{"schema",std::string("icax.assembly-process-input")},{"schemaVersion",1ull},
        {"parts",ObjectMap{{"stock",stock}}},
        {"geometry",ObjectMap{{"angle",90.0},{"station",station},
            {"frame",ObjectMap{{"origin",VariantArray{station,0.0,0.0}},
                {"xAxis",VariantArray{1.0,0.0,0.0}},
                {"yAxis",VariantArray{0.0,std::cos(angle),std::sin(angle)}},
                {"zAxis",VariantArray{0.0,-std::sin(angle),std::cos(angle)}}}}}}};
}
ObjectMap assemblyProcessPlanFixture(const std::vector<std::pair<double,double>>& locations) {
    VariantArray instances;
    for(std::size_t i=0;i<locations.size();++i) {
        const auto [station,rotation]=locations[i];
        instances.emplace_back(ObjectMap{{"instanceId",std::string("bend-")+std::to_string(i+1)},
            {"templateId",std::string("node-v-notch-integrated")},
            {"processInput",assemblyProcessPlanLocalInput(station,rotation)},
            {"parameters",ObjectMap{}},
            {"processDrafts",ObjectMap{{"node-slot",ObjectMap{{"v-notch-sharp",ObjectMap{
                {"bottomStrategy",std::string("sharp")},{"leaveBottom",1.0},
                {"bottomReference",std::string("outer")}}}}}}},
            {"targets",ObjectMap{{"stock",std::string("shared-stock")}}}});
    }
    return {{"stocks",VariantArray{assemblyProcessPlanStock()}},{"instances",instances}};
}
ObjectMap assemblyProcessPlanNativeResult(const ObjectMap& plan) {
    const auto results=plan.at("nativeResults").To<VariantArray>();
    if(results.size()!=1)throw std::runtime_error("Expected exactly one physical stock preview");
    const auto result=results.front().To<ObjectMap>();
    EXPECT_EQ(result.at("blankId").To<std::string>(),"shared-stock");
    EXPECT_TRUE(result.at("resultValid").To<bool>());
    EXPECT_TRUE(result.at("previewComputed").To<bool>());
    EXPECT_EQ(finishedProductNumber(result.at("solidCount")),1.0);
    return result;
}
TopoDS_Shape assemblyProcessPlanNativeShape(Scene& scene,const ObjectMap& plan) {
    return finishedProductNativeShape(scene,assemblyProcessPlanNativeResult(plan));
}
ObjectMap assemblyProcessPlanManufacturingRequest(const ObjectMap& plan,std::size_t featureCount) {
    const auto parts=plan.at("manufacturingParts").To<VariantArray>();
    if(parts.size()!=1)throw std::runtime_error("Repeated operations duplicated the physical stock");
    const auto item=parts.front().To<ObjectMap>();
    EXPECT_EQ(item.at("blankId").To<std::string>(),"shared-stock");
    const auto request=item.at("request").To<ObjectMap>();
    const auto features=request.at("features").To<VariantArray>();
    EXPECT_EQ(features.size(),featureCount);
    std::set<std::string> ids;
    for(const auto& value:features) {
        const auto id=value.To<ObjectMap>().at("id").To<std::string>();
        EXPECT_FALSE(id.empty());
        EXPECT_TRUE(ids.insert(id).second)<<id;
    }
    return request;
}
TopAbs_State assemblyProcessPlanMaterial(const TopoDS_Shape& shape,double station,
        double localY,double localZ,double rotation) {
    const double angle=rotation*3.14159265358979323846/180;
    return BRepClass3d_SolidClassifier(shape,gp_Pnt(station,
        localY*std::cos(angle)-localZ*std::sin(angle),
        localY*std::sin(angle)+localZ*std::cos(angle)),1e-5).State();
}
}

TEST(AssemblyProcessPlanSDOTest, MultipleVGroovesShareOneNativeStockAndRetainRotatedBottoms)
{
    for(const auto& locations:{std::vector<std::pair<double,double>>{{250,0},{750,90}},
            std::vector<std::pair<double,double>>{{150,0},{500,90},{850,180}}}) {
        SCOPED_TRACE(locations.size());
        Scene scene;
        const auto input=assemblyProcessPlanFixture(locations);
        const auto inputBefore=iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(input);
        const auto plan=invoke(scene,"PreviewAssemblyProcessPlan",input);
        EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(input),inputBefore);
        EXPECT_EQ(plan.at("schema").To<std::string>(),"icax.assembly-process-plan");
        (void)assemblyProcessPlanManufacturingRequest(plan,locations.size());
        const auto shape=assemblyProcessPlanNativeShape(scene,plan);
        const auto uncut=invoke(scene,"PreviewAssemblyProcessPlan",assemblyProcessPlanFixture({}));
        const auto blank=assemblyProcessPlanNativeShape(scene,uncut);
        double sumRemoved=0;
        for(const auto& [station,rotation]:locations) {
            SCOPED_TRACE(rotation);
            const double halfDepth=rotation==90?30:20;
            // Probe the physical bottom wall in each cutter's own frame.
            // A nominal 40-mm-depth cutter rotated onto the 60-mm side would
            // leave these material samples wrong, even if its BRep is valid.
            EXPECT_EQ(assemblyProcessPlanMaterial(shape,station,0,-halfDepth+0.5,rotation),TopAbs_IN);
            EXPECT_EQ(assemblyProcessPlanMaterial(shape,station,0,-halfDepth+1.5,rotation),TopAbs_OUT);
            EXPECT_EQ(assemblyProcessPlanMaterial(shape,station,0,halfDepth-1,rotation),TopAbs_OUT);
            EXPECT_EQ(assemblyProcessPlanMaterial(shape,station+90,0,halfDepth-1,rotation),TopAbs_IN);
            const auto single=invoke(scene,"PreviewAssemblyProcessPlan",
                assemblyProcessPlanFixture({{station,rotation}}));
            sumRemoved+=volume(blank)-volume(assemblyProcessPlanNativeShape(scene,single));
        }
        EXPECT_GT(sumRemoved,1.0);
        EXPECT_NEAR(volume(blank)-volume(shape),sumRemoved,0.01);
    }
}

TEST(AssemblyProcessPlanSDOTest, ReplayReplaceAndDeleteRebuildTheSamePhysicalStock)
{
    Scene scene;
    auto input=assemblyProcessPlanFixture({{250,0},{750,90}});
    const auto first=invoke(scene,"PreviewAssemblyProcessPlan",input);
    const auto replay=invoke(scene,"PreviewAssemblyProcessPlan",input);
    EXPECT_EQ(first.at("planDigest"),replay.at("planDigest"));
    EXPECT_EQ(assemblyProcessPlanNativeResult(first).at("geometry"),
        assemblyProcessPlanNativeResult(replay).at("geometry"));
    (void)assemblyProcessPlanManufacturingRequest(replay,2);
    auto instances=input.at("instances").To<VariantArray>();
    auto replacement=instances.front().To<ObjectMap>();
    replacement["processInput"]=assemblyProcessPlanLocalInput(400,180);
    instances.front()=replacement;
    input["instances"]=instances;
    const auto changed=invoke(scene,"PreviewAssemblyProcessPlan",input);
    EXPECT_NE(first.at("planDigest"),changed.at("planDigest"));
    (void)assemblyProcessPlanManufacturingRequest(changed,2);
    const auto shape=assemblyProcessPlanNativeShape(scene,changed);
    EXPECT_EQ(assemblyProcessPlanMaterial(shape,250,0,19,0),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(shape,400,0,19,180),TopAbs_OUT);
    instances.erase(instances.begin());
    input["instances"]=instances;
    const auto deleted=invoke(scene,"PreviewAssemblyProcessPlan",input);
    (void)assemblyProcessPlanManufacturingRequest(deleted,1);
    const auto remaining=assemblyProcessPlanNativeShape(scene,deleted);
    EXPECT_EQ(assemblyProcessPlanMaterial(remaining,400,0,19,180),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(remaining,750,0,29,90),TopAbs_OUT);
}

TEST(AssemblyProcessPlanSDOTest, StaleDigestRejectsBeforePublishingNativeGeometry)
{
    Scene scene;
    auto input=assemblyProcessPlanFixture({{250,0}});
    const auto plan=invoke(scene,"ResolveAssemblyProcessPlan",input);
    input["expectedPlanDigest"]=plan.at("planDigest");
    EXPECT_NO_THROW(invoke(scene,"PreviewAssemblyProcessPlan",input));
    const auto resourceCount=scene.Resources().GetInfos().size();
    auto instances=input.at("instances").To<VariantArray>();
    auto changed=instances.front().To<ObjectMap>();
    changed["processInput"]=assemblyProcessPlanLocalInput(600,90);
    instances.front()=changed;
    input["instances"]=instances;
    EXPECT_THROW(invoke(scene,"PreviewAssemblyProcessPlan",input),std::exception);
    EXPECT_EQ(scene.Resources().GetInfos().size(),resourceCount);
}

TEST(AssemblyProcessPlanSDOTest, LocalEvaluationRejectsUnsupportedFramesAndPlanRejectsUnknownTargets)
{
    Scene scene;
    auto local=assemblyProcessPlanLocalInput(250,90);
    const ObjectMap request{{"templateId",std::string("node-v-notch-integrated")},
        {"processInput",local},{"parameters",ObjectMap{}}};
    const auto result=invoke(scene,"EvaluateAssemblyProcess",request);
    EXPECT_EQ(result.at("schema").To<std::string>(),"icax.assembly-process-result");
    EXPECT_TRUE(result.at("applicable").To<bool>());
    auto geometry=local.at("geometry").To<ObjectMap>();
    geometry["frame"]=ObjectMap{{"origin",VariantArray{250.0,0.0,0.0}},
        {"xAxis",VariantArray{0.0,1.0,0.0}},{"yAxis",VariantArray{-1.0,0.0,0.0}},
        {"zAxis",VariantArray{0.0,0.0,1.0}}};
    local["geometry"]=geometry;
    auto unsupported=request;
    unsupported["processInput"]=local;
    const auto rejected=invoke(scene,"EvaluateAssemblyProcess",unsupported);
    EXPECT_FALSE(rejected.at("applicable").To<bool>());
    EXPECT_FALSE(rejected.at("reason").To<std::string>().empty());
    auto input=assemblyProcessPlanFixture({{250,0}});
    auto instances=input.at("instances").To<VariantArray>();
    auto changed=instances.front().To<ObjectMap>();
    changed["targets"]=ObjectMap{{"stock",std::string("unknown-stock")}};
    instances.front()=changed;
    input["instances"]=instances;
    EXPECT_THROW(invoke(scene,"PreviewAssemblyProcessPlan",input),std::exception);
}

TEST(AssemblyProcessPlanSDOTest, CommitReopensAndUpdatesOneStockWithoutAccumulatingOldCuts)
{
    Scene scene;
    auto input=assemblyProcessPlanFixture({{250,0},{750,90}});
    input["planId"]=std::string("repeat-v-plan");
    input["requestId"]=std::string("create-v-plan");
    const auto preview=invoke(scene,"PreviewAssemblyProcessPlan",input);
    input["expectedPlanDigest"]=preview.at("planDigest");
    const auto commit=invoke(scene,"CommitAssemblyProcessPlan",input);
    const auto ids=commit.at("entityIds").To<ObjectMap>();
    ASSERT_EQ(ids.size(),1u);
    const auto id=ids.at("shared-stock").To<std::string>();
    auto saved=part(scene,id);
    EXPECT_EQ(recipe(saved).at("features").To<VariantArray>().size(),2u);
    assertFrozenRecords(recipe(saved));
    checkSameShape(assemblyProcessPlanNativeShape(scene,preview),shape(scene,saved));
    const auto before=recipe(saved);
    const auto bytes=persistedShapeBytes(scene,saved);
    const auto count=scene.Resources().GetInfos().size();
    const auto replay=invoke(scene,"CommitAssemblyProcessPlan",input);
    EXPECT_TRUE(replay.at("replayed").To<bool>());
    EXPECT_EQ(replay.at("entityIds"),commit.at("entityIds"));
    EXPECT_EQ(recipe(part(scene,id)),before);
    EXPECT_EQ(scene.Resources().GetInfos().size(),count);
    const auto plans=invoke(scene,"GetAssemblyProcessPlans",{});
    ASSERT_EQ(plans.at("plans").To<VariantArray>().size(),1u);
    const auto stored=plans.at("plans").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_EQ(stored.at("entityIds"),commit.at("entityIds"));
    EXPECT_EQ(stored.at("sourceInput").To<ObjectMap>().at("stocks"),input.at("stocks"));
    EXPECT_EQ(stored.at("sourceInput").To<ObjectMap>().at("instances"),input.at("instances"));

    const auto dir=std::filesystem::current_path()/"tmp/assembly-process-acceptance";
    std::filesystem::create_directories(dir);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
        .ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="assembly-repeat-v";
    const auto path=dir/"assembly-repeat-v.ictd";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());
    EXPECT_EQ(invoke(reopened,"GetAssemblyProcessPlans",{}),plans);
    EXPECT_EQ(recipe(part(reopened,id)),before);
    EXPECT_EQ(persistedShapeBytes(reopened,part(reopened,id)),bytes);
    checkSameShape(shape(scene,saved),shape(reopened,part(reopened,id)));

    auto changed=stored.at("sourceInput").To<ObjectMap>();
    auto instances=changed.at("instances").To<VariantArray>();
    auto replacement=instances.front().To<ObjectMap>();
    replacement["processInput"]=assemblyProcessPlanLocalInput(400,180);
    instances.front()=replacement;changed["instances"]=instances;
    changed["planId"]=std::string("repeat-v-plan");
    changed["requestId"]=std::string("replace-v-plan");
    changed["expectedStoredPlanDigest"]=commit.at("planDigest");
    const auto update=invoke(reopened,"CommitAssemblyProcessPlan",changed);
    EXPECT_EQ(update.at("entityIds"),commit.at("entityIds"));
    const auto updateRetry=invoke(reopened,"CommitAssemblyProcessPlan",changed);
    EXPECT_TRUE(updateRetry.at("replayed").To<bool>());
    EXPECT_EQ(updateRetry.at("entityIds"),update.at("entityIds"));
    auto changedBody=changed;
    changedBody["previewResourceKey"]=std::string("different-retry-body");
    EXPECT_THROW(invoke(reopened,"CommitAssemblyProcessPlan",changedBody),std::exception);
    const auto updated=shape(reopened,part(reopened,id));
    EXPECT_EQ(assemblyProcessPlanMaterial(updated,250,0,19,0),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(updated,400,0,19,180),TopAbs_OUT);
    EXPECT_EQ(assemblyProcessPlanMaterial(updated,750,0,29,90),TopAbs_OUT);
    const auto oldReplay=invoke(reopened,"CommitAssemblyProcessPlan",input);
    EXPECT_TRUE(oldReplay.at("replayed").To<bool>());
    EXPECT_EQ(oldReplay.at("entityIds"),commit.at("entityIds"));
    EXPECT_EQ(assemblyProcessPlanMaterial(shape(reopened,part(reopened,id)),400,0,19,180),TopAbs_OUT);
    changed.erase("expectedStoredPlanDigest");
    changed["requestId"]=std::string("remove-all-v-plan");
    changed["instances"]=VariantArray{};
    const auto cleared=invoke(reopened,"CommitAssemblyProcessPlan",changed);
    EXPECT_EQ(cleared.at("entityIds"),commit.at("entityIds"));
    EXPECT_TRUE(recipe(part(reopened,id)).at("features").To<VariantArray>().empty());
    const auto raw=shape(reopened,part(reopened,id));
    EXPECT_EQ(assemblyProcessPlanMaterial(raw,400,0,19,180),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(raw,750,0,29,90),TopAbs_IN);
}

TEST(AssemblyProcessPlanSDOTest, CommitFailureIsAtomicAndRequestIdentityCannotChange)
{
    Scene scene;
    auto input=assemblyProcessPlanFixture({{250,0}});
    input["planId"]=std::string("atomic-plan");input["requestId"]=std::string("original-request");
    const auto commit=invoke(scene,"CommitAssemblyProcessPlan",input);
    const auto id=commit.at("entityIds").To<ObjectMap>().at("shared-stock").To<std::string>();
    const auto before=recipe(part(scene,id));
    const auto bytes=persistedShapeBytes(scene,part(scene,id));
    const auto task=std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubeDesignerRootComponent>(
        scene.Database().GetMetaEntity()->GetComponent(iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName))->GetNestingTask();
    auto invalid=input;
    invalid["instances"]=assemblyProcessPlanFixture({{400,0}}).at("instances");
    EXPECT_THROW(invoke(scene,"CommitAssemblyProcessPlan",invalid),std::exception);
    auto stock=assemblyProcessPlanStock();stock["id"]=std::string("z-last-stock");
    stock["label"]=std::string(161,'x');
    invalid["stocks"]=VariantArray{assemblyProcessPlanStock(),stock};
    invalid["requestId"]=std::string("failed-two-stocks");
    // The first stock can finish its preparation, but the second must fail.
    const auto resourcesBefore=scene.Resources().GetInfos().size();
    try { invoke(scene,"CommitAssemblyProcessPlan",invalid);FAIL()<<"Invalid second stock was committed"; }
    catch(const std::exception& failure) {
        EXPECT_NE(std::string(failure.what()).find("z-last-stock 保存预备失败"),std::string::npos)<<failure.what();
    }
    EXPECT_GT(scene.Resources().GetInfos().size(),resourcesBefore);
    EXPECT_EQ(recipe(part(scene,id)),before);
    EXPECT_EQ(persistedShapeBytes(scene,part(scene,id)),bytes);
    EXPECT_EQ(std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubeDesignerRootComponent>(
        scene.Database().GetMetaEntity()->GetComponent(iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName))->GetNestingTask(),task);
    const auto plans=invoke(scene,"GetAssemblyProcessPlans",{}).at("plans").To<VariantArray>();
    ASSERT_EQ(plans.size(),1u);
    EXPECT_EQ(plans.front().To<ObjectMap>().at("entityIds"),commit.at("entityIds"));
}

TEST(AssemblyProcessPlanSDOTest, OwnerMigrationAndIndependentWizardEditAreProtected)
{
    Scene scene;
    auto input=assemblyProcessPlanFixture({});
    auto first=assemblyProcessPlanStock();first["id"]=std::string("a-first");
    auto last=assemblyProcessPlanStock();last["id"]=std::string("z-last");
    input["stocks"]=VariantArray{first,last};
    input["planId"]=std::string("owner-plan");input["requestId"]=std::string("owner-create");
    const auto commit=invoke(scene,"CommitAssemblyProcessPlan",input);
    const auto ids=commit.at("entityIds").To<ObjectMap>();
    const auto firstId=ids.at("a-first").To<std::string>();
    const auto lastId=ids.at("z-last").To<std::string>();
    EXPECT_TRUE(recipe(part(scene,firstId)).at("assemblyProcessPlan").To<ObjectMap>().contains("sourceInput"));
    EXPECT_FALSE(recipe(part(scene,lastId)).at("assemblyProcessPlan").To<ObjectMap>().contains("sourceInput"));
    input["stocks"]=VariantArray{last};input["requestId"]=std::string("owner-migrate");
    const auto migrated=invoke(scene,"CommitAssemblyProcessPlan",input);
    ASSERT_EQ(migrated.at("entityIds").To<ObjectMap>().size(),1u);
    EXPECT_EQ(migrated.at("entityIds").To<ObjectMap>().at("z-last").To<std::string>(),lastId);
    EXPECT_THROW(part(scene,firstId),std::exception);
    const auto config=recipe(part(scene,lastId));
    EXPECT_TRUE(config.at("assemblyProcessPlan").To<ObjectMap>().contains("sourceInput"));
    EXPECT_EQ(config.at("assemblyProcessPlan").To<ObjectMap>().at("requestJournal").To<VariantArray>().size(),2u);
    const auto p=part(scene,lastId);
    invoke(scene,"ApplyPunchWizard",{{"partEntityId",lastId},{"resourceId",p->GetManufacturingGeometryResourceID()},
        {"resourceVersion",p->GetManufacturingGeometryResourceVersion()},
        {"features",config.at("features")},{"ends",config.at("ends")}});
    EXPECT_EQ(recipe(part(scene,lastId)).at("assemblyProcessPlan"),config.at("assemblyProcessPlan"));
    input["requestId"]=std::string("owner-overwrite-edit");
    EXPECT_THROW(invoke(scene,"CommitAssemblyProcessPlan",input),std::exception);
}

namespace {
VariantArray assemblyProcessPlanMatrix(double x=0,double y=0,double z=0) {
    return {1.0,0.0,0.0,x,0.0,1.0,0.0,y,0.0,0.0,1.0,z,0.0,0.0,0.0,1.0};
}
ObjectMap assemblyProcessPlanPairedHoleInput(double station) {
    auto a=assemblyProcessPlanStock(),b=a;
    a["matrix"]=assemblyProcessPlanMatrix();
    b["id"]=std::string("hole-mate");b["matrix"]=assemblyProcessPlanMatrix(0,0,40);
    a["anchor"]=ObjectMap{{"kind",std::string("side")},{"reference",std::string("start")},
        {"station",station},{"face",std::string("top")},{"offset",0.0}};
    b["anchor"]=ObjectMap{{"kind",std::string("side")},{"reference",std::string("start")},
        {"station",station},{"face",std::string("bottom")},{"offset",0.0}};
    return {{"schema",std::string("icax.assembly-process-input")},{"schemaVersion",1ull},
        {"parts",ObjectMap{{"fastenedPart",a},{"basePart",b}}},
        {"geometry",ObjectMap{{"overlapLength",1000.0}}}};
}
ObjectMap assemblyProcessPlanHoleInstance(double station,const std::string& id,std::uint64_t count=2) {
    return {{"instanceId",id},{"templateId",std::string("mechanical-fastener")},
        {"processInput",assemblyProcessPlanPairedHoleInput(station)},
        {"parameters",ObjectMap{{"fastenerType",std::string("bolt")},{"nominalDiameter",10.0},
            {"holeClearance",1.0},{"count",count},{"pitch",50.0}}},
        {"targets",ObjectMap{{"fastenedPart",std::string("shared-stock")},{"basePart",std::string("hole-mate")}}}};
}
TopoDS_Shape assemblyProcessPlanFindNativeShape(Scene& scene,const ObjectMap& plan,const std::string& id) {
    for(const auto& value:plan.at("nativeResults").To<VariantArray>()) {
        const auto result=value.To<ObjectMap>();
        if(result.at("blankId").To<std::string>()!=id)continue;
        EXPECT_TRUE(result.at("resultValid").To<bool>());
        EXPECT_EQ(finishedProductNumber(result.at("solidCount")),1.0);
        return finishedProductNativeShape(scene,result);
    }
    throw std::runtime_error("Expected stock native result missing: "+id);
}
}

TEST(AssemblyProcessPlanSDOTest, RepeatedPairedHoleArraysUseActualCentersOnTheSameStock)
{
    Scene scene;
    auto mate=assemblyProcessPlanStock();mate["id"]=std::string("hole-mate");
    mate["matrix"]=assemblyProcessPlanMatrix(0,0,40);
    const ObjectMap input{{"stocks",VariantArray{assemblyProcessPlanStock(),mate}},
        {"instances",VariantArray{assemblyProcessPlanHoleInstance(200,"holes-left"),
            assemblyProcessPlanHoleInstance(700,"holes-right")}}};
    const auto plan=invoke(scene,"PreviewAssemblyProcessPlan",input);
    ASSERT_EQ(plan.at("nativeResults").To<VariantArray>().size(),2u);
    for(const auto& id:{"shared-stock","hole-mate"}) {
        SCOPED_TRACE(id);
        const auto result=assemblyProcessPlanFindNativeShape(scene,plan,id);
        for(const auto station:{175.0,225.0,675.0,725.0}) {
            EXPECT_EQ(assemblyProcessPlanMaterial(result,station,0,19,0),TopAbs_OUT)<<station;
            EXPECT_EQ(assemblyProcessPlanMaterial(result,station,0,-19,0),TopAbs_OUT)<<station;
            EXPECT_EQ(assemblyProcessPlanMaterial(result,station,8,19,0),TopAbs_IN)<<station;
        }
        for(const auto untouched:{200.0,500.0,700.0})
            EXPECT_EQ(assemblyProcessPlanMaterial(result,untouched,0,19,0),TopAbs_IN)<<untouched;
    }
}

TEST(AssemblyProcessPlanSDOTest, BothSelectedEndsAndMiddleHolesShareOneNativeStock)
{
    Scene scene;
    auto left=assemblyProcessPlanStock();left["id"]=std::string("left-mate");
    left["matrix"]=assemblyProcessPlanMatrix(-1000,0,0);
    auto right=assemblyProcessPlanStock();right["id"]=std::string("right-mate");
    right["matrix"]=assemblyProcessPlanMatrix(1000,0,0);
    auto hole=assemblyProcessPlanStock();hole["id"]=std::string("hole-mate");
    hole["matrix"]=assemblyProcessPlanMatrix(0,0,40);
    VariantArray instances;
    for(const auto& end:{"start","end"}) {
        const bool start=std::string(end)=="start";
        auto a=assemblyProcessPlanStock(),b=start?left:right;
        a["matrix"]=assemblyProcessPlanMatrix();
        a["anchor"]=ObjectMap{{"kind",std::string("end")},{"end",std::string(end)}};
        b["anchor"]=ObjectMap{{"kind",std::string("end")},{"end",std::string(start?"end":"start")}};
        const ObjectMap local{{"schema",std::string("icax.assembly-process-input")},{"schemaVersion",1ull},
            {"parts",ObjectMap{{"memberA",a},{"memberB",b}}},
            {"geometry",ObjectMap{{"jointAngle",0.0},{"planeRotation",0.0}}}};
        instances.emplace_back(ObjectMap{{"instanceId",std::string(start?"start-cut":"end-cut")},
            {"templateId",std::string("two-end-end-angle")},{"processInput",local},
            {"parameters",ObjectMap{{"fitGap",start?2.0:4.0}}},
            {"targets",ObjectMap{{"memberA",std::string("shared-stock")},
                {"memberB",std::string(start?"left-mate":"right-mate")}}}});
    }
    instances.emplace_back(assemblyProcessPlanHoleInstance(500,"middle-hole",1));
    const ObjectMap input{{"stocks",VariantArray{assemblyProcessPlanStock(),left,right,hole}},
        {"instances",instances}};
    const auto plan=invoke(scene,"PreviewAssemblyProcessPlan",input);
    ASSERT_EQ(plan.at("nativeResults").To<VariantArray>().size(),4u);
    const auto result=assemblyProcessPlanFindNativeShape(scene,plan,"shared-stock");
    EXPECT_EQ(assemblyProcessPlanMaterial(result,0.5,0,19,0),TopAbs_OUT);
    EXPECT_EQ(assemblyProcessPlanMaterial(result,1.5,0,19,0),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(result,997.5,0,19,0),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(result,998.5,0,19,0),TopAbs_OUT);
    EXPECT_EQ(assemblyProcessPlanMaterial(result,500,0,19,0),TopAbs_OUT);
    EXPECT_EQ(assemblyProcessPlanMaterial(result,500,8,19,0),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(result,600,0,19,0),TopAbs_IN);
}

TEST(AssemblyProcessPlanSDOTest, ReverseIntervalsMirrorHoleArrayPitchAndBothWalls)
{
    Scene scene;
    auto mate=assemblyProcessPlanStock();mate["id"]=std::string("hole-mate");
    mate["matrix"]=assemblyProcessPlanMatrix(0,0,40);
    VariantArray instances;
    for(const auto& location:{std::pair{200.0,std::string("holes-left")},
            std::pair{700.0,std::string("holes-right")}}) {
        auto instance=assemblyProcessPlanHoleInstance(location.first,location.second);
        auto local=instance.at("processInput").To<ObjectMap>();
        auto parts=local.at("parts").To<ObjectMap>();
        for(const auto& role:{"fastenedPart","basePart"}) {
            auto part=parts.at(role).To<ObjectMap>();
            part["matrix"]=VariantArray{-1.0,0.0,0.0,1000.0,0.0,-1.0,0.0,0.0,
                0.0,0.0,1.0,std::string(role)=="basePart"?40.0:0.0,0.0,0.0,0.0,1.0};
            parts[role]=part;
        }
        local["parts"]=parts;instance["processInput"]=local;
        instance["targets"]=ObjectMap{
            {"fastenedPart",ObjectMap{{"stockId",std::string("shared-stock")},{"start",0.0},{"reverse",true}}},
            {"basePart",ObjectMap{{"stockId",std::string("hole-mate")},{"start",0.0},{"reverse",true}}}};
        instances.emplace_back(instance);
    }
    const auto plan=invoke(scene,"PreviewAssemblyProcessPlan",
        {{"stocks",VariantArray{assemblyProcessPlanStock(),mate}},{"instances",instances}});
    for(const auto& id:{"shared-stock","hole-mate"}) {
        const auto result=assemblyProcessPlanFindNativeShape(scene,plan,id);
        for(const auto station:{825.0,775.0,325.0,275.0}) {
            EXPECT_EQ(assemblyProcessPlanMaterial(result,station,0,19,0),TopAbs_OUT)<<station;
            EXPECT_EQ(assemblyProcessPlanMaterial(result,station,0,-19,0),TopAbs_OUT)<<station;
        }
        for(const auto untouched:{875.0,375.0,800.0,500.0,300.0})
            EXPECT_EQ(assemblyProcessPlanMaterial(result,untouched,0,19,0),TopAbs_IN)<<untouched;
    }
}

TEST(AssemblyProcessPlanSDOTest, CommitPreservesOtherSelectionsAndDoesNotSelectUnselectedParts)
{
    Scene scene;
    const auto unselected=invoke(scene,"AddNestingPunchPart",request("unselected-tube","rect"));
    const auto unselectedId=unselected.at("partEntityId").To<std::string>();
    const auto selected=invoke(scene,"AddNestingPunchPart",request("selected-product-reference","rect"));
    const auto selectedId=selected.at("partEntityId").To<std::string>();
    auto other=assemblyProcessPlanFixture({});
    other["planId"]=std::string("other-plan");other["requestId"]=std::string("other-create");
    const auto otherCommit=invoke(scene,"CommitAssemblyProcessPlan",other);
    const auto otherId=otherCommit.at("entityIds").To<ObjectMap>().at("shared-stock").To<std::string>();
    const auto root=std::dynamic_pointer_cast<iCAX::TubeDesigner::CTubeDesignerRootComponent>(
        scene.Database().GetMetaEntity()->GetComponent(iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName));
    auto task=root->GetNestingTask();
    const ObjectMap productReference{{"productEntityId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
        {"partEntityId",selectedId},
        {"generationRunId",iCAX::Data::to_string(part(scene,selectedId)->GetGenerationRunID())},
        {"selectionNote",std::string("product-reference-sentinel")}};
    const ObjectMap otherReference{{"partEntityId",otherId},
        {"generationRunId",iCAX::Data::to_string(part(scene,otherId)->GetGenerationRunID())},
        {"selectionNote",std::string("preserve-this-reference")}};
    task["parts"]=VariantArray{productReference,otherReference};
    auto& transaction=scene.Database().BeginTransaction("Set acceptance nesting selection");
    transaction.ModifyComponent(scene.Database().GetMetaEntity()->GetID(),
        iCAX::TubeDesigner::CTubeDesignerRootComponent::S_ClassName,
        {{iCAX::TubeDesigner::CTubeDesignerRootComponent::PropertyName_NestingTask,task}});
    std::string error;ASSERT_TRUE(scene.Database().CommitTransaction(transaction,error))<<error;
    auto input=assemblyProcessPlanFixture({});
    input["planId"]=std::string("selected-plan");input["requestId"]=std::string("selected-create");
    const auto created=invoke(scene,"CommitAssemblyProcessPlan",input);
    const auto id=created.at("entityIds").To<ObjectMap>().at("shared-stock").To<std::string>();
    for(const auto requestId:{"selected-update-one","selected-update-two"}) {
        input["requestId"]=std::string(requestId);
        const auto updated=invoke(scene,"CommitAssemblyProcessPlan",input);
        EXPECT_EQ(updated.at("entityIds"),created.at("entityIds"));
        const auto references=root->GetNestingTask().at("parts").To<VariantArray>();
        ASSERT_EQ(references.size(),3u);
        EXPECT_EQ(references[0],productReference);
        EXPECT_EQ(references[1],otherReference);
        EXPECT_EQ(references[2].To<ObjectMap>().at("partEntityId").To<std::string>(),id);
        for(const auto& value:references) {
            const auto reference=value.To<ObjectMap>();
            if(reference.contains("partEntityId"))
                EXPECT_NE(reference.at("partEntityId").To<std::string>(),unselectedId);
        }
        EXPECT_NO_THROW(part(scene,otherId));
        EXPECT_NO_THROW(part(scene,unselectedId));
    }
}

TEST(AssemblyProcessPlanSDOTest, AuthoritativeSectionsIgnoreClientGeometryAndUserRevisionInvalidatesPreview)
{
    Scene scene;
    auto ordinary=assemblyProcessPlanFixture({{250,90}});
    const auto trusted=invoke(scene,"ResolveAssemblyProcessPlan",ordinary);
    auto instances=ordinary.at("instances").To<VariantArray>();
    auto instance=instances.front().To<ObjectMap>();
    auto input=instance.at("processInput").To<ObjectMap>();
    auto parts=input.at("parts").To<ObjectMap>();
    auto stock=parts.at("stock").To<ObjectMap>();
    stock["section"]=ObjectMap{{"profile",systemProfile("rect",{{"width",800.0},{"depth",600.0}})}};
    parts["stock"]=stock;input["parts"]=parts;instance["processInput"]=input;
    instances.front()=instance;ordinary["instances"]=instances;
    ordinary["nativeProfileDependencies"]=VariantArray{ObjectMap{{"forged",true}}};
    const auto ignored=invoke(scene,"ResolveAssemblyProcessPlan",ordinary);
    EXPECT_EQ(ignored.at("planDigest"),trusted.at("planDigest"));
    EXPECT_EQ(ignored.at("manufacturingParts"),trusted.at("manufacturingParts"));
    const ObjectMap localRequest{{"templateId",std::string("node-v-notch-integrated")},
        {"processInput",input},{"parameters",ObjectMap{}}};
    const auto local=invoke(scene,"EvaluateAssemblyProcess",localRequest);
    EXPECT_TRUE(local.at("applicable").To<bool>());
    EXPECT_EQ(local.at("parameters"),ObjectMap{});

    AssemblyProcessProfileProduct product;
    const auto profileId=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
    iCAX::Application::CProductUserDataRecord record;
    record.FeatureID="profile";record.RecordType="fixed-section";
    record.SubjectType="profile-definition";record.SubjectID=profileId;record.RecordID=profileId;
    record.Payload=systemProfile("rect",assemblyProcessPlanStock().at("parameters").To<ObjectMap>());
    auto stored=product.GetUserDataStore()->Put(record,0);
    auto user=assemblyProcessPlanFixture({{250,90}});
    auto raw=assemblyProcessPlanStock();
    raw["profileRef"]=ObjectMap{{"scope",std::string("user")},{"id",profileId}};
    raw["parameters"]=ObjectMap{};user["stocks"]=VariantArray{raw};
    instances=user.at("instances").To<VariantArray>();instance=instances.front().To<ObjectMap>();
    input=instance.at("processInput").To<ObjectMap>();parts=input.at("parts").To<ObjectMap>();
    stock=parts.at("stock").To<ObjectMap>();stock["profileRef"]=raw.at("profileRef");stock["parameters"]=ObjectMap{};
    // Even a user section supplied by the client must be replaced by the saved
    // library contour, before a function estimates its material intervals.
    stock["section"]=ObjectMap{{"profile",systemProfile("round",{{"width",200.0},{"wallThickness",10.0}})}};
    parts["stock"]=stock;input["parts"]=parts;instance["processInput"]=input;
    instances.front()=instance;user["instances"]=instances;
    const auto plan=invoke(scene,"PreviewAssemblyProcessPlan",user,&product);
    const auto shape=assemblyProcessPlanNativeShape(scene,plan);
    EXPECT_EQ(assemblyProcessPlanMaterial(shape,250,0,-29.5,90),TopAbs_IN);
    EXPECT_EQ(assemblyProcessPlanMaterial(shape,250,0,29,90),TopAbs_OUT);
    user["expectedPlanDigest"]=plan.at("planDigest");
    // An unchanged contour with a new library revision still invalidates a
    // cached plan, before publishing native geometry or saving any entity.
    stored=product.GetUserDataStore()->Put(stored,stored.Revision);
    const auto resourceCount=scene.Resources().GetInfos().size();
    EXPECT_THROW(invoke(scene,"PreviewAssemblyProcessPlan",user,&product),std::exception);
    EXPECT_EQ(scene.Resources().GetInfos().size(),resourceCount);
    user.erase("expectedPlanDigest");
    const auto newer=invoke(scene,"ResolveAssemblyProcessPlan",user,&product);
    EXPECT_NE(newer.at("planDigest"),plan.at("planDigest"));
}
