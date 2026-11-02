// Product-owned head semantics, using the neutral URDF/SDF description loader.
// Only publication touches the Scene; parsing and mesh imports run on the worker.
std::shared_ptr<ImportResult> PrepareImportedHeadAssembly(
    const std::shared_ptr<iCAX::CAD::ICadBRepImportService>& importer,
    const iCAX::CAD::SCadBRepImportRequest& input)
{
    const auto cancelled=[&]{if(input.IsCancelled&&input.IsCancelled())throw std::runtime_error("CAM_HEAD_IMPORT_CANCELLED");};
    cancelled();auto result=std::make_shared<ImportResult>();result->HeadDescription=std::make_shared<iCAX::CAM::CMachineDescriptionResource>();
    auto& description=*result->HeadDescription;const auto raw=input.SourcePath.u8string();const Text path(raw.begin(),raw.end());
    if(std::filesystem::file_size(input.SourcePath)>8*1024*1024)throw std::invalid_argument("CAM_HEAD_DESCRIPTION_TOO_LARGE");
    std::ifstream source(input.SourcePath,std::ios::binary);if(!source)throw std::invalid_argument("CAM_HEAD_SOURCE_REQUIRED");
    result->Import.SourceBytes.assign(std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>());
    if(result->Import.SourceBytes.size()>8*1024*1024)throw std::invalid_argument("CAM_HEAD_DESCRIPTION_TOO_LARGE");
    Text error;if(!iCAX::CAM::LoadMachineDescription(path,description,error))throw std::invalid_argument("CAM_HEAD_DESCRIPTION_INVALID: "+error);
    std::ifstream reread(input.SourcePath,std::ios::binary);const std::vector<std::uint8_t> verified{std::istreambuf_iterator<char>(reread),std::istreambuf_iterator<char>()};
    if(verified!=result->Import.SourceBytes)throw std::runtime_error("CAM_HEAD_SOURCE_CHANGED");
    const auto fileName=input.SourcePath.filename().u8string();result->Import.DisplayName=Text(fileName.begin(),fileName.end());
    result->Import.FormatId=description.SourceFormat;
    if(description.Elements.empty()||description.Elements.size()>128)throw std::invalid_argument("CAM_HEAD_STRUCTURE_SIZE");
    std::set<Text> parents;std::vector<std::size_t> tips,tools;
    for(const auto& e:description.Elements)if(!e.ParentElementID.empty())parents.insert(e.ParentElementID);
    for(std::size_t i=0;i<description.Elements.size();++i){const auto& e=description.Elements[i];const auto& j=e.JointToParent;
        if(e.Tool.bValid)tools.push_back(i);if(!parents.contains(e.ElementID))tips.push_back(i);
        if(j.bValid&&j.JointType!="fixed"&&j.JointType!="revolute"&&j.JointType!="continuous")throw std::invalid_argument("CAM_HEAD_ROTARY_JOINTS_ONLY: 刀头定义只允许固定和旋转关节；XYZ 机构属于机床。");
        if(j.bValid&&j.JointType!="fixed"){
            if(!j.Axis.bValid||j.Axis.XYZ.size()!=3)throw std::invalid_argument("CAM_HEAD_JOINT_AXIS_REQUIRED");
            double length=0;for(const auto& value:j.Axis.XYZ){const auto x=FiniteNumber(value,"CAM_HEAD_JOINT_AXIS_INVALID");length+=x*x;}if(length<1e-16)throw std::invalid_argument("CAM_HEAD_JOINT_AXIS_INVALID");
            if(!std::isfinite(j.dLowerLimit)||!std::isfinite(j.dUpperLimit)||j.dLowerLimit>j.dUpperLimit)throw std::invalid_argument("CAM_HEAD_JOINT_LIMITS_INVALID");
        }
    }
    if(tools.size()>1||tools.empty()&&tips.size()!=1)throw std::invalid_argument("CAM_HEAD_TOOL_AMBIGUOUS: 请在刀头定义中明确唯一的工具端。");
    result->HeadTool=tools.empty()?tips.front():tools.front();result->HeadCalibrated=!tools.empty();
    auto& tool=description.Elements[result->HeadTool].Tool;
    if(tool.bValid){
        if(tool.TcpLocalPosition.size()!=3||tool.BeamLocalDirection.size()!=3)throw std::invalid_argument("CAM_HEAD_TOOL_FRAME_INVALID");
        double length=0;for(int i=0;i<3;++i){if(std::abs(FiniteNumber(tool.TcpLocalPosition[i],"CAM_HEAD_TCP_RANGE"))>100000)throw std::invalid_argument("CAM_HEAD_TCP_RANGE");const auto value=FiniteNumber(tool.BeamLocalDirection[i],"CAM_HEAD_BEAM_REQUIRED");length+=value*value;}
        if(length<1e-16)throw std::invalid_argument("CAM_HEAD_BEAM_REQUIRED");length=std::sqrt(length);for(auto& value:tool.BeamLocalDirection)value=FiniteNumber(value,"CAM_HEAD_BEAM_REQUIRED")/length;
    }
    result->HeadVisuals.resize(description.Elements.size());std::map<Text,std::shared_ptr<iCAX::CAD::SCadBRepImportResult>> loaded;bool visible=false;
    for(std::size_t i=0;i<description.Elements.size();++i)for(const auto& visual:description.Elements[i].Visuals){
        cancelled();ImportResult::HeadVisual item;item.Declaration=visual;const auto type=String(visual.Geometry,"type");
        if(visual.Geometry.contains("scale")){const auto scale=visual.Geometry.at("scale").To<Array>();if(scale.size()!=3)throw std::invalid_argument("CAM_HEAD_SCALE_INVALID");for(const auto& value:scale)if(FiniteNumber(value,"CAM_HEAD_SCALE_INVALID")<=0)throw std::invalid_argument("CAM_HEAD_SCALE_INVALID");}
        if(type=="mesh"){
            const auto resourcePath=iCAX::CAM::ResolveMachineDescriptionResourcePath(description,String(visual.Geometry,"uri"));
            if(!loaded.contains(resourcePath)){auto request=input;request.SourcePath=std::filesystem::path(std::u8string(resourcePath.begin(),resourcePath.end()));loaded[resourcePath]=std::make_shared<iCAX::CAD::SCadBRepImportResult>(importer->Load(request));}
            item.Geometry=loaded.at(resourcePath);item.UnitScale=item.Geometry->IsValidBRep?1.:1000.;
        }else{
            item.Geometry=std::make_shared<iCAX::CAD::SCadBRepImportResult>();iCAX::GeometryData::Triangulation3Record mesh;mesh.Id=1;mesh.Geometry=iCAX::CAM::TessellateMachinePrimitive(visual.Geometry);item.Geometry->Geometry.Triangulations3.push_back(std::move(mesh));
        }
        result->HeadVisuals[i].push_back(std::move(item));visible=true;
    }
    if(!visible)throw std::invalid_argument("CAM_HEAD_VISUAL_REQUIRED: 刀头定义没有显示外形。");
    cancelled();return result;
}

Map PublishImportedHeadAssembly(Scene& scene,const ID& setupID,ImportResult& result)
{
    const auto& description=*result.HeadDescription;auto& db=scene.Database();const auto setup=Require<CFiveAxisSetupComponent>(db,setupID);const auto job=Require<CFiveAxisJobComponent>(db,setup->GetJobID());
    auto document=std::make_shared<iCAX::Resource::CBinaryResource>();document->DisplayName=result.Import.DisplayName;const auto extension=std::filesystem::path(std::u8string(description.SourcePath.begin(),description.SourcePath.end())).extension().u8string();document->FileExtension.assign(extension.begin(),extension.end());document->Content=std::move(result.Import.SourceBytes);
    const auto sourceRef=Publish(scene,document,"resource.binary",description.ModelName);std::map<const iCAX::CAD::SCadBRepImportResult*,Ref> sources;
    Array nodes,axes;std::map<Text,int> indices;std::vector<std::size_t> order;
    for(std::size_t pass=0;order.size()<description.Elements.size()&&pass<description.Elements.size();++pass)for(std::size_t i=0;i<description.Elements.size();++i){const auto& e=description.Elements[i];if(!indices.contains(e.ElementID)&&(e.ParentElementID.empty()||indices.contains(e.ParentElementID))){indices[e.ElementID]=static_cast<int>(order.size());order.push_back(i);}}
    if(order.size()!=description.Elements.size())throw std::invalid_argument("CAM_HEAD_TREE_INVALID");
    int toolIndex=-1;Map toolGeometryRef,toolPreviewRef;Array toolBoxes;
    for(const auto i:order){const auto& e=description.Elements[i];const auto& joint=e.JointToParent;Array origin=Identity();const auto& p=joint.bValid?joint.Pose:e.Pose;
        const auto matrix=iCAX::Transform::MakeLocalMatrix(p[0]*1000,p[1]*1000,p[2]*1000,p[5],p[4],p[3],1,1,1);for(int r=0;r<4;++r)for(int c=0;c<4;++c)origin[r*4+c]=matrix(r,c);
        auto model=std::make_shared<iCAX::GeometryData::BRepModel>();std::vector<Ref> dependencies{sourceRef};
        for(const auto& visual:result.HeadVisuals[i]){
            if(!visual.Geometry->SourceBytes.empty()){
                const auto key=visual.Geometry.get();if(!sources.contains(key)){auto bytes=std::make_shared<iCAX::Resource::CBinaryResource>();bytes->DisplayName=visual.Geometry->DisplayName;bytes->Content=visual.Geometry->SourceBytes;bytes->Metadata={{"sha256",visual.Geometry->SourceSha256},{"formatId",visual.Geometry->FormatId}};sources[key]=Publish(scene,bytes,"resource.binary",visual.Geometry->DisplayName);}dependencies.push_back(sources.at(key));
            }
            std::array<double,3> scale{visual.UnitScale,visual.UnitScale,visual.UnitScale};if(visual.Declaration.Geometry.contains("scale")){const auto values=visual.Declaration.Geometry.at("scale").To<Array>();if(values.size()!=3)throw std::invalid_argument("CAM_HEAD_SCALE_INVALID");for(int k=0;k<3;++k){const auto value=FiniteNumber(values[k],"CAM_HEAD_SCALE_INVALID");if(value<=0)throw std::invalid_argument("CAM_HEAD_SCALE_INVALID");scale[k]*=value;}}
            const auto& pose=visual.Declaration.Pose;const auto transform=iCAX::Transform::MakeLocalMatrix(pose[0]*1000,pose[1]*1000,pose[2]*1000,pose[5],pose[4],pose[3],scale[0],scale[1],scale[2]);
            for(auto mesh:visual.Geometry->Geometry.Triangulations3){mesh.Id=model->Triangulations3.size()+1;mesh.Geometry.Normals.clear();for(auto& point:mesh.Geometry.Vertices){const auto x=point.X,y=point.Y,z=point.Z;point={transform(0,0)*x+transform(0,1)*y+transform(0,2)*z+transform(0,3),transform(1,0)*x+transform(1,1)*y+transform(1,2)*z+transform(1,3),transform(2,0)*x+transform(2,1)*y+transform(2,2)*z+transform(2,3)};}model->Triangulations3.push_back(std::move(mesh));}
        }
        Map geometryRef,previewRef;if(!model->Triangulations3.empty()){
            const auto geometry=Publish(scene,model,"geometry.brep",e.Name,dependencies);auto preview=std::make_shared<iCAX::Resource::CFlatBufferResource>(iCAX::RenderInteraction::MakeFrontendBRepGeometry(*model,{},1));const auto display=Publish(scene,preview,iCAX::Resource::CFlatBufferResource::kResourceTypeName,e.Name,{geometry});geometryRef=StoredRef(geometry);previewRef=StoredRef(display);
        }
        Array boxes;for(const auto& collision:e.Collisions){Array pose;for(double value:collision.Pose)pose.emplace_back(value);boxes.emplace_back(Map{{"id",collision.CollisionID},{"name",collision.Name},{"enabled",true},{"pose",pose},{"geometry",collision.Geometry}});}
        const auto type=joint.bValid?joint.JointType:Text("fixed");if(type!="fixed")axes.emplace_back(joint.Name);
        nodes.emplace_back(Map{{"name",e.Name},{"parent",e.ParentElementID.empty()?-1:indices.at(e.ParentElementID)},{"origin",origin},{"type",type},{"joint",joint.Name},{"axis",joint.Axis.bValid?joint.Axis.XYZ:Array{0.,0.,1.}},{"lower",joint.dLowerLimit},{"upper",joint.dUpperLimit},{"position",joint.dHomePosition},{"geometryRef",geometryRef},{"previewRef",previewRef},{"collisionBoxes",boxes}});
        if(i==result.HeadTool){toolIndex=static_cast<int>(nodes.size())-1;toolGeometryRef=geometryRef;toolPreviewRef=previewRef;toolBoxes=boxes;}
    }
    const auto& tool=description.Elements[result.HeadTool].Tool;const Array tcp=tool.bValid?tool.TcpLocalPosition:Array{0.,0.,0.},beam=tool.bValid?tool.BeamLocalDirection:Array{0.,0.,-1.};
    const Text motion=axes.empty()?"Rigid":"Articulated";
    Map head{{"kind",Text("Imported")},{"motionType",motion},{"rotaryAxisCount",static_cast<UInt>(axes.size())},{"name",description.ModelName},{"calibrated",result.HeadCalibrated},{"sourceRef",StoredRef(sourceRef)},{"geometryRef",toolGeometryRef},{"previewRef",toolPreviewRef},{"tcpLocalPosition",tcp},{"beamLocalDirection",beam},{"collisionSource",Text("HeadDescription")},{"collisionBoxes",toolBoxes},{"requiresMachineSimulation",!axes.empty()},
        {"assembly",Map{{"kind",motion},{"source",Text("Imported")},{"nodes",nodes},{"tool",toolIndex},{"tcp",tcp},{"beam",beam},{"headAxes",axes},{"workpieceAxes",Array{}}}}};
    // Previewing a module never certifies the rest of an unbound machine or
    // the sweep of all articulated/fixed bodies through controller motion.
    head["requiresMachineSimulation"]=true;
    Transaction(db,"导入切割头机构",[&](auto& tx){AttachProjectHead(scene,tx,setupID,head);});
    return {{"setupId",iCAX::Data::to_string(setupID)}};
}
