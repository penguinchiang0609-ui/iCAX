// Product user data owns reusable snapshots. Scene entities/resources remain
// independent, embedded copies so a library edit cannot alter a saved job.
namespace {
    using LibraryStore=iCAX::Application::IProductUserDataStore;
    using LibraryRecord=iCAX::Application::CProductUserDataRecord;
    constexpr const char* FixtureLibraryFeature="fixture-library";
    LibraryStore& FixtureStore(Scene& scene) {
        auto store=Runtime(scene)->FixtureLibraryStore;
        if(!store)throw std::runtime_error("工装库存储不可用。");
        return *store;
    }
    Text FixtureLibraryName(const Map& request) {
        auto name=String(request,"name");
        const auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");
        if(first==Text::npos||name.size()>512)throw std::invalid_argument("请填写有效的工装名称。");
        return name.substr(first,last-first+1);
    }
    Text PackFixtureBytes(std::span<const uint8_t> bytes) {
        constexpr char hex[]="0123456789abcdef";Text out;out.resize(bytes.size()*2);
        for(size_t i=0;i<bytes.size();++i){out[i*2]=hex[bytes[i]>>4];out[i*2+1]=hex[bytes[i]&15];}return out;
    }
    std::vector<uint8_t> UnpackFixtureBytes(const Text& text) {
        if(text.size()%2)throw std::runtime_error("工装库几何数据损坏。");
        auto digit=[](char c)->uint8_t{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("工装库几何数据损坏。");};
        std::vector<uint8_t> bytes(text.size()/2);
        for(size_t i=0;i<bytes.size();++i)bytes[i]=(digit(text[2*i])<<4)|digit(text[2*i+1]);return bytes;
    }
    LibraryRecord LibraryRecordFor(const Text& type,const Text& id,Map payload) {
        LibraryRecord record;record.FeatureID=FixtureLibraryFeature;record.RecordType=type;
        record.SubjectType="product";record.SubjectID="icax.five-axis-cam";record.RecordID=id;record.Payload=std::move(payload);return record;
    }
    std::optional<LibraryRecord> FixtureDefaults(LibraryStore& store) {return store.Get(FixtureLibraryFeature,"preferences","icax.five-axis-cam");}
    Text DefaultFixtureID(LibraryStore& store) {
        const auto defaults=FixtureDefaults(store);return defaults?String(defaults->Payload.To<Map>(),"entryId"):Text();
    }
    LibraryRecord RequireLibraryEntry(LibraryStore& store,const Text& id) {
        auto entry=store.Get(FixtureLibraryFeature,"assembly",id);
        if(!entry)throw std::invalid_argument("工装库条目已删除，请刷新。");return *entry;
    }
    Map LibrarySummary(const LibraryRecord& entry,const Text& defaultID) {
        const auto data=entry.Payload.To<Map>();const auto parts=data.at("parts").To<Array>();
        UInt generated=0;for(const auto& p:parts)generated+=p.To<Map>().at("generated").To<bool>();
        return {{"id",entry.RecordID},{"revision",std::to_string(entry.Revision)},{"name",data.at("name")},
            {"partCount",static_cast<UInt>(parts.size())},{"source",Text(generated==parts.size()?"Generated":generated?"Mixed":"Imported")},
            {"isDefault",entry.RecordID==defaultID}};
    }
    Array PrepareLibraryFixtures(Scene& scene,const LibraryRecord& entry) {
        const auto data=entry.Payload.To<Map>();Array result;
        const auto unitID=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());
        // Validate every part before publishing any Scene entity.
        struct Part {Map data;std::shared_ptr<iCAX::GeometryData::BRepModel> geometry;std::shared_ptr<iCAX::Resource::CFlatBufferResource> preview;};
        std::vector<Part> parts;
        for(const auto& value:data.at("parts").To<Array>()){
            auto p=value.To<Map>();RigidMatrix(p.at("transform").To<Array>());
            auto geometry=std::make_shared<iCAX::GeometryData::BRepModel>(iCAX::GeometryData::Persistence::Deserialize(UnpackFixtureBytes(String(p,"geometry"))));
            auto bytes=UnpackFixtureBytes(String(p,"preview"));if(bytes.empty())throw std::runtime_error("工装库显示数据缺失。");
            parts.push_back({p,geometry,std::make_shared<iCAX::Resource::CFlatBufferResource>(std::move(bytes))});
        }
        if(parts.empty())throw std::invalid_argument("工装组合为空。");
        for(auto& p:parts){
            auto geometry=Publish(scene,p.geometry,"geometry.brep",String(p.data,"name"));
            auto preview=Publish(scene,p.preview,"resource.flatbuffer",String(p.data,"name"),{geometry});
            Map metadata=p.data.contains("metadata")?p.data.at("metadata").To<Map>():Map();metadata["generated"]=p.data.at("generated");metadata["libraryEntryId"]=entry.RecordID;metadata["libraryRevision"]=std::to_string(entry.Revision);
            result.emplace_back(Map{{"id",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID())},{"name",p.data.at("name")},
                {"geometryRef",StoredRef(geometry)},{"previewRef",StoredRef(preview)},{"transform",p.data.at("transform")},
                {"visible",p.data.at("visible")},{"collisionEnabled",p.data.at("collisionEnabled")},
                {"metadata",metadata}});
        }
        Map unit{{"id",unitID},{"transform",data.contains("transform")?data.at("transform"):Variant(Identity())},{"name",data.at("name")},{"kind",data.contains("unitKind")?data.at("unitKind"):Variant(Text(data.contains("design")?"Generated":"Imported"))},{"design",Map()}};
        if(data.contains("design")){
            auto d=data.at("design").To<Map>();d["unitId"]=unitID;d["unitSnapshot"]=true;d.erase("candidate");d.erase("breakdown");d.erase("delivery");
            auto a=d.at("assembly").To<Map>();auto original=a.at("parts").To<Array>();std::map<Text,Text> remap;
            for(size_t i=0;i<result.size();++i)remap[String(original[i].To<Map>(),"partId")]=String(result[i].To<Map>(),"id");
            Array rebuilt;for(size_t i=0;i<original.size();++i){auto v=original[i].To<Map>(),part=result[i].To<Map>();v["partId"]=part.at("id");v["geometryRef"]=part.at("geometryRef");v["previewRef"]=part.at("previewRef");
                if(v.contains("joints")){Array joints;for(const auto& j:v.at("joints").To<Array>()){auto value=j.To<Map>();const auto found=remap.find(String(value,"matePartId"));if(found!=remap.end())value["matePartId"]=found->second;joints.emplace_back(value);}v["joints"]=joints;}rebuilt.emplace_back(v);}
            a["id"]=iCAX::Data::to_string(iCAX::Data::GenerateNewUUID());a["unitId"]=unitID;a["parts"]=rebuilt;a["validity"]=Text("Current");d["assembly"]=a;unit["design"]=d;
        }
        for(auto& value:result){auto part=value.To<Map>();part["unit"]=unit;value=part;}return result;
    }
    Map PublishFixtureLibraryImport(Scene& scene,const Ref& geometryRef,const Ref& previewRef,const Text& name) {
        auto& store=FixtureStore(scene);
        const auto geometry=scene.Resources().Get<iCAX::GeometryData::BRepModel>(geometryRef.URL,geometryRef.nVersion);
        const auto preview=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(previewRef.URL,previewRef.nVersion);
        if(!geometry||!preview)throw std::runtime_error("工装模型数据缺失。");
        Array parts{Map{{"name",name},{"transform",Identity()},{"visible",true},{"collisionEnabled",true},{"generated",false},
            {"geometry",PackFixtureBytes(iCAX::GeometryData::Persistence::Serialize(*geometry))},{"preview",PackFixtureBytes(preview->Bytes())}}};
        auto saved=store.Put(LibraryRecordFor("assembly",iCAX::Data::to_string(iCAX::Data::GenerateNewUUID()),{{"name",name},{"parts",parts}}),0);
        return LibrarySummary(saved,DefaultFixtureID(store));
    }
    Array PrepareDefaultFixtures(Scene& scene) {
        const auto store=Runtime(scene)->FixtureLibraryStore;if(!store)return {};
        const auto id=DefaultFixtureID(*store);if(id.empty())return {};
        return PrepareLibraryFixtures(scene,RequireLibraryEntry(*store,id));
    }
    void AttachLibraryFixtures(iCAX::Database::ITransaction& tx,const ID& setupID,const Array& parts) {
        if(parts.empty())return;
        const auto unit=parts.front().To<Map>().at("unit").To<Map>();const auto unitID=Identifier(unit,"id");
        tx.CreateEntity(unitID);tx.AttachComponent(unitID,CFiveAxisFixtureUnitComponent::S_ClassName,{{"SetupID",setupID},{"Name",unit.at("name")},{"Kind",unit.at("kind")},{"Transform",unit.at("transform")},{"Design",unit.at("design")}});
        for(const auto& value:parts){const auto p=value.To<Map>();const auto id=Identifier(p,"id");
            tx.CreateEntity(id);tx.AttachComponent(id,CFiveAxisFixtureComponent::S_ClassName,
                {{"SetupID",setupID},{"UnitID",unitID},{"Name",p.at("name")},{"GeometryRef",p.at("geometryRef")},{"PreviewRef",p.at("previewRef")},
                 {"Transform",p.at("transform")},{"Visible",p.at("visible")},{"CollisionEnabled",p.at("collisionEnabled")},{"ImportMetadata",p.at("metadata")}});
            tx.AttachComponent(id,iCAX::Transform::CTransformComponent::S_ClassName);
            const auto preview=*iCAX::ProjectFile::TryGetResourceReferenceValue(p.at("previewRef").To<Map>());
            tx.AttachComponent(id,iCAX::RenderInteraction::CRenderInstanceComponent::S_ClassName,
                {{"GeometryResourceID",preview.URL},{"GeometryResourceVersion",static_cast<UInt>(preview.nVersion)}});
        }
    }
    template<class F> Map FixtureLibraryCommand(Scene& scene,const Map& request,const Text& operation,F fn) {
        auto runtime=Runtime(scene);const auto id=iCAX::Data::to_string(Identifier(request,"commandId"));
        const auto payload=operation+iCAX::Data::VariantSerializer::Serialize(request);
        if(const auto it=runtime->Receipts.find(id);it!=runtime->Receipts.end()){
            if(it->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return it->second.second;
        }
        CheckExpected(*runtime,request);CheckCapacity(*runtime);
        auto result=fn(FixtureStore(scene));runtime->Receipts.emplace(id,std::make_pair(payload,result));return result;
    }
}
void BindFixtureLibraryStore(Scene& scene,std::shared_ptr<LibraryStore> store) {
    if(store&&store->GetProductID()!="icax.five-axis-cam")throw std::invalid_argument("CAM_LIBRARY_PRODUCT_MISMATCH");
    Runtime(scene)->FixtureLibraryStore=std::move(store);
    CaptureInitialProductHeadDefault(scene);
    CaptureInitialProductMachineTypeDefault(scene);
}
Map ReadFixtureLibrary(Scene& scene,const Map&) {
    auto& store=FixtureStore(scene);const auto defaults=FixtureDefaults(store);const auto defaultID=DefaultFixtureID(store);
    iCAX::Application::CProductUserDataQuery query;query.FeatureID=FixtureLibraryFeature;query.RecordType="assembly";
    Array entries;for(const auto& entry:store.List(query))entries.emplace_back(LibrarySummary(entry,defaultID));
    return {{"entries",entries},{"defaultId",defaultID},{"preferencesRevision",std::to_string(defaults?defaults->Revision:0)}};
}
Map SaveFixtureLibrary(Scene& scene,const Map& request) {
    return FixtureLibraryCommand(scene,request,"LibrarySave",[&](LibraryStore& store){
        const auto setupID=Identifier(request,"setupId");Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        const auto name=FixtureLibraryName(request);auto ids=request.at("fixtureIds").To<Array>();const auto requested=ids;ids.clear();Map design;Array unitPose=Identity();Text unitKind="Imported";
        for(const auto& value:requested){const auto id=Identifier(Map{{"id",value}},"id");if(const auto unit=Component<CFiveAxisFixtureUnitComponent>(scene.Database().GetEntity(id))){
            if(unit->GetSetupID()!=setupID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
            if(requested.size()==1){design=unit->GetDesign();unitPose=unit->GetTransform();unitKind=unit->GetKind();}
            if(!design.empty()){for(const auto& part:design.at("assembly").To<Map>().at("parts").To<Array>())ids.push_back(part.To<Map>().at("partId"));}
            else for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetUnitID()==id)ids.emplace_back(iCAX::Data::to_string(e->GetID()));
        }else ids.push_back(value);}
        if(ids.empty())throw std::invalid_argument("请先添加工装。");
        Array parts;std::set<ID> unique;
        for(const auto& value:ids){const auto id=Identifier(Map{{"id",value}},"id");if(!unique.insert(id).second)throw std::invalid_argument("工装重复。");
            const auto f=Require<CFiveAxisFixtureComponent>(scene.Database(),id);if(f->GetSetupID()!=setupID)throw std::invalid_argument("CAM_SETUP_MISMATCH");
            const auto geometry=FixtureGeometry(scene,f->GetGeometryRef());const auto ref=*iCAX::ProjectFile::TryGetResourceReferenceValue(f->GetPreviewRef());
            const auto preview=scene.Resources().Get<iCAX::Resource::CFlatBufferResource>(ref.URL,ref.nVersion);if(!preview)throw std::runtime_error("CAM_PREVIEW_MISSING");
            const auto metadata=f->GetImportMetadata();
            parts.emplace_back(Map{{"name",f->GetName()},{"transform",f->GetTransform()},{"visible",f->GetVisible()},{"collisionEnabled",f->GetCollisionEnabled()},
                {"generated",metadata.contains("generated")&&metadata.at("generated").To<bool>()},{"metadata",metadata},
                {"geometry",PackFixtureBytes(iCAX::GeometryData::Persistence::Serialize(*geometry))},{"preview",PackFixtureBytes(preview->Bytes())}});
        }
        Map payload{{"name",name},{"parts",parts},{"transform",unitPose},{"unitKind",unitKind}};if(!design.empty())payload["design"]=design;
        const auto record=LibraryRecordFor("assembly",String(request,"commandId"),payload);
        auto saved=store.Put(record,0);return LibrarySummary(saved,DefaultFixtureID(store));
    });
}
Map UpdateFixtureLibrary(Scene& scene,const Map& request) {
    return FixtureLibraryCommand(scene,request,"LibraryUpdate",[&](LibraryStore& store)->Map {
        const auto action=String(request,"action"),id=String(request,"entryId");
        if(action=="default"){
            if(!id.empty())RequireLibraryEntry(store,id);
            auto record=LibraryRecordFor("preferences","icax.five-axis-cam",{{"entryId",id}});
            store.Put(record,Integer(request,"preferencesRevision"));return {{"defaultId",id}};
        }
        auto entry=RequireLibraryEntry(store,id);const auto revision=Integer(request,"revision");
        if(action=="rename"){auto data=entry.Payload.To<Map>();data["name"]=FixtureLibraryName(request);entry.Payload=data;return LibrarySummary(store.Put(entry,revision),DefaultFixtureID(store));}
        if(action=="delete"){
            if(DefaultFixtureID(store)==id)throw std::invalid_argument("请先取消默认工装，再删除。");
            store.Delete(FixtureLibraryFeature,"assembly",id,revision);return {{"deleted",true}};
        }throw std::invalid_argument("CAM_LIBRARY_ACTION");
    });
}
Map UseFixtureLibrary(Scene& scene,const Map& request) {
    return FixtureLibraryCommand(scene,request,"LibraryUse",[&](LibraryStore& store){
        const auto setupID=Identifier(request,"setupId");RequireSetupUnlocked(scene,setupID);RequireSetupWorkpieceEnabled(scene,setupID);
        auto entry=RequireLibraryEntry(store,String(request,"entryId"));
        if(entry.Revision!=Integer(request,"revision"))throw std::invalid_argument("工装库条目已变化，请刷新后选用。");
        const auto parts=PrepareLibraryFixtures(scene,entry);
        Transaction(scene.Database(),"从工装库添加",[&](auto& tx){AttachLibraryFixtures(tx,setupID,parts);Invalidate(tx,scene.Database(),setupID);});
        Array ids;for(const auto& part:parts)ids.push_back(part.To<Map>().at("id"));
        return Map{{"unitId",parts.front().To<Map>().at("unit").To<Map>().at("id")},{"fixtureIds",ids},{"setupId",iCAX::Data::to_string(setupID)}};
    });
}
