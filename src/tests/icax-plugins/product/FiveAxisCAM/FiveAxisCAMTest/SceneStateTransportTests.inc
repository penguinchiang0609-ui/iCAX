namespace state_transport_tests
{
    std::uint32_t U32(std::span<const std::uint8_t> b,std::size_t at){if(at+4>b.size())throw std::runtime_error("Invalid sample buffer");std::uint32_t v;std::memcpy(&v,b.data()+at,4);return v;}
    std::size_t Field(std::span<const std::uint8_t> b,std::size_t table,unsigned index){std::int32_t displacement;std::memcpy(&displacement,b.data()+table,4);const auto vt=table-displacement;std::uint16_t size,offset=0;std::memcpy(&size,b.data()+vt,2);if(4+index*2+2<=size)std::memcpy(&offset,b.data()+vt+4+index*2,2);return offset?table+offset:0;}
    std::size_t Target(std::span<const std::uint8_t> b,std::size_t field){return field+U32(b,field);}
    Text StringAt(std::span<const std::uint8_t> b,std::size_t field){const auto at=Target(b,field);const auto size=U32(b,at);return {reinterpret_cast<const char*>(b.data()+at+4),size};}
    Map ActiveSetupSnapshot(const Map& state){for(const auto& v:state.at("setups").To<Array>())if(v.To<Map>().at("id")==state.at("activeSetupId"))return v.To<Map>();throw std::runtime_error("Setup missing");}
    std::size_t CheckSamples(Scene& scene,const Map& state,const Map& authoritative)
    {
        const auto g=ActiveSetupSnapshot(state).at("generatedGeometry").To<Map>(),full=ActiveSetupSnapshot(authoritative).at("generatedGeometry").To<Map>();
        const auto ref=g.at("sampleGeometryRef").To<Map>();const auto data=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));
        EXPECT_TRUE(data);if(!data)return 0;const auto bytes=data->Bytes();EXPECT_EQ("ICPS",Text(reinterpret_cast<const char*>(bytes.data()+4),4));
        const auto root=U32(bytes,0);const auto entries=Target(bytes,Field(bytes,root,1));
        std::map<Text,std::size_t> offsets;for(std::size_t i=0;i<U32(bytes,entries);++i){const auto table=Target(bytes,entries+4+i*4);offsets.emplace(StringAt(bytes,Field(bytes,table,0)),table);}
        std::size_t checked=0;
        for(const auto& value:authoritative.at("intentToolpaths").To<Array>()){const auto cut=value.To<Map>();const auto table=offsets.at("definition:"+cut.at("id").To<Text>()),parts=Target(bytes,Field(bytes,table,1));EXPECT_EQ(1u,U32(bytes,parts));
            const auto part=Target(bytes,parts+4),vector=Target(bytes,Field(bytes,part,1));const auto points=cut.at("previewPoints").To<Array>();EXPECT_EQ("Definition",StringAt(bytes,Field(bytes,part,0)));EXPECT_EQ(points.size()*3,U32(bytes,vector));
            for(std::size_t j=0;j<points.size();++j)for(std::size_t k=0;k<3;++k){double actual;std::memcpy(&actual,bytes.data()+vector+4+(j*3+k)*8,8);EXPECT_DOUBLE_EQ(points[j].To<Array>()[k].To<double>(),actual);++checked;}
        }
        for(const auto& value:full.at("operations").To<Array>()){const auto op=value.To<Map>();const auto table=offsets.at(op.at("intentId").To<Text>()),parts=Target(bytes,Field(bytes,table,1));const auto originals=op.at("parts").To<Array>();EXPECT_EQ(originals.size(),U32(bytes,parts));
            for(std::size_t i=0;i<originals.size();++i){const auto original=originals[i].To<Map>();const auto part=Target(bytes,parts+4+i*4);EXPECT_EQ(original.at("role").To<Text>(),StringAt(bytes,Field(bytes,part,0)));
                for(const auto [key,index]:{std::pair{"points",1u},std::pair{"directions",2u}}){const auto vector=Target(bytes,Field(bytes,part,index));const auto triples=original.at(key).To<Array>();EXPECT_EQ(triples.size()*3,U32(bytes,vector));
                    for(std::size_t j=0;j<triples.size();++j)for(std::size_t k=0;k<3;++k){double actual;std::memcpy(&actual,bytes.data()+vector+4+(j*3+k)*8,8);EXPECT_DOUBLE_EQ(triples[j].To<Array>()[k].To<double>(),actual);++checked;}}
            }
        }
        for(const auto& value:g.at("operations").To<Array>())for(const auto& p:value.To<Map>().at("parts").To<Array>()){const auto part=p.To<Map>();EXPECT_FALSE(part.contains("points"));EXPECT_FALSE(part.contains("directions"));EXPECT_FALSE(part.contains("sourceSamples"));EXPECT_TRUE(part.contains("pointCount"));}
        return checked;
    }
}
TEST(FiveAxisStateTransport, SamplesUseVersionedResourcesAndMetadataTraversesTheDefaultSDOQueue)
{
    using namespace state_transport_tests;Scene scene;PrepareOrderPart(scene);
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();const auto wire=Invoke(&scene,"CamScene","ReadState");
    EXPECT_LT(iCAX::Data::VariantSerializer::Serialize(wire).size(),16u*1024*1024);EXPECT_GT(CheckSamples(scene,wire,before),0u);
    const auto again=Invoke(&scene,"CamScene","ReadState");EXPECT_EQ(ActiveSetupSnapshot(wire).at("generatedGeometry"),ActiveSetupSnapshot(again).at("generatedGeometry"));
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false))).at("status").To<Text>());
    const auto generated=Invoke(&scene,"CamScene","ReadState");EXPECT_GT(CheckSamples(scene,generated,Read(scene)),0u);
    EXPECT_NE(ActiveSetupSnapshot(wire).at("generatedGeometry").To<Map>().at("sampleGeometryRef"),ActiveSetupSnapshot(generated).at("generatedGeometry").To<Map>().at("sampleGeometryRef"));
    for(const auto& t:ActiveSetupSnapshot(generated).at("generatedGeometry").To<Map>().at("transitions").To<Array>()){EXPECT_EQ("Generated",t.To<Map>().at("geometryStatus").To<Text>());EXPECT_FALSE(t.To<Map>().contains("points"));EXPECT_EQ("4",t.To<Map>().at("pointCount").To<Text>());}
}
TEST(FiveAxisStateTransport, UngeneratedCutDefinitionsAlsoUseTheResourceChannel)
{
    Scene scene;PrepareOrderPart(scene,false);const auto before=Read(scene),wire=Invoke(&scene,"CamScene","ReadState");
    const auto geometry=state_transport_tests::ActiveSetupSnapshot(wire).at("generatedGeometry").To<Map>();EXPECT_EQ("Missing",geometry.at("status").To<Text>());EXPECT_TRUE(geometry.contains("sampleGeometryRef"));
    ASSERT_FALSE(wire.at("intentToolpaths").To<Array>().empty());for(const auto& value:wire.at("intentToolpaths").To<Array>()){const auto cut=value.To<Map>();EXPECT_FALSE(cut.contains("previewPoints"));EXPECT_GT(std::stoul(cut.at("previewPointCount").To<Text>()),0u);}
    EXPECT_EQ(before,Read(scene));
}
TEST(FiveAxisStateTransport, LegacyFailedRapidsAcquireEditableSamplesWithoutReplanningAndSurviveReopen)
{
    using namespace state_transport_tests;Scene scene;PrepareOrderPart(scene);
    ASSERT_EQ("Completed",Wait(scene,Invoke(&scene,"CamRapid","Generate",RapidRequest(scene,false))).at("status").To<Text>());
    const auto original=Generated(scene);const auto transitions=original.at("transitions").To<Array>();ASSERT_GE(transitions.size(),2u);
    const auto setupID=ID::from_string(Read(scene).at("activeSetupId").To<Text>()).value();const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    auto legacy=setup->GetGeneratedRapidPaths();auto entries=legacy.at("entries").To<Map>();
    const std::array<Text,2> reasons{"RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS","RAPID_ENDPOINT_BLOCKED"};
    for(size_t i=0;i<2;++i){const auto transition=transitions[i].To<Map>();const auto key=transition.at("inputKey").To<Text>();auto record=entries.at(key).To<Map>();auto result=record.at("result").To<Map>();
        for(const auto* field:{"points","directions","surfacePoints","heightsMm","construction","shapeHandle","sourceStart","sourceEnd","editingOnly"})result.erase(field);
        result["geometryStatus"]=Text("Failed");result["validation"]=Text("Unchecked");result["reason"]=reasons[i];record["result"]=result;entries[key]=record;
    }
    legacy["entries"]=entries;auto& mutation=scene.Database().BeginTransaction("Prepare legacy failures without samples");mutation.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"GeneratedRapidPaths",legacy}});ASSERT_TRUE(scene.Database().CommitTransaction(mutation));
    auto& disable=scene.Database().BeginTransaction("Read disabled setup with legacy rapid failures");disable.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"Enabled",false}});ASSERT_TRUE(scene.Database().CommitTransaction(disable));
    EXPECT_NO_THROW(Invoke(&scene,"CamScene","ReadState"));EXPECT_EQ(legacy,setup->GetGeneratedRapidPaths());
    auto& enable=scene.Database().BeginTransaction("Restore setup for rapid editing");enable.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"Enabled",true}});ASSERT_TRUE(scene.Database().CommitTransaction(enable));
    const auto history=scene.Database().GetUndoArray().size();const auto current=Generated(scene);EXPECT_EQ(original.at("operations"),current.at("operations"));EXPECT_EQ(std::to_string(transitions.size()-2),current.at("rapidGeneratedCount").To<Text>());EXPECT_EQ(current.at("rapidGeneratedCount"),current.at("rapidClearCount"));
    const auto inspect=[&](Scene& target){
        const auto full=Generated(target),wire=Invoke(&target,"CamScene","ReadState");const auto projected=ActiveSetupSnapshot(wire).at("generatedGeometry").To<Map>();const auto beforeRead=Read(target);
        const auto ref=projected.at("sampleGeometryRef").To<Map>();const auto body=target.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(body);
        const auto bytes=body->Bytes();const auto resourceEntries=Target(bytes,Field(bytes,U32(bytes,0),1));std::set<Text> encoded;
        for(size_t i=0;i<U32(bytes,resourceEntries);++i){const auto table=Target(bytes,resourceEntries+4+i*4);encoded.insert(StringAt(bytes,Field(bytes,table,0)));}
        for(size_t i=0;i<2;++i){const auto path=full.at("transitions").To<Array>()[i].To<Map>(),metadata=projected.at("transitions").To<Array>()[i].To<Map>();
            EXPECT_EQ("Failed",path.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",path.at("validation").To<Text>());EXPECT_EQ(reasons[i],path.at("reason").To<Text>());EXPECT_TRUE(path.at("editingOnly").To<bool>());EXPECT_EQ("Setup",path.at("frame").To<Text>());EXPECT_EQ("ToolTip",path.at("referencePoint").To<Text>());
            ASSERT_GE(path.at("points").To<Array>().size(),2u);EXPECT_EQ(path.at("points").To<Array>().size(),path.at("directions").To<Array>().size());EXPECT_EQ(std::to_string(path.at("points").To<Array>().size()),metadata.at("pointCount").To<Text>());EXPECT_FALSE(metadata.contains("points"));EXPECT_TRUE(metadata.at("editingOnly").To<bool>());
            EXPECT_TRUE(encoded.contains("rapid:"+path.at("from").To<Text>()+"/"+path.at("to").To<Text>()));
            auto read=RapidRequest(target);read.erase("commandId");read["from"]=path.at("from");read["to"]=path.at("to");read["policy"]=path.at("policy");const auto editor=Invoke(&target,"CamRapid","ReadEditPath",read);
            EXPECT_EQ(path.at("points"),editor.at("points"));EXPECT_EQ(path.at("directions"),editor.at("directions"));EXPECT_EQ(path.at("sourceStart"),editor.at("sourceStart"));EXPECT_EQ(path.at("sourceEnd"),editor.at("sourceEnd"));EXPECT_TRUE(editor.at("endpointLocked").To<bool>());EXPECT_EQ("Failed",editor.at("geometryStatus").To<Text>());EXPECT_TRUE(editor.at("editingOnly").To<bool>());
        }
        EXPECT_EQ(beforeRead,Read(target));EXPECT_EQ(original.at("operations"),full.at("operations"));
    };
    inspect(scene);EXPECT_EQ(legacy,setup->GetGeneratedRapidPaths());EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ(current,Generated(scene));
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});iCAX::ProjectFile::CProjectDocumentInfo info;info.ProjectID=scene.Project;info.MainSceneID=scene.GetSceneID();info.ProjectName="Legacy failed editable rapids";
    const auto path=std::filesystem::temp_directory_path()/(iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())+".i5cam");file.Save(path,info,scene.Database(),scene.Resources());Scene reopened(scene.Project,scene.GetSceneID(),false);file.Open(path,reopened.Database(),reopened.Resources());InitializeScene(reopened);inspect(reopened);EXPECT_EQ(current,Generated(reopened));std::filesystem::remove(path);
    const auto first=current.at("transitions").To<Array>().front().To<Map>();auto edit=RapidRequest(scene);edit["policy"]=first.at("policy");auto construction=first.at("construction").To<Map>();construction["liftHeightMm"]=construction.at("liftHeightMm").To<double>()+2.;edit["manualPath"]=Map{{"construction",construction}};Invoke(&scene,"CamRapid","EditPath",edit);
    const auto draft=Generated(scene).at("transitions").To<Array>().front().To<Map>();EXPECT_EQ("Draft",draft.at("geometryStatus").To<Text>());EXPECT_EQ("Unchecked",draft.at("validation").To<Text>());EXPECT_EQ(first.at("points").To<Array>().front(),draft.at("points").To<Array>().front());EXPECT_EQ(first.at("points").To<Array>().back(),draft.at("points").To<Array>().back());EXPECT_EQ(original.at("operations"),Generated(scene).at("operations"));
}
TEST(FiveAxisStateTransport, CollisionReportUsesVersionedResourceAndPreservesCompleteResults)
{
    using namespace state_transport_tests;Scene scene;PrepareOrderPart(scene);RunCollision(scene);
    const auto before=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto report=ActiveSetupSnapshot(before).at("simulationCheck").To<Map>();ASSERT_EQ("Current",report.at("validity").To<Text>());
    const auto wire=Invoke(&scene,"CamScene","ReadState"),metadata=ActiveSetupSnapshot(wire).at("simulationCheck").To<Map>();
    EXPECT_FALSE(metadata.contains("frames"));EXPECT_FALSE(metadata.contains("segments"));EXPECT_FALSE(metadata.contains("keypoints"));
    const auto ref=metadata.at("reportResourceRef").To<Map>();
    const auto resource=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(resource);
    const auto bytes=resource->Bytes();ASSERT_EQ("ICMJ",Text(reinterpret_cast<const char*>(bytes.data()+4),4));
    const auto restored=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(StringAt(bytes,Field(bytes,U32(bytes,0),1))).To<Map>();
    // Standard JSON normalizes integral numeric variants; serialization checks
    // every value while preserving all double coordinates and witness data.
    std::function<Variant(const Variant&)> jsonValue=[&](const Variant& value)->Variant{
        if(value.Is<ID>())return iCAX::Data::to_string(value.To<ID>());
        if(value.Is<Map>()){Map object;for(const auto& [key,child]:value.To<Map>())object[key]=jsonValue(child);return object;}
        if(value.Is<Array>()){Array array;for(const auto& child:value.To<Array>())array.emplace_back(jsonValue(child));return array;}return value;
    };
    EXPECT_EQ(iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(jsonValue(report)),iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(restored));
    EXPECT_EQ(std::to_string(report.at("frames").To<Array>().size()),metadata.at("frameCount").To<Text>());
    EXPECT_EQ(metadata,ActiveSetupSnapshot(Invoke(&scene,"CamScene","ReadState")).at("simulationCheck").To<Map>());
    EXPECT_EQ(before,Read(scene));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
}
TEST(FiveAxisStateTransport, CollisionReportSerializesNativeSourceAnchorUUIDsWithoutChangingStoredReport)
{
    using namespace state_transport_tests;Scene scene;PrepareOrderPart(scene);RunCollision(scene);
    const auto setupID=ID::from_string(Read(scene).at("activeSetupId").To<Text>()).value();
    const auto setup=std::dynamic_pointer_cast<CFiveAxisSetupComponent>(scene.Database().GetEntity(setupID)->GetComponent(CFiveAxisSetupComponent::S_ClassName));ASSERT_TRUE(setup);
    auto stored=setup->GetSimulationCheckData(),body=stored.at("value").To<Map>();auto frames=body.at("frames").To<Array>();ASSERT_FALSE(frames.empty());
    const auto operation=iCAX::Data::GenerateNewUUID(),port=iCAX::Data::GenerateNewUUID();
    auto frame=frames.front().To<Map>();frame["sourceSample"]=Map{{"anchor",Map{{"port",Map{{"operationId",operation},{"portId",port}}},{"parameter",.123456789012345}}}};frames.front()=frame;body["frames"]=frames;stored["value"]=body;
    auto& tx=scene.Database().BeginTransaction("Add native source anchor");tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"SimulationCheckData",stored}});ASSERT_TRUE(scene.Database().CommitTransaction(tx));
    const auto history=scene.Database().GetUndoArray().size();
    const auto state=Invoke(&scene,"CamScene","ReadState"),metadata=ActiveSetupSnapshot(state).at("simulationCheck").To<Map>(),ref=metadata.at("reportResourceRef").To<Map>();
    const auto resource=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.at("url").To<Text>(),std::stoull(ref.at("version").To<Text>()));ASSERT_TRUE(resource);
    const auto bytes=resource->Bytes();const auto decoded=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(StringAt(bytes,Field(bytes,U32(bytes,0),1))).To<Map>();
    const auto anchor=decoded.at("frames").To<Array>().front().To<Map>().at("sourceSample").To<Map>().at("anchor").To<Map>(),wirePort=anchor.at("port").To<Map>();
    EXPECT_EQ(iCAX::Data::to_string(operation),wirePort.at("operationId").To<Text>());EXPECT_EQ(iCAX::Data::to_string(port),wirePort.at("portId").To<Text>());EXPECT_DOUBLE_EQ(.123456789012345,anchor.at("parameter").To<double>());
    EXPECT_EQ(stored,setup->GetSimulationCheckData());EXPECT_EQ(history,scene.Database().GetUndoArray().size());EXPECT_EQ("Current",metadata.at("validity").To<Text>());
}
TEST(FiveAxisStateTransport, DISABLED_Real480Paths)
{
    using namespace ompl_evaluation;using namespace state_transport_tests;
    const auto path=Env(L"ICAX_RAPID_GEOMETRY_PROJECT"),reportName=Env(L"ICAX_RAPID_GEOMETRY_REPORT");ASSERT_FALSE(path.empty());ASSERT_FALSE(reportName.empty());
    iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_FIVE_AXIS_CAM",.ProductID="icax.five-axis-cam",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
    const auto prepared=file.PrepareOpen(path);const auto info=prepared.Result().Info;Scene scene(info.ProjectID,info.MainSceneID,false);file.Restore(prepared,scene.Database(),scene.Resources());InitializeScene(scene);
    const auto original=Read(scene);const auto history=scene.Database().GetUndoArray().size();
    const auto full=iCAX::Data::VariantSerializer::Serialize(original);iCAX::Interaction::CSDOQueue queue;iCAX::Interaction::CSDOFrame large;large.Payload.assign(full.begin(),full.end());
    const bool oldPayloadRejected=!queue.TryEnqueue(std::move(large));std::clog<<"Original scene response bytes="<<full.size()<<" default queue rejects="<<oldPayloadRejected<<std::endl;
    const auto began=Clock::now();const auto wire=Invoke(&scene,"CamScene","ReadState");const auto readMs=Ms(began);const auto wireBytes=iCAX::Data::VariantSerializer::Serialize(wire).size();
    EXPECT_LT(wireBytes,16u*1024*1024);const auto checked=CheckSamples(scene,wire,original);EXPECT_GT(checked,0u);
    auto request=Map{{"commandId",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"expected",wire.at("expected")},{"setupId",wire.at("activeSetupId")},{"geometryRevision",ActiveSetupSnapshot(wire).at("generatedGeometry").To<Map>().at("revision")}};
    auto task=Invoke(&scene,"CamRapid","Generate",request);task=Wait(scene,task);ASSERT_EQ("Completed",task.at("status").To<Text>());
    EXPECT_EQ("479",task.at("result").To<Map>().at("generatedCount").To<Text>());
    const auto after=Invoke(&scene,"CamScene","ReadState");EXPECT_EQ("479",ActiveSetupSnapshot(after).at("generatedGeometry").To<Map>().at("rapidGeneratedCount").To<Text>());
    const auto ref=ActiveSetupSnapshot(after).at("generatedGeometry").To<Map>().at("sampleGeometryRef").To<Map>();const auto body=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.at("url").To<Text>(),1);
    EXPECT_EQ(ActiveSetupSnapshot(original).at("generatedGeometry").To<Map>().at("operations"),ActiveSetupSnapshot(Read(scene)).at("generatedGeometry").To<Map>().at("operations"));EXPECT_EQ(history,scene.Database().GetUndoArray().size());
    Map report{{"passed",!HasFailure()},{"oldScenePayloadBytes",std::to_string(full.size())},{"oldPayloadRejectedByDefaultQueue",oldPayloadRejected},{"scenePayloadBytes",std::to_string(wireBytes)},{"readWireMs",readMs},
        {"pathSampleResourceBytes",std::to_string(body->Size())},{"verifiedDoubleComponents",std::to_string(checked)},{"operationCount",Text("480")},{"rapidCount",Text("479")},{"queueLimitBytes",Text("16777216")},{"queueLimitUnchanged",true},{"sourceGeometryUnchanged",true}};
    std::ofstream output(std::filesystem::path(reportName),std::ios::binary);output<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(report);
}
