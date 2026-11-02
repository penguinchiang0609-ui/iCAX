// The installed machine declarations own this catalog. Selecting a head embeds
// its tool-link visuals and declared calibration, so saved projects are portable.
void BindBuiltinHeadSources(Scene& scene,const Array& sources){Runtime(scene)->BuiltinHeadSources=sources;}
Map ListBuiltinHeads(Scene& scene,const Map& request){
    CutFields(request,{});Array items;
    const auto sources=Runtime(scene)->BuiltinHeadSources;
    const bool dedicated=std::any_of(sources.begin(),sources.end(),[](const auto& v){return v.template To<Map>().contains("standaloneHead");});
    for(const auto& value:sources){
        const auto item=value.To<Map>();if(dedicated?!item.contains("standaloneHead"):!item.contains("headRoot"))continue;
        auto kind=String(item,"kind");if(kind=="4+1")kind="A";if(kind=="3+2")kind="Fixed";
        const auto name=dedicated?item.at("name"):Variant(kind=="Fixed"?Text("固定切割头"):kind+" 切割头");
        Map listed{{"id",item.at("id")},{"name",name},{"kind",kind},{"workpieceAxes",Array{}}};if(item.contains("headAxes"))listed["headAxes"]=item.at("headAxes");items.emplace_back(std::move(listed));
    }
    return {{"items",items}};
}
namespace {
    // Bound displays follow the actual instance, including edited frames and
    // calibration. A previously selected, different head is never reused.
    Map BoundBuiltinHeadAssembly(Scene& scene,const Map& configuration){
        const auto machine=Component<iCAX::CAM::CMachineInstanceComponent>(scene.Database().GetEntity(Identifier(configuration,"machineId")));if(!machine)return {};
        Map source;for(const auto& value:Runtime(scene)->BuiltinHeadSources){const auto item=value.To<Map>();if(String(item,"id")==machine->GetMachineDefinitionID())source=item;}
        if(!source.contains("headRoot"))return {};
        std::vector<std::shared_ptr<iCAX::Database::IEntity>> chain;auto entity=scene.Database().GetEntity(Identifier(configuration,"toolId"));bool found=false;std::size_t legacyDepth=0;
        for(std::size_t count=0;entity&&count<64;++count){
            const auto element=Component<iCAX::CAM::CMachineElementComponent>(entity);if(!element)break;chain.push_back(entity);
            const auto part=Component<iCAX::CAM::CMachineLinkComponent>(entity);if(part&&part->GetLinkName()==String(source,"headRoot")){found=true;break;}
            if(part&&source.contains("headRootAliases"))for(const auto& alias:source.at("headRootAliases").To<Array>())if(part->GetLinkName()==alias.To<Text>())legacyDepth=chain.size();
            const auto transform=Component<iCAX::Transform::CTransformComponent>(entity);entity=transform?scene.Database().GetEntity(transform->GetParentEntityID()):nullptr;
        }
        // Prefer the complete current assembly. Older saved instances retain
        // their original boundary and embedded geometry instead of being rebuilt.
        if(!found&&legacyDepth){chain.resize(legacyDepth);found=true;}
        if(!found)return {};std::reverse(chain.begin(),chain.end());Array nodes;
        for(std::size_t i=0;i<chain.size();++i){const auto& e=chain[i];const auto element=Component<iCAX::CAM::CMachineElementComponent>(e);const auto joint=Component<iCAX::CAM::CMachineJointComponent>(e);
            const auto render=Component<iCAX::RenderInteraction::CRenderInstanceComponent>(e);if(!render||render->GetGeometryResourceID().empty())return {};
            Array origin=Identity();if(i){const auto& p=element->GetRestPose();if(p.size()!=6)return {};const auto matrix=iCAX::Transform::MakeLocalMatrix(FiniteNumber(p[0],"CAM_MACHINE_ORIGIN_INVALID"),FiniteNumber(p[1],"CAM_MACHINE_ORIGIN_INVALID"),FiniteNumber(p[2],"CAM_MACHINE_ORIGIN_INVALID"),FiniteNumber(p[3],"CAM_MACHINE_ORIGIN_INVALID"),FiniteNumber(p[4],"CAM_MACHINE_ORIGIN_INVALID"),FiniteNumber(p[5],"CAM_MACHINE_ORIGIN_INVALID"),1,1,1);for(int r=0;r<4;++r)for(int c=0;c<4;++c)origin[r*4+c]=matrix(r,c);}
            const auto collision=Component<iCAX::CAM::CMachineCollisionComponent>(e);
            nodes.emplace_back(Map{{"name",element->GetName()},{"parent",static_cast<int>(i)-1},{"origin",origin},{"type",joint?joint->GetJointType():Text("fixed")},{"joint",joint?joint->GetJointName():Text{}},
                {"axis",joint?joint->GetAxis():Array{0.,0.,1.}},{"lower",joint?joint->GetLowerLimit():0.},{"upper",joint?joint->GetUpperLimit():0.},{"position",joint?joint->GetPosition():0.},
                {"previewRef",StoredRef({render->GetGeometryResourceID(),scene.Resources().GetVersion(render->GetGeometryResourceID())})},{"collisionBoxes",collision?collision->GetCollisions():Array{}}});
        }
        return {{"kind",source.at("kind")},{"nodes",nodes},{"tool",static_cast<int>(nodes.size())-1},{"tcp",configuration.at("tcp")},{"beam",configuration.at("beam")},{"headAxes",source.at("headAxes")},{"workpieceAxes",source.at("workpieceAxes")},{"source",Text("MachineInstance")}};
    }
    Map BuiltinHeadSource(Scene& scene,const Text& definitionID){
        Map source;
        for(const auto& value:Runtime(scene)->BuiltinHeadSources){const auto item=value.To<Map>();if(String(item,"id")==definitionID)source=item;}
        if(source.empty())throw std::invalid_argument("CAM_HEAD_BUILTIN_REQUIRED: 内置切割头不存在，请重新选择。");
        return source;
    }
    const iCAX::CAM::SMachineElementData& BuiltinHeadTool(const iCAX::CAM::CMachineDescriptionResource& description){
        const iCAX::CAM::SMachineElementData* tool=nullptr;
        for(const auto& element:description.Elements)if(element.Tool.bValid){if(tool)throw std::runtime_error("内置机床定义包含多个切割头，请明确工具端。");tool=&element;}
        if(!tool)throw std::runtime_error("内置机床定义缺少切割头或喷嘴标定。");return *tool;
    }
    Map DeclaredBuiltinHeadCollisionMetadata(const iCAX::CAM::SMachineElementData& tool){
        Array boxes;
        for(const auto& collision:tool.Collisions){Array pose;for(const auto value:collision.Pose)pose.emplace_back(value);
            boxes.emplace_back(Map{{"id",collision.CollisionID},{"name",collision.Name},{"pose",pose},{"geometry",collision.Geometry},{"enabled",true}});
        }
        return {{"collisionSource",Text("MachineDefinition")},{"collisionBoxes",boxes}};
    }
    // Old embedded visual snapshots can acquire their missing collision
    // declarations without importing display geometry or modifying the project.
    Map BuiltinHeadCollisionMetadata(Scene& scene,const Text& definitionID){
        const auto source=BuiltinHeadSource(scene,definitionID);iCAX::CAM::CMachineDescriptionResource description;Text error;
        if(!iCAX::CAM::LoadMachineDescription(String(source,"sourcePath"),description,error))throw std::runtime_error("内置切割头定义读取失败："+error);
        return DeclaredBuiltinHeadCollisionMetadata(BuiltinHeadTool(description));
    }
    Map BuiltinHeadPart(Scene& scene,const Map& source,const iCAX::CAM::SMachineElementData& element){
        const auto* tool=&element;
        if(tool->Visuals.empty())throw std::runtime_error("内置机床定义缺少切割头模型。");
        auto model=std::make_shared<iCAX::GeometryData::BRepModel>();std::vector<Ref> dependencies;
        for(const auto& visual:tool->Visuals){
            if(String(visual.Geometry,"type")!="mesh")throw std::runtime_error("内置切割头必须声明完整网格模型。");
            const auto uri=String(visual.Geometry,"uri");const auto relative=std::filesystem::path(std::u8string(uri.begin(),uri.end()));
            if(relative.empty()||relative.is_absolute())throw std::runtime_error("内置切割头网格路径必须相对于机床定义。");
            for(const auto& part:relative)if(part=="..")throw std::runtime_error("内置切割头网格路径不能越出机床定义目录。");
            const auto raw=String(source,"sourcePath");const auto path=(std::filesystem::path(std::u8string(raw.begin(),raw.end())).parent_path()/relative).u8string();
            iCAX::Resource::CResourceImportRequest request;request.SourcePath=Text(path.begin(),path.end());request.FormatID="geometry.stl";request.Persistence=iCAX::Resource::EResourcePersistenceMode::Embedded;
            iCAX::Resource::CResourceImportResult imported;const auto mesh=scene.Resources().Import<iCAX::GeometryData::CTriangleMeshResource>(request,&imported);
            if(!mesh)throw std::runtime_error("内置切割头模型读取失败："+imported.Error);
            if(!imported.PrimaryResourceID.empty())dependencies.push_back({imported.PrimaryResourceID,scene.Resources().GetVersion(imported.PrimaryResourceID)});
            std::array<double,3> scale{1000,1000,1000};if(visual.Geometry.contains("scale")){const auto values=visual.Geometry.at("scale").To<Array>();if(values.size()!=3)throw std::runtime_error("内置切割头网格缩放无效。");for(int k=0;k<3;++k)scale[k]*=FiniteNumber(values[k],"CAM_HEAD_SCALE_INVALID");}
            const auto& p=visual.Pose;const auto transform=iCAX::Transform::MakeLocalMatrix(p[0]*1000,p[1]*1000,p[2]*1000,p[5],p[4],p[3],scale[0],scale[1],scale[2]);
            iCAX::GeometryData::Triangulation3Record record;record.Id=model->Triangulations3.size()+1;record.Metadata.Name=visual.Name;record.Geometry=mesh->Mesh;record.Geometry.Normals.clear();
            for(auto& point:record.Geometry.Vertices){const auto x=point.X,y=point.Y,z=point.Z;point={transform(0,0)*x+transform(0,1)*y+transform(0,2)*z+transform(0,3),transform(1,0)*x+transform(1,1)*y+transform(1,2)*z+transform(1,3),transform(2,0)*x+transform(2,1)*y+transform(2,2)*z+transform(2,3)};}
            model->Triangulations3.push_back(std::move(record));
        }
        const auto name=String(source,"name");const auto geometry=Publish(scene,model,"geometry.brep",name,dependencies);
        auto preview=std::make_shared<iCAX::Resource::CFlatBufferResource>(iCAX::RenderInteraction::MakeFrontendBRepGeometry(*model,{},1));
        const auto display=Publish(scene,preview,iCAX::Resource::CFlatBufferResource::kResourceTypeName,name,{geometry});
        Map head{{"kind",Text("Builtin")},{"motionType",Text("Rigid")},{"rotaryAxisCount",0},{"name",name},{"builtinDefinitionId",source.at("id")},{"calibrated",true},
            {"geometryRef",StoredRef(geometry)},{"previewRef",StoredRef(display)},{"tcpLocalPosition",tool->Tool.TcpLocalPosition},{"beamLocalDirection",tool->Tool.BeamLocalDirection}};
        for(const auto& [key,value]:DeclaredBuiltinHeadCollisionMetadata(*tool))head[key]=value;
        return head;
    }
    Map BuiltinHeadDefinition(Scene& scene,const Text& definitionID){
        const auto source=BuiltinHeadSource(scene,definitionID);iCAX::CAM::CMachineDescriptionResource description;Text error;
        if(!iCAX::CAM::LoadMachineDescription(String(source,"sourcePath"),description,error))throw std::runtime_error("内置切割头定义读取失败："+error);
        const auto& tool=BuiltinHeadTool(description);auto head=BuiltinHeadPart(scene,source,tool);
        if(!source.contains("headRoot"))return head;
        std::vector<const iCAX::CAM::SMachineElementData*> chain;const auto* part=&tool;
        for(;;){chain.push_back(part);if(part->OriginalName==String(source,"headRoot"))break;
            const auto parent=std::find_if(description.Elements.begin(),description.Elements.end(),[&](const auto& e){return e.ElementID==part->ParentElementID;});
            if(parent==description.Elements.end()||chain.size()>description.Elements.size())throw std::runtime_error("内置切割头装配边界无效。");part=&*parent;
        }
        std::reverse(chain.begin(),chain.end());Array nodes;
        for(std::size_t i=0;i<chain.size();++i){const auto& e=*chain[i];const auto& j=e.JointToParent;
            const auto body=&e==&tool?head:BuiltinHeadPart(scene,source,e);Array origin=Identity();
            if(i){const auto& p=j.bValid?j.Pose:e.Pose;const auto matrix=iCAX::Transform::MakeLocalMatrix(p[0]*1000,p[1]*1000,p[2]*1000,p[5],p[4],p[3],1,1,1);for(int r=0;r<4;++r)for(int c=0;c<4;++c)origin[r*4+c]=matrix(r,c);}
            nodes.emplace_back(Map{{"name",e.Name},{"parent",static_cast<int>(i)-1},{"origin",origin},{"type",j.bValid?j.JointType:Text("fixed")},{"joint",j.Name},
                {"axis",j.Axis.bValid?j.Axis.XYZ:Array{0.,0.,1.}},{"lower",j.dLowerLimit},{"upper",j.dUpperLimit},{"position",j.dHomePosition},
                {"geometryRef",body.at("geometryRef")},{"previewRef",body.at("previewRef")},{"collisionBoxes",body.at("collisionBoxes")}});
        }
        head["assembly"]=Map{{"kind",source.at("kind")},{"nodes",nodes},{"tool",static_cast<int>(chain.size())-1},{"tcp",tool.Tool.TcpLocalPosition},{"beam",tool.Tool.BeamLocalDirection},{"headAxes",source.at("headAxes")},{"workpieceAxes",source.at("workpieceAxes")}};
        const auto count=source.at("headAxes").To<Array>().size();head["motionType"]=Text(count?"Articulated":"Rigid");head["rotaryAxisCount"]=static_cast<UInt>(count);
        head["requiresMachineSimulation"]=true;
        return head;
    }
}
