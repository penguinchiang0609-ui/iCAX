namespace
{
    Map PathEditWait(Scene& scene,const Map& task)
    {const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(180);do{auto result=Invoke(&scene,"CamWorkpiece","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});if(result.at("status").To<Text>()!="Running")return result;std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(std::chrono::steady_clock::now()<until);throw std::runtime_error("Path editing task timed out");}
    std::vector<std::uint8_t> PathEditCadBytes(Scene& scene)
    {iCAX::GeometryData::Persistence::Writer writer;for(const auto& item:Read(scene).at("workpieces").To<Array>()){const auto ref=item.To<Map>().at("geometryRef").To<Map>();const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));if(!geometry)throw std::runtime_error("CAD missing");writer.Write(*geometry);}return writer.Bytes;}
    Array PathEditIds(Scene& scene,const Map& task)
    {const auto terminal=PathEditWait(scene,task);if(terminal.at("status").To<Text>()!="Completed")throw std::runtime_error(iCAX::Data::VariantSerializer::Serialize(terminal));return terminal.at("result").To<Map>().at("intentIds").To<Array>();}
    Map PathEditRow(Scene& scene,const Variant& id){for(const auto& value:Read(scene).at("intentToolpaths").To<Array>()){const auto row=value.To<Map>();if(row.at("id")==id)return row;}throw std::runtime_error("Path row missing");}
    Array PathEditMatrix(double x=0,double y=0,double z=0){return {1.,0.,0.,x,0.,1.,0.,y,0.,0.,1.,z,0.,0.,0.,1.};}
    Array PathEditSplit(Scene& scene,const Variant& id)
    {const auto row=PathEditRow(scene,id);const auto points=row.at("previewPoints").To<Array>();const auto anchor=Invoke(&scene,"CamCut","ResolveSplitPoint",{{"expected",Read(scene).at("expected")},{"intentId",id},{"nearPoint",points.at(points.size()/3)},{"maximumDistanceMm",.2},{"mode",Text("ClosestPoint")},{"frame",Text("Workpiece")}}).at("anchor");return PathEditIds(scene,Invoke(&scene,"CamCut","Split",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"cuts",Array{Map{{"intentId",id},{"points",Array{anchor}}}}}}));}
}
TEST(FiveAxisPathEditing, SplitMergeTransformUndoAndReopenNeverMutateCad)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);const auto original=Read(scene);const auto id=ids.front();
    const auto pieces=PathEditSplit(scene,id);ASSERT_EQ(2u,pieces.size());EXPECT_EQ(original.at("intentToolpaths").To<Array>().size()+1,Read(scene).at("intentToolpaths").To<Array>().size());
    const auto request=EditRequest(scene,pieces,{{"toleranceMm",.1}});const auto merged=PathEditIds(scene,Invoke(&scene,"CamCut","Merge",request));ASSERT_EQ(1u,merged.size());EXPECT_EQ(original.at("intentToolpaths").To<Array>().size(),Read(scene).at("intentToolpaths").To<Array>().size());
    const auto beforeMove=PathEditRow(scene,merged.front());const auto moved=PathEditIds(scene,Invoke(&scene,"CamCut","Transform",EditRequest(scene,merged,{{"matrix",PathEditMatrix(2.,-3.,1.)}})));ASSERT_EQ(merged,moved);const auto after=Read(scene);const auto row=PathEditRow(scene,moved.front());const auto a=beforeMove.at("startPoint").To<Array>(),b=row.at("startPoint").To<Array>();EXPECT_NEAR(a[0].To<double>()+2,b[0].To<double>(),1e-8);EXPECT_NEAR(a[1].To<double>()-3,b[1].To<double>(),1e-8);EXPECT_NEAR(a[2].To<double>()+1,b[2].To<double>(),1e-8);
    EXPECT_EQ(original.at("workpieces"),after.at("workpieces"));EXPECT_EQ(original.at("placements"),after.at("placements"));ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(beforeMove,PathEditRow(scene,merged.front()));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(row,PathEditRow(scene,merged.front()));
}
TEST(FiveAxisPathEditing, ToleranceConnectsCorrectedFragmentsAndFailuresAreAtomic)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);const auto pieces=PathEditSplit(scene,ids.front());ASSERT_EQ(2u,pieces.size());
    PathEditIds(scene,Invoke(&scene,"CamCut","Transform",EditRequest(scene,{pieces.back()},{{"matrix",PathEditMatrix(.04,0,0)}})));
    auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","Merge",EditRequest(scene,pieces,{{"toleranceMm",.001}})),std::exception);EXPECT_EQ(before,Read(scene));
    EXPECT_THROW(Invoke(&scene,"CamCut","Merge",EditRequest(scene,{pieces.front()},{{"toleranceMm",.1}})),std::exception);EXPECT_EQ(before,Read(scene));
    const auto merged=PathEditIds(scene,Invoke(&scene,"CamCut","Merge",EditRequest(scene,pieces,{{"toleranceMm",.1}})));ASSERT_EQ(1u,merged.size());EXPECT_EQ(2u,Read(scene).at("intentToolpaths").To<Array>().size());
    EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    try{Invoke(&scene,"CamProcess","Update",EditRequest(scene,merged,{{"patch",Map{{"kerf",Map{{"widthMm",.2}}}}}}));}catch(const std::exception& e){FAIL()<<"merged process update: "<<e.what();return;}
    const auto generated=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
    if(generated.at("status").To<Text>()!="Completed"){
        const auto diagnostic=Read(scene);
        if(diagnostic.contains("intentGenerationErrors"))std::cerr<<"merged generation diagnostics: "<<iCAX::Data::VariantSerializer::Serialize(diagnostic.at("intentGenerationErrors"))<<std::endl;
    }
    EXPECT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated);
}
TEST(FiveAxisPathEditing, RotationPreservesAxesAndScalingIsRejected)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0.,nullptr,0,true).at("createdIntentIds").To<Array>().front();const auto before=Read(scene),row=PathEditRow(scene,id);const Array rotation{1.,0.,0.,0.,0.,0.,-1.,0.,0.,1.,0.,0.,0.,0.,0.,1.};PathEditIds(scene,Invoke(&scene,"CamCut","Transform",EditRequest(scene,{id},{{"matrix",rotation}})));const auto rotated=PathEditRow(scene,id);const auto a=row.at("startPoint").To<Array>(),b=rotated.at("startPoint").To<Array>();EXPECT_NEAR(a[0].To<double>(),b[0].To<double>(),1e-8);EXPECT_NEAR(-a[2].To<double>(),b[1].To<double>(),1e-8);EXPECT_NEAR(a[1].To<double>(),b[2].To<double>(),1e-8);
    const auto axis=Invoke(&scene,"CamCut","ReadToolAxis",{{"expected",Read(scene).at("expected")},{"intentId",id}}).at("direction").To<Array>();EXPECT_NEAR(0.,axis[0].To<double>(),1e-8);EXPECT_NEAR(-1.,axis[1].To<double>(),1e-8);EXPECT_NEAR(0.,axis[2].To<double>(),1e-8);
    const auto state=Read(scene);const auto undo=scene.Database().GetUndoArray();
    for(const auto& matrix:Array{Array{1.05,0.,0.,0.,0.,1.05,0.,0.,0.,0.,1.05,0.,0.,0.,0.,1.},Array{1.,.1,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.},Array{-1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.}}){
        try{Invoke(&scene,"CamCut","Transform",EditRequest(scene,{id},{{"matrix",matrix}}));FAIL()<<"Non-rigid transform accepted";}catch(const std::invalid_argument& e){EXPECT_NE(Text::npos,Text(e.what()).find("CAM_CUT_RIGID_TRANSFORM_REQUIRED"));}
        EXPECT_EQ(state,Read(scene));EXPECT_EQ(undo,scene.Database().GetUndoArray());
    }
    EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));EXPECT_EQ(before.at("placements"),Read(scene).at("placements"));
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("Contour")}}));AxisNear(AxisRead(scene,id).at("direction"),{0.,-1.,0.});
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,{id},{{"cutType",Text("StandardHole")}}));AxisNear(AxisRead(scene,id).at("direction"),{0.,-1.,0.});
    const auto expected=Read(scene).at("intentToolpaths");iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="刀路几何修正";const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);EXPECT_EQ(expected,Read(reopened).at("intentToolpaths"));std::filesystem::remove(path);
}
TEST(FiveAxisPathEditing, CircleDimensionsKeepProcessAndCad)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0.,nullptr,0,true).at("createdIntentIds").To<Array>().front();const auto cad=PathEditCadBytes(scene);const auto original=Read(scene),before=PathEditRow(scene,id);ASSERT_EQ("Circle",before.at("geometryKind").To<Text>());const auto diameter=before.at("standardHole").To<Map>().at("diameterMm").To<double>();
    const auto task=Invoke(&scene,"CamCut","SetStandardHoleDimensions",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",id},{"dimensions",Map{{"diameterMm",diameter+2}}}});const auto changed=PathEditIds(scene,task);ASSERT_EQ(Array{id},changed);const auto row=PathEditRow(scene,id);EXPECT_DOUBLE_EQ(diameter+2,row.at("standardHole").To<Map>().at("diameterMm").To<double>());EXPECT_EQ(before.at("processRef"),row.at("processRef"));EXPECT_EQ(original.at("workpieces"),Read(scene).at("workpieces"));ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,PathEditRow(scene,id));ASSERT_TRUE(scene.Database().Redo());
    const auto generated=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));EXPECT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated);EXPECT_EQ(cad,PathEditCadBytes(scene));
}
TEST(FiveAxisPathEditing, RectangleAndSlotDimensionsKeepCompensationAndLeads)
{
    const auto rectangle=BRepPrimAPI_MakeBox(gp_Pnt(-16,-3,-1),8,6,12).Shape();
    auto slot=BRepAlgoAPI_Fuse(BRepPrimAPI_MakeBox(gp_Pnt(9,-2,-1),6,4,12).Shape(),BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(9,0,-1),gp::DZ()),2,12).Shape()).Shape();
    slot=BRepAlgoAPI_Fuse(slot,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(15,0,-1),gp::DZ()),2,12).Shape()).Shape();
    const auto cutouts=BRepAlgoAPI_Cut(BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-25,-14,0),50,28,10).Shape(),rectangle).Shape(),slot).Shape();
    const auto shape=BRepAlgoAPI_Cut(cutouts,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,-8,-1),gp::DZ()),1.5,12).Shape()).Shape();
    Scene scene;File source(shape);const auto placement=Imported(scene,source);auto extraction=ExtractRequest(scene,placement);extraction["geometryOnly"]=true;extraction["requestedKinds"]=Array{Text("GeneralContour"),Text("Circle"),Text("Ellipse"),Text("Rectangle"),Text("Slot")};
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal));
    auto state=Read(scene);ASSERT_TRUE(state.at("intentGenerationErrors").To<Array>().empty())<<iCAX::Data::VariantSerializer::Serialize(state.at("intentGenerationErrors"));const auto cad=PathEditCadBytes(scene);std::set<Text> found;std::cerr<<"Standard hole fixture ready\n"<<std::flush;
    for(const auto& item:state.at("intentToolpaths").To<Array>())
    {
        const auto row=item.To<Map>();const auto kind=row.at("geometryKind").To<Text>();if((kind!="Rectangle"&&kind!="Slot")||found.contains(kind))continue;found.insert(kind);const auto id=row.at("id");
        const Map lead{{"kind",Text("Line")},{"placement",Text("ScrapSide")},{"lengthMm",.3},{"angleRad",0.}};
        std::cerr<<kind<<" process update\n"<<std::flush;
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"kerf",Map{{"widthMm",.2}}},{"leadIn",lead},{"leadOut",lead}}}}));
        const auto before=PathEditRow(scene,id),hole=before.at("standardHole").To<Map>();const double width=hole.at("widthMm").To<double>()+1,length=hole.at("lengthMm").To<double>()+2;
        std::cerr<<kind<<" dimension update\n"<<std::flush;
        const auto changed=PathEditIds(scene,Invoke(&scene,"CamCut","SetStandardHoleDimensions",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",id},{"dimensions",Map{{"widthMm",width},{"lengthMm",length}}}}));
        ASSERT_EQ(Array{id},changed);const auto after=PathEditRow(scene,id);EXPECT_DOUBLE_EQ(width,after.at("standardHole").To<Map>().at("widthMm").To<double>());EXPECT_DOUBLE_EQ(length,after.at("standardHole").To<Map>().at("lengthMm").To<double>());EXPECT_EQ(before.at("processRef"),after.at("processRef"));
        ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,PathEditRow(scene,id));ASSERT_TRUE(scene.Database().Redo());
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{id},{{"patch",Map{{"kerf",Map{{"widthMm",.3}}}}}}));
        const auto corrected=PathEditRow(scene,id),descriptor=corrected.at("standardHole").To<Map>();EXPECT_DOUBLE_EQ(width,descriptor.at("widthMm").To<double>());
        const auto center=descriptor.at("center").To<Array>(),x=descriptor.at("xDirection").To<Array>(),n=descriptor.at("direction").To<Array>();const double y[3]{n[1].To<double>()*x[2].To<double>()-n[2].To<double>()*x[1].To<double>(),n[2].To<double>()*x[0].To<double>()-n[0].To<double>()*x[2].To<double>(),n[0].To<double>()*x[1].To<double>()-n[1].To<double>()*x[0].To<double>()};double lo[2]{1e9,1e9},hi[2]{-1e9,-1e9};
        for(const auto& point:corrected.at("previewPoints").To<Array>()){const auto p=point.To<Array>();double u=0,v=0;for(int i=0;i<3;++i){const auto d=p[i].To<double>()-center[i].To<double>();u+=d*x[i].To<double>();v+=d*y[i];}lo[0]=std::min(lo[0],u);hi[0]=std::max(hi[0],u);lo[1]=std::min(lo[1],v);hi[1]=std::max(hi[1],v);}
        EXPECT_NEAR((kind=="Slot"?length:width)-.3,hi[0]-lo[0],.06)<<kind;EXPECT_NEAR((kind=="Slot"?width:length)-.3,hi[1]-lo[1],.06)<<kind;
    }
    EXPECT_EQ((std::set<Text>{"Rectangle","Slot"}),found);std::cerr<<"Generate edited standard holes\n"<<std::flush;const auto generated=PathEditWait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));EXPECT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated);EXPECT_EQ(cad,PathEditCadBytes(scene));
}
TEST(FiveAxisPathEditing, AuthoredAxisSurvivesSuccessiveTransformsAndFurtherEditing)
{
    Scene scene;const auto id=ConfirmWithPolicies(scene,0).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{id});const auto cad=PathEditCadBytes(scene);
    const auto points=PathEditRow(scene,id).at("previewPoints").To<Array>();
    const auto anchor=Invoke(&scene,"CamCut","ResolveToolAxisPoint",{{"expected",Read(scene).at("expected")},{"intentId",id},{"nearPoint",points[points.size()/3]},{"maximumDistanceMm",.2},{"frame",Text("Workpiece")}}).at("anchor").To<Map>();
    const auto point=NewAxisPoint();Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,point,anchor,{0.,.6,.8}));GenerateAxis(scene);
    const Array rotation{1.,0.,0.,0.,0.,0.,-1.,0.,0.,1.,0.,0.,0.,0.,0.,1.};PathEditIds(scene,Invoke(&scene,"CamCut","Transform",EditRequest(scene,{id},{{"matrix",rotation}})));AxisNear(AxisPointRead(scene,id,anchor).at("direction"),{0.,-.8,.6});
    PathEditIds(scene,Invoke(&scene,"CamCut","Transform",EditRequest(scene,{id},{{"matrix",PathEditMatrix(2,3,4)}})));AxisNear(AxisPointRead(scene,id,anchor).at("direction"),{0.,-.8,.6});
    Invoke(&scene,"CamCut","EditToolAxisPoint",AxisPointRequest(scene,id,point,anchor,{.6,0.,.8}));GenerateAxis(scene);AxisNear(AxisPointRead(scene,id,anchor).at("direction"),{.6,0.,.8});
    PathEditIds(scene,Invoke(&scene,"CamCut","Transform",EditRequest(scene,{id},{{"matrix",rotation}})));AxisNear(AxisPointRead(scene,id,anchor).at("direction"),{.6,-.8,0.});GenerateAxis(scene);EXPECT_EQ(cad,PathEditCadBytes(scene));
}
TEST(FiveAxisPathEditing, RecognizedEllipseExposesEditableDiameters)
{
    const gp_Elips ellipse(gp_Ax2(gp_Pnt(0,0,-1),gp::DZ()),7,3);const auto wire=BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(ellipse).Edge()).Wire();const auto opening=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire).Face(),gp_Vec(0,0,12)).Shape();const auto part=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-20,-15,0),40,30,10).Shape(),opening).Shape();const auto shape=BRepAlgoAPI_Cut(part,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-13,0,-1),gp::DZ()),2,12).Shape()).Shape();const auto sceneId=iCAX::Data::GenerateNewUUID();Scene scene(sceneId,sceneId);File source(shape);const auto placement=Imported(scene,source);auto request=ExtractRequest(scene,placement);request["geometryOnly"]=true;request["requestedKinds"]=Array{Text("GeneralContour"),Text("Circle"),Text("Ellipse"),Text("Rectangle"),Text("Slot")};const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",request));Invoke(&scene,"CamCut","Create",CreateRequest(scene,request,terminal));const auto before=Read(scene);Variant id;bool ellipseFound=false;
    ASSERT_TRUE(before.at("intentGenerationErrors").To<Array>().empty())<<iCAX::Data::VariantSerializer::Serialize(before.at("intentGenerationErrors"));
    for(const auto& item:before.at("intentToolpaths").To<Array>()){const auto row=item.To<Map>();if(row.at("geometryKind").To<Text>()=="Ellipse"){ellipseFound=true;id=row.at("id");ASSERT_EQ("Current",row.at("sourceState").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(row);EXPECT_EQ("StandardHole",row.at("cutType").To<Text>());EXPECT_NEAR(14.,row.at("standardHole").To<Map>().at("majorDiameterMm").To<double>(),1e-7);EXPECT_NEAR(6.,row.at("standardHole").To<Map>().at("minorDiameterMm").To<double>(),1e-7);}}
    ASSERT_TRUE(ellipseFound);
    const auto changed=PathEditIds(scene,Invoke(&scene,"CamCut","SetStandardHoleDimensions",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",id},{"dimensions",Map{{"majorDiameterMm",16.},{"minorDiameterMm",8.}}}}));ASSERT_EQ(1u,changed.size());const auto hole=PathEditRow(scene,id).at("standardHole").To<Map>();EXPECT_DOUBLE_EQ(16.,hole.at("majorDiameterMm").To<double>());EXPECT_DOUBLE_EQ(8.,hole.at("minorDiameterMm").To<double>());EXPECT_EQ(before.at("workpieces"),Read(scene).at("workpieces"));
    if(const auto filename=ompl_evaluation::Env(L"ICAX_PATH_EDIT_PROJECT");!filename.empty())
    {iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="刀路编辑 · 椭圆孔";file.Save(filename,info,scene.Database(),scene.Resources());}
}
TEST(FiveAxisPathEditing, ManualHoleDeclarationPreservesContourAndEnablesDimensions)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);const auto pieces=PathEditSplit(scene,ids.front());
    const auto merged=PathEditIds(scene,Invoke(&scene,"CamCut","Merge",EditRequest(scene,pieces,{{"toleranceMm",.1}}))).front();
    const auto before=Read(scene),row=PathEditRow(scene,merged);const auto cad=PathEditCadBytes(scene);const auto history=scene.Database().GetUndoArray();
    ASSERT_TRUE(row.at("standardHole").To<Map>().empty());
    EXPECT_THROW(Invoke(&scene,"CamCut","SetStandardHoleDimensions",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",before.at("expected")},{"intentId",merged},{"dimensions",Map{{"diameterMm",10.}}}}),std::invalid_argument);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray());
    const auto query=Invoke(&scene,"CamCut","QueryStandardHole",{{"expected",before.at("expected")},{"intentId",merged}});
    const auto diameter=query.at("suggestions").To<Map>().at("Circle").To<Map>().at("diameterMm");EXPECT_EQ(before,Read(scene));
    const auto request=EditRequest(scene,{merged},{{"cutType",Text("StandardHole")},{"standardHole",Map{{"kind",Text("Circle")},{"dimensions",Map{{"diameterMm",diameter}}}}}});
    const auto receipt=Invoke(&scene,"CamCut","SetType",request);EXPECT_EQ(receipt,Invoke(&scene,"CamCut","SetType",request));
    const auto declared=PathEditRow(scene,merged);EXPECT_EQ(row.at("previewPoints"),declared.at("previewPoints"));EXPECT_EQ("StandardHole",declared.at("cutType").To<Text>());EXPECT_TRUE(declared.at("standardHole").To<Map>().at("manual").To<bool>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(row,PathEditRow(scene,merged));ASSERT_TRUE(scene.Database().Redo());
    PathEditIds(scene,Invoke(&scene,"CamCut","SetStandardHoleDimensions",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",merged},{"dimensions",Map{{"diameterMm",diameter.To<double>()+2}}}}));
    EXPECT_DOUBLE_EQ(diameter.To<double>()+2,PathEditRow(scene,merged).at("standardHole").To<Map>().at("diameterMm").To<double>());EXPECT_EQ(cad,PathEditCadBytes(scene));
}
TEST(FiveAxisPathEditing, CurvedSurfaceHoleCanBeDeclaredAndResizedWithoutFlattening)
{
    const auto body=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,-10,0),gp::DY(),gp::DX()),20,20).Shape();
    const auto bore=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,-25),gp::DZ()),4,50).Shape();const auto perforated=BRepAlgoAPI_Cut(body,bore).Shape();TopoDS_Face curved;
    for(TopExp_Explorer e(perforated,TopAbs_FACE);e.More();e.Next()){const auto face=TopoDS::Face(e.Current());BRepAdaptor_Surface surface(face);if(surface.GetType()==GeomAbs_Cylinder&&std::abs(surface.Cylinder().Radius()-20)<1e-7){curved=face;break;}}
    ASSERT_FALSE(curved.IsNull());const auto sceneId=iCAX::Data::GenerateNewUUID();Scene scene(sceneId,sceneId);File source(curved);const auto ids=Imported(scene,source);
    const auto state=Read(scene);const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")}});
    const auto ref=sources.at("sourceGeometry").To<Map>();const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
    const auto mainFace=std::max_element(model->Faces.begin(),model->Faces.end(),[](const auto& a,const auto& b){return a.WireIds.size()<b.WireIds.size();});Array selected;
    for(const auto& face:sources.at("faceUses").To<Array>())if(face.To<Map>().at("localId").To<Text>()==std::to_string(mainFace->Id))selected.push_back(face);
    Map extraction{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},{"setupId",ids.at("setupId")},{"placementId",ids.at("placementId")},{"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},{"selection",Map{{"mode",Text("Faces")},{"faceUses",sources.at("faceUses")}}},{"representation",Text("Solid")},{"workpieceFamily",Text("GeneralSolid")},{"side",Map{{"outwardSign",1.}}},{"requestedKinds",Array{Text("GeneralContour"),Text("Circle"),Text("Ellipse")}},{"draftId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"draftRevision",Text("1")},{"draftDigest",Text("curved-hole-test")},{"geometryOnly",true}};
    extraction["selection"]=Map{{"mode",Text("Faces")},{"faceUses",selected}};extraction["representation"]=Text("SkinSurface");
    const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));auto creation=CreateRequest(scene,extraction,terminal);ASSERT_FALSE(creation.at("candidateIds").To<Array>().empty())<<iCAX::Data::VariantSerializer::Serialize(CandidatePage(scene,terminal));
    auto process=creation.at("process").To<Map>(),assignment=process.at("value").To<Map>(),kerf=assignment.at("kerf").To<Map>();kerf["widthMm"]=0.;assignment["kerf"]=kerf;process["value"]=assignment;creation["process"]=process;
    const auto created=Invoke(&scene,"CamCut","Create",creation);
    Variant chosen;for(const auto& item:Read(scene).at("intentToolpaths").To<Array>()){
        const auto row=item.To<Map>();if(row.at("split").To<bool>()||!row.at("standardHole").To<Map>().empty())continue;
        const auto p=row.at("startPoint").To<Array>();if(std::abs(p[2].To<double>())>19){chosen=row.at("id");break;}
    }
    ASSERT_FALSE(std::holds_alternative<std::monostate>(chosen.m_Value));
    const auto before=PathEditRow(scene,chosen);const auto cad=PathEditCadBytes(scene);const auto query=Invoke(&scene,"CamCut","QueryStandardHole",{{"expected",Read(scene).at("expected")},{"intentId",chosen}});const auto hole=query.at("suggestions").To<Map>().at("Ellipse").To<Map>();
    const auto filename=ompl_evaluation::Env(L"ICAX_MANUAL_HOLE_PROJECT");if(!filename.empty()){
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="曲面孔手动指定";file.Save(filename,info,scene.Database(),scene.Resources());
    }
    const double major=hole.at("majorDiameterMm").To<double>(),minor=hole.at("minorDiameterMm").To<double>();
    Invoke(&scene,"CamCut","SetType",EditRequest(scene,{chosen},{{"cutType",Text("StandardHole")},{"standardHole",Map{{"kind",Text("Ellipse")},{"dimensions",Map{{"majorDiameterMm",major},{"minorDiameterMm",minor}}}}}}));
    EXPECT_EQ(before.at("previewPoints"),PathEditRow(scene,chosen).at("previewPoints"));
    PathEditIds(scene,Invoke(&scene,"CamCut","SetStandardHoleDimensions",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",chosen},{"dimensions",Map{{"majorDiameterMm",major+2},{"minorDiameterMm",minor+1}}}}));
    const auto after=PathEditRow(scene,chosen);EXPECT_DOUBLE_EQ(major+2,after.at("standardHole").To<Map>().at("majorDiameterMm").To<double>());
    const auto depth=[&](const Map& row){double lo=1e100,hi=-1e100;const auto n=hole.at("direction").To<Array>();for(const auto& p:row.at("previewPoints").To<Array>()){const auto a=p.To<Array>();double d=0;for(int j=0;j<3;++j)d+=a[j].To<double>()*n[j].To<double>();lo=std::min(lo,d);hi=std::max(hi,d);}return hi-lo;};
    EXPECT_GT(depth(before),.1);EXPECT_NEAR(depth(before),depth(after),.01);EXPECT_EQ(cad,PathEditCadBytes(scene));
    ASSERT_TRUE(scene.Database().Undo());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after,PathEditRow(scene,chosen));
}
