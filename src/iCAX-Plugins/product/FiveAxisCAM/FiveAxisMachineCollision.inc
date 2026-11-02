namespace {
    namespace Collision=iCAX::GeometryServices;
    struct MachineCollisionBody {
        int Node=-1;Text ID,Name;Collision::CollisionDistanceModelRef Model;
        Motion::Matrix Local=Motion::Identity();double Radius=0;bool Fixture=false;
        Motion::Vector Min{1e100,1e100,1e100},Max{-1e100,-1e100,-1e100};
        std::vector<std::pair<Map,Collision::CollisionDistanceModelRef>> DiagnosticParts;
    };
    double MachineBoxDistance(const MachineCollisionBody& a,const MachineCollisionBody& b,const Motion::Matrix& relative){
        Motion::Vector lo{1e100,1e100,1e100},hi{-1e100,-1e100,-1e100};
        for(int bits=0;bits<8;++bits){const auto p=Motion::Transform(relative,{bits&1?b.Max[0]:b.Min[0],bits&2?b.Max[1]:b.Min[1],bits&4?b.Max[2]:b.Min[2]});for(int k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}}
        double square=0;for(int k=0;k<3;++k){const auto gap=std::max({0.,lo[k]-a.Max[k],a.Min[k]-hi[k]});square+=gap*gap;}return std::sqrt(square);
    }
    MachineCollisionBody PrepareMachineBody(Scene& scene,const MachineSnapshot& machine,int index,const Collision::ICollisionDistanceService& service){
        const auto& node=machine.Nodes[index];Array declarations;Text name=node.ID;
        for(const auto& value:machine.CollisionDefinitions){const auto d=value.To<Map>();if(String(d,"id")==node.ID){declarations=d.at("geometry").To<Array>();if(d.contains("name"))name=String(d,"name");break;}}
        if(machine.HeadMotion.empty()){const auto entity=scene.Database().GetEntity(ID::from_string(node.ID).value());const auto element=Component<iCAX::CAM::CMachineElementComponent>(entity);if(element)name=element->GetName();}
        MachineCollisionBody body;body.Node=index;body.ID=node.ID;body.Name=name;
        if(body.Name.empty())body.Name=body.ID;
        if(declarations.empty())throw std::invalid_argument("部件“"+body.Name+"”没有碰撞盒，请配置有效碰撞盒。");
        const auto key=iCAX::Data::VariantSerializer::Serialize(Map{{"boxes",declarations},{"algorithm",Text("MachineDeclaredBoxesV5")},{"ownerName",body.Name}});
        auto& cache=Runtime(scene)->MachineCollisionCache;
        if(const auto found=cache.find(body.ID);found!=cache.end()&&found->second.Key==key){
            if(!found->second.Model&&index==machine.Target.Tool)throw std::invalid_argument("CAM_HEAD_COLLISION_BOXES_REQUIRED: 请启用机床切割头碰撞盒。");
            body.Model=found->second.Model;body.Radius=found->second.Radius;body.Min=found->second.Min;body.Max=found->second.Max;body.DiagnosticParts=found->second.DiagnosticParts;return body;
        }
        std::vector<Collision::CollisionBox> boxes;
        int attachmentIndex=0;
        for(const auto& value:declarations){
            const auto attachment=value.To<Map>();const auto boxIndex=attachmentIndex++;
            if(attachment.contains("enabled")&&!attachment.at("enabled").To<bool>())continue;
            const auto geometry=attachment.at("geometry").To<Map>();
            if(String(geometry,"type")!="box")throw std::invalid_argument("部件“"+body.Name+"”需要配置碰撞盒。");
            const auto pose=attachment.at("pose").To<Array>(),size=geometry.at("size").To<Array>();
            if(pose.size()!=6||size.size()!=3)throw std::invalid_argument("CAM_MACHINE_BOX_INVALID");
            std::array<double,6> p{};for(int k=0;k<6;++k)p[k]=FiniteNumber(pose[k],"CAM_MACHINE_BOX_INVALID");
            const auto t=iCAX::Transform::MakeLocalMatrix(p[0]*1000,p[1]*1000,p[2]*1000,p[5],p[4],p[3],1,1,1);
            Collision::CollisionBox box;
            for(int k=0;k<3;++k){box.Center[k]=t(k,3);box.HalfExtents[k]=FiniteNumber(size[k],"CAM_MACHINE_BOX_INVALID")*500;if(box.HalfExtents[k]<=0)throw std::invalid_argument("CAM_MACHINE_BOX_SIZE");for(int j=0;j<3;++j)box.Axes[k][j]=t(j,k);}
            double radius=0;for(int k=0;k<3;++k){double extent=0;for(int j=0;j<3;++j)extent+=std::abs(box.Axes[j][k])*box.HalfExtents[j];body.Min[k]=std::min(body.Min[k],box.Center[k]-extent);body.Max[k]=std::max(body.Max[k],box.Center[k]+extent);radius+=std::pow(std::abs(box.Center[k])+extent,2);}
            body.Radius=std::max(body.Radius,std::sqrt(radius));boxes.push_back(box);
            const auto id=attachment.contains("id")?String(attachment,"id"):Text{};
            auto name=attachment.contains("name")?String(attachment,"name"):Text{};if(name.empty()&&attachment.contains("collisionName"))name=String(attachment,"collisionName");if(name.empty())name=Text("碰撞盒 ")+std::to_string(boxIndex+1);
            body.DiagnosticParts.emplace_back(Map{{"ownerElementId",body.ID},{"ownerElementName",body.Name},{"boxId",id},{"boxName",name},{"boxIndex",boxIndex},{"representation",Text("Box")},{"editable",true}},service.PrepareBoxes({box}));
        }
        if(boxes.empty()&&index==machine.Target.Tool)throw std::invalid_argument("CAM_HEAD_COLLISION_BOXES_REQUIRED: 请启用机床切割头碰撞盒。");
        if(!boxes.empty())body.Model=service.PrepareBoxes(boxes);
        if(cache.size()>=256&&!cache.contains(body.ID))cache.erase(cache.begin());
        cache[body.ID]={key,body.Model,{},body.Radius,body.Min,body.Max,{},body.DiagnosticParts};return body;
    }
    struct PreparedMachineCollision {
        std::vector<MachineCollisionBody> Bodies;
        std::vector<std::pair<size_t,size_t>> Pairs;
        std::vector<std::vector<size_t>> RelativeJoints;
        Array Exclusions;
        std::shared_ptr<Collision::ICollisionDistanceService> Service;
        bool Ready=false;
    };
    PreparedMachineCollision PrepareMachineCollision(Scene& scene,const MachineSnapshot& machine,const SimulationCollisionSnapshot& input){
        PreparedMachineCollision context;
            const auto service=scene.Services().Resolve<Collision::ICollisionDistanceService>();
            if(!service)throw std::runtime_error("整机碰撞服务不可用。");
            const auto runtime=Runtime(scene);
            if(runtime->MachineCollisionService!=service){runtime->MachineCollisionCache.clear();runtime->MachineCollisionService=service;}
            auto& bodies=context.Bodies;bodies.clear();
            for(int i=0;i<static_cast<int>(machine.Nodes.size());++i){
                bool declared=false;for(const auto& value:machine.CollisionDefinitions){const auto d=value.To<Map>();if(String(d,"id")==machine.Nodes[i].ID&&!d.at("geometry").To<Array>().empty()){declared=true;break;}}
                if(!declared&&(machine.Nodes[i].Parent<0||!machine.HeadMotion.empty()&&i!=machine.Target.Tool))continue;
                auto body=PrepareMachineBody(scene,machine,i,*service);if(body.Model)bodies.push_back(std::move(body));
            }
            for(const auto& obstacle:input.Query.Obstacles){
                MachineCollisionBody body;body.Node=machine.Target.Support;body.ID=obstacle.Id;body.Name="工件或夹具";body.Fixture=true;
                const auto entity=scene.Database().GetEntity(Identifier(Map{{"id",obstacle.Id}},"id"));
                if(const auto placement=Component<CFiveAxisPlacementComponent>(entity))body.Name=Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID())->GetName();
                else if(const auto fixture=Component<CFiveAxisFixtureComponent>(entity))body.Name=fixture->GetName();
                body.Local=Motion::Multiply(machine.Placement,obstacle.Transform);
                auto& cache=runtime->MachineCollisionCache;
                if(!obstacle.Mesh)throw std::runtime_error("CAM_MACHINE_TARGET_MESH_REQUIRED");
                if(const auto found=cache.find(body.ID);found!=cache.end()&&found->second.Key=="OwnMeshV1"&&found->second.Mesh==obstacle.Mesh){body.Model=found->second.Model;body.Radius=found->second.Radius;body.Min=found->second.Min;body.Max=found->second.Max;}
                else {
                    body.Model=service->PrepareMesh(*obstacle.Mesh,[&]{return scene.IsStopRequested();});for(const auto& p:obstacle.Mesh->Vertices){for(int k=0;k<3;++k){body.Min[k]=std::min(body.Min[k],p[k]);body.Max[k]=std::max(body.Max[k],p[k]);}body.Radius=std::max(body.Radius,Motion::Length(p));}
                    for(int k=0;k<3;++k){body.Min[k]-=obstacle.Mesh->Padding;body.Max[k]+=obstacle.Mesh->Padding;}body.Radius+=obstacle.Mesh->Padding;
                    if(cache.size()>=256&&!cache.contains(body.ID))cache.erase(cache.begin());
                    cache[body.ID]={Text("OwnMeshV1"),body.Model,obstacle.Geometry,body.Radius,body.Min,body.Max,obstacle.Mesh};
                }
                body.Radius+=Motion::Length({body.Local[3],body.Local[7],body.Local[11]});bodies.push_back(body);
                bodies.back().DiagnosticParts.emplace_back(Map{{"ownerElementId",body.ID},{"ownerElementName",body.Name},{"boxId",Text{}},{"boxName",Text("真实模型网格")},{"boxIndex",-1},{"representation",Text("OwnMesh")},{"editable",false}},body.Model);
            }
            const auto movingRoot=[&](int i){while(i>=0&&machine.Nodes[i].Type=="fixed")i=machine.Nodes[i].Parent;return i;};
            auto& pairs=context.Pairs;auto& exclusions=context.Exclusions;pairs.clear();exclusions.clear();
            for(size_t a=0;a<bodies.size();++a)for(size_t b=a+1;b<bodies.size();++b){const auto& A=bodies[a];const auto& B=bodies[b];
                if(movingRoot(A.Node)==movingRoot(B.Node))continue; // rigidly attached: no relative motion
                if(!A.Fixture&&!B.Fixture&&(machine.Nodes[A.Node].Parent==B.Node||machine.Nodes[B.Node].Parent==A.Node)){
                    exclusions.emplace_back(Map{{"first",A.ID},{"second",B.ID},{"reason",Text("直接连接关节的机械接触副")}});continue;}
                pairs.emplace_back(a,b);
                int common=-1;for(int i=A.Node;i>=0&&common<0;i=machine.Nodes[i].Parent)for(int j=B.Node;j>=0;j=machine.Nodes[j].Parent)if(i==j){common=i;break;}
                std::vector<size_t> joints;
                for(const auto node:{A.Node,B.Node})for(int i=node;i>=0&&i!=common;i=machine.Nodes[i].Parent)if(machine.Nodes[i].Type!="fixed")joints.push_back(static_cast<size_t>(i));
                context.RelativeJoints.push_back(std::move(joints));
            }
            context.Service=service;context.Ready=true;
        return context;
    }
    // An interactive run owns this cache. A sample is a completed distance
    // proof at one relative pose, never a certificate for a motion interval.
    // Fixed geometry and the configuration fence belong to that same run.
    struct MachineCollisionQueryCache {
        struct Sample {double Lower=0;bool Contact=false;};
        const PreparedMachineCollision* Owner=nullptr;
        std::vector<std::map<std::vector<double>,Sample>> Samples;
        void Bind(const PreparedMachineCollision& context){if(Owner!=&context||Samples.size()!=context.Pairs.size()){Owner=&context;Samples.clear();Samples.resize(context.Pairs.size());}}
    };
    struct MachineCollisionInterrupted final:std::runtime_error {
        Text Kind;
        MachineCollisionInterrupted(Text kind,const char* message):std::runtime_error(message),Kind(std::move(kind)){}
    };
    // Frozen worker-owned models; no scene/database access while solving.
    void CheckPreparedMachineCollision(const MachineSnapshot& machine,Map& result,
        const PreparedMachineCollision& context,const std::function<bool()>& cancelled={},double budgetMs=50000,
        const std::vector<double>* initialPositions=nullptr,MachineCollisionQueryCache* cache=nullptr){
        result["collisionStatus"]=Text("Unchecked");result["collisionScope"]=Text("MachineDeclaredBoxesAndWorkpieceFixtureMesh");result["collisionAlgorithm"]=Text("MachineDeclaredBoxesV5");result["meshDeflectionMm"]=CollisionMeshDeflectionMm;
        result.erase("collisionPairs");result["collisionDiagnosticsComplete"]=false;
        result["collisionRepresentation"]=Text("MachineBoxes; WorkpieceFixtureOwnMesh");result["toolCollisionSource"]=Text("MachineDeclaredCollisionBoxes");
        result.erase("collisionInterruption");const auto started=std::chrono::steady_clock::now();size_t queries=0,hits=0,distanceQueries=0,narrowQueries=0;
        const auto metrics=[&]{result["collisionQueries"]=std::to_string(queries);result["collisionCacheHits"]=std::to_string(hits);result["collisionDistanceQueries"]=std::to_string(distanceQueries);result["collisionNarrowPhaseQueries"]=std::to_string(narrowQueries);result["collisionBudgetMs"]=budgetMs;result["collisionElapsedMs"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();};
        try{
            const auto& bodies=context.Bodies;const auto& pairs=context.Pairs;const auto& service=context.Service;
            result["collisionPairCount"]=std::to_string(pairs.size());result["allowedContacts"]=context.Exclusions;
            std::vector<double> previous;for(const auto& n:machine.Nodes)previous.push_back(n.Position);
            if(initialPositions)previous=*initialPositions;
            const auto frames=result.at("frames").To<Array>();if(cache)cache->Bind(context);
            const auto deadline=started+std::chrono::duration<double,std::milli>(budgetMs);
            const auto checkStop=[&]{
                if(cancelled&&cancelled())throw MachineCollisionInterrupted("Cancelled","本次机床间隙检查已取消，机床保持原位置，可以继续调整。");
                if(std::chrono::steady_clock::now()>deadline)throw MachineCollisionInterrupted("Deadline","本次机床间隙检查超时，机床保持原位置，可缩小调整量后继续。");
            };
            for(size_t frame=0;frame<frames.size();++frame){std::vector<double> next;for(const auto& v:frames[frame].To<Map>().at("joints").To<Array>())next.push_back(v.To<double>());
                double firstFraction=2;Map alarm;size_t alarmFirst=0,alarmSecond=0;double alarmWitness=0;
                // All collision pairs share the same kinematic poses. Compute
                // the parent chain once per sampled pose, not once per pair.
                std::map<std::vector<double>,std::vector<Motion::Matrix>> poses;
                for(size_t pair=0;pair<pairs.size();++pair){checkStop();const auto [a,b]=pairs[pair];const auto& A=bodies[a];const auto& B=bodies[b];
                    const auto bound=Motion::RelativeSweepBound(machine.Nodes,A.Node,A.Radius,B.Node,B.Radius,previous,next);
                    double witnessFraction=2;
                    const auto checked=Motion::CertifySweep(previous,next,bound,.1,[&](const auto& q){
                        checkStop();++queries;
                        std::vector<double> coordinates;if(cache)for(const auto index:context.RelativeJoints.at(pair))coordinates.push_back(q.at(index));
                        const auto contact=[&]{double fraction=0;for(size_t i=0;i<q.size();++i)if(std::abs(next[i]-previous[i])>1e-12){fraction=(q[i]-previous[i])/(next[i]-previous[i]);break;}witnessFraction=std::min(witnessFraction,fraction);};
                        if(cache){const auto& samples=cache->Samples[pair];if(const auto found=samples.find(coordinates);found!=samples.end()){++hits;if(found->second.Contact)contact();return found->second.Lower;}}
                        auto found=poses.find(q);if(found==poses.end())found=poses.emplace(q,Motion::Forward(machine.Nodes,q)).first;
                        const auto& world=found->second;const auto first=Motion::Multiply(world[A.Node],A.Local),second=Motion::Multiply(world[B.Node],B.Local);
                        const auto relative=Motion::Multiply(Motion::Inverse(first),second);const auto gap=MachineBoxDistance(A,B,relative);
                        MachineCollisionQueryCache::Sample sample;sample.Lower=gap;
                        if(gap<=.1){
                            ++distanceQueries;const auto separated=service->Separation(A.Model,B.Model,relative,[&]{checkStop();return false;});narrowQueries+=separated.NarrowPhaseQueries;
                            sample={separated.LowerBound,separated.Overlapping||separated.UpperBound<=.1};
                        }
                        if(cache){auto& samples=cache->Samples[pair];if(samples.size()>=64)samples.clear();samples.emplace(std::move(coordinates),sample);}
                        checkStop();if(sample.Contact)contact();return sample.Lower;
                    });
                    if(!checked.Clear&&checked.Fraction<firstFraction){
                        if(witnessFraction>1)throw std::runtime_error("机床运动间隙尚未获得完整证明，请缩短轨迹后重试。");
                        firstFraction=checked.Fraction;
                        alarm["status"]=Text("Blocked");alarm["collisionStatus"]=Text("Blocked");alarm["collisionFrame"]=std::to_string(frame);alarm["collisionFraction"]=checked.Fraction;
                        alarm["collisionBodies"]=Array{Variant(A.ID),Variant(B.ID)};alarm["reason"]=Text((frame<=1?"起始接近段":"轨迹段 "+std::to_string(frame))+"：“"+A.Name+"”与“"+B.Name+"”的碰撞包络未满足 0.1 mm 间隙，仿真将在此报警并停止。");
                        alarm["collisionWitnessFraction"]=witnessFraction;
                        alarmFirst=a;alarmSecond=b;alarmWitness=witnessFraction;
                        auto collisionPose=previous;for(size_t k=0;k<next.size();++k)collisionPose[k]+=(next[k]-previous[k])*checked.Fraction;alarm["collisionJoints"]=MachineNumbers(collisionPose);alarm["collisionQueries"]=std::to_string(queries);
                        alarm["alarmFrame"]=std::max(0.,static_cast<double>(frame)-1+checked.Fraction);alarm["alarmJoints"]=alarm.at("collisionJoints");
                    }
                }
                if(!alarm.empty()){
                    // Diagnostic identity is proved at the actual contact
                    // witness. It does not change the already Blocked motion
                    // result if optional per-box enumeration runs out of time.
                    Array diagnostics;bool complete=true;
                    try{
                        const auto diagnosticDeadline=std::min(deadline-std::chrono::milliseconds(1),std::chrono::steady_clock::now()+std::chrono::duration<double,std::milli>(8));
                        const auto checkDiagnostic=[&]{checkStop();if(std::chrono::steady_clock::now()>=diagnosticDeadline)throw std::runtime_error("Collision box diagnostic budget exhausted");};
                        auto witness=previous;for(size_t i=0;i<witness.size();++i)witness[i]+=(next[i]-previous[i])*alarmWitness;
                        const auto world=Motion::Forward(machine.Nodes,witness);const auto& A=bodies[alarmFirst];const auto& B=bodies[alarmSecond];
                        const auto relative=Motion::Multiply(Motion::Inverse(Motion::Multiply(world[A.Node],A.Local)),Motion::Multiply(world[B.Node],B.Local));
                        for(const auto& [firstRef,firstModel]:A.DiagnosticParts)for(const auto& [secondRef,secondModel]:B.DiagnosticParts){
                            checkDiagnostic();if(diagnostics.size()>=32){complete=false;break;}
                            const auto separated=service->Separation(firstModel,secondModel,relative,[&]{checkDiagnostic();return false;});
                            checkDiagnostic();if(separated.Overlapping||separated.UpperBound<=.1)diagnostics.emplace_back(Map{{"first",firstRef},{"second",secondRef}});
                        }
                    }catch(const std::exception&){complete=false;}
                    if(diagnostics.empty())complete=false;
                    alarm["collisionPairs"]=diagnostics;alarm["collisionDiagnosticsComplete"]=complete;
                    for(const auto& [key,value]:alarm)result[key]=value;metrics();return;
                }
                previous=next;
            }
            result["collisionStatus"]=Text("Clear");result["collisionDiagnosticsComplete"]=true;
        }catch(const MachineCollisionInterrupted& e){result["status"]=Text("Unchecked");result["collisionStatus"]=Text("Unchecked");result["collisionInterruption"]=e.Kind;result["reason"]=Text(e.what());}
        catch(const std::exception& e){result["status"]=Text("Unchecked");result["collisionStatus"]=Text("Unchecked");result["collisionInterruption"]=Text("Unresolved");result["reason"]=Text(e.what());}
        metrics();
    }
    iCAX::GeometryServices::MachineMotion::Validation CheckPreparedPolynomialCollision(const MachineSnapshot& machine,
        const iCAX::GeometryServices::MachineMotion::AxisSegment& segment,const PreparedMachineCollision& context,const std::function<bool()>& cancelled){
        namespace MM=iCAX::GeometryServices::MachineMotion;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);std::size_t queries=0;
        if(segment.Coefficients.size()!=machine.Nodes.size())return {MM::Verdict::Unresolved,"Polynomial axis count does not match the frozen machine."};
        std::map<double,std::vector<Motion::Matrix>> poses;
        const auto worldAt=[&](double u)->const std::vector<Motion::Matrix>&{const auto found=poses.find(u);if(found!=poses.end())return found->second;return poses.emplace(u,Motion::Forward(machine.Nodes,MM::Evaluate(segment,u))).first->second;};
        for(const auto& pair:context.Pairs){const auto& A=context.Bodies[pair.first];const auto& B=context.Bodies[pair.second];
            int common=-1;for(int i=A.Node;i>=0&&common<0;i=machine.Nodes[i].Parent)for(int j=B.Node;j>=0;j=machine.Nodes[j].Parent)if(i==j){common=i;break;}
            std::function<MM::Validation(double,double,unsigned)> visit=[&](double first,double last,unsigned depth)->MM::Validation{
                if(cancelled&&cancelled())return {MM::Verdict::Unresolved,"Cancelled while certifying the complete polynomial motion."};
                if(++queries>8192||std::chrono::steady_clock::now()>deadline)return {MM::Verdict::Unresolved,"Polynomial collision proof budget exhausted."};
                const double middle=(first+last)/2;const auto& world=worldAt(middle);
                const auto relative=Motion::Multiply(Motion::Inverse(Motion::Multiply(world[A.Node],A.Local)),Motion::Multiply(world[B.Node],B.Local));
                const auto bound=[&](int body,double radius){double travel=0;for(int i=body;i>=0&&i!=common;i=machine.Nodes[i].Parent){const auto& node=machine.Nodes[i];const auto range=MM::PolynomialBounds(segment.Coefficients[i],first,last);const auto derivative=MM::PolynomialBounds(segment.Coefficients[i],first,last,1);const double delta=std::max(std::abs(derivative[0]),std::abs(derivative[1]))*(last-first)/2;
                        if(node.Type=="prismatic"){travel+=delta;radius+=std::max(std::abs(range[0]),std::abs(range[1]));}else if(node.Type!="fixed")travel+=delta*radius;
                        radius+=Motion::Length({node.Origin[3],node.Origin[7],node.Origin[11]});}return travel;};
                const double travel=bound(A.Node,A.Radius)+bound(B.Node,B.Radius);
                // A disjoint bounding box can already prove the complete
                // interval clear. Near pairs still use the actual mesh.
                if(MachineBoxDistance(A,B,relative)>.1+travel)return {MM::Verdict::Accepted,{}};
                const auto separated=context.Service->Separation(A.Model,B.Model,relative,cancelled);
                if(separated.Overlapping||separated.UpperBound<=.1)return {MM::Verdict::Rejected,"连续轴轨迹与“"+A.Name+"”或“"+B.Name+"”发生干涉。"};
                if(separated.LowerBound>.1+travel)return {MM::Verdict::Accepted,{}};
                if(depth>=22||travel<1e-7)return {MM::Verdict::Unresolved,"The complete polynomial motion clearance could not be certified."};
                const auto left=visit(first,middle,depth+1);return left.State==MM::Verdict::Accepted?visit(middle,last,depth+1):left;
            };
            const auto result=visit(0,1,0);if(result.State!=MM::Verdict::Accepted)return result;
        }return {MM::Verdict::Accepted,{}};
    }
    void CheckMachineCollision(Scene& scene,const MachineSnapshot& machine,const SimulationCollisionSnapshot& input,Map& result,PreparedMachineCollision* prepared=nullptr,double budgetMs=50000){
        try{
            PreparedMachineCollision temporary;
            auto& context=prepared?*prepared:temporary;
            if(!context.Ready)context=PrepareMachineCollision(scene,machine,input);
            CheckPreparedMachineCollision(machine,result,context,[&]{return scene.IsStopRequested();},budgetMs);
        }catch(const std::exception& error){result["status"]=Text("Unchecked");result["collisionStatus"]=Text("Unchecked");result["reason"]=Text(error.what());}
    }
}
