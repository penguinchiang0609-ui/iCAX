namespace side_sketch_acceptance {
ObjectMap rectangleContour(double width, double height) {
    const std::array<VariantArray,4> points{{
        {-width/2,-height/2},{width/2,-height/2},{width/2,height/2},{-width/2,height/2}}};
    VariantArray segments;
    for(std::size_t i=0;i<4;++i) segments.emplace_back(ObjectMap{
        {"kind",std::string("line")},{"start",points[i]},{"end",points[(i+1)%4]}});
    return {{"kind",std::string("path")},{"closed",true},{"segments",segments}};
}
ObjectMap profile() {
    return {{"schema",std::string("icax.imported-tube-profile")},{"schemaVersion",1ull},
        {"kind",std::string("fixed-section")},{"profileForm",std::string("fixed")},
        {"name",std::string("Side sketch tube")},{"width",40.0},{"depth",20.0},
        {"contours",VariantArray{rectangleContour(40,20),rectangleContour(36,16)}}};
}
ObjectMap sketch(double width=1.0,bool open=false) {
    const ObjectMap entity=open
        ? ObjectMap{{"id",std::string("kerf")},{"kind",std::string("line")},
            {"x1",60.0},{"y1",12.0},{"x2",100.0},{"y2",12.0},{"closed",false}}
        : ObjectMap{{"id",std::string("window")},{"kind",std::string("rectangle")},
            {"x",60.0},{"y",7.0},{"width",30.0},{"height",10.0},{"closed",true}};
    return {{"schema",std::string("icax.tube-sketch")},{"schemaVersion",1ull},
        {"kind",std::string("side")},{"unit",std::string("mm")},
        {"length",200.0},{"faceHeight",120.0},{"trajectoryWidth",width},
        {"entities",VariantArray{entity}}};
}
ObjectMap request() {
    return {{"profile",profile()},{"length",200.0},{"quantity",2ull},
        {"name",std::string("二维镂空验收")},{"previewResourceKey",std::string("native-side-sketch")}};
}
ObjectMap roundContour(double radius) {
    VariantArray segments;
    const auto pi=std::acos(-1.0);
    const auto point=[&](double angle) {return VariantArray{radius*std::cos(angle),radius*std::sin(angle)};};
    for(int index=0;index<4;++index) segments.emplace_back(ObjectMap{
        {"kind",std::string("arc")},{"start",point(index*pi/2)},
        {"middle",point((index+.5)*pi/2)},{"end",index==3?point(0):point((index+1)*pi/2)}});
    return {{"kind",std::string("path")},{"closed",true},{"segments",segments}};
}
std::size_t curveEdges(const TopoDS_Shape& value,GeomAbs_CurveType type) {
    std::vector<TopoDS_Edge> unique;
    for(TopExp_Explorer edges(value,TopAbs_EDGE);edges.More();edges.Next()) {
        const auto edge=TopoDS::Edge(edges.Current());
        if(BRepAdaptor_Curve(edge).GetType()!=type)continue;
        if(std::none_of(unique.begin(),unique.end(),[&](const auto& saved){return saved.IsSame(edge);}))
            unique.push_back(edge);
    }
    return unique.size();
}
class ProductScope final : public iCAX::Product::IProductContext {
public:
    ProductScope() {
        definition.ProductID="icax.tube-designer";
        store=std::make_shared<iCAX::Application::CProductUserDataStore>(
            std::make_shared<iCAX::Application::CSqliteUserDataStore>(":memory:"),definition.ProductID);
    }
    const iCAX::Product::CProductDefinition& GetDefinition() const override {return definition;}
    const std::string& GetProductID() const override {return definition.ProductID;}
    std::shared_ptr<iCAX::Application::IProductUserDataStore> GetUserDataStore() const override {return store;}
    iCAX::Product::CProductData GetProductData() const override {return {};}
    iCAX::Services::CServiceProvider& GetServiceProvider() const override {throw std::logic_error("No product services");}
    iCAX::Database::IMetaRegistry& GetMetaRegistry() const override {throw std::logic_error("No product registry");}
    iCAX::Behaviour::IBehaviourRegistry& GetBehaviourRegistry() const override {throw std::logic_error("No product behaviors");}
    iCAX::Resource::CResourceLoaderRegistry& GetResourceLoaderRegistry() const override {throw std::logic_error("No product loaders");}
    iCAX::Interaction::CSDORegistry& GetSDORegistry() const override {throw std::logic_error("Use registered SDO");}
private:
    iCAX::Product::CProductDefinition definition;
    std::shared_ptr<iCAX::Application::IProductUserDataStore> store;
};
}

TEST(NestingSideSketchSDO, PreviewIsNeutralAndPublishesRealMeshThenReleasesAllDraftResources) {
    Scene scene;
    const auto ids=scene.Database().GetEntityIDs();
    const auto undo=scene.Database().GetUndoArray();
    auto payload=side_sketch_acceptance::request();
    const auto blank=invoke(scene,"PreviewNestingSideSketchPart",payload);
    EXPECT_TRUE(blank.at("available").To<bool>());
    EXPECT_NEAR(120.0,blank.at("unfolding").To<ObjectMap>().at("perimeter").To<double>(),0.001);
    payload["sketch"]=side_sketch_acceptance::sketch();
    const auto result=invoke(scene,"PreviewNestingSideSketchPart",payload);
    const auto preview=result.at("preview").To<ObjectMap>();
    const auto geometry=preview.at("baseGeometry").To<ObjectMap>();
    const auto url=geometry.at("url").To<std::string>();
    const auto version=geometry.at("version").To<unsigned long long>();
    const auto mesh=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(url,version);
    ASSERT_NE(nullptr,mesh);
    EXPECT_GT(mesh->Size(),1000u);
    EXPECT_EQ("ICRG",std::string(reinterpret_cast<const char*>(mesh->Data())+4,4));
    EXPECT_EQ(ids,scene.Database().GetEntityIDs());
    EXPECT_EQ(undo,scene.Database().GetUndoArray());
    invoke(scene,"ReleaseNestingSideSketchPreview",{{"previewResourceKey",std::string("native-side-sketch")}});
    EXPECT_FALSE(scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(url,version));
    for(const auto& info:scene.Resources().GetManifest(true))
        EXPECT_EQ(std::string::npos,info.Key.Source.find("nesting-side-sketch-preview"));
}

TEST(NestingSideSketchSDO, ClosedRegionRemovesItsInteriorAndCreationHasOneUndo) {
    Scene scene;
    const auto ids=scene.Database().GetEntityIDs();
    const auto undoCount=scene.Database().GetUndoArray().size();
    auto payload=side_sketch_acceptance::request();
    payload["sketch"]=side_sketch_acceptance::sketch();
    const auto added=invoke(scene,"AddNestingSideSketchPart",payload);
    const auto id=added.at("partEntityId").To<std::string>();
    const auto saved=part(scene,id);
    EXPECT_EQ(2ull,saved->GetQuantity());
    EXPECT_EQ("二维绘制零件",saved->GetRole());
    const auto result=shape(scene,saved);
    EXPECT_TRUE(BRepCheck_Analyzer(result).IsValid());
    // Blank wall area is 40*20-36*16=224; a 30*10 window
    // cuts the complete 2 mm wall, not merely its perimeter.
    EXPECT_NEAR(200.0*224.0-600.0,volume(result),1.0);
    const auto properties=saved->GetItemProperties();
    EXPECT_TRUE(properties.contains("tubeDesigner.sideSketchBaseResourceId"));
    EXPECT_EQ(1.0,properties.at("tubeDesigner.sideSketch").To<ObjectMap>().at("trajectoryWidth").To<double>());
    EXPECT_EQ(undoCount+1,scene.Database().GetUndoArray().size());
    ASSERT_TRUE(scene.Database().Undo());
    EXPECT_EQ(ids,scene.Database().GetEntityIDs());
    ASSERT_TRUE(scene.Database().Redo());
    EXPECT_TRUE(scene.Database().GetEntity(*iCAX::Data::uuid::from_string(id)));
}

TEST(NestingSideSketchSDO, RejectsEmptyInvalidWidthOrMismatchedBoundsWithoutCreatingAJob) {
    Scene scene;
    const auto ids=scene.Database().GetEntityIDs();
    const auto undo=scene.Database().GetUndoArray();
    auto payload=side_sketch_acceptance::request();
    EXPECT_THROW(invoke(scene,"AddNestingSideSketchPart",payload),std::exception);
    auto draft=side_sketch_acceptance::sketch();
    draft["entities"]=VariantArray{};payload["sketch"]=draft;
    EXPECT_THROW(invoke(scene,"AddNestingSideSketchPart",payload),std::exception);
    for(const auto width:{0.0,-1.0,1001.0}) {
        draft=side_sketch_acceptance::sketch(width,true);payload["sketch"]=draft;
        EXPECT_THROW(invoke(scene,"AddNestingSideSketchPart",payload),std::exception);
    }
    draft=side_sketch_acceptance::sketch();draft["faceHeight"]=119.0;payload["sketch"]=draft;
    EXPECT_THROW(invoke(scene,"AddNestingSideSketchPart",payload),std::exception);
    EXPECT_EQ(ids,scene.Database().GetEntityIDs());
    EXPECT_EQ(undo,scene.Database().GetUndoArray());
}

TEST(NestingSideSketchSDO, SaveReopenAndReplaceAlwaysUseFrozenBlankAndPersistKerfWidth) {
    Scene scene;
    auto payload=side_sketch_acceptance::request();payload["sketch"]=side_sketch_acceptance::sketch(1.0,true);
    const auto id=invoke(scene,"AddNestingSideSketchPart",payload).at("partEntityId").To<std::string>();
    const auto initialVolume=volume(shape(scene,part(scene,id)));
    EXPECT_NEAR(200.0*224.0-80.0,initialVolume,1.0);
    const auto directory=std::filesystem::current_path()/"tmp/side-sketch-persistence";
    std::filesystem::create_directories(directory);
    const auto path=directory/"side-sketch.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="二维绘制验收";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());
    EXPECT_NEAR(initialVolume,volume(shape(reopened,part(reopened,id))),0.001);
    auto draft=side_sketch_acceptance::sketch(2.0,true);
    for(int repeat=0;repeat<2;++repeat) {
        const auto current=part(reopened,id);
        invoke(reopened,"SavePartSketch",{{"partEntityId",id},
            {"resourceVersion",current->GetManufacturingGeometryResourceVersion()},{"sketch",draft}});
        EXPECT_NEAR(200.0*224.0-160.0,volume(shape(reopened,part(reopened,id))),1.0);
    }
    EXPECT_EQ(2.0,part(reopened,id)->GetItemProperties().at("tubeDesigner.sideSketch")
        .To<ObjectMap>().at("trajectoryWidth").To<double>());
    file.Save(path,info,reopened.Database(),reopened.Resources());
    Scene finalScene(scene.project,scene.GetSceneID(),false);file.Open(path,finalScene.Database(),finalScene.Resources());
    EXPECT_NEAR(200.0*224.0-160.0,volume(shape(finalScene,part(finalScene,id))),1.0);
    draft["entities"]=VariantArray{};
    invoke(finalScene,"SavePartSketch",{{"partEntityId",id},{"sketch",draft}});
    EXPECT_NEAR(200.0*224.0,volume(shape(finalScene,part(finalScene,id))),0.001);
}

TEST(NestingSideSketchSDO, PeriodicHorizontalCoordinatesCropAxialOverflowAndReopenTheOriginalBlank) {
    Scene scene;
    auto payload=side_sketch_acceptance::request();
    const auto preview=invoke(scene,"PreviewNestingSideSketchPart",payload);
    const auto perimeter=preview.at("unfolding").To<ObjectMap>().at("perimeter").To<double>();
    auto draft=side_sketch_acceptance::sketch();
    draft["coordinateSpace"]=std::string("arc-length-axial");
    draft["entities"]=VariantArray{ObjectMap{{"id",std::string("periodic-trim")},
        {"kind",std::string("rectangle")},{"x",1000000.0 * perimeter},
        {"y",-10.0},{"width",perimeter * 3.0},{"height",40.0},{"closed",true}}};
    payload["sketch"]=draft;
    const auto id=invoke(scene,"AddNestingSideSketchPart",payload).at("partEntityId").To<std::string>();
    auto saved=part(scene,id);
    EXPECT_NEAR(170.0,saved->GetLength(),0.001);
    EXPECT_NEAR(170.0 * 224.0,volume(shape(scene,saved)),0.001);
    const auto version=saved->GetManufacturingGeometryResourceVersion();
    // No product context/profile library is available to this invocation.
    const auto existing=invoke(scene,"PreviewNestingSideSketchPart",{
        {"partEntityId",id},{"resourceVersion",version},{"previewResourceKey",std::string("frozen-existing")},
        {"profileRef",ObjectMap{{"scope",std::string("user")},{"id",std::string("deleted-profile")}}}});
    EXPECT_NEAR(200.0,existing.at("length").To<double>(),0.001);
    EXPECT_NEAR(200.0,existing.at("unfolding").To<ObjectMap>().at("length").To<double>(),0.001);
    EXPECT_THROW(invoke(scene,"PreviewNestingSideSketchPart",{{"partEntityId",id},
        {"resourceVersion",version + 1},{"previewResourceKey",std::string("stale")}}),std::exception);
    auto entities=draft.at("entities").To<VariantArray>();auto entity=entities.front().To<ObjectMap>();
    entity["height"]=45.0;entities.front()=entity;draft["entities"]=entities;
    invoke(scene,"SavePartSketch",{{"partEntityId",id},{"resourceVersion",version},{"sketch",draft}});
    EXPECT_NEAR(165.0,part(scene,id)->GetLength(),0.001);
    draft["entities"]=VariantArray{};
    invoke(scene,"SavePartSketch",{{"partEntityId",id},{"sketch",draft}});
    EXPECT_NEAR(200.0,part(scene,id)->GetLength(),0.001);
    EXPECT_NEAR(200.0 * 224.0,volume(shape(scene,part(scene,id))),0.001);
}

TEST(NestingSideSketchSDO, PreservesActualMouseFractionalCoordinatesOnRoundTube) {
    Scene scene;
    auto profile=side_sketch_acceptance::profile();
    profile["width"]=40.0;profile["depth"]=40.0;
    profile["contours"]=VariantArray{side_sketch_acceptance::roundContour(20),side_sketch_acceptance::roundContour(18)};
    ObjectMap draft{{"schema",std::string("icax.tube-sketch")},{"schemaVersion",1ull},
        {"kind",std::string("side")},{"unit",std::string("mm")},{"coordinateSpace",std::string("arc-length-axial")},
        {"length",400.0},{"faceHeight",40.0*std::acos(-1.0)},{"trajectoryWidth",2.0},
        {"entities",VariantArray{
            ObjectMap{{"id",std::string("mouse-window")},{"kind",std::string("rectangle")},
                {"x",620.3145896374909},{"y",100.0},{"width",15.999996871862827},
                {"height",40.00002906436012},{"radius",0.0},{"closed",true}},
            ObjectMap{{"id",std::string("mouse-kerf")},{"kind",std::string("line")},
                {"x1",35.000001192027575},{"y1",249.99997093563988},
                {"x2",35.000001192027575},{"y2",279.99998546781995}}}}};
    ObjectMap payload{{"profile",profile},{"length",400.0},{"sketch",draft},
        {"previewResourceKey",std::string("mouse-fractional")}};
    auto referenceRequest=payload;
    referenceRequest.erase("sketch");
    const auto reference=invoke(scene,"PreviewNestingSideSketchPart",referenceRequest)
        .at("unfolding").To<ObjectMap>();
    EXPECT_NEAR(reference.at("perimeter").To<double>(),40.0*std::acos(-1.0),1.0e-7)
        << "the UI period must use the same true arc length as the cutter mapping";
    EXPECT_NO_THROW(invoke(scene,"PreviewNestingSideSketchPart",payload));
    const auto id=invoke(scene,"AddNestingSideSketchPart",payload).at("partEntityId").To<std::string>();
    const auto saved=part(scene,id);
    EXPECT_TRUE(BRepCheck_Analyzer(shape(scene,saved)).IsValid());
    const auto entities=saved->GetItemProperties().at("tubeDesigner.sideSketch").To<ObjectMap>().at("entities").To<VariantArray>();
    EXPECT_DOUBLE_EQ(620.3145896374909,entities.front().To<ObjectMap>().at("x").To<double>());
}

TEST(NestingSideSketchSDO, FullyOutsideAxialDraftCreatesAndSavesTheUncutBlank) {
    Scene scene;
    auto payload=side_sketch_acceptance::request();
    auto draft=side_sketch_acceptance::sketch();
    draft["coordinateSpace"]=std::string("arc-length-axial");
    draft["entities"]=VariantArray{ObjectMap{{"id",std::string("outside")},{"kind",std::string("rectangle")},
        {"x",-1000.0},{"y",-30.0},{"width",300.0},{"height",20.0},{"closed",true}}};
    payload["sketch"]=draft;
    EXPECT_NO_THROW(invoke(scene,"PreviewNestingSideSketchPart",payload));
    const auto id=invoke(scene,"AddNestingSideSketchPart",payload).at("partEntityId").To<std::string>();
    EXPECT_NEAR(200.0*224.0,volume(shape(scene,part(scene,id))),0.001);
    EXPECT_NEAR(200.0,part(scene,id)->GetLength(),0.001);
    auto entities=draft.at("entities").To<VariantArray>();auto entity=entities.front().To<ObjectMap>();
    entity["y"]=230.0;entities.front()=entity;draft["entities"]=entities;
    invoke(scene,"SavePartSketch",{{"partEntityId",id},{"sketch",draft}});
    EXPECT_NEAR(200.0*224.0,volume(shape(scene,part(scene,id))),0.001);
    const auto saved=part(scene,id)->GetItemProperties().at("tubeDesigner.sideSketch").To<ObjectMap>();
    EXPECT_DOUBLE_EQ(230.0,saved.at("entities").To<VariantArray>().front().To<ObjectMap>().at("y").To<double>());
}

TEST(NestingSideSketchSDO, CurveCircleRemainsAnalyticAfterSaveReopenAndReplacement) {
    Scene scene;
    auto payload=side_sketch_acceptance::request();
    auto draft=side_sketch_acceptance::sketch();
    draft["coordinateSpace"]=std::string("arc-length-axial");
    draft["entities"]=VariantArray{ObjectMap{{"id",std::string("analytic-hole")},
        {"kind",std::string("circle")},{"cx",10.0},{"cy",80.0},{"radius",4.0}}};
    payload["sketch"]=draft;
    const auto id=invoke(scene,"AddNestingSideSketchPart",payload).at("partEntityId").To<std::string>();
    const auto initial=shape(scene,part(scene,id));
    ASSERT_TRUE(BRepCheck_Analyzer(initial).IsValid());
    EXPECT_GE(side_sketch_acceptance::curveEdges(initial,GeomAbs_Circle),2u);
    EXPECT_NEAR(200.0*224.0-std::acos(-1.0)*16.0*2.0,volume(initial),0.005);
    const auto directory=std::filesystem::current_path()/"tmp/side-sketch-curve-persistence";
    std::filesystem::create_directories(directory);
    const auto path=directory/"curved-side-sketch.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="曲线整体切除验收";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());
    EXPECT_GE(side_sketch_acceptance::curveEdges(shape(reopened,part(reopened,id)),GeomAbs_Circle),2u);
    auto entities=draft.at("entities").To<VariantArray>();
    auto circle=entities.front().To<ObjectMap>();circle["cy"]=100.0;circle["radius"]=5.0;
    entities.front()=circle;draft["entities"]=entities;
    for(int repeat=0;repeat<2;++repeat) {
        invoke(reopened,"SavePartSketch",{{"partEntityId",id},
            {"resourceVersion",part(reopened,id)->GetManufacturingGeometryResourceVersion()},{"sketch",draft}});
        const auto edited=shape(reopened,part(reopened,id));
        EXPECT_TRUE(BRepCheck_Analyzer(edited).IsValid());
        EXPECT_GE(side_sketch_acceptance::curveEdges(edited,GeomAbs_Circle),2u);
        EXPECT_NEAR(200.0*224.0-std::acos(-1.0)*25.0*2.0,volume(edited),0.005)
            << "replacing the circle must restore the old hole from the frozen blank";
    }
    file.Save(path,info,reopened.Database(),reopened.Resources());
    Scene finalScene(scene.project,scene.GetSceneID(),false);
    file.Open(path,finalScene.Database(),finalScene.Resources());
    const auto finalShape=shape(finalScene,part(finalScene,id));
    EXPECT_GE(side_sketch_acceptance::curveEdges(finalShape,GeomAbs_Circle),2u);
    const auto saved=part(finalScene,id)->GetItemProperties().at("tubeDesigner.sideSketch").To<ObjectMap>()
        .at("entities").To<VariantArray>().front().To<ObjectMap>();
    EXPECT_EQ(saved.at("kind").To<std::string>(),"circle");
    EXPECT_DOUBLE_EQ(saved.at("radius").To<double>(),5.0);
    payload["sketch"]=draft;
    const auto freshId=invoke(finalScene,"AddNestingSideSketchPart",payload).at("partEntityId").To<std::string>();
    const auto freshShape=shape(finalScene,part(finalScene,freshId));
    EXPECT_NEAR(volume(finalShape),volume(freshShape),0.001);
    EXPECT_NEAR(volume(finalShape),volume(BRepAlgoAPI_Common(finalShape,freshShape).Shape()),0.001);
}

TEST(NestingSideSketchSDO, CurvePreviewCreationAndReleaseRejectTheProductChannel) {
    Scene scene;
    const auto ids=scene.Database().GetEntityIDs();
    const auto undo=scene.Database().GetUndoArray();
    side_sketch_acceptance::ProductScope product;
    Application application;
    iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    auto payload=side_sketch_acceptance::request();
    payload["sketch"]=side_sketch_acceptance::sketch();
    for(const auto& method:{"PreviewNestingSideSketchPart","AddNestingSideSketchPart","ReleaseNestingSideSketchPreview"}) {
        SCOPED_TRACE(method);
        iCAX::Interaction::CInvocation request;request.nCallID=1;
        request.Method=iCAX::Interaction::MakeSDOMethod("TubeDesigner",method);
        const auto json=iCAX::Data::VariantSerializer::Serialize(payload);
        request.Payload.assign(json.begin(),json.end());
        const auto sdo=registry.Find(request.Method.nSDOCode);
        ASSERT_NE(sdo,nullptr);
        std::string rejection;
        try {
            const auto result=sdo->Invoke(request,application,&product,nullptr,nullptr);
            EXPECT_FALSE(result.IsOK());
            rejection=result.strError;
        } catch(const std::exception& error) {
            rejection=error.what();
        }
        EXPECT_NE(rejection.find("请求无效"),std::string::npos)
            << "the native handler must reject the missing scene scope: " << rejection;
    }
    EXPECT_EQ(ids,scene.Database().GetEntityIDs());
    EXPECT_EQ(undo,scene.Database().GetUndoArray());
}

TEST(NestingSideSketchSDO, EllipseArcKerfAndBezierPersistWithoutPolylineFallback) {
    Scene scene;
    auto payload=side_sketch_acceptance::request();
    auto draft=side_sketch_acceptance::sketch();
    draft["coordinateSpace"]=std::string("arc-length-axial");
    draft["entities"]=VariantArray{
        ObjectMap{{"id",std::string("bezier-window")},{"kind",std::string("path")},
            {"closed",true},{"segments",VariantArray{
                ObjectMap{{"kind",std::string("bezier")},{"points",VariantArray{
                    VariantArray{6.0,110.0},VariantArray{6.0,118.0},VariantArray{14.0,118.0},VariantArray{14.0,110.0}}}},
                ObjectMap{{"kind",std::string("line")},{"x1",14.0},{"y1",110.0},
                    {"x2",6.0},{"y2",110.0}}}}},
        ObjectMap{{"id",std::string("ellipse-kerf")},{"kind",std::string("ellipseArc")},
            {"cx",10.0},{"cy",60.0},{"radiusX",6.0},{"radiusY",4.0},{"rotation",0.0},
            {"startAngle",0.0},{"sweep",std::acos(-1.0)},{"closed",false}}};
    payload["sketch"]=draft;
    ObjectMap added;
    try {
        added=invoke(scene,"AddNestingSideSketchPart",payload);
    } catch(const std::exception& error) {
        FAIL() << "complete curve part creation must persist without reconstruction errors: " << error.what();
    }
    const auto id=added.at("partEntityId").To<std::string>();
    const auto inspect=[&](Scene& state,const char* stage) {
        const auto saved=part(state,id);
        const auto model=state.Resources().Get<BRepModel>(saved->GetManufacturingGeometryResourceID(),
            saved->GetManufacturingGeometryResourceVersion());
        ASSERT_NE(model,nullptr);
        const auto polyline2=std::count_if(model->Curves2.begin(),model->Curves2.end(),[](const auto& curve) {
            return std::holds_alternative<iCAX::GeometryData::Polyline2>(curve.Geometry);
        });
        const auto polyline3=std::count_if(model->Curves3.begin(),model->Curves3.end(),[](const auto& curve) {
            return std::holds_alternative<iCAX::GeometryData::Polyline3>(curve.Geometry);
        });
        RecordProperty(std::string(stage)+"Polyline2",static_cast<int>(polyline2));
        RecordProperty(std::string(stage)+"Polyline3",static_cast<int>(polyline3));
        EXPECT_EQ(polyline2,0) << stage << ": native pcurves must persist as smooth curves";
        EXPECT_EQ(polyline3,0) << stage << ": manufacturing edges must not fall back to sampled polylines";
        const auto smooth3=std::count_if(model->Curves3.begin(),model->Curves3.end(),[](const auto& curve) {
            return std::holds_alternative<iCAX::GeometryData::Bezier3>(curve.Geometry)
                ||std::holds_alternative<iCAX::GeometryData::BSpline3>(curve.Geometry)
                ||std::holds_alternative<iCAX::GeometryData::NURBS3>(curve.Geometry);
        });
        EXPECT_GT(smooth3,0) << stage;
        const auto restored=shape(state,saved);
        EXPECT_TRUE(BRepCheck_Analyzer(restored).IsValid()) << stage;
    };
    inspect(scene,"created");
    const auto before=shape(scene,part(scene,id));
    GProp_GProps initialProperties;
    BRepGProp::VolumeProperties(before,initialProperties,1.0e-9);
    EXPECT_GT(200.0*224.0-initialProperties.Mass(),80.0);
    EXPECT_LT(200.0*224.0-initialProperties.Mass(),130.0);
    const auto directory=std::filesystem::current_path()/"tmp/side-sketch-offset-persistence";
    std::filesystem::create_directories(directory);
    const auto path=directory/"ellipse-arc-and-bezier.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="椭圆切缝与贝塞尔持久化验收";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());
    inspect(reopened,"reopened");
    const auto after=shape(reopened,part(reopened,id));
    GProp_GProps reopenedProperties;
    BRepGProp::VolumeProperties(after,reopenedProperties,1.0e-9);
    EXPECT_NEAR(initialProperties.Mass(),reopenedProperties.Mass(),0.001);
    const auto savedDraft=part(reopened,id)->GetItemProperties().at("tubeDesigner.sideSketch").To<ObjectMap>();
    const auto entities=savedDraft.at("entities").To<VariantArray>();
    ASSERT_EQ(entities.size(),2u);
    EXPECT_EQ(entities[0].To<ObjectMap>().at("kind").To<std::string>(),"path");
    EXPECT_EQ(entities[1].To<ObjectMap>().at("kind").To<std::string>(),"ellipseArc");
}

TEST(NestingSideSketchSDO, ProfileJunctionGuidesKeepTheFrozenBlankAcrossHoleAndProjectReopen) {
    Scene scene;
    auto payload=side_sketch_acceptance::request();
    const auto inspect=[](const ObjectMap& result) {
        const auto unfolding=result.at("unfolding").To<ObjectMap>();
        EXPECT_NEAR(unfolding.at("perimeter").To<double>(),120.0,1.0e-7);
        const auto surfaces=unfolding.at("surfaces").To<VariantArray>();
        ASSERT_EQ(surfaces.size(),1u);
        std::vector<double> coordinates;
        for(const auto& value:surfaces.front().To<ObjectMap>().at("wires").To<VariantArray>()) {
            const auto wire=value.To<ObjectMap>();
            if(wire.at("role").To<std::string>()!="profile-junction")continue;
            const auto points=wire.at("points").To<VariantArray>();
            ASSERT_EQ(points.size(),2u);
            const auto first=points.front().To<VariantArray>(),last=points.back().To<VariantArray>();
            ASSERT_EQ(first.size(),2u);ASSERT_EQ(last.size(),2u);
            EXPECT_NEAR(first[0].To<double>(),0.0,1.0e-7);
            EXPECT_NEAR(last[0].To<double>(),200.0,1.0e-7);
            EXPECT_NEAR(first[1].To<double>(),last[1].To<double>(),1.0e-7);
            coordinates.push_back(first[1].To<double>());
        }
        std::sort(coordinates.begin(),coordinates.end());
        ASSERT_EQ(coordinates.size(),3u);
        const std::array expected{40.0,60.0,100.0};
        for(std::size_t index=0;index<coordinates.size();++index)
            EXPECT_NEAR(coordinates[index],expected[index],1.0e-7);
    };
    inspect(invoke(scene,"PreviewNestingSideSketchPart",payload));
    auto draft=side_sketch_acceptance::sketch();
    draft["coordinateSpace"]=std::string("arc-length-axial");
    draft["entities"]=VariantArray{ObjectMap{{"id",std::string("corner-hole")},
        {"kind",std::string("circle")},{"cx",40.0},{"cy",75.0},{"radius",4.0},{"closed",true}}};
    payload["sketch"]=draft;
    const auto added=invoke(scene,"AddNestingSideSketchPart",payload);
    const auto id=added.at("partEntityId").To<std::string>();
    const auto checkPersisted=[&](Scene& state) {
        const auto saved=part(state,id);
        inspect(invoke(state,"PreviewNestingSideSketchPart",{{"partEntityId",id},
            {"resourceVersion",saved->GetManufacturingGeometryResourceVersion()},
            {"previewResourceKey",std::string("junction-guide-reopen")}}));
        const auto stored=saved->GetItemProperties().at("tubeDesigner.sideSketch").To<ObjectMap>();
        const auto entities=stored.at("entities").To<VariantArray>();
        ASSERT_EQ(entities.size(),1u);
        EXPECT_EQ(entities.front().To<ObjectMap>().at("id").To<std::string>(),"corner-hole");
        EXPECT_TRUE(BRepCheck_Analyzer(shape(state,saved)).IsValid());
    };
    checkPersisted(scene);
    const auto directory=std::filesystem::current_path()/"tmp/side-sketch-junction-persistence";
    std::filesystem::create_directories(directory);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project;info.MainSceneID=scene.GetSceneID();info.ProjectName="截面交接参考线验收";
    const auto path=directory/"profile-junctions.ictd";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());
    checkPersisted(reopened);
}
