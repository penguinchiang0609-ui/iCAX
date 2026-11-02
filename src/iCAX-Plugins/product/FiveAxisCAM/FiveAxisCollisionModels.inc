// Product-owned collision representation. The neutral checker receives only
// box compounds and the supplied triangle meshes, never a cutting-head BRep.
#include "FiveAxisCollisionMeshDiskCache.inc"
namespace {
    constexpr double CollisionMeshDeflectionMm=.01;
    using HeadBox=iCAX::GeometryServices::CollisionBox;
    using HeadVector=iCAX::GeometryServices::CollisionVector;
    void RememberCollisionMesh(SceneRuntime& runtime,const Text& key,const std::shared_ptr<const iCAX::GeometryServices::CollisionMesh>& mesh){
        constexpr std::size_t capacity=128;
        if(runtime.CollisionMeshes.size()>=capacity){
            const auto oldest=std::min_element(runtime.CollisionMeshes.begin(),runtime.CollisionMeshes.end(),[](const auto& a,const auto& b){return a.second.LastUsed<b.second.LastUsed;});
            runtime.CollisionMeshes.erase(oldest);
        }
        runtime.CollisionMeshes.emplace(key,SceneRuntime::CollisionMeshCacheEntry{mesh,++runtime.CollisionMeshClock});
    }
    std::vector<iCAX::GeometryServices::AxialSolid> BuiltinHeadSolids(){return {{0,16,0,7},{16,16,7,11},{32,24,12,12}};}
    std::vector<HeadBox> BuiltinHeadCollisionBoxes(){
        std::vector<HeadBox> boxes;
        for(const auto& solid:BuiltinHeadSolids()){
            // Fine axial slabs preserve the small nozzle next to the TCP;
            // a single body-wide box would fill the useful nozzle clearance.
            const unsigned count=solid.LowerRadius==solid.UpperRadius?1:solid.Z==0?16:8;
            for(unsigned i=0;i<count;++i){const double a=static_cast<double>(i)/count,b=static_cast<double>(i+1)/count;
                const auto radius=std::max(solid.LowerRadius+(solid.UpperRadius-solid.LowerRadius)*a,solid.LowerRadius+(solid.UpperRadius-solid.LowerRadius)*b);
                HeadBox box;box.Center={0,0,solid.Z+solid.Height*(a+b)/2};box.HalfExtents={radius,radius,solid.Height*(b-a)/2};boxes.push_back(box);
            }
        }return boxes;
    }
    std::shared_ptr<const iCAX::GeometryServices::CollisionMesh> CollisionMeshFor(Scene& scene,const Map& reference){
        const auto key=iCAX::Data::VariantSerializer::Serialize(Map{{"resource",WireRef(reference)},{"deflectionMm",CollisionMeshDeflectionMm},{"algorithm",Text("TriangleMeshV1")}});
        auto runtime=Runtime(scene);if(const auto found=runtime->CollisionMeshes.find(key);found!=runtime->CollisionMeshes.end()){
            ++runtime->CollisionMeshHits;found->second.LastUsed=++runtime->CollisionMeshClock;return found->second.Mesh;
        }
        ++runtime->CollisionMeshMisses;const auto prepareStarted=std::chrono::steady_clock::now();
        const auto geometry=FixtureGeometry(scene,reference);const auto diskKey=CollisionMeshDiskKey(scene,reference,*geometry,CollisionMeshDeflectionMm);
        if(const auto mesh=ReadCollisionMeshDiskCache(diskKey)){
            ++runtime->CollisionMeshDiskHits;RememberCollisionMesh(*runtime,key,mesh);
            runtime->CollisionMeshPreparationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-prepareStarted).count();return mesh;
        }
        ++runtime->CollisionMeshDiskMisses;const auto service=scene.Services().Resolve<iCAX::GeometryServices::IBRepDisplayService>();
        if(!service)throw std::runtime_error("CAM_COLLISION_MESH_SERVICE_REQUIRED");
        iCAX::GeometryServices::BRepDisplayOptions options;options.Deflection=CollisionMeshDeflectionMm;
        // Workpiece and fixture triangle inputs retain their authored vertices.
        const auto refinementStarted=std::chrono::steady_clock::now();const auto refined=geometry->Faces.empty()?*geometry:service->RefineMesh(*geometry,options);
        if(!geometry->Faces.empty())runtime->CollisionMeshRefinementMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-refinementStarted).count();
        auto mesh=std::make_shared<iCAX::GeometryServices::CollisionMesh>();
        std::map<std::uint64_t,std::pair<unsigned,unsigned>> triangleRanges;
        // Same record order, local positions and triangle indices as the render
        // resource adapter. CAD occurrences are already represented by these
        // imported face meshes; the placement transform is applied separately.
        for(const auto& record:refined.Triangulations3){const auto& triangles=record.Geometry;if(triangles.Vertices.empty()||triangles.Triangles.empty())continue;
            if(mesh->Vertices.size()+triangles.Vertices.size()>std::numeric_limits<unsigned>::max())throw std::runtime_error("CAM_COLLISION_MESH_TOO_LARGE");
            if(mesh->Triangles.size()+triangles.Triangles.size()>std::numeric_limits<unsigned>::max())throw std::runtime_error("CAM_COLLISION_MESH_TOO_LARGE");
            triangleRanges[record.Id]={static_cast<unsigned>(mesh->Triangles.size()),static_cast<unsigned>(triangles.Triangles.size())};
            const auto base=static_cast<unsigned>(mesh->Vertices.size());for(const auto& p:triangles.Vertices)mesh->Vertices.push_back({p.X,p.Y,p.Z});
            for(const auto& triangle:triangles.Triangles){for(auto index:triangle)if(index>=triangles.Vertices.size())throw std::runtime_error("CAM_COLLISION_MESH_INDEX_INVALID");mesh->Triangles.push_back({base+triangle[0],base+triangle[1],base+triangle[2]});}
        }
        if(mesh->Triangles.empty())throw std::runtime_error("CAM_COLLISION_MESH_REQUIRED");
        // CAD owns closed-volume topology. Shared curved-face boundary points
        // need not be bit-identical after independent UV evaluation. Preserve
        // their coordinates and supply all shells (including cavities) of each
        // material solid as one parity group; separate solids form a union.
        for(const auto& solid:refined.Solids){bool closed=!solid.ShellIds.empty();std::set<unsigned> indices;
            for(auto shellID:solid.ShellIds){const auto shell=std::find_if(refined.Shells.begin(),refined.Shells.end(),[&](const auto& s){return s.Id==shellID;});
                if(shell==refined.Shells.end()||!shell->Closed){closed=false;break;}
                for(auto faceID:shell->FaceIds){const auto face=std::find_if(refined.Faces.begin(),refined.Faces.end(),[&](const auto& f){return f.Id==faceID;});
                    if(face==refined.Faces.end()||!triangleRanges.contains(face->Triangulation3Id)){closed=false;break;}
                    const auto [first,count]=triangleRanges.at(face->Triangulation3Id);for(unsigned i=0;i<count;++i)indices.insert(first+i);
                }if(!closed)break;
            }if(closed&&!indices.empty())mesh->ClosedVolumes.emplace_back(indices.begin(),indices.end());
        }
        if(refined.Faces.empty())mesh->ClosedVolumes=iCAX::GeometryServices::ProveClosedMeshVolumes(*mesh,[&]{return scene.IsStopRequested();});
        // Padding is intentionally zero. The verdict describes these supplied
        // mesh triangles; display deflection is not an exact-CAD proof margin.
        // One query supports 128 obstacles. Keep that entire working set so a
        // scene with 35 fixture plates and its workpiece does not rebuild all
        // meshes (and their downstream BVHs) on every single-route check.
        WriteCollisionMeshDiskCache(diskKey,*mesh);RememberCollisionMesh(*runtime,key,mesh);
        runtime->CollisionMeshPreparationMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-prepareStarted).count();return mesh;
    }
    iCAX::GeometryServices::TravelObstacle CollisionObstacle(Scene& scene,Text id,const Map& reference,const std::array<double,16>& transform){
        return {std::move(id),FixtureGeometry(scene,reference),transform,CollisionMeshFor(scene,reference)};
    }
    bool LegacySchematicHead(const Map& head){return !head.contains("geometryRef")&&!head.contains("collisionBoxes")&&!head.contains("builtinDefinitionId")&&!head.contains("collisionSource");}
    std::vector<HeadBox> DeclaredHeadCollisionBoxes(const Map& head){
        namespace M=iCAX::GeometryServices::RigidMotion;
        const auto rotation=M::Between(RegistrationPoint(head.at("beamLocalDirection")),{0,0,-1});const auto tcp=RegistrationPoint(head.at("tcpLocalPosition"));
        std::vector<HeadBox> boxes;
        if(!head.contains("collisionBoxes"))return boxes;
        for(const auto& value:head.at("collisionBoxes").To<Array>()){
            const auto attachment=value.To<Map>();if(attachment.contains("enabled")&&!attachment.at("enabled").To<bool>())continue;
            const auto geometry=attachment.at("geometry").To<Map>();if(String(geometry,"type")!="box")throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_REQUIRED");
            const auto pose=attachment.at("pose").To<Array>(),size=geometry.at("size").To<Array>();if(pose.size()!=6||size.size()!=3)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_INVALID");
            std::array<double,6> p{};for(int k=0;k<6;++k)p[k]=FiniteNumber(pose[k],"CAM_HEAD_COLLISION_BOX_INVALID");
            const auto matrix=iCAX::Transform::MakeLocalMatrix(p[0]*1000,p[1]*1000,p[2]*1000,p[5],p[4],p[3],1,1,1);
            HeadBox box;box.Center=M::Rotate(rotation,M::Sub({matrix(0,3),matrix(1,3),matrix(2,3)},tcp));
            for(int k=0;k<3;++k){box.HalfExtents[k]=FiniteNumber(size[k],"CAM_HEAD_COLLISION_BOX_INVALID")*500;if(box.HalfExtents[k]<=0)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_SIZE");box.Axes[k]=M::Rotate(rotation,{matrix(0,k),matrix(1,k),matrix(2,k)});}
            boxes.push_back(box);
        }return boxes;
    }
    void RequireRigidHeadDirection(const Map& head,const iCAX::GeometryServices::RigidMotion::Vector& direction){
        // The unassigned schematic head supplies a preview envelope, not a
        // machine-axis constraint. Explicitly selected rigid heads still do.
        if(LegacySchematicHead(head))return;
        if(String(head,"motionType")!="Rigid"||(head.contains("collisionSource")&&String(head,"collisionSource")=="MachineInstance"))return;
        const auto axis=iCAX::GeometryServices::RigidMotion::Unit(direction);
        if(std::abs(axis[0])>1e-7||std::abs(axis[1])>1e-7||axis[2]<1.-1e-8)
            throw std::invalid_argument("CAM_HEAD_FIXED_AXIS_REQUIRED: 刚性刀头没有旋转轴，不能执行倾斜刀轴。");
    }
    std::vector<HeadBox> HeadCollisionBoxes(Scene& scene,const Map& head){
        if(head.contains("requiresMachineSimulation")&&head.at("requiresMachineSimulation").To<bool>()&&!head.contains("motion")&&String(head,"collisionSource")!="MachineInstance")throw std::invalid_argument("CAM_HEAD_MACHINE_BINDING_REQUIRED: 请在仿真中选择并绑定实际机床，再检查完整机构。");
        if(LegacySchematicHead(head))return BuiltinHeadCollisionBoxes();
        if(!head.at("calibrated").To<bool>())throw std::invalid_argument("CAM_COLLISION_HEAD_CALIBRATION_REQUIRED");
        const auto key=iCAX::Data::VariantSerializer::Serialize(Map{{"algorithm",Text("MachineDeclaredHeadBoxesV2")},{"boxes",head.contains("collisionBoxes")?head.at("collisionBoxes"):Variant(Array{})},{"tcp",head.at("tcpLocalPosition")},{"beam",head.at("beamLocalDirection")}});
        auto runtime=Runtime(scene);if(const auto found=runtime->HeadCollisionBoxes.find(key);found!=runtime->HeadCollisionBoxes.end())return found->second;
        auto boxes=DeclaredHeadCollisionBoxes(head);
        if(boxes.empty())throw std::invalid_argument("CAM_HEAD_COLLISION_BOXES_REQUIRED: 请在机床切割头部件中配置并启用碰撞盒。");
        if(runtime->HeadCollisionBoxes.size()>=16)runtime->HeadCollisionBoxes.clear();runtime->HeadCollisionBoxes.emplace(key,boxes);return boxes;
    }
    bool IndependentArticulatedHead(const Map& head){return head.contains("assembly")&&head.contains("motion")&&String(head,"collisionSource")!="MachineInstance";}
    std::vector<HeadBox> AssemblyHeadCollisionBoxes(const Map& head){
        namespace M=iCAX::CAM::Motion;
        const auto assembly=head.at("assembly").To<Map>();std::vector<M::Node> nodes;std::vector<double> positions;
        for(const auto& value:assembly.at("nodes").To<Array>()){const auto n=value.To<Map>();M::Node node;node.Parent=n.at("parent").To<int>();node.Type=String(n,"type");node.Origin=RigidMatrix(n.at("origin").To<Array>());node.Axis=RegistrationPoint(n.at("axis"));nodes.push_back(node);positions.push_back(RegistrationNumber(n.at("position")));}
        const auto world=M::Forward(nodes,positions);const auto tool=assembly.at("tool").To<int>();const auto tcp=M::Transform(world.at(tool),RegistrationPoint(assembly.at("tcp")));
        const auto beam=M::Unit(M::Transform(world.at(tool),RegistrationPoint(assembly.at("beam")),true));
        const auto rotation=iCAX::GeometryServices::RigidMotion::Between(beam,{0,0,-1});std::vector<HeadBox> result;
        const auto values=assembly.at("nodes").To<Array>();for(std::size_t i=0;i<values.size();++i){const auto n=values[i].To<Map>();if(!n.contains("collisionBoxes"))continue;
            const Map local{{"collisionBoxes",n.at("collisionBoxes")},{"tcpLocalPosition",Array{0.,0.,0.}},{"beamLocalDirection",Array{0.,0.,-1.}}};
            for(auto box:DeclaredHeadCollisionBoxes(local)){box.Center=iCAX::GeometryServices::RigidMotion::Rotate(rotation,iCAX::GeometryServices::RigidMotion::Sub(M::Transform(world[i],box.Center),tcp));for(auto& axis:box.Axes)axis=iCAX::GeometryServices::RigidMotion::Rotate(rotation,M::Transform(world[i],axis,true));result.push_back(box);}}
        if(result.empty())throw std::invalid_argument("CAM_HEAD_COLLISION_BOXES_REQUIRED");return result;
    }
    Map HeadCollisionState(Scene& scene,const Map& head){
        const bool calibrated=head.at("calibrated").To<bool>(),legacy=LegacySchematicHead(head);
        const bool assembly=IndependentArticulatedHead(head);
        bool active=legacy;
        if(head.contains("collisionBoxes"))for(const auto& value:head.at("collisionBoxes").To<Array>()){const auto box=value.To<Map>();if(!box.contains("enabled")||box.at("enabled").To<bool>())active=true;}
        if(assembly)for(const auto& node:head.at("assembly").To<Map>().at("nodes").To<Array>()){const auto part=node.To<Map>();if(part.contains("collisionBoxes"))for(const auto& value:part.at("collisionBoxes").To<Array>()){const auto box=value.To<Map>();if(!box.contains("enabled")||box.at("enabled").To<bool>())active=true;}}
        Array boxes;
        const bool machineRequired=head.contains("requiresMachineSimulation")&&head.at("requiresMachineSimulation").To<bool>()&&!head.contains("motion")&&String(head,"collisionSource")!="MachineInstance";
        if(calibrated&&active)for(const auto& b:assembly?AssemblyHeadCollisionBoxes(head):machineRequired?DeclaredHeadCollisionBoxes(head):HeadCollisionBoxes(scene,head)){Array center,half,axes;for(int k=0;k<3;++k){center.emplace_back(b.Center[k]);half.emplace_back(b.HalfExtents[k]);Array axis;for(double x:b.Axes[k])axis.emplace_back(x);axes.emplace_back(axis);}boxes.emplace_back(Map{{"center",center},{"halfExtents",half},{"axes",axes},{"frame",Text("CalibratedTCP")}});}
        return {{"representation",Text("BoxCompoundMesh")},{"frame",Text("CalibratedTCP")},{"boxCount",std::to_string(boxes.size())},{"boxes",boxes},{"meshDeflectionMm",CollisionMeshDeflectionMm},
            {"basis",Text(legacy?"BuiltinAxialSlabBoxes":"MachineDeclaredBoxes")},{"coverage",Text(assembly?"CompleteSelectedHeadAssembly":"ToolLinkOnly")},{"status",Text(machineRequired?"MachineBindingRequired":!calibrated?"CalibrationRequired":active?"Ready":"CollisionBoxesRequired")}};
    }
    void AppendAdditionalHeadEnvelope(std::vector<HeadBox>& boxes,const Map& settings){
        const auto radius=RegistrationNumber(settings.at("envelopeRadiusMm"));if(radius<=0)return;const auto length=RegistrationNumber(settings.at("envelopeLengthMm"));HeadBox box;box.Center={0,0,length/2};box.HalfExtents={radius,radius,length/2};boxes.push_back(box);
    }
}
