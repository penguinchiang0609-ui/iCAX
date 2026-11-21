namespace
{
    Map OrderPathRowsByIdentity(const Variant& rows)
    {Map result;for(const auto& item:rows.To<Array>())result.emplace(item.To<Map>().at("id").To<Text>(),item);return result;}
    Array OrderedIDs(const Map& generated){Array result;for(const auto& value:generated.at("operations").To<Array>())result.push_back(value.To<Map>().at("intentId"));return result;}
    Map OrderRequest(Scene& scene,const Text& mode,const Array& ids={}){
        auto request=GenerateRequest(scene);request["mode"]=mode;
        if(mode=="Manual")request["intentIds"]=ids;return request;
    }
    void PrepareOrderPart(Scene& scene,bool generate=true,const Text& machineType="HeadHead"){
        auto part=RecognitionPart();for(const auto& center:{gp_Pnt(12,0,-1),gp_Pnt(-10,7,-1)})
            part=BRepAlgoAPI_Cut(part,BRepPrimAPI_MakeCylinder(gp_Ax2(center,gp::DZ()),3,12).Shape()).Shape();
        TopoDS_Shape surface;
        for(TopExp_Explorer faces(part,TopAbs_FACE);faces.More();faces.Next()){
            BRepAdaptor_Surface candidate(TopoDS::Face(faces.Current()));
            if(candidate.GetType()==GeomAbs_Plane&&std::abs(candidate.Plane().Location().Z()-10.)<1e-8){surface=faces.Current();break;}}
        ASSERT_FALSE(surface.IsNull());File file(surface);const auto placement=Imported(scene,file,machineType);auto extraction=ExtractRequest(scene,placement);
        extraction["representation"]=Text("SkinSurface");extraction["workpieceFamily"]=Text("SheetSurface");extraction["declaredThicknessMm"]=2.;
        const auto terminal=WaitCut(scene,Invoke(&scene,"CamCut","ExtractCandidates",extraction));
        const auto created=Invoke(&scene,"CamCut","Create",CreateRequest(scene,extraction,terminal));
        ZeroGenerationPolicies(scene,created.at("createdIntentIds").To<Array>());
        if(!generate)return;
        const auto result=Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene)));
        ASSERT_EQ("Completed",result.at("status").To<Text>())<<iCAX::Data::VariantSerializer::Serialize(result);
    }
    void CheckOrderedConnections(const Map& generated){
        const auto ops=generated.at("operations").To<Array>(),rapid=generated.at("transitions").To<Array>();ASSERT_EQ(ops.size()-1,rapid.size());
        for(std::size_t i=0;i<ops.size();++i){EXPECT_EQ(std::to_string(i+1),ops[i].To<Map>().at("order").To<Text>());
            if(i){const auto from=ops[i-1].To<Map>(),to=ops[i].To<Map>(),connection=rapid[i-1].To<Map>();
                EXPECT_EQ(from.at("intentId"),connection.at("from"));EXPECT_EQ(to.at("intentId"),connection.at("to"));
                EXPECT_EQ("Setup",connection.at("frame").To<Text>());
                EXPECT_EQ(from.at("setupEnd"),connection.at("points").To<Array>()[0]);EXPECT_EQ(to.at("setupStart"),connection.at("points").To<Array>()[1]);
                EXPECT_FALSE(connection.at("laser").To<bool>());EXPECT_EQ("Unchecked",connection.at("validation").To<Text>());}}
    }
}
TEST(FiveAxisOrder, ManualPermutationIsAtomicUndoableAndPreservesExactGeometry)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Generated(scene);const auto source=Read(scene).at("intentToolpaths");
    ASSERT_GE(before.at("operations").To<Array>().size(),4u);
    auto ids=OrderedIDs(before);std::reverse(ids.begin(),ids.end());
    const auto history=scene.Database().GetUndoArray().size();const auto sequence=Read(scene).at("expected");
    const auto request=OrderRequest(scene,"Manual",ids),result=Invoke(&scene,"CamPlan","SetOrder",request);
    EXPECT_TRUE(result.at("changed").To<bool>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    const auto after=Generated(scene);EXPECT_EQ(ids,OrderedIDs(after));EXPECT_EQ("Current",after.at("status").To<Text>());
    EXPECT_EQ(before.at("revision"),after.at("revision"));EXPECT_EQ(OrderPathRowsByIdentity(source),OrderPathRowsByIdentity(Read(scene).at("intentToolpaths")));CheckOrderedConnections(after);
    auto prior=before.at("operations").To<Array>();std::reverse(prior.begin(),prior.end());const auto ops=after.at("operations").To<Array>();
    for(std::size_t i=0;i<ops.size();++i){auto a=prior[i].To<Map>(),b=ops[i].To<Map>();a.erase("order");b.erase("order");EXPECT_EQ(a,b);}
    EXPECT_EQ(result,Invoke(&scene,"CamPlan","SetOrder",request));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    const auto unchanged=Read(scene);EXPECT_FALSE(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids)).at("changed").To<bool>());EXPECT_EQ(unchanged,Read(scene));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(before,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(after,Generated(scene));
    EXPECT_NE(sequence,Read(scene).at("expected"));
}
TEST(FiveAxisOrder, DefinitionsCanBeSortedBeforeGenerationAndOrderSurvivesFirstGeneration)
{
    Scene scene;PrepareOrderPart(scene,false);
    const auto definitions=Read(scene).at("intentToolpaths");
    ASSERT_EQ("Missing",Generated(scene).at("status").To<Text>());
    const auto initial=Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>();
    const auto sorted=Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Automatic"));
    const auto automatic=sorted.at("ordering").To<Map>().at("intentIds").To<Array>();ASSERT_GE(automatic.size(),4u);
    const auto cuts=definitions.To<Array>();
    const auto outer=std::find_if(cuts.begin(),cuts.end(),[&](const auto& v){return v.To<Map>().at("id")==automatic.back();});
    ASSERT_NE(outer,cuts.end());EXPECT_EQ("OuterTrim",outer->To<Map>().at("boundaryRole").To<Text>());
    auto manual=automatic;std::reverse(manual.begin(),manual.end());
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",manual));
    EXPECT_EQ(manual,Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>());
    EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    EXPECT_EQ(OrderPathRowsByIdentity(definitions),OrderPathRowsByIdentity(Read(scene).at("intentToolpaths")));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(automatic,Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(initial,Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>());
    ASSERT_TRUE(scene.Database().Redo());ASSERT_TRUE(scene.Database().Redo());
    EXPECT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    EXPECT_EQ(manual,OrderedIDs(Generated(scene)));CheckOrderedConnections(Generated(scene));
    EXPECT_EQ(OrderPathRowsByIdentity(definitions),OrderPathRowsByIdentity(Read(scene).at("intentToolpaths")));
}
TEST(FiveAxisOrder, AutomaticReplacesManualOrderUsesExistingEndpointsAndLeavesOuterLast)
{
    Scene scene;PrepareOrderPart(scene);const auto initial=Generated(scene);auto reversed=OrderedIDs(initial);std::reverse(reversed.begin(),reversed.end());
    ASSERT_EQ("OuterTrim",initial.at("operations").To<Array>().back().To<Map>().at("boundaryRole").To<Text>());
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",reversed));const auto manual=Generated(scene);
    const auto request=OrderRequest(scene,"Automatic");const auto sorted=Invoke(&scene,"CamPlan","SetOrder",request);const auto automatic=Generated(scene);
    auto originalIDs=OrderedIDs(initial),sortedIDs=OrderedIDs(automatic);const auto byID=[](const Variant& a,const Variant& b){return a.To<Text>()<b.To<Text>();};
    std::sort(originalIDs.begin(),originalIDs.end(),byID);std::sort(sortedIDs.begin(),sortedIDs.end(),byID);EXPECT_EQ(originalIDs,sortedIDs);
    EXPECT_EQ("OuterTrim",automatic.at("operations").To<Array>().back().To<Map>().at("boundaryRole").To<Text>());
    EXPECT_EQ("Automatic",automatic.at("ordering").To<Map>().at("mode").To<Text>());CheckOrderedConnections(automatic);
    const auto report=sorted.at("optimization").To<Map>();EXPECT_EQ("ExistingContourEndpoints",report.at("endpointSource").To<Text>());
    EXPECT_EQ("HeightPenalizedStraightLineTravelEstimate",report.at("costScope").To<Text>());EXPECT_EQ("mm",report.at("costUnit").To<Text>());
    EXPECT_EQ("Unchecked",report.at("clearanceValidation").To<Text>());EXPECT_GT(report.at("afterCost").To<double>(),0);
    const auto before=Read(scene);EXPECT_FALSE(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Automatic")).at("changed").To<bool>());EXPECT_EQ(before,Read(scene));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(manual,Generated(scene));ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(automatic,Generated(scene));
}
TEST(FiveAxisOrder, PreGenerationOrderingPreservesDisabledCutsGroupsAndLocks)
{
    Scene scene;PrepareOrderPart(scene,false);
    const auto order=[&]{return Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>();};
    const auto tree=[&](const Text& action,const Array& ids,const Text& group=Text()){
        auto request=GenerateRequest(scene);request["action"]=action;request["intentIds"]=ids;
        if(!group.empty())request["groupId"]=group;return Invoke(&scene,"CamCut","ManageTree",request);
    };
    const auto initial=order();ASSERT_GE(initial.size(),4u);
    tree("Disable",{initial.back()});auto enabled=initial;enabled.pop_back();std::reverse(enabled.begin(),enabled.end());
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",enabled));
    auto expected=enabled;expected.push_back(initial.back());EXPECT_EQ(expected,order());
    EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
    tree("Enable",{initial.back()});EXPECT_EQ(expected,order());
    tree("Group",{expected[1],expected[2]});
    const auto group=Read(scene).at("setups").To<Array>().front().To<Map>().at("cutGroups").To<Array>().front().To<Map>().at("id").To<Text>();
    tree("Lock",{},group);const auto locked=order();
    const auto automatic=Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Automatic"));const auto sorted=order();
    EXPECT_EQ("Automatic",automatic.at("ordering").To<Map>().at("mode").To<Text>());
    EXPECT_EQ(sorted,automatic.at("ordering").To<Map>().at("intentIds").To<Array>());
    for(const auto& id:{expected[1],expected[2]})EXPECT_EQ(std::find(locked.begin(),locked.end(),id)-locked.begin(),std::find(sorted.begin(),sorted.end(),id)-sorted.begin());
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(locked,order());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(sorted,order());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Automatic common ordering";
    const auto saved=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(saved,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(saved,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(sorted,Read(reopened).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>());std::filesystem::remove(saved);
    auto invalid=sorted;std::reverse(invalid.begin(),invalid.end());const auto before=Read(scene);
    EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",invalid)),std::exception);EXPECT_EQ(before,Read(scene));
    EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());
}
TEST(FiveAxisOrder, ManualOrderReordersNestedGroupMembersAndRejectsInterleaving)
{
    Scene scene;PrepareOrderPart(scene,false);
    const auto order=[&]{return Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>();};
    const auto tree=[&](const Text& action,const Array& ids,const Array& groups=Array()){
        auto request=GenerateRequest(scene);request["action"]=action;request["intentIds"]=ids;request["groupIds"]=groups;return Invoke(&scene,"CamCut","ManageTree",request);};
    const auto initial=order();ASSERT_GE(initial.size(),4u);
    const auto inner=tree("Group",{initial[0],initial[2]}).at("groupId");
    tree("Group",{initial[1]},{inner});
    Array desired{initial[3],initial[2],initial[0],initial[1]};desired.insert(desired.end(),initial.begin()+4,initial.end());
    EXPECT_NO_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",desired)));EXPECT_EQ(desired,order());
    auto invalid=desired;std::swap(invalid[2],invalid[3]);const auto before=Read(scene);
    EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",invalid)),std::invalid_argument);EXPECT_EQ(before,Read(scene));
}
TEST(FiveAxisOrder, ManualOrderPersistsThroughRegenerationAndEmbeddedReopen)
{
    Scene scene;PrepareOrderPart(scene);auto ids=OrderedIDs(Generated(scene));std::rotate(ids.begin(),ids.begin()+1,ids.end());
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));EXPECT_EQ("Stale",Generated(scene).at("status").To<Text>());
    EXPECT_EQ("Completed",Wait(scene,Invoke(&scene,"CamPlan","Generate",GenerateRequest(scene))).at("status").To<Text>());
    const auto generated=Generated(scene);EXPECT_EQ(ids,OrderedIDs(generated));CheckOrderedConnections(generated);
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Ordered paths";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(generated,Generated(reopened));std::filesystem::remove(path);
}
TEST(FiveAxisOrder, InvalidStaleAndRejectedRequestsCannotPartiallyChangeOrder)
{
    Scene empty;auto request=GenerateRequest(empty);request["mode"]=Text("Automatic");
    EXPECT_THROW(Invoke(&empty,"CamPlan","SetOrder",request),std::exception);
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene);auto ids=OrderedIDs(Generated(scene));
    auto invalid=ids;invalid.pop_back();EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",invalid)),std::exception);
    invalid=ids;invalid[1]=invalid[0];EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",invalid)),std::exception);
    invalid=ids;invalid[0]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",invalid)),std::exception);
    EXPECT_EQ(before,Read(scene));request=OrderRequest(scene,"Manual",ids);
    Invoke(&scene,"CamCut","Reverse",EditRequest(scene,{ids[0]},{}));EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",request),std::exception);
    EXPECT_NO_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Automatic")));ASSERT_TRUE(scene.Database().Undo());ASSERT_TRUE(scene.Database().Undo());
    struct RejectOrder final:iCAX::Database::IChecker {
        bool AllowAttachByName(const iCAX::Database::IEntity&,const Text&,Text&) override{return true;}
        bool AllowRemoveByName(const iCAX::Database::IEntity&,const Text&,Text&) override{return true;}
        bool AllowModify(const iCAX::Database::CComponentBase& component,const iCAX::Data::PropertySet& fields,Text& error) override
        {
            if(fields.contains("Definition"))
            {
                const auto before=component.GetProperties().at("Definition").To<Map>(),after=fields.at("Definition").To<Map>();
                if(before.at("executionConstraints")!=after.at("executionConstraints")){error="Injected order failure";return false;}
            }
            return true;
        }
    };
    scene.Database().GetMetaRegistry()->RegistCheckerByName(std::make_shared<RejectOrder>(),"CEditablePlanComponent");
    const auto state=Read(scene);const auto history=scene.Database().GetUndoArray().size();std::reverse(ids.begin(),ids.end());
    EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids)),std::exception);
    EXPECT_EQ(state,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisOrder, LockedReleaseBeforeOpeningsRejectsAtomicallyInsteadOfSilentlyBreakingTechnology)
{
    Scene scene;PrepareOrderPart(scene);auto ids=OrderedIDs(Generated(scene));const auto outer=ids.back();std::reverse(ids.begin(),ids.end());
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",ids));
    auto lock=GenerateRequest(scene);lock["intentIds"]=Array{outer};lock["action"]=Text("Lock");Invoke(&scene,"CamCut","ManageTree",lock);
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Automatic")),std::invalid_argument);
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisOrder, PreGenerationCostUsesStraightLineEndpointsWithoutPublishingGeometry)
{
    Scene scene;const auto ids=ConfirmPolicyPair(scene);ZeroGenerationPolicies(scene,ids);
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,ids,{{"patch",Map{{"kerf",Map{{"widthMm",.2}}}}}}));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[0]},{{"patch",Map{{"gapLengthMm",1.},{"leadIn",Lead("Line",.2)},{"leadOut",Lead("Arc",.2)}}}}));
    Invoke(&scene,"CamProcess","Update",EditRequest(scene,{ids[1]},{{"patch",Map{{"overcutLengthMm",2.}}}}));
    const auto definitions=Read(scene).at("intentToolpaths");auto request=OrderRequest(scene,"Automatic");request["preview"]=true;
    const auto task=Wait(scene,Invoke(&scene,"CamPlan","BeginOrder",request));ASSERT_EQ("Completed",task.at("status").To<Text>());
    const auto candidate=task.at("result").To<Map>();const auto order=candidate.at("definitionOrder").To<Array>();ASSERT_EQ(2u,order.size());
    Map endpoints;for(const auto& row:candidate.at("endpoints").To<Array>()){const auto op=row.To<Map>();endpoints[op.at("intentId").To<Text>()]=op;}
    const auto point=[](const Variant& value){const auto p=value.To<Array>();return std::array<double,3>{p[0].To<double>(),p[1].To<double>(),p[2].To<double>()};};
    const auto from=point(endpoints.at(order[0].To<Text>()).To<Map>().at("setupEnd")),to=point(endpoints.at(order[1].To<Text>()).To<Map>().at("setupStart"));
    const auto report=candidate.at("optimization").To<Map>();EXPECT_EQ("mm",report.at("costUnit").To<Text>());const auto distance=std::hypot(from[0]-to[0],from[1]-to[1],from[2]-to[2]);
    EXPECT_NEAR(distance,report.at("afterTravelCost").To<double>(),1e-8);EXPECT_NEAR(distance+3*std::abs(from[2]-to[2]),report.at("afterCost").To<double>(),1e-8);
    auto apply=OrderRequest(scene,"Automatic");apply["previewTaskId"]=task.at("taskId");Invoke(&scene,"CamPlan","SetOrder",apply);
    EXPECT_EQ("Missing",Generated(scene).at("status").To<Text>());EXPECT_EQ(OrderPathRowsByIdentity(definitions),OrderPathRowsByIdentity(Read(scene).at("intentToolpaths")));
}
TEST(FiveAxisCutNames, RenameIsMetadataOnlyIdempotentUndoableAndStableThroughSorting)
{
    Scene scene;PrepareOrderPart(scene);const auto ids=OrderedIDs(Generated(scene));const auto id=ids.front();const auto geometry=Generated(scene);
    const auto request=[&](const Text& name){return Map{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",Read(scene).at("expected")},{"intentId",id},{"name",name}};};
    const auto rename=request("Vent A");const auto result=Invoke(&scene,"CamCut","Rename",rename);EXPECT_TRUE(result.at("changed").To<bool>());
    EXPECT_EQ(geometry,Generated(scene));EXPECT_EQ(result,Invoke(&scene,"CamCut","Rename",rename));
    EXPECT_EQ(Read(scene).at("expected"),result.at("statePatch").To<Map>().at("expected"));
    const auto name=[&]{for(const auto& v:Read(scene).at("intentToolpaths").To<Array>()){const auto c=v.To<Map>();if(c.at("id")==id)return c.at("name").To<Text>();}return Text();};
    EXPECT_EQ("Vent A",name());ASSERT_TRUE(scene.Database().Undo());EXPECT_NE("Vent A",name());ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ("Vent A",name());
    auto reversed=ids;std::reverse(reversed.begin(),reversed.end());Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Manual",reversed));EXPECT_EQ("Vent A",name());
    Invoke(&scene,"CamPlan","SetOrder",OrderRequest(scene,"Automatic"));EXPECT_EQ("Vent A",name());
    for(const auto* invalid:{"","   ","a\nb"}){const auto before=Read(scene);EXPECT_THROW(Invoke(&scene,"CamCut","Rename",request(invalid)),std::invalid_argument);EXPECT_EQ(before,Read(scene));}
    auto lock=GenerateRequest(scene);lock["action"]=Text("Lock");lock["intentIds"]=Array{id};Invoke(&scene,"CamCut","ManageTree",lock);
    EXPECT_THROW(Invoke(&scene,"CamCut","Rename",request("Blocked")),std::invalid_argument);EXPECT_EQ("Vent A",name());
}
TEST(FiveAxisCutNames, TreeReceiptContainsAuthoritativeMetadataWithoutPathPoints)
{
    Scene scene;PrepareOrderPart(scene);const auto original=Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder");
    for(const auto* action:{"Disable","Lock","Unlock","Enable"}){
        auto request=GenerateRequest(scene);request["action"]=Text(action);request["intentIds"]=Array{original.To<Array>().front()};
        const auto result=Invoke(&scene,"CamCut","ManageTree",request),state=Read(scene),patch=result.at("statePatch").To<Map>();
        EXPECT_EQ(state.at("expected"),patch.at("expected"));EXPECT_EQ(state.at("inputRevision"),patch.at("inputRevision"));
        const auto setup=state.at("setups").To<Array>().front().To<Map>(),partial=patch.at("setup").To<Map>();
        EXPECT_EQ(original,setup.at("definitionOrder"));
        for(const auto* key:{"id","inputRevision","planValidity","cutGroups","definitionOrder"})EXPECT_EQ(setup.at(key),partial.at(key))<<key;
        for(const auto& [key,value]:partial.at("generatedGeometry").To<Map>())EXPECT_EQ(setup.at("generatedGeometry").To<Map>().at(key),value)<<key;
        for(const auto& v:patch.at("intentToolpaths").To<Array>()){
            const auto cut=v.To<Map>();bool found=false;
            for(const auto& item:state.at("intentToolpaths").To<Array>()){const auto full=item.To<Map>();if(full.at("id")!=cut.at("id"))continue;found=true;
                for(const auto& [key,value]:cut)EXPECT_EQ(full.at(key),value)<<key;
            }EXPECT_TRUE(found);EXPECT_FALSE(cut.contains("previewPoints"));
        }
        EXPECT_FALSE(partial.at("generatedGeometry").To<Map>().contains("operations"));
    }
}
TEST(FiveAxisOrder, AutomaticStrategyIsValidatedUndoableAndPersistsInThePlan)
{
    Scene scene;PrepareOrderPart(scene,false);
    auto request=OrderRequest(scene,"Automatic");request["strategy"]=Text("MarkAndHoleTrim");
    const auto before=Read(scene),result=Invoke(&scene,"CamPlan","SetOrder",request);
    EXPECT_EQ("MarkAndHoleTrim",result.at("ordering").To<Map>().at("strategy").To<Text>());
    EXPECT_EQ("MarkAndHoleTrim",result.at("optimization").To<Map>().at("strategy").To<Text>());
    const auto combined=Read(scene);EXPECT_EQ(before.at("workpieces"),combined.at("workpieces"));EXPECT_EQ(before.at("intentToolpaths"),combined.at("intentToolpaths"));
    const auto policy=[&]{return Read(scene).at("setups").To<Array>().front().To<Map>().at("generatedGeometry").To<Map>().at("ordering").To<Map>();};
    EXPECT_EQ("MarkAndHoleTrim",policy().at("strategy").To<Text>());
    request=OrderRequest(scene,"Automatic");request["strategy"]=Text("MarkAndHoleTrim");EXPECT_FALSE(Invoke(&scene,"CamPlan","SetOrder",request).at("changed").To<bool>());
    request=OrderRequest(scene,"Automatic");request["strategy"]=Text("MarkHoleTrim");Invoke(&scene,"CamPlan","SetOrder",request);
    EXPECT_EQ("MarkHoleTrim",policy().at("strategy").To<Text>());ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ("MarkAndHoleTrim",policy().at("strategy").To<Text>());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ("MarkHoleTrim",policy().at("strategy").To<Text>());
    request=OrderRequest(scene,"Automatic");request["strategy"]=Text("invalid");const auto stable=Read(scene);
    EXPECT_THROW(Invoke(&scene,"CamPlan","SetOrder",request),std::invalid_argument);EXPECT_EQ(stable,Read(scene));
    EXPECT_THROW(Invoke(&scene,"CamPlan","BeginOrder",request),std::invalid_argument);EXPECT_EQ(stable,Read(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Order strategy";
    const auto saved=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(saved,info,scene.Database(),scene.Resources());
    Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(saved,reopened.Database(),reopened.Resources());InitializeScene(reopened);
    EXPECT_EQ(policy(),Read(reopened).at("setups").To<Array>().front().To<Map>().at("generatedGeometry").To<Map>().at("ordering").To<Map>());std::filesystem::remove(saved);
}
TEST(FiveAxisCutNames, DeleteReceiptRemovesRowsWithoutStartingGeometryGeneration)
{
    Scene scene;PrepareOrderPart(scene,false);const auto before=Read(scene);const auto rows=before.at("intentToolpaths").To<Array>();ASSERT_GE(rows.size(),2u);
    const auto id=rows.front().To<Map>().at("id");auto request=GenerateRequest(scene);request["action"]=Text("Delete");request["intentIds"]=Array{id};
    const auto result=Invoke(&scene,"CamCut","ManageTree",request),patch=result.at("statePatch").To<Map>();
    const auto state=iCAX::FiveAxisCAM::ReadState(scene);
    EXPECT_TRUE(state.at("intentGenerationTasks").To<Array>().empty());
    EXPECT_EQ(Array{id},patch.at("removedIntentIds").To<Array>());
    EXPECT_EQ(rows.size()-1,patch.at("intentToolpaths").To<Array>().size());
    EXPECT_EQ(state.at("expected"),patch.at("expected"));
    for(const auto& row:patch.at("intentToolpaths").To<Array>())EXPECT_NE(id,row.To<Map>().at("id"));
    for(const auto* key:{"workpieces","placements","fixtures","fixtureUnits"})EXPECT_EQ(before.at(key),state.at(key));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(rows,Read(scene).at("intentToolpaths").To<Array>());
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(rows.size()-1,Read(scene).at("intentToolpaths").To<Array>().size());
}

TEST(FiveAxisOrder, RowMoveQueriesOrderSeparatelyAndPreservesGeometryAndHistory)
{
    Scene scene;PrepareOrderPart(scene);const auto before=Read(scene),geometry=Generated(scene);
    const auto original=before.at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>();ASSERT_GE(original.size(),3u);
    auto request=GenerateRequest(scene);request["action"]=Text("Move");request["intentIds"]=Array{original[2]};request["groupIds"]=Array{};
    request["target"]=Map{{"kind",Text("Cut")},{"id",original[1]},{"position",Text("Before")}};
    const auto history=scene.Database().GetUndoArray().size();const auto receipt=Invoke(&scene,"CamCut","ManageTree",request);
    EXPECT_TRUE(receipt.at("orderOnly").To<bool>());EXPECT_FALSE(receipt.contains("statePatch"));EXPECT_FALSE(receipt.contains("order"));
    const Map query{{"setupId",request.at("setupId")},{"expected",receipt.at("expected")}};
    const auto projected=Invoke(&scene,"CamPlan","QueryOrder",query);auto expected=original;std::swap(expected[1],expected[2]);
    EXPECT_EQ(expected,projected.at("definitionOrder").To<Array>());EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    EXPECT_EQ(receipt,Invoke(&scene,"CamCut","ManageTree",request));EXPECT_EQ(history+1,scene.Database().GetUndoArray().size());
    const auto after=Read(scene);for(const auto* key:{"workpieces","placements","fixtures","intentToolpaths"})EXPECT_EQ(before.at(key),after.at(key));
    EXPECT_EQ(geometry.at("revision"),Generated(scene).at("revision"));
    ASSERT_TRUE(scene.Database().Undo());EXPECT_EQ(original,Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>());
    EXPECT_THROW(Invoke(&scene,"CamPlan","QueryOrder",query),std::exception);
    ASSERT_TRUE(scene.Database().Redo());EXPECT_EQ(expected,Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>());
}
TEST(FiveAxisOrder, RowMoveRefreshesGroupMembershipAndLocksAcrossHistory)
{
    Scene scene;PrepareOrderPart(scene,false);const auto initial=Read(scene).at("setups").To<Array>().front().To<Map>().at("definitionOrder").To<Array>();ASSERT_GE(initial.size(),4u);
    const auto move=[&](const Variant& source,const Variant& target){auto request=GenerateRequest(scene);request["action"]=Text("Move");request["intentIds"]=Array{source};request["target"]=Map{{"kind",Text("Cut")},{"id",target},{"position",Text("Before")}};return Invoke(&scene,"CamCut","ManageTree",request);};
    EXPECT_TRUE(move(initial[2],initial[1]).at("orderOnly").To<bool>());
    auto group=GenerateRequest(scene);group["action"]=Text("Group");group["intentIds"]=Array{initial[2],initial[1]};const auto groupId=Invoke(&scene,"CamCut","ManageTree",group).at("groupId");
    EXPECT_TRUE(move(initial[1],initial[2]).at("orderOnly").To<bool>());
    const auto lock=[&](const Text& action){auto request=GenerateRequest(scene);request["action"]=action;request["intentIds"]=Array{};request["groupIds"]=Array{groupId};return Invoke(&scene,"CamCut","ManageTree",request);};
    lock("Lock");EXPECT_THROW(move(initial[2],initial[1]),std::invalid_argument);
    ASSERT_TRUE(scene.Database().Undo());ASSERT_TRUE(scene.Database().Redo());EXPECT_THROW(move(initial[2],initial[1]),std::invalid_argument);
    lock("Unlock");EXPECT_TRUE(move(initial[2],initial[1]).at("orderOnly").To<bool>());
    ASSERT_TRUE(scene.Database().Undo());ASSERT_TRUE(scene.Database().Redo());
    const auto state=Read(scene),setup=state.at("setups").To<Array>().front().To<Map>();auto expected=initial;std::swap(expected[1],expected[2]);
    const auto queried=Invoke(&scene,"CamPlan","QueryOrder",{{"setupId",state.at("activeSetupId")},{"expected",state.at("expected")}});
    EXPECT_EQ(expected,queried.at("definitionOrder").To<Array>());EXPECT_EQ(1u,setup.at("cutGroups").To<Array>().size());
}
