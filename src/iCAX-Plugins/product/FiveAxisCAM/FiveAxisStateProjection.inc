namespace
{
    // Keep sampled paths on the versioned resource channel. SDO carries only
    // the current ordering/validity, part descriptors and a fixed reference.
    Ref BuildPathSamplesProjection(Scene& scene,const Map& geometry,const std::vector<const Map*>& definitions)
    {
        flatbuffers::FlatBufferBuilder builder;
        std::vector<flatbuffers::Offset<void>> entries;
        const auto add=[&](const Text& id,const Array& parts){
            std::vector<flatbuffers::Offset<void>> encoded;
            for(const auto& value:parts){const auto& part=std::get<Map>(value.m_Value);std::vector<double> points,directions;
                const auto flatten=[&](const char* key,std::vector<double>& target){for(const auto& p:std::get<Array>(part.at(key).m_Value))for(const auto& n:std::get<Array>(p.m_Value))target.push_back(RegistrationNumber(n));};
                flatten("points",points);if(part.contains("directions"))flatten("directions",directions);
                if((String(part,"role")!="Definition"&&points.size()!=directions.size())||points.size()%3)throw std::runtime_error("CAM_PATH_SAMPLE_COUNT_MISMATCH");
                const auto role=builder.CreateString(String(part,"role"));const auto p=builder.CreateVector(points),d=builder.CreateVector(directions);
                const auto start=builder.StartTable();builder.AddOffset(4,role);builder.AddOffset(6,p);builder.AddOffset(8,d);encoded.emplace_back(builder.EndTable(start));
            }
            const auto name=builder.CreateString(id);const auto vector=builder.CreateVector(encoded);const auto start=builder.StartTable();builder.AddOffset(4,name);builder.AddOffset(6,vector);entries.emplace_back(builder.EndTable(start));
        };
        for(const auto* cut:definitions)add("definition:"+String(*cut,"id"),{Map{{"role",Text("Definition")},{"points",cut->at("previewPoints")}}});
        if(geometry.contains("operations"))for(const auto& value:std::get<Array>(geometry.at("operations").m_Value)){const auto& op=std::get<Map>(value.m_Value);add(String(op,"intentId"),std::get<Array>(op.at("parts").m_Value));}
        if(geometry.contains("transitions"))for(const auto& value:std::get<Array>(geometry.at("transitions").m_Value)){const auto& t=std::get<Map>(value.m_Value);if(String(t,"geometryStatus")!="Generated"&&String(t,"geometryStatus")!="Draft"&&!((String(t,"geometryStatus")=="Failed"||String(t,"geometryStatus")=="Stale")&&HasRapidEditingGeometry(t)))continue;
            auto part=t;part["role"]=Text("Rapid");add("rapid:"+RapidPair(t),{part});}
        const auto vector=builder.CreateVector(entries);const auto start=builder.StartTable();builder.AddElement<std::uint32_t>(4,1,0);builder.AddOffset(6,vector);
        builder.Finish(flatbuffers::Offset<void>(builder.EndTable(start)),"ICPS");
        auto body=std::make_shared<iCAX::Resource::CFlatBufferResource>(std::vector<std::uint8_t>(builder.GetBufferPointer(),builder.GetBufferPointer()+builder.GetSize()));
        iCAX::Resource::CResourceInfo info;info.Name="刀路显示采样";info.ResourceTypeID="resource.flatbuffer";info.nVersion=1;info.nSchemaVersion=1;info.nMinimumReaderVersion=1;
        info.Persistence=iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;

        Ref ref{scene.Resources().AllocateResourceURL(),1};scene.Resources().Set(ref.URL,body,info);return ref;
    }
    Map CutDefinitionsProjection(Scene& scene,const Array& ids)
    {
        Array items;std::set<ID> unique;ID setupID{};
        for(const auto& value:ids){const auto id=CutID(value);
            if(!unique.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");
            const auto cut=ReadIntentCut(scene,id);const auto setup=cut->GetSetupID();
            if(items.empty()){
                setupID=setup;
                // Resolve the design ports with one validated plan read, as
                // the full scene projection does. Reading them one row at a
                // time reloads and validates the complete plan for every cut.
                PrimeIntentDesignPaths(scene,setupID);
            }else if(setupID!=setup)throw std::invalid_argument("CAM_SETUP_MISMATCH");
            auto item=IntentToolpathState(scene,cut);
            items.emplace_back(std::move(item));}
        std::vector<const Map*> definitions;for(const auto& item:items)definitions.push_back(&std::get<Map>(item.m_Value));
        const auto ref=BuildPathSamplesProjection(scene,{},definitions);
        for(auto& item:items){auto& row=std::get<Map>(item.m_Value);
            row["previewPointCount"]=std::to_string(std::get<Array>(row.at("previewPoints").m_Value).size());row.erase("previewPoints");}
        auto patch=CutMetadataPatch(scene,setupID,false);auto setup=patch.at("setup").To<Map>();
        setup["definitionOrder"]=CutTreeOrder(scene,setupID);patch["setup"]=std::move(setup);
        return {{"items",items},{"sampleGeometryRef",WireRef(StoredRef(ref))},{"statePatch",patch}};
    }
    void ProjectPathSamples(Scene& scene,const ID& setupID,Map& geometry,const Array& intents)
    {
        std::vector<const Map*> definitions;Array definitionRefs;
        for(const auto& value:intents){const auto& cut=std::get<Map>(value.m_Value);if(Identifier(cut,"setupId")!=setupID)continue;definitions.push_back(&cut);definitionRefs.emplace_back(Map{{"id",cut.at("id")},{"feature",cut.at("featureRef")},{"geometry",cut.at("curveRef")},{"state",cut.at("sourceState")}});}
        if(!geometry.contains("operations")&&definitions.empty())return;
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto runtime=Runtime(scene);
        Array rapidInputs;if(geometry.contains("transitions"))for(const auto& value:std::get<Array>(geometry.at("transitions").m_Value)){const auto& t=std::get<Map>(value.m_Value);rapidInputs.emplace_back(Map{{"pair",RapidPair(t)},{"input",t.at("inputKey")},{"status",t.at("geometryStatus")},{"hasEditingGeometry",HasRapidEditingGeometry(t)}});}
        const auto key=iCAX::Data::VariantSerializer::Serialize(Map{{"geometry",GeneratedDataRevision(*setup)},{"definitions",definitionRefs},{"rapids",RapidDataRevision(*setup)},{"overrides",setup->GetRapidOverrides()},{"rapidInputs",rapidInputs}});
        auto& cache=runtime->PathSamples[setupID];if(cache.first!=key){cache.second=BuildPathSamplesProjection(scene,geometry,definitions);cache.first=key;}
        geometry["sampleGeometryRef"]=WireRef(StoredRef(cache.second));
        if(geometry.contains("operations"))for(auto& value:std::get<Array>(geometry.at("operations").m_Value)){
            auto& op=std::get<Map>(value.m_Value);if(op.contains("curveData")){op["curveRevision"]=EngineeringIdentity(op.at("curveData").To<Map>());op.erase("curveData");}
            for(auto& partValue:std::get<Array>(op.at("parts").m_Value)){auto& part=std::get<Map>(partValue.m_Value);
                part["pointCount"]=std::to_string(std::get<Array>(part.at("points").m_Value).size());part.erase("points");part.erase("directions");part.erase("sourceSamples");}
        }
        if(geometry.contains("transitions"))for(auto& value:std::get<Array>(geometry.at("transitions").m_Value)){auto& transition=std::get<Map>(value.m_Value);if(String(transition,"geometryStatus")!="Generated"&&String(transition,"geometryStatus")!="Draft"&&!((String(transition,"geometryStatus")=="Failed"||String(transition,"geometryStatus")=="Stale")&&HasRapidEditingGeometry(transition)))continue;
            transition["pointCount"]=std::to_string(std::get<Array>(transition.at("points").m_Value).size());transition.erase("points");transition.erase("directions");transition.erase("surfacePoints");transition.erase("heightsMm");}
    }
}
Map ReadStateProjection(Scene& scene)
{
    auto state=BuildSceneState(scene,true);auto& intents=std::get<Array>(state.at("intentToolpaths").m_Value);
    for(auto& value:std::get<Array>(state.at("setups").m_Value)){
        auto& setup=std::get<Map>(value.m_Value);const auto id=Identifier(setup,"id");
        setup["fixtureCheck"]=FixtureFields(setup.at("fixtureCheck").To<Map>(),{"status","validity","scope","inputRevision","clearanceMm","contactCount","clearanceCount"});
        ProjectPathSamples(scene,id,std::get<Map>(setup.at("generatedGeometry").m_Value),intents);
        auto& report=std::get<Map>(setup.at("simulationCheck").m_Value);
        if(report.contains("frames")){
            const auto stored=EngineeringIdentity(Require<CFiveAxisSetupComponent>(scene.Database(),id)->GetSimulationCheckData());
            const auto wire=PublishMachineWireResource(scene,id,"collision",iCAX::Data::VariantSerializer::Serialize(Map{{"stored",stored},{"trajectory",SimulationCollisionKey(scene,id)}}),report,{});
            report["frameCount"]=std::to_string(std::get<Array>(report.at("frames").m_Value).size());
            report["segmentCount"]=std::to_string(std::get<Array>(report.at("segments").m_Value).size());
            report.erase("frames");report.erase("segments");report.erase("keypoints");
            report["reportResourceRef"]=WireRef(StoredRef(wire));
        }
    }
    for(auto& value:intents){auto& cut=std::get<Map>(value.m_Value);cut["previewPointCount"]=std::to_string(std::get<Array>(cut.at("previewPoints").m_Value).size());cut.erase("previewPoints");}
    return state;
}
