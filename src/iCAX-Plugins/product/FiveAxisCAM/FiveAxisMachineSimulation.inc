namespace {
    namespace Motion=iCAX::CAM::Motion;
    struct MachineSnapshot {
        std::vector<Motion::Node> Nodes;Motion::Target Target;Motion::Matrix Placement=Motion::Identity();
        Map Configuration,Postprocessor,HeadMotion;Array CollisionDefinitions;
        std::vector<Ref> ToolGeometryDependencies;
    };
    struct MachineFollower {Text ID;Motion::Matrix Local;};
    Array MachineNumbers(const auto& values){Array result;for(const auto v:values)result.emplace_back(v);return result;}
    std::vector<double> MachineJointValues(const MachineSnapshot& machine){std::vector<double> result;for(const auto& n:machine.Nodes)result.push_back(n.Position);return result;}
    std::vector<MachineFollower> MachineFollowingFrames(Scene& scene,const ID& setup,const Motion::Matrix& placement){
        std::vector<MachineFollower> result;if(setup.is_nil())return result;
        for(const auto& entity:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto p=Component<CFiveAxisPlacementComponent>(entity);p&&p->GetSetupID()==setup)result.push_back({iCAX::Data::to_string(entity->GetID()),Motion::Multiply(placement,RigidMatrix(p->GetTransform()))});
            if(const auto f=Component<CFiveAxisFixtureComponent>(entity);f&&f->GetSetupID()==setup)result.push_back({iCAX::Data::to_string(entity->GetID()),Motion::Multiply(placement,RigidMatrix(f->GetTransform()))});
        }return result;
    }
    std::vector<Motion::Matrix> MachineWorldFrames(const MachineSnapshot& machine,const std::vector<MachineFollower>& followers){
        auto world=Motion::Forward(machine.Nodes,MachineJointValues(machine));for(const auto& f:followers)world.push_back(Motion::Multiply(world.at(machine.Target.Support),f.Local));return world;
    }
    Motion::Matrix MachineLocal(const iCAX::Transform::CTransformComponent& t){
        const auto matrix=iCAX::Transform::MakeLocalMatrix(t.GetPositionX(),t.GetPositionY(),t.GetPositionZ(),t.GetYawRadians(),t.GetPitchRadians(),t.GetRollRadians(),t.GetScaleX(),t.GetScaleY(),t.GetScaleZ());
        Motion::Matrix result{};for(int i=0;i<16;++i)result[i]=matrix(i/4,i%4);(void)RigidMatrix(MachineNumbers(result));return result;
    }
    Map MachineBoxConfiguration(Map configuration){
        const auto representation=String(configuration,"toolCollisionRepresentation");
        if(representation!="OwnMesh"&&representation!="Boxes")throw std::invalid_argument("CAM_MACHINE_TOOL_COLLISION_REPRESENTATION_REQUIRED");
        // Existing projects may still store OwnMesh. The declared editable
        // machine boxes are the only effective tool collision representation.
        configuration["toolCollisionRepresentation"]=Text("Boxes");return configuration;
    }
    MachineSnapshot CaptureMachine(Scene& scene,const Map& inputConfiguration){
        const auto configuration=HeadMachineConfiguration(scene,inputConfiguration);
        MachineSnapshot result;result.Configuration=MachineBoxConfiguration(configuration);auto& db=scene.Database();
        const auto machineID=Identifier(configuration,"machineId");const auto machine=Require<iCAX::CAM::CMachineInstanceComponent>(db,machineID);
        result.Postprocessor=machine->GetPostprocessor();
        if(!machine->GetEnabled())throw std::invalid_argument("CAM_MACHINE_DISABLED: 请先启用机床实例。");
        const auto toolID=Identifier(configuration,"toolId"),supportID=Identifier(configuration,"supportId");
        result.Placement=RigidMatrix(configuration.at("placement").To<Array>());
        result.Target.TCP=RegistrationPoint(configuration.at("tcp"));result.Target.Beam=Motion::Unit(RegistrationPoint(configuration.at("beam")));
        std::vector<std::shared_ptr<iCAX::Database::IEntity>> pending{db.GetEntity(machineID)};
        for(const auto& entity:db.FilterEntities([](auto){return true;}))if(const auto part=Component<iCAX::CAM::CMachineElementComponent>(entity);part&&part->GetMachineID()==machineID)pending.push_back(entity);
        std::map<ID,int> indices;
        while(!pending.empty()){
            bool progress=false;
            for(auto it=pending.begin();it!=pending.end();){const auto entity=*it;const auto t=Require<iCAX::Transform::CTransformComponent>(db,entity->GetID());
                const auto parent=t->GetParentEntityID();if(entity->GetID()!=machineID&&!indices.contains(parent)){++it;continue;}
                Motion::Node n;n.ID=iCAX::Data::to_string(entity->GetID());n.Parent=entity->GetID()==machineID?-1:indices.at(parent);n.Origin=MachineLocal(*t);
                if(entity->GetID()==machineID&&!parent.is_nil())throw std::invalid_argument("CAM_MACHINE_ROOT_PARENT_UNSUPPORTED");
                if(const auto joint=Component<iCAX::CAM::CMachineJointComponent>(entity)){
                    n.Type=joint->GetJointType();n.Position=joint->GetPosition();n.Lower=joint->GetLowerLimit();n.Upper=joint->GetUpperLimit();n.Velocity=joint->GetVelocityLimit();
                    if(n.Type!="fixed")n.Axis=Motion::Unit(RegistrationPoint(Variant(joint->GetAxis())));
                    if(!std::isfinite(n.Lower)||!std::isfinite(n.Upper)||n.Lower>n.Upper||n.Position<n.Lower||n.Position>n.Upper)throw std::invalid_argument("CAM_MACHINE_JOINT_LIMIT: 机床轴位置或限位无效。");
                }
                if(const auto element=Component<iCAX::CAM::CMachineElementComponent>(entity)){
                    const auto& p=element->GetRestPose();if(p.size()!=6)throw std::invalid_argument("CAM_MACHINE_ORIGIN_REQUIRED: 机床缺少持久化关节原点，请从模板重新创建实例。");
                    std::array<double,6> values{};for(int k=0;k<6;++k)values[k]=FiniteNumber(p[k],"CAM_MACHINE_ORIGIN_INVALID");
                    const auto m=iCAX::Transform::MakeLocalMatrix(values[0],values[1],values[2],values[3],values[4],values[5],1,1,1);for(int k=0;k<16;++k)n.Origin[k]=m(k/4,k%4);
                }
                const auto index=static_cast<int>(result.Nodes.size());indices.emplace(entity->GetID(),index);
                if(const auto collision=Component<iCAX::CAM::CMachineCollisionComponent>(entity))result.CollisionDefinitions.emplace_back(Map{{"id",n.ID},{"geometry",collision->GetCollisions()}});
                if(entity->GetID()==toolID)result.Target.Tool=index;if(entity->GetID()==supportID)result.Target.Support=index;
                result.Nodes.push_back(n);it=pending.erase(it);progress=true;
            }
            if(!progress)throw std::invalid_argument("CAM_MACHINE_TREE_INVALID: 机床结构存在循环或缺失父节点。");
        }
        if(result.Target.Tool<0||result.Target.Support<0||toolID==supportID)throw std::invalid_argument("CAM_MACHINE_LINK_REQUIRED: 请选择该实例内不同的工作台与刀具部件。");
        return result;
    }
    Map MachineSnapshotState(const MachineSnapshot& machine){
        Array nodes,positions;
        for(const auto& n:machine.Nodes){nodes.emplace_back(Map{{"id",n.ID},{"parent",n.Parent},{"type",n.Type},{"origin",MachineNumbers(n.Origin)},{"axis",MachineNumbers(n.Axis)},{"lower",n.Lower},{"upper",n.Upper},{"velocity",n.Velocity}});positions.emplace_back(n.Position);}
        Map result{{"postprocessor",machine.Postprocessor},{"configuration",machine.Configuration},{"nodes",nodes},{"positions",positions},{"tool",machine.Target.Tool},{"support",machine.Target.Support},{"collisionDefinitions",machine.CollisionDefinitions},
            {"collisionAlgorithm",Text("MachineDeclaredBoxesV5")},{"collisionScope",Text("MachineDeclaredBoxesAndWorkpieceFixtureMesh")},
            {"collisionRepresentation",Text("MachineBoxes; WorkpieceFixtureOwnMesh")},
            {"toolCollisionSource",Text("MachineDeclaredCollisionBoxes")},{"meshDeflectionMm",CollisionMeshDeflectionMm}};
        if(!machine.HeadMotion.empty()){result["independentHead"]=true;result["headNodes"]=machine.HeadMotion.at("headNodes");}
        result["modelKey"]=iCAX::Data::VariantSerializer::Serialize(result);return result;
    }
    MachineSnapshot CaptureSetupMachine(Scene& scene,const CFiveAxisSetupComponent& setup){
        if(!setup.GetMachineSetup().empty())return CaptureMachine(scene,setup.GetMachineSetup());
        const auto head=HeadDefinition(scene,setup);
        if(!head.contains("motion"))throw std::invalid_argument("CAM_HEAD_INSTANCE_REQUIRED: 请先设置切割头。");
        MachineSnapshot result;result.HeadMotion=head.at("motion").To<Map>();
        result.Configuration={{"placement",HeadNumbers(Motion::Identity())},{"headInstanceId",iCAX::Data::to_string(setup.GetHeadInstanceID())},{"tcp",head.at("tcpLocalPosition")},{"beam",head.at("beamLocalDirection")}};
        result.Target.Tool=result.HeadMotion.at("tool").To<int>();result.Target.Support=result.HeadMotion.at("support").To<int>();
        result.Target.TCP=HeadParameterVector(head.at("tcpLocalPosition"));result.Target.Beam=HeadParameterVector(head.at("beamLocalDirection"),true);
        result.CollisionDefinitions=result.HeadMotion.at("collisionDefinitions").To<Array>();
        for(const auto& value:result.HeadMotion.at("nodes").To<Array>()){
            const auto v=value.To<Map>();Motion::Node n;n.ID=String(v,"id");n.Parent=v.at("parent").To<int>();n.Type=String(v,"type");
            n.Origin=RigidMatrix(v.at("origin").To<Array>());n.Axis=HeadParameterVector(v.at("axis"),true);
            n.Lower=v.at("lower").To<double>();n.Upper=v.at("upper").To<double>();n.Position=v.at("position").To<double>();n.Velocity=v.at("velocity").To<double>();result.Nodes.push_back(n);
        }
        // Start clear of the programming origin; all subsequent poses are
        // solved relative to the moving support, including the initial approach.
        const auto world=Motion::Forward(result.Nodes,MachineJointValues(result));
        const auto tcp=Motion::Transform(world.at(result.Target.Tool),result.Target.TCP);
        result.Nodes.at(3).Position=500-tcp[2];
        return result;
    }
}
#include "FiveAxisMachineCollision.inc"
#include "FiveAxisMachinePlacement.inc"
Map ConfigureMachineSimulation(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","configuration","placementPivot","placementRotation"});
    return RapidMutation(scene,request,"ConfigureMachine:",[&](const ID& id,const auto& setup){
        RequireSetupUnlocked(scene,id);auto configuration=request.at("configuration").To<Map>();
        CutFields(configuration,{"machineId","toolId","supportId","placement","tcp","beam","toolCollisionRepresentation","headInstanceId"});configuration.erase("headInstanceId");
        const auto snapshot=CaptureMachine(scene,configuration);
        configuration=snapshot.Configuration;
        configuration["beam"]=MachineNumbers(snapshot.Target.Beam);
        const auto previous=setup->GetMachineSetup();
        const bool same=!previous.empty()&&String(previous,"machineId")==String(configuration,"machineId")&&String(previous,"supportId")==String(configuration,"supportId");
        ValidateMachinePlacement(scene,id,snapshot,same?RigidMatrix(previous.at("placement").To<Array>()):snapshot.Placement,request.contains("placementPivot")?RegistrationPoint(request.at("placementPivot")):Motion::Vector{},request.contains("placementRotation")?request.at("placementRotation").To<Map>():Map{});
        const auto undo=scene.Database().BeginUndoCommand("设置机床与工件摆放");
        Transaction(scene.Database(),"设置机床与工件摆放",[&](auto& tx){AttachBoundProjectHead(scene,tx,id,configuration);tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"MachineSetup",configuration}});},false);
        ProjectSetupPlacements(scene,id);undo->End();
        return Map{{"configuration",configuration}};
    });
}
Map SetMachinePlacement(Scene& scene,const Map& request){
    CutFields(request,{"commandId","expected","setupId","placement"});
    return RapidMutation(scene,request,"SetMachinePlacement:",[&](const ID& id,const auto& setup){
        RequireSetupUnlocked(scene,id);auto configuration=setup->GetMachineSetup();
        if(configuration.empty())throw std::invalid_argument("CAM_MACHINE_SETUP_REQUIRED");
        const bool representationChanged=String(configuration,"toolCollisionRepresentation")!="Boxes";
        configuration=MachineBoxConfiguration(std::move(configuration));
        const auto placement=RigidMatrix(request.at("placement").To<Array>());
        const bool changed=representationChanged||configuration.at("placement")!=request.at("placement");
        if(changed){
            configuration["placement"]=MachineNumbers(placement);
            const auto undo=scene.Database().BeginUndoCommand("调整工件与工装摆放");
            Transaction(scene.Database(),"调整工件与工装摆放",[&](auto& tx){tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"MachineSetup",configuration}});},false);
            ProjectSetupPlacements(scene,id);undo->End();
        }
        // This is an engineering placement edit, not simulated machine motion.
        // It invalidates previous checks; collision work is explicitly requested.
        const auto runtime=Runtime(scene);
        return Map{{"changed",changed},{"configuration",configuration},{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}},{"collisionStatus",Text("Unchecked")}};
    });
}
Map MountMachine(Scene& scene,const Map& request){
    return MountMachineWithTemplates(scene,request,{});
}
Map MountMachineWithTemplates(Scene& scene,const Map& request,const Map& templateSources){
    CutFields(request,{"commandId","expected","setupId","machineId","configuration"});
    return RapidMutation(scene,request,"MountMachine:",[&](const ID& id,const auto& setup){
        RequireSetupUnlocked(scene,id);SetupIdle(scene);
        const auto machineID=Identifier(request,"machineId");
        const auto instance=Require<iCAX::CAM::CMachineInstanceComponent>(scene.Database(),machineID);
        const bool explicitConfiguration=request.contains("configuration");
        auto configuration=explicitConfiguration?request.at("configuration").To<Map>():HeadMachineConfiguration(scene,setup->GetMachineSetup());
        if(explicitConfiguration){
            CutFields(configuration,{"machineId","toolId","supportId","placement","tcp","beam","toolCollisionRepresentation","headInstanceId"});configuration.erase("headInstanceId");
            if(String(configuration,"machineId")!=iCAX::Data::to_string(machineID))throw std::invalid_argument("CAM_MACHINE_LINK_REQUIRED: 请选择该实例内不同的工作台与刀具部件。");
        }
        const bool configured=explicitConfiguration||(!configuration.empty()&&String(configuration,"machineId")==iCAX::Data::to_string(machineID));
        if(!configured){
            std::shared_ptr<iCAX::Database::IEntity> toolEntity,supportEntity;iCAX::CAM::SMachineToolData recoveredTool;
            for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
                const auto element=Component<iCAX::CAM::CMachineElementComponent>(e);
                if(!element||element->GetMachineID()!=machineID)continue;
                if(Component<iCAX::CAM::CMachineToolComponent>(e)){
                    if(toolEntity)throw std::invalid_argument("CAM_MACHINE_ENDPOINT_AMBIGUOUS: 机床有多个工具端，请手动选择。");
                    toolEntity=e;
                }
                if(element->GetWorkSupport()){
                    if(supportEntity)throw std::invalid_argument("CAM_MACHINE_ENDPOINT_AMBIGUOUS: 机床有多个工作台，请手动选择。");
                    supportEntity=e;
                }
            }
            // Old instances and managed template copies may predate both role
            // markers. Recover declarations by stable definition ID, original
            // source index/identity and unchanged local frame, without replacing
            // any instance geometry, joints or edited collision boxes.
            std::vector<Text> sources;if(!instance->GetSourcePath().empty())sources.push_back(instance->GetSourcePath());
            if(templateSources.contains(instance->GetMachineDefinitionID())){
                const auto& paths=templateSources.at(instance->GetMachineDefinitionID());
                if(paths.Is<Array>())for(const auto& path:paths.To<Array>())sources.push_back(path.To<Text>());else sources.push_back(paths.To<Text>());
            }
            for(const auto& sourcePath:sources){
                if(toolEntity&&supportEntity)break;
                iCAX::CAM::CMachineDescriptionResource description;Text error;
                if(iCAX::CAM::LoadMachineDescription(sourcePath,description,error)){
                    // A compatibility declaration describes a complete original
                    // kinematic tree. Every part frame and parent must still
                    // match; a partial match cannot borrow another version's TCP.
                    bool compatible=true;
                    for(std::size_t index=0;index<description.Elements.size()&&compatible;++index){
                        const auto& source=description.Elements[index];if(source.ElementKind!="part")continue;
                        std::shared_ptr<iCAX::Database::IEntity> match;
                        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
                            const auto element=Component<iCAX::CAM::CMachineElementComponent>(e);const auto link=Component<iCAX::CAM::CMachineLinkComponent>(e);
                            if(element&&link&&element->GetMachineID()==machineID&&element->GetSourceIndex()==index&&link->GetLinkName()==source.OriginalName){match=e;break;}
                        }
                        if(!match){compatible=false;break;}
                        const auto element=Component<iCAX::CAM::CMachineElementComponent>(match);const auto pose=source.JointToParent.bValid?source.JointToParent.Pose:source.Pose;
                        const auto rest=element->GetRestPose();const std::array<double,6> expected{pose[0]*1000,pose[1]*1000,pose[2]*1000,pose[5],pose[4],pose[3]};
                        if(rest.size()!=6){compatible=false;break;}for(int k=0;k<6;++k)if(std::abs(FiniteNumber(rest[k],"CAM_MACHINE_ORIGIN_INVALID")-expected[k])>1e-6)compatible=false;
                        if(!source.ParentElementID.empty()){
                            const auto parentSource=std::find_if(description.Elements.begin(),description.Elements.end(),[&](const auto& p){return p.ElementID==source.ParentElementID;});
                            if(parentSource!=description.Elements.end()&&parentSource->ElementKind=="part"){
                                const auto transform=Component<iCAX::Transform::CTransformComponent>(match);
                                const auto parent=transform?scene.Database().GetEntity(transform->GetParentEntityID()):nullptr;const auto parentElement=Component<iCAX::CAM::CMachineElementComponent>(parent);
                                if(!parentElement||parentElement->GetSourceIndex()!=std::distance(description.Elements.begin(),parentSource))compatible=false;
                            }
                        }
                    }
                    if(!compatible)continue;
                    const bool needTool=!toolEntity,needSupport=!supportEntity;
                    for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
                        const auto element=Component<iCAX::CAM::CMachineElementComponent>(e);const auto link=Component<iCAX::CAM::CMachineLinkComponent>(e);
                        if(!element||!link||element->GetMachineID()!=machineID)continue;
                        if(element->GetSourceIndex()>=description.Elements.size())continue;
                        const auto& source=description.Elements[element->GetSourceIndex()];
                        if(source.ElementKind!="part"||source.OriginalName!=link->GetLinkName())continue;
                        if(!(needSupport&&source.bWorkSupport)&&!(needTool&&source.Tool.bValid))continue;
                        const auto pose=source.JointToParent.bValid?source.JointToParent.Pose:source.Pose;const auto rest=element->GetRestPose();
                        const std::array<double,6> expected{pose[0]*1000,pose[1]*1000,pose[2]*1000,pose[5],pose[4],pose[3]};bool matching=rest.size()==6;
                        if(matching)for(int k=0;k<6;++k)if(std::abs(FiniteNumber(rest[k],"CAM_MACHINE_ORIGIN_INVALID")-expected[k])>1e-6)matching=false;
                        if(!matching)continue;
                        if(needSupport&&source.bWorkSupport){if(supportEntity)throw std::invalid_argument("CAM_MACHINE_ENDPOINT_AMBIGUOUS: 机床源文件声明了多个工作台，请手动选择。");supportEntity=e;}
                        if(needTool&&source.Tool.bValid){if(toolEntity)throw std::invalid_argument("CAM_MACHINE_ENDPOINT_AMBIGUOUS: 机床源文件声明了多个工具端，请手动选择。");toolEntity=e;recoveredTool=source.Tool;}
                    }
                }
            }
            if(!toolEntity||!supportEntity)throw std::invalid_argument("CAM_MACHINE_ENDPOINT_METADATA_REQUIRED: 请在右侧选择工作台和刀具部件并确认 TCP、光束方向，或从已声明端点的模板创建机床实例。");
            const auto tool=Component<iCAX::CAM::CMachineToolComponent>(toolEntity);
            configuration={{"machineId",iCAX::Data::to_string(machineID)},{"toolId",iCAX::Data::to_string(toolEntity->GetID())},
                {"supportId",iCAX::Data::to_string(supportEntity->GetID())},{"placement",MachineNumbers(Motion::Identity())},
                {"tcp",tool?tool->GetTcpLocalPosition():recoveredTool.TcpLocalPosition},{"beam",tool?tool->GetBeamLocalDirection():recoveredTool.BeamLocalDirection},
                {"toolCollisionRepresentation",Text("Boxes")}};
        }
        const auto machine=CaptureMachine(scene,configuration);
        configuration=machine.Configuration;
        const auto supportEntity=scene.Database().GetEntity(Identifier(configuration,"supportId"));
        const auto service=scene.Services().Resolve<Collision::ICollisionDistanceService>();
        if(!service)throw std::runtime_error("CAM_MACHINE_COLLISION_SERVICE_REQUIRED");
        const auto table=PrepareMachineBody(scene,machine,machine.Target.Support,*service);
        if(!table.Model||!std::isfinite(table.Max[2]))throw std::invalid_argument("CAM_MACHINE_SUPPORT_SURFACE_REQUIRED: 工作台须有有效的碰撞包络。");
        // Automatic mounting supports a horizontal, authored BoxCollider top.
        // A rotated table requires an explicit user-defined registration.
        const auto collider=Component<iCAX::CAM::CMachineCollisionComponent>(supportEntity);
        if(!collider)throw std::invalid_argument("CAM_MACHINE_SUPPORT_SURFACE_REQUIRED: 工作台须有有效的碰撞包络。");
        for(const auto& value:collider->GetCollisions()){
            const auto attachment=value.To<Map>();if(attachment.contains("enabled")&&!attachment.at("enabled").To<bool>())continue;
            const auto pose=attachment.at("pose").To<Array>();
            if(std::abs(std::sin(pose[3].To<double>()))>1e-9||std::abs(std::sin(pose[4].To<double>()))>1e-9)
                throw std::invalid_argument("CAM_MACHINE_SUPPORT_REGISTRATION_REQUIRED: 倾斜工作台请手动设置装夹坐标。");
        }
        auto orientation=Motion::Identity();Map mountingBasis;Motion::Vector mountingOrigin{};
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
            const auto fixture=Component<CFiveAxisFixtureComponent>(e);
            if(!fixture||fixture->GetSetupID()!=id||!fixture->GetEnabled())continue;
            const auto metadata=fixture->GetImportMetadata();if(!metadata.contains("mountingFrame"))continue;
            if(!mountingBasis.empty())throw std::invalid_argument("CAM_MACHINE_MOUNT_DATUM_AMBIGUOUS: 有多个工装安装基准，请手动设置装夹坐标。");
            const auto frame=Motion::Multiply(RigidMatrix(fixture->GetTransform()),RigidMatrix(metadata.at("mountingFrame").To<Array>()));
            orientation=Motion::Inverse(frame);orientation[3]=orientation[7]=orientation[11]=0;
            mountingOrigin=Motion::Transform(orientation,{frame[3],frame[7],frame[11]});
            mountingBasis={{"fixtureId",iCAX::Data::to_string(e->GetID())},{"frame",MachineNumbers(frame)}};
        }
        Motion::Vector lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};Array included;std::size_t workpieces=0;SimulationCollisionSnapshot mountObstacles;
        const auto extend=[&](const ID& entityID,const Map& geometry,const Array& transform,bool collisionEnabled=true){
            const auto mesh=CollisionMeshFor(scene,geometry);const auto matrix=RigidMatrix(transform);
            if(!mesh||mesh->Vertices.empty())throw std::invalid_argument("CAM_MACHINE_MOUNT_GEOMETRY_REQUIRED");
            const auto aligned=Motion::Multiply(orientation,matrix);
            for(const auto& vertex:mesh->Vertices){const auto point=Motion::Transform(aligned,vertex);for(int k=0;k<3;++k){lo[k]=std::min(lo[k],point[k]);hi[k]=std::max(hi[k],point[k]);}}
            included.emplace_back(iCAX::Data::to_string(entityID));
            if(collisionEnabled)mountObstacles.Query.Obstacles.push_back(CollisionObstacle(scene,iCAX::Data::to_string(entityID),geometry,matrix));
        };
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==id){
                const auto workpiece=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
                if(workpiece->GetEnabled()){extend(e->GetID(),workpiece->GetGeometryRef(),p->GetTransform());++workpieces;}
            }
            if(const auto fixture=Component<CFiveAxisFixtureComponent>(e);fixture&&fixture->GetSetupID()==id&&fixture->GetEnabled())
                extend(e->GetID(),fixture->GetGeometryRef(),fixture->GetTransform(),fixture->GetCollisionEnabled());
        }
        if(!workpieces)throw std::invalid_argument("CAM_MACHINE_MOUNT_WORKPIECE_REQUIRED: 请先启用要装夹的工件。");
        // Do not scale the part or silently omit fixtures to fit the table.
        for(int k=0;k<2;++k)if(hi[k]-lo[k]>table.Max[k]-table.Min[k]+.01)
            throw std::invalid_argument("CAM_MACHINE_MOUNT_EXCEEDS_TABLE: 工件与夹具的外形超出工作台，请调整装夹姿态。");
        auto placement=orientation;
        for(int k=0;k<2;++k)placement[k*4+3]=(table.Min[k]+table.Max[k])/2-(mountingBasis.empty()?(lo[k]+hi[k])/2:mountingOrigin[k]);
        placement[11]=table.Max[2]-(mountingBasis.empty()?lo[2]:mountingOrigin[2]);
        if(!mountingBasis.empty()&&lo[2]+placement[11]<table.Max[2]-.01)
            throw std::invalid_argument("CAM_MACHINE_MOUNT_BELOW_DATUM: 工件或其他夹具低于工装安装底面，请调整装夹姿态。");
        for(int k=0;k<2;++k)if(lo[k]+placement[k*4+3]<table.Min[k]-.01||hi[k]+placement[k*4+3]>table.Max[k]+.01)
            throw std::invalid_argument("CAM_MACHINE_MOUNT_EXCEEDS_TABLE: 按工装安装基准对正后，工件与夹具超出工作台，请调整装夹姿态。");
        configuration["placement"]=MachineNumbers(placement);
        configuration["beam"]=MachineNumbers(machine.Target.Beam);
        const auto mounted=CaptureMachine(scene,configuration);
        ValidateMachinePlacement(scene,id,mounted,mounted.Placement);
        Map mountCheck{{"status",Text("Ready")},{"frames",Array{Map{{"joints",MachineNumbers(MachineJointValues(mounted))}}}}};
        CheckMachineCollision(scene,mounted,mountObstacles,mountCheck);
        if(String(mountCheck,"collisionStatus")!="Clear")
            throw std::invalid_argument("CAM_MACHINE_MOUNT_INITIAL_POSE_BLOCKED: 装夹后当前轴姿态不安全。请先在未装夹时将机床移到安全装料位。"+String(mountCheck,"reason"));
        const auto undo=scene.Database().BeginUndoCommand("将工件与夹具放到工作台");
        Transaction(scene.Database(),"将工件与夹具放到工作台",[&](auto& tx){AttachBoundProjectHead(scene,tx,id,configuration);tx.ModifyComponent(id,CFiveAxisSetupComponent::S_ClassName,{{"MachineSetup",configuration}});},false);
        ProjectSetupPlacements(scene,id);undo->End();
        return Map{{"configuration",configuration},{"mounting",Map{
            {"source",Text(explicitConfiguration?"ExplicitMachineEndpointsAndOwnMeshBounds":configured?"ConfiguredMachineEndpointsAndOwnMeshBounds":"AuthoredMachineEndpointsAndOwnMeshBounds")},{"includedEntityIds",included},
            {"mountingBasis",mountingBasis},
            {"setupBounds",Map{{"min",MachineNumbers(lo)},{"max",MachineNumbers(hi)}}},
            {"supportBounds",Map{{"min",MachineNumbers(table.Min)},{"max",MachineNumbers(table.Max)}}},
            {"surfaceZMm",table.Max[2]},{"clearanceMm",0.},{"followHeightMm",RapidSettings(*setup).at("followHeightMm")},
            {"toolTCPSource",Text(explicitConfiguration?"ExplicitToolTCP":configured?"ConfiguredToolTCP":"AuthoredPhysicalNozzle")},{"calibrated",false},{"collisionStatus",Text("Clear")},
            {"initialAxisPositions",MachineNumbers(MachineJointValues(mounted))},{"qualification",Text("Authored loading pose; machine BoxCollider and own mesh checked; physical calibration required")}}}};
    });
}
Map ReadMachineSimulation(Scene& scene,const Map& request){
    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),Identifier(request,"setupId"));
    if(setup->GetMachineSetup().empty()&&setup->GetHeadInstanceID().is_nil())return {{"configuration",Map{}},{"configured",false}};
    auto result=MachineSnapshotState(CaptureSetupMachine(scene,*setup));result["configured"]=true;return result;
}
static std::pair<ID,Map> MachineMoveConfiguration(Scene& scene,const Map& request){
    ID id;Map configuration;
    if(request.contains("setupId")){id=Identifier(request,"setupId");configuration=Require<CFiveAxisSetupComponent>(scene.Database(),id)->GetMachineSetup();}
    else {const auto machineID=Identifier(request,"machineId");Require<iCAX::CAM::CMachineInstanceComponent>(scene.Database(),machineID);Text tool;
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;}))if(const auto p=Component<iCAX::CAM::CMachineElementComponent>(e);p&&p->GetMachineID()==machineID){tool=iCAX::Data::to_string(e->GetID());break;}
        if(tool.empty())throw std::invalid_argument("机床没有可移动部件。");
        configuration={{"machineId",request.at("machineId")},{"supportId",request.at("machineId")},{"toolId",tool},{"placement",MachineNumbers(Motion::Identity())},{"tcp",Array{0.,0.,0.}},{"beam",Array{0.,0.,-1.}},{"toolCollisionRepresentation",Text("Boxes")}};
    }
    return {id,configuration};
}
static SimulationCollisionSnapshot MachineMoveObstacles(Scene& scene,const ID& id){
    SimulationCollisionSnapshot input;
    for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
        if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==id){const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());
            input.Query.Obstacles.push_back(CollisionObstacle(scene,iCAX::Data::to_string(e->GetID()),w->GetGeometryRef(),RigidMatrix(p->GetTransform())));}
        if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetSetupID()==id&&f->GetEnabled()&&f->GetCollisionEnabled())
            input.Query.Obstacles.push_back(CollisionObstacle(scene,iCAX::Data::to_string(e->GetID()),f->GetGeometryRef(),RigidMatrix(f->GetTransform())));
    }
    return input;
}
static Map MachineMove(Scene& scene,const Map& request,bool commit){
    CutFields(request,{"expected","setupId","machineId","positions"});CheckExpected(*Runtime(scene),request);
    const auto [id,configuration]=MachineMoveConfiguration(scene,request);
    const auto machine=CaptureMachine(scene,configuration);auto result=commit?Map{}:MachineSnapshotState(machine);
    const auto edits=request.at("positions").To<Map>();std::vector<double> positions;size_t found=0;
    for(const auto& n:machine.Nodes){double q=n.Position;if(edits.contains(n.ID)){++found;q=FiniteNumber(edits.at(n.ID),"CAM_MACHINE_POSITION_INVALID");
        if(n.Type=="fixed"||q<n.Lower||q>n.Upper)throw std::invalid_argument("CAM_MACHINE_JOINT_LIMIT: 目标轴位置超出机床行程。");}positions.push_back(q);}
    if(found!=edits.size()||edits.empty())throw std::invalid_argument("CAM_MACHINE_JOINT_REQUIRED");
    auto input=MachineMoveObstacles(scene,id);
    result["status"]=Text("Ready");result["frames"]=Array{Variant(Map{{"joints",MachineNumbers(positions)}})};
    CheckMachineCollision(scene,machine,input,result);
    if(commit&&String(result,"status")=="Ready"&&String(result,"collisionStatus")=="Clear"){
        // Check and commit run in one scene dispatch. No unchecked interval
        // between two bridge calls, and only the edited local joints mutate.
        Transaction(scene.Database(),"机床点动",[&](auto& tx){for(const auto& [key,value]:edits)
            tx.ModifyComponent(ID::from_string(key).value(),iCAX::CAM::CMachineJointComponent::S_ClassName,{{"Position",FiniteNumber(value,"CAM_MACHINE_POSITION_INVALID")}});
        });
        for(const auto& [key,value]:edits)iCAX::CAM::SynchronizeMachineElementPose(*scene.Database().GetEntity(ID::from_string(key).value()));
        result["positions"]=edits;
    }
    if(commit){result.erase("frames");result.erase("allowedContacts");}return result;
}
Map CheckMachineMove(Scene& scene,const Map& request){return MachineMove(scene,request,false);}
Map InspectMachineCollision(Scene& scene,const Map& request){
    CutFields(request,{"expected","setupId","positions","firstId","secondId"});CheckExpected(*Runtime(scene),request);
    const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    const auto machine=CaptureMachine(scene,setup->GetMachineSetup());
    const auto raw=request.at("positions").To<Array>();if(raw.size()!=machine.Nodes.size())throw std::invalid_argument("CAM_MACHINE_POSITION_INVALID");
    std::vector<double> positions;for(const auto& value:raw)positions.push_back(FiniteNumber(value,"CAM_MACHINE_POSITION_INVALID"));
    const auto collision=PrepareMachineCollision(scene,machine,MachineMoveObstacles(scene,setupID));
    const auto find=[&](const Text& id)->const MachineCollisionBody&{for(const auto& body:collision.Bodies)if(body.ID==id)return body;throw std::invalid_argument("CAM_MACHINE_COLLISION_BODY_MISSING");};
    const auto& first=find(String(request,"firstId"));const auto& second=find(String(request,"secondId"));
    const auto world=Motion::Forward(machine.Nodes,positions);
    const auto fixed=Motion::Multiply(world.at(first.Node),first.Local),moving=Motion::Multiply(world.at(second.Node),second.Local);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    const auto separated=collision.Service->Separation(first.Model,second.Model,Motion::Multiply(Motion::Inverse(fixed),moving),[&]{return scene.IsStopRequested()||std::chrono::steady_clock::now()>=deadline;});
    Map result{{"firstName",first.Name},{"secondName",second.Name},{"frame",Text("MachineWorld")},{"lowerBoundMm",separated.LowerBound},{"upperBoundMm",separated.UpperBound},{"overlapping",separated.Overlapping},
        {"tcpWorld",MachineNumbers(Motion::Transform(world.at(machine.Target.Tool),machine.Target.TCP))}};
    if(separated.Witness){const auto& witness=*separated.Witness;result["witness"]=Map{{"firstPoint",MachineNumbers(Motion::Transform(fixed,witness.PointOnFixed))},{"secondPoint",MachineNumbers(Motion::Transform(fixed,witness.PointOnMoving))},
        {"separationMm",witness.Separation},{"surfaceDistanceMm",witness.SurfaceDistance},{"containment",witness.Containment}};}
    return result;
}
Map MoveMachine(Scene& scene,const Map& request){
    if(Runtime(scene)->JogTick)throw std::invalid_argument("请先停止连续点动。");
    return MachineMove(scene,request,true);
}
#include "FiveAxisInteractiveIK.inc"
#include "FiveAxisMachineJog.inc"
Map PrepareMachines(Scene& scene,const Map& request){
    CutFields(request,{"machineIds"});Array prepared;
    for(const auto& value:request.at("machineIds").To<Array>()){
      try {
        const auto machineId=value.To<Text>();const auto runtime=Runtime(scene);
        Map check{{"machineId",machineId},{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}}};
        Map positions;
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto setup=Component<CFiveAxisSetupComponent>(e);setup&&String(setup->GetMachineSetup(),"machineId")==machineId){check.erase("machineId");check["setupId"]=iCAX::Data::to_string(e->GetID());}
            if(const auto part=Component<iCAX::CAM::CMachineElementComponent>(e);part&&iCAX::Data::to_string(part->GetMachineID())==machineId)
                if(const auto joint=Component<iCAX::CAM::CMachineJointComponent>(e);joint&&joint->GetJointType()!="fixed")positions[iCAX::Data::to_string(e->GetID())]=joint->GetPosition();
        }
        if(positions.empty())continue;check["positions"]=positions;
        auto result=MachineMove(scene,check,false);prepared.emplace_back(Map{{"machineId",machineId},{"collisionStatus",result.at("collisionStatus")}});
      }catch(const std::exception& error){prepared.emplace_back(Map{{"machineId",value},{"collisionStatus",Text("Unchecked")},{"reason",Text(error.what())}});}
    }
    return {{"machines",prepared}};
}
#include "FiveAxisMachinePlanning.inc"
