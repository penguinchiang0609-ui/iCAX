TEST(TubeCadDocumentSDO, ImportedStepEditsAcrossViewsAndSurvivesProjectReload)
{
    namespace G = iCAX::GeometryData; namespace T = G::Tube;
    Scene scene;
    const auto imported = invoke(scene,"ImportNestingPart",{{"sourcePath",(std::filesystem::current_path()
        / "samples/tube-geometry/05_double_cavity_blind_hole.step").string()}});
    const auto id = imported.at("partEntityId").To<std::string>();
    const auto originalShape = shape(scene,part(scene,id));
    const auto read = invoke(scene,"GetTubeCadDocument",{{"partEntityId",id}});
    const auto geometry = iCAX::CAD::TubeCadJson::Decode<T::CTubeNeutralGeometry>(read.at("construction"));
    const auto hole = std::find_if(geometry.SolidNodes.begin(),geometry.SolidNodes.end(),[&](const auto& node) {
        if (node.ID == geometry.BaseNodeID) return false;
        const auto* extrusion = std::get_if<T::SExtrudedRegionNode>(&node.Data);
        return extrusion && extrusion->Section.Boundaries.size() == 1
            && extrusion->Section.Boundaries.front().Curves.size() == 1
            && std::holds_alternative<G::Circle2>(extrusion->Section.Boundaries.front().Curves.front().Segment.Curve);
    });
    ASSERT_NE(geometry.SolidNodes.end(),hole);
    auto sketch = invoke(scene,"GetTubeCadDocument",{{"partEntityId",id},{"nodeId",hole->ID}}).at("sketch").To<ObjectMap>();
    auto viewGeometry = iCAX::CAD::TubeCadJson::Decode<decltype(iCAX::CAD::STubeCadSketchView{}.Geometry)>(sketch.at("geometry"));
    auto& circle = std::get<G::Circle2>(std::get<T::CPlanarRegion2>(viewGeometry).Boundaries.front().Curves.front().Segment.Curve);
    circle.Radius += 1;
    sketch["geometry"] = iCAX::CAD::TubeCadJson::Encode(viewGeometry);
    const auto saved = invoke(scene,"ApplyTubeCadDocument",{{"partEntityId",id},
        {"resourceVersion",read.at("resourceVersion")},{"revision",read.at("revision")},{"sketch",sketch}});
    EXPECT_EQ("2",saved.at("revision").To<std::string>());
    EXPECT_LT(volume(shape(scene,part(scene,id))),volume(originalShape)-1);
    EXPECT_THROW(invoke(scene,"ApplyTubeCadDocument",{{"partEntityId",id},
        {"resourceVersion",read.at("resourceVersion")},{"revision",read.at("revision")},
        {"construction",read.at("construction")}}),std::exception);
    const auto document = scene.Resources().Get<T::CTubeCadDocument>(saved.at("documentResourceId").To<std::string>(),
        saved.at("documentResourceVersion").To<unsigned long long>());
    ASSERT_TRUE(document);
    const auto bytes = T::Persistence::Serialize(*document);
    const auto cachedShape = shape(scene,part(scene,id));
    const auto directory = std::filesystem::current_path()/"output/tests/tube-cad-step/sdo";
    std::filesystem::create_directories(directory);
    const auto path = directory/"imported-neutral-document.ictd";
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",.ProductID="icax.tube-designer",
        .CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;
    info.ProjectID=scene.project; info.MainSceneID=scene.GetSceneID(); info.ProjectName="Imported neutral CAD";
    file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.project,scene.GetSceneID(),false);
    file.Open(path,reopened.Database(),reopened.Resources());
    auto reopenedRead = invoke(reopened,"GetTubeCadDocument",{{"partEntityId",id}});
    const auto reloaded = reopened.Resources().Get<T::CTubeCadDocument>(
        reopenedRead.at("documentResourceId").To<std::string>(),
        reopenedRead.at("documentResourceVersion").To<unsigned long long>());
    ASSERT_TRUE(reloaded); EXPECT_EQ(bytes,T::Persistence::Serialize(*reloaded));
    checkSameShape(cachedShape,shape(reopened,part(reopened,id)));
    auto moved = iCAX::CAD::TubeCadJson::Decode<T::CTubeNeutralGeometry>(reopenedRead.at("construction"));
    auto& movedHole = *std::find_if(moved.SolidNodes.begin(),moved.SolidNodes.end(),[&](const auto& node){return node.ID==hole->ID;});
    std::get<T::SExtrudedRegionNode>(movedHole.Data).Frame.Location.X += 5;
    const auto saved3D = invoke(reopened,"ApplyTubeCadDocument",{{"partEntityId",id},
        {"resourceVersion",reopenedRead.at("resourceVersion")},{"revision",reopenedRead.at("revision")},
        {"construction",iCAX::CAD::TubeCadJson::Encode(moved)}});
    EXPECT_EQ("3",saved3D.at("revision").To<std::string>());
    const auto nextView = invoke(reopened,"GetTubeCadDocument",{{"partEntityId",id},{"nodeId",hole->ID}}).at("sketch").To<ObjectMap>();
    const auto frame = iCAX::CAD::TubeCadJson::Decode<std::optional<G::Placement3>>(nextView.at("constructionFrame"));
    ASSERT_TRUE(frame);
    EXPECT_NEAR(frame->Location.X,std::get<T::SExtrudedRegionNode>(hole->Data).Frame.Location.X+5,1e-7);
    const auto nextDocument = reopened.Resources().Get<T::CTubeCadDocument>(saved3D.at("documentResourceId").To<std::string>(),
        saved3D.at("documentResourceVersion").To<unsigned long long>());
    ASSERT_TRUE(nextDocument); EXPECT_EQ(document->Bodies.size(),nextDocument->Bodies.size());
    for (const auto& [bodyId,body] : document->Bodies)
        EXPECT_EQ(G::Persistence::Serialize(body),G::Persistence::Serialize(nextDocument->Bodies.at(bodyId)));
    file.Save(path,info,reopened.Database(),reopened.Resources());
}
