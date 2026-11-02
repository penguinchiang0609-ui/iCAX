namespace
{
    Map WaitBranchGeneration(Scene& scene,const Map& task)
    {
        const auto started=std::chrono::steady_clock::now(),deadline=started+std::chrono::seconds(60);Map result;
        const auto record=[&]
        {
            static std::size_t sequence=0;const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
            ::testing::Test::RecordProperty("branchGenerationMs_"+std::to_string(++sequence),std::to_string(elapsed));
        };
        do
        {
            result=Invoke(&scene,"CamWorkpiece","QueryTask",{{"epoch",task.at("epoch")},{"taskId",task.at("taskId")}});
            if(result.at("status").To<Text>()!="Running"){record();return result;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }while(std::chrono::steady_clock::now()<deadline);
        record();throw std::runtime_error("Native branch generation did not terminate within 60 seconds: "+iCAX::Data::VariantSerializer::Serialize(result));
    }
    iCAX::CAM::Intent::SToolpathValue PublishedBranchValue(Scene& scene,const Variant& id)
    {
        const auto state=Read(scene);Text fingerprint;
        for(const auto& value:state.at("intentToolpaths").To<Array>())
        {const auto row=value.To<Map>();if(row.at("id")==id){if(row.at("sourceState").To<Text>()!="Current")throw std::runtime_error("Native product path is not current");fingerprint=row.at("curveRef").To<Map>().at("revision").To<Text>();}}
        if(fingerprint.empty())throw std::runtime_error("Native product path missing");
        // Inspect only the immutable result whose fingerprint the completed
        // public product read currently exposes; no freshness is guessed here.
        const auto path=ID::from_string(id.To<Text>()).value();
        for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
            if(const auto component=entity->GetComponent("CEditableDerivedComponent"))
            {
                const auto properties=component->GetProperties();if(!properties.contains("Data"))continue;const auto data=properties.at("Data").To<Map>();
                if(!data.contains("outputs")||!data.contains("state")||data.at("state").To<Text>()!="Current")continue;
                for(const auto& value:data.at("outputs").To<Array>())
                {const auto output=value.To<Map>();if(output.at("pathId").To<ID>()==path&&output.at("fingerprint").To<Text>()==fingerprint)return iCAX::CAM::Intent::ToolpathFromData(output.at("value").To<Map>());}
            }
        throw std::runtime_error("Current product fingerprint has no exact published value");
    }
    void ExpectBranchDistance(const iCAX::CAM::Intent::SToolpathValue& value,double distance)
    {
        std::size_t laws=0;for(const auto& segment:value.Geometry.Segments)
            if(const auto* law=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&segment.Curve)){++laws;EXPECT_NEAR(distance,std::abs(law->SignedDistance),1e-12);}
        EXPECT_GT(laws,0u);
    }
    Variant AuthoredNativeSourceGeometry(Scene& scene,const Variant& id)
    {
        const auto path=ID::from_string(id.To<Text>()).value();
        for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
            if(const auto component=entity->GetComponent("CEditableOperationComponent"))
            {const auto definition=component->GetProperties().at("Definition").To<Map>();
             if(definition.at("typeId").To<Text>()=="icax.source"&&definition.at("sourcePathId").To<ID>()==path)
                return definition.at("parameters").To<Map>().at("value").To<Map>().at("geometry");}
        throw std::runtime_error("Authored source definition missing");
    }
    iCAX::CAM::Intent::SPortRef PublishedBranchPort(Scene& scene,const Variant& id)
    {
        Text fingerprint;for(const auto& value:Read(scene).at("intentToolpaths").To<Array>())
        {const auto row=value.To<Map>();if(row.at("id")==id)fingerprint=row.at("curveRef").To<Map>().at("revision").To<Text>();}
        const auto path=ID::from_string(id.To<Text>()).value();
        for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))if(const auto component=entity->GetComponent("CEditableDerivedComponent"))
        {const auto data=component->GetProperties().at("Data").To<Map>();if(!data.contains("outputs")||!data.contains("operationId"))continue;
         for(const auto& item:data.at("outputs").To<Array>()){const auto output=item.To<Map>();if(output.at("pathId").To<ID>()==path&&output.at("fingerprint").To<Text>()==fingerprint)return {data.at("operationId").To<ID>(),output.at("portId").To<ID>()};}}
        throw std::runtime_error("Published product port missing");
    }
    bool NativePolicyFailureIsVisible(const Map& state,const Variant& id)
    {
        if(state.contains("intentGenerationErrors"))
        {const auto& errors=state.at("intentGenerationErrors");if(errors.Is<Array>()&&!errors.To<Array>().empty())return true;if(errors.Is<Map>()&&!errors.To<Map>().empty())return true;}
        for(const auto& item:state.at("intentToolpaths").To<Array>()){const auto row=item.To<Map>();if(row.at("id")==id&&row.at("sourceState").To<Text>()!="Current")return true;}
        return false;
    }
    Map ExportNativeBranchDocument(Scene& scene)
    {
        const auto root=scene.Database().GetMetaEntity();const auto job=root->GetComponent<CFiveAxisJobComponent>();
        iCAX::CAM::Intent::CEditableToolpathDocument document(root->GetRepository(),job->GetIntentDocumentID(),std::make_shared<iCAX::CAM::Intent::CToolpathOperationRegistry>());
        return document.ExportDocument();
    }
    Map NativeBranchInteriorAnchor(Scene& scene,const Variant& id)
    {
        const auto value=PublishedBranchValue(scene,id);const auto port=PublishedBranchPort(scene,id);
        for(const auto& segment:value.Geometry.Segments)if(segment.ConstructionRole.empty())
        {
            const auto range=std::visit([](const auto& curve){if constexpr(requires{curve.Source.Range;})return curve.Source.Range;else return curve.Range;},segment.Curve);
            return iCAX::CAM::Intent::ToData(iCAX::CAM::Intent::SPathAnchor{port,segment.Id,(range.First+range.Last)/2});
        }
        throw std::runtime_error("Native branch has no material interval to split");
    }
}
TEST(FiveAxisIntentWorkflow, ACompensatedSplitChildHasIndependentProcessingAcrossNativeUndoAndProjectReopen)
{
    Text stage="create and compensate";std::filesystem::path saved;
    try
    {
        Scene scene;const auto ids=ConfirmPolicyPair(scene);ASSERT_FALSE(ids.empty());ZeroGenerationPolicies(scene,ids);const auto source=ids.front();
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{source},{{"patch",Map{{"kerf",Map{{"widthMm",.4}}}}}}));
        ASSERT_EQ("Completed",WaitBranchGeneration(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
        Map anchor;for(const auto& value:Generated(scene).at("operations").To<Array>())
        {const auto operation=value.To<Map>();if(operation.at("intentId")!=source)continue;for(const auto& p:operation.at("parts").To<Array>())
            {const auto part=p.To<Map>();if(part.at("role").To<Text>()!="Cut")continue;const auto samples=part.at("sourceSamples").To<Array>();ASSERT_GT(samples.size(),4u);anchor=samples[samples.size()/3].To<Map>().at("anchor").To<Map>();break;}}
        ASSERT_FALSE(anchor.empty());stage="split the exact compensated result";
        const auto split=Wait(scene,Invoke(&scene,"CamCut","Split",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},
            {"cuts",Array{Map{{"intentId",source},{"points",Array{anchor}}}}}}));
        ASSERT_EQ("Completed",split.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(split);
        const auto children=split.at("result").To<Map>().at("intentIds").To<Array>();ASSERT_EQ(2u,children.size());const auto child=children[0],sibling=children[1];
        const auto siblingBefore=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,sibling));const auto history=scene.Database().GetUndoArray().size();
        stage="change only one child's kerf";
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{child},{{"patch",Map{{"kerf",Map{{"widthMm",.8}}}}}}));
        ASSERT_EQ("Completed",WaitBranchGeneration(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
        EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());ExpectBranchDistance(PublishedBranchValue(scene,child),.4);
        EXPECT_EQ(siblingBefore,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,sibling)));AssertUnifiedCutAuthority(scene);
        stage="save and reopen the real product";
        iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
        iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Independent branch processing";
        saved=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(saved,info,scene.Database(),scene.Resources());
        Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(saved,reopened.Database(),reopened.Resources());InitializeScene(reopened);
        ExpectBranchDistance(PublishedBranchValue(reopened,child),.4);const auto siblingReopened=iCAX::CAM::Intent::ToData(PublishedBranchValue(reopened,sibling));
        stage="edit the reopened branch and undo one native step";
        Invoke(&reopened,"CamProcess","Update",EditRequest(reopened,{child},{{"patch",Map{{"kerf",Map{{"widthMm",1.2}}}}}}));
        ASSERT_EQ("Completed",WaitBranchGeneration(reopened,Invoke(&reopened,"CamPlan","Generate",GenerateRequest(reopened))).at("status").To<Text>());
        ExpectBranchDistance(PublishedBranchValue(reopened,child),.6);EXPECT_EQ(siblingReopened,iCAX::CAM::Intent::ToData(PublishedBranchValue(reopened,sibling)));
        ASSERT_TRUE(reopened.Database().Undo());ExpectBranchDistance(PublishedBranchValue(reopened,child),.4);
        EXPECT_EQ(siblingReopened,iCAX::CAM::Intent::ToData(PublishedBranchValue(reopened,sibling)));AssertUnifiedCutAuthority(reopened);
        stage="clear a failed child policy without touching the sibling";
        Invoke(&reopened,"CamProcess","Update",EditRequest(reopened,{child},{{"patch",Map{{"kerf",Map{{"widthMm",20.}}}}}}));
        const auto failed=Read(reopened);ASSERT_TRUE(NativePolicyFailureIsVisible(failed,child));
        const auto authored=Invoke(&reopened,"CamProcess","Read",{{"expected",failed.at("expected")},{"intentIds",Array{child}}}).at("items").To<Array>().front().To<Map>();
        EXPECT_DOUBLE_EQ(20.,authored.at("assignment").To<Map>().at("kerf").To<Map>().at("widthMm").To<double>());
        Invoke(&reopened,"CamProcess","Clear",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",failed.at("expected")},
            {"intentIds",Array{child}},{"categories",Array{Text("Kerf")}}});
        ASSERT_EQ("Completed",WaitBranchGeneration(reopened,Invoke(&reopened,"CamPlan","Generate",GenerateRequest(reopened))).at("status").To<Text>());
        for(const auto& segment:PublishedBranchValue(reopened,child).Geometry.Segments)EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::CurveSegment3>(segment.Curve));
        EXPECT_EQ(siblingReopened,iCAX::CAM::Intent::ToData(PublishedBranchValue(reopened,sibling)));AssertUnifiedCutAuthority(reopened);
        std::filesystem::remove(saved);
    }
    catch(const std::exception& error){if(!saved.empty()){std::error_code ignored;std::filesystem::remove(saved,ignored);}FAIL()<<stage<<": "<<error.what();}
}
TEST(FiveAxisIntentWorkflow, AFailedWholeProcessPolicyCanBeClearedWithoutUndoingTheAuthoringCommand)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);const auto source=ids.front();
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{source},{{"patch",Map{{"kerf",Map{{"widthMm",20.}}}}}}));
    // The public command remains an editable definition even when its worker
    // rejects the geometry. Recovery edits that definition, never a stale cache.
    try{(void)Read(scene);}catch(const std::exception&){}
    Map state=iCAX::FiveAxisCAM::ReadState(scene);
    ASSERT_TRUE(NativePolicyFailureIsVisible(state,source))<<iCAX::Data::VariantSerializer::Serialize(state);
    const auto authored=Invoke(&scene,"CamProcess","Read",{{"expected",state.at("expected")},{"intentIds",Array{source}}}).at("items").To<Array>().front().To<Map>();
    EXPECT_DOUBLE_EQ(20.,authored.at("assignment").To<Map>().at("kerf").To<Map>().at("widthMm").To<double>());
    Invoke(&scene,"CamProcess","Clear",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},
        {"intentIds",Array{source}},{"categories",Array{Text("Kerf")}}});
    ASSERT_EQ("Completed",WaitBranchGeneration(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto value=PublishedBranchValue(scene,source);for(const auto& segment:value.Geometry.Segments)EXPECT_TRUE(std::holds_alternative<iCAX::GeometryData::CurveSegment3>(segment.Curve));
}
TEST(FiveAxisIntentWorkflow, AnInitialSourceWhoseDefaultCompensationFailsCanRecoverWithoutChangingItsExactGeometry)
{
    Scene scene;const auto source=ConfirmWithPolicies(scene,20.,nullptr,0,true).at("createdIntentIds").To<Array>().front();
    const auto geometry=AuthoredNativeSourceGeometry(scene,source);try{(void)Read(scene);}catch(const std::exception&){}
    const auto state=iCAX::FiveAxisCAM::ReadState(scene);
    ASSERT_TRUE(NativePolicyFailureIsVisible(state,source))<<iCAX::Data::VariantSerializer::Serialize(state);
    Invoke(&scene,"CamProcess","Clear",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},
        {"intentIds",Array{source}},{"categories",Array{Text("Kerf")}}});
    ASSERT_EQ("Completed",WaitBranchGeneration(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    EXPECT_EQ(geometry,AuthoredNativeSourceGeometry(scene,source));EXPECT_FALSE(PublishedBranchValue(scene,source).Geometry.Segments.empty());
}
TEST(FiveAxisIntentWorkflow, ARealOffsetConnectorCutReportsNeedsRebindBeforeLocalReprocessing)
{
    Text stage="recognize a square hole";
    try
    {
        Scene scene;File file(BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(-20,-20,0),40,40,10).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-4,-4,-1),8,8,12).Shape()).Shape());
        const auto placement=Imported(scene,file),state=Read(scene);
        const auto sources=Invoke(&scene,"CamCut","QuerySources",{{"expected",state.at("expected")},{"setupId",placement.at("setupId")},{"placementId",placement.at("placementId")}});
        const auto reference=sources.at("sourceGeometry").To<Map>();const auto model=scene.Resources().Get<iCAX::GeometryData::BRepModel>(reference.at("url").To<Text>(),std::stoull(reference.at("version").To<Text>()));
        const auto analysis=scene.Services().Resolve<iCAX::Geometry::IGeometryFeatureAnalysisService>();Map chosen;
        for(const auto& value:sources.at("faceUses").To<Array>()){const auto face=value.To<Map>();for(const auto& boundary:analysis->AnalyzeFace(*model,std::stoull(face.at("localId").To<Text>()),{}).Boundaries)
            if(boundary.RegionRelation=="InnerLoop"&&std::abs(boundary.Center[2]-10)<1e-6)chosen=face;}
        ASSERT_FALSE(chosen.empty());Map extraction{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",state.at("expected")},
            {"setupId",placement.at("setupId")},{"placementId",placement.at("placementId")},{"sourceGeometry",sources.at("sourceGeometry")},{"extractionRuleRef",sources.at("extractionRuleRef")},
            {"selection",Map{{"mode",Text("Faces")},{"faceUses",Array{chosen}}}},{"representation",Text("Solid")},{"workpieceFamily",Text("GeneralSolid")},
            {"side",Map{{"outwardSign",1.}}},{"requestedKinds",Array{Text("Rectangle"),Text("GeneralContour")}},{"draftId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},
            {"draftRevision",Text("1")},{"draftDigest",Text("branch-connector-native")}};
        const auto candidates=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));auto create=CreateRequest(scene,extraction,candidates);
        const auto created=Invoke(&scene,"CamCut","Create",create).at("createdIntentIds").To<Array>();ASSERT_EQ(1u,created.size());const auto source=created.front();
        Invoke(&scene,"CamCut","SetType",EditRequest(scene,{source},{{"cutType",Text("Contour")}}));
        Invoke(&scene,"CamCut","SetRetainedSide",EditRequest(scene,{source},{{"retainedSide",Text("Interior")}}));
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{source},{{"patch",Map{{"kerf",Map{{"widthMm",.4},{"cornerMode",Text("Round")}}}}}}));
        stage="generate a real round offset connector";
        const auto generated=WaitBranchGeneration(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
        std::filesystem::path diagnosticPath;
        if(generated.at("status").To<Text>()!="Completed")
        {
            Array definitions;
            for(const auto& entity:scene.Database().FilterEntities([](const auto&){return true;}))
                if(const auto definition=entity->GetComponent("CEditableOperationComponent"))
                {
                    Map item{{"definition",definition->GetProperties().at("Definition")}};
                    if(const auto derived=entity->GetComponent("CEditableDerivedComponent"))item["derived"]=derived->GetProperties().at("Data");
                    definitions.emplace_back(std::move(item));
                }
            diagnosticPath=std::filesystem::temp_directory_path()/"icax-native-connector-failure.json";
            std::ofstream diagnostic(diagnosticPath);diagnostic<<iCAX::Data::VariantSerializer::Serialize(Map{{"task",generated},{"publishedValue",iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,source))},{"operations",definitions}});
        }
        ASSERT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated)<<" exact diagnostic: "<<diagnosticPath.string();
        const auto exact=PublishedBranchValue(scene,source);const auto port=PublishedBranchPort(scene,source);Map anchor;
        for(const auto& segment:exact.Geometry.Segments)if(segment.ConstructionRole=="OffsetConnector")
        {const auto range=std::visit([](const auto& curve){if constexpr(requires{curve.Source.Range;})return curve.Source.Range;else return curve.Range;},segment.Curve);
         anchor=iCAX::CAM::Intent::ToData(iCAX::CAM::Intent::SPathAnchor{port,segment.Id,(range.First+range.Last)/2});break;}
        ASSERT_FALSE(anchor.empty());stage="split in the native connector and request a local policy";
        const auto split=Wait(scene,Invoke(&scene,"CamCut","Split",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},
            {"cuts",Array{Map{{"intentId",source},{"points",Array{anchor}}}}}}));ASSERT_EQ("Completed",split.at("status").To<Text>());
        const auto children=split.at("result").To<Map>().at("intentIds").To<Array>();ASSERT_EQ(2u,children.size());Variant selected;
        for(const auto& child:children){const auto value=PublishedBranchValue(scene,child);if(value.Geometry.Segments.front().ConstructionRole=="OffsetConnector"||value.Geometry.Segments.back().ConstructionRole=="OffsetConnector"){selected=child;break;}}
        ASSERT_TRUE(selected.Is<Text>());const auto sibling=children.front()==selected?children.back():children.front();
        const auto siblingBefore=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,sibling));
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{selected},{{"patch",Map{{"kerf",Map{{"widthMm",.6}}}}}}));
        const auto failed=Read(scene);ASSERT_TRUE(failed.contains("intentGenerationErrors"));
        const auto diagnostic=iCAX::Data::VariantSerializer::Serialize(failed.at("intentGenerationErrors"));
        EXPECT_NE(Text::npos,diagnostic.find("NEEDS_REBIND_CONNECTOR_BOUNDARY"))<<diagnostic;
        EXPECT_EQ(siblingBefore,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,sibling)));
        const auto authored=Invoke(&scene,"CamProcess","Read",{{"expected",failed.at("expected")},{"intentIds",Array{selected}}}).at("items").To<Array>().front().To<Map>();
        EXPECT_DOUBLE_EQ(.6,authored.at("assignment").To<Map>().at("kerf").To<Map>().at("widthMm").To<double>());
    }
    catch(const std::exception& error){FAIL()<<stage<<": "<<error.what();}
}
TEST(FiveAxisIntentWorkflow, LocalLeadPreviewsMatchFirstExclusiveAndSharedBranchEditsWithoutChangingTheDocument)
{
    Text stage="create a compensated split";
    try
    {
        Scene scene;const auto source=ConfirmWithPolicies(scene,0.).at("createdIntentIds").To<Array>().front();ZeroGenerationPolicies(scene,{source});
        Invoke(&scene,"CamProcess","Update",EditRequest(scene,{source},{{"patch",Map{{"kerf",Map{{"widthMm",.4}}}}}}));
        const auto split=[&](const Variant& id)
        {
            const auto anchor=NativeBranchInteriorAnchor(scene,id);
            return Wait(scene,Invoke(&scene,"CamCut","Split",{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},
                {"cuts",Array{Map{{"intentId",id},{"points",Array{anchor}}}}}}));
        };
        const auto separated=split(source);ASSERT_EQ("Completed",separated.at("status").To<Text>());
        auto children=separated.at("result").To<Map>().at("intentIds").To<Array>();ASSERT_EQ(2u,children.size());Variant selected=children.front();
        const auto previewAndCommit=[&](const Map& patch)
        {
            const auto before=Read(scene);const auto exactBefore=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,selected));
            Map siblings;for(const auto& item:before.at("intentToolpaths").To<Array>()){const auto id=item.To<Map>().at("id");if(id!=selected)siblings[id.To<Text>()]=iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,id));}
            const auto exported=ExportNativeBranchDocument(scene);const auto history=scene.Database().GetUndoArray();
            const auto preview=LeadPreview(scene,{selected},patch);
            EXPECT_EQ(exported,ExportNativeBranchDocument(scene));EXPECT_EQ(history,scene.Database().GetUndoArray());
            EXPECT_EQ(before.at("expected"),Read(scene).at("expected"));EXPECT_EQ(exactBefore,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,selected)));
            for(const auto& [id,value]:siblings)EXPECT_EQ(value,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,Text(id))));
            Invoke(&scene,"CamProcess","Update",EditRequest(scene,{selected},{{"patch",patch}}));
            const auto generated=WaitBranchGeneration(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
            ASSERT_EQ("Completed",generated.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(generated);
            EXPECT_EQ(preview,LeadPreview(scene,{selected}));
            const auto previewItem=preview.at("items").To<Array>().front().To<Map>();Map execution;
            for(const auto& item:Generated(scene).at("operations").To<Array>())if(item.To<Map>().at("intentId")==selected)execution=item.To<Map>();
            ASSERT_FALSE(execution.empty());std::size_t leads=0;
            for(const auto& item:execution.at("parts").To<Array>())
            {
                const auto part=item.To<Map>();const auto role=part.at("role").To<Text>();if(role!="LeadIn"&&role!="LeadOut")continue;++leads;
                const auto expected=previewItem.at("leads").To<Array>();const auto found=std::find_if(expected.begin(),expected.end(),[&](const auto& value){return value.To<Map>().at("role")==part.at("role");});
                ASSERT_NE(expected.end(),found);EXPECT_EQ(found->To<Map>().at("points"),part.at("points"));
            }
            EXPECT_EQ(previewItem.at("leads").To<Array>().size(),leads);
            for(const auto& [id,value]:siblings)EXPECT_EQ(value,iCAX::CAM::Intent::ToData(PublishedBranchValue(scene,Text(id))));
        };
        stage="preview a first local branch";ASSERT_NO_FATAL_FAILURE(previewAndCommit({{"leadIn",Lead("Line",.15)},{"leadOut",Lead("Line",.2)}}));
        stage="preview an existing exclusive branch";ASSERT_NO_FATAL_FAILURE(previewAndCommit({{"leadOut",Lead("Arc",.25)}}));
        stage="split the branch so its producer is shared";const auto shared=split(selected);ASSERT_EQ("Completed",shared.at("status").To<Text>());
        children=shared.at("result").To<Map>().at("intentIds").To<Array>();ASSERT_EQ(2u,children.size());selected=children.back();
        stage="preview one child of a shared branch";ASSERT_NO_FATAL_FAILURE(previewAndCommit({{"leadIn",Lead("Line",.1)},{"leadOut",Lead("Line",.12)}}));
        AssertUnifiedCutAuthority(scene);
    }
    catch(const std::exception& error){FAIL()<<stage<<": "<<error.what();}
}
