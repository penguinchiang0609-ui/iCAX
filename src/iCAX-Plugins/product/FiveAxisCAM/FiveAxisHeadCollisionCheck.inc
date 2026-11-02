namespace {
    // Frozen selected-head data only. No machine instance, avoidance search,
    // generated geometry or editable state is changed by this checker.
    void PrepareFullHeadCollision(Scene& scene,const ID& setupID,SimulationCollisionSnapshot& input){
        namespace R=iCAX::GeometryServices::RigidMotion;
        namespace K=iCAX::GeometryServices::MachineKinematics;
        using Result=iCAX::GeometryServices::RigidMotionCollisionResult;
        using Pose=iCAX::GeometryServices::RigidMotionPose;
        const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        const auto head=HeadDefinition(scene,*setup);
        if(!head.at("calibrated").To<bool>())throw std::invalid_argument("CAM_COLLISION_HEAD_CALIBRATION_REQUIRED");
        const auto machine=CaptureSetupMachine(scene,*setup);
        if(machine.HeadMotion.empty())throw std::invalid_argument("CAM_INDEPENDENT_HEAD_REQUIRED");
        const auto distanceService=scene.Services().Resolve<Collision::ICollisionDistanceService>();
        const auto kinematics=scene.Services().Resolve<Collision::IMachineKinematicsService>();
        if(!distanceService||!kinematics)throw std::runtime_error("CAM_HEAD_COLLISION_SERVICE_REQUIRED");
        struct PartBox {int Node;HeadBox Box;double Radius;Collision::CollisionDistanceModelRef Model;Map Identity;};
        struct Obstacle {Text ID;Motion::Matrix Local;double Radius;Collision::CollisionDistanceModelRef Model;
            Motion::Vector Min{1e100,1e100,1e100},Max{-1e100,-1e100,-1e100};};
        std::vector<PartBox> boxes;std::vector<Obstacle> obstacles;
        Array definitions;
        const auto partNodes=machine.HeadMotion.at("headNodes").To<Array>();
        for(std::size_t part=0;part<partNodes.size();++part){const int node=partNodes[part].To<int>();
            for(const auto& value:machine.CollisionDefinitions){const auto declaration=value.To<Map>();
                if(String(declaration,"id")!=machine.Nodes[node].ID)continue;
                for(const auto& attachment:declaration.at("geometry").To<Array>()){
                    const auto a=attachment.To<Map>();if(a.contains("enabled")&&!a.at("enabled").To<bool>())continue;
                    const Map local{{"collisionBoxes",Array{attachment}},{"tcpLocalPosition",Array{0.,0.,0.}},{"beamLocalDirection",Array{0.,0.,-1.}}};
                    for(const auto& box:DeclaredHeadCollisionBoxes(local)){
                        Map identity{{"index",std::to_string(boxes.size())},{"partIndex",std::to_string(part)},
                            {"partId",machine.Nodes[node].ID},{"partName",String(declaration,"name")},{"boxId",String(a,"id")},{"boxName",String(a,"name")}};
                        definitions.emplace_back(identity);
                        boxes.push_back({node,box,Motion::Length(box.Center)+Motion::Length(box.HalfExtents),distanceService->PrepareBoxes({box}),identity});
                    }
                }
            }
        }
        if(boxes.size()!=input.BaseHeadBoxCount)throw std::runtime_error("CAM_HEAD_ASSEMBLY_BOX_COUNT_MISMATCH");
        const auto beamRotation=R::Between(RegistrationPoint(head.at("beamLocalDirection")),{0,0,-1});
        const R::Quaternion inverse{-beamRotation[0],-beamRotation[1],-beamRotation[2],beamRotation[3]};
        for(std::size_t i=input.BaseHeadBoxCount;i<input.Query.Boxes.size();++i){auto box=input.Query.Boxes[i];
            box.Center=R::Add(R::Rotate(inverse,box.Center),RegistrationPoint(head.at("tcpLocalPosition")));
            for(auto& axis:box.Axes)axis=R::Rotate(inverse,axis);
            Map identity{{"index",std::to_string(i)},{"partId",machine.Nodes[machine.Target.Tool].ID},{"partName",Text("附加包络")},{"boxId",Text("additional-envelope")}};
            definitions.emplace_back(identity);boxes.push_back({machine.Target.Tool,box,Motion::Length(box.Center)+Motion::Length(box.HalfExtents),distanceService->PrepareBoxes({box}),identity});
        }
        for(const auto& obstacle:input.Query.Obstacles){
            if(!obstacle.Mesh)throw std::invalid_argument("CAM_COLLISION_MESH_REQUIRED");
            double radius=0;for(const auto& point:obstacle.Mesh->Vertices)radius=std::max(radius,Motion::Length(point));
            radius+=obstacle.Mesh->Padding+Motion::Length({obstacle.Transform[3],obstacle.Transform[7],obstacle.Transform[11]});
            Obstacle prepared{obstacle.Id,obstacle.Transform,radius,distanceService->PrepareMesh(*obstacle.Mesh,[&]{return scene.IsStopRequested();})};
            for(const auto& point:obstacle.Mesh->Vertices)for(int k=0;k<3;++k){prepared.Min[k]=std::min(prepared.Min[k],point[k]);prepared.Max[k]=std::max(prepared.Max[k],point[k]);}
            for(int k=0;k<3;++k){prepared.Min[k]-=obstacle.Mesh->Padding;prepared.Max[k]+=obstacle.Mesh->Padding;}obstacles.push_back(std::move(prepared));
        }
        K::Snapshot model;K::Task task;Array nodes;
        task.ToolFrameID=machine.Nodes.at(machine.Target.Tool).ID;task.ReferenceFrameID=machine.Nodes.at(machine.Target.Support).ID;
        task.TCP=machine.Target.TCP;task.Beam=machine.Target.Beam;
        for(const auto& n:machine.Nodes){K::Node k;k.ID=n.ID;k.Parent=n.Parent;k.Origin=n.Origin;k.Axis=n.Axis;k.Lower=n.Lower;k.Upper=n.Upper;k.ReferenceValue=n.Type=="fixed"?0:n.Position;
            k.Type=n.Type=="fixed"?K::JointType::Fixed:n.Type=="prismatic"?K::JointType::Prismatic:n.Type=="continuous"?K::JointType::Continuous:K::JointType::Revolute;model.Nodes.push_back(k);
            nodes.emplace_back(Map{{"id",n.ID},{"parent",n.Parent},{"origin",MachineNumbers(n.Origin)},{"axis",MachineNumbers(n.Axis)},{"type",n.Type}});
        }
        const auto built=kinematics->BuildModel(model);
        const auto instanceID=iCAX::Data::to_string(setup->GetHeadInstanceID());
        const auto configure=[machine,boxes,obstacles,kinematics,built,task,distanceService,definitions,nodes,partNodes,instanceID](SimulationCollisionSnapshot& input){
        std::vector<Pose> frames;
        for(const auto& value:input.Frames){const auto f=value.To<Map>();Pose pose;pose.Position=RegistrationPoint(f.at("position"));const auto q=f.at("rotation").To<Array>();for(int k=0;k<4;++k)pose.Rotation[k]=RegistrationNumber(q[k]);
            frames.push_back(pose);}
        const auto keys=input.KinematicKeyframes;
        if(keys.empty())throw std::runtime_error("CAM_HEAD_KEYPOSES_REQUIRED");
        // Each keypose interval owns its parameterization. A changed length in
        // an earlier cut must not perturb another cut through prefix-sum rounding.
        std::vector<double> fractions(frames.size());
        for(std::size_t key=1;key<keys.size();++key){const auto a=keys[key-1],b=keys[key];double length=0;
            for(auto i=a+1;i<=b;++i){length+=R::Length(R::Sub(frames[i].Position,frames[i-1].Position));fractions[i]=length;}
            for(auto i=a+1;i<b;++i)fractions[i]=length>1e-12?fractions[i]/length:double(i-a)/(b-a);
        }
        input.HeadCoverage={{"scope",Text("CompleteSelectedHeadAssembly")},{"headInstanceId",instanceID},
            {"boxCount",std::to_string(boxes.size())},{"boxes",definitions},{"nodes",nodes},{"headNodes",partNodes},{"support",machine.Target.Support},
            {"motion",Text("KeyposeJointInterpolationWithOriginalTCP")},{"tcp",MachineNumbers(task.TCP)},{"tool",machine.Target.Tool},
            {"keyposeCount",std::to_string(keys.size())},{"inverseSolveCount",Text("0")},
            {"machineInstanceIncluded",false},{"machineReady",false}};
        input.HeadDiagnostics.assign(input.FullSegmentCount,Map{});input.InterpolationStarts.clear();input.KeyposeChecks.clear();input.InterpolationChecks.clear();
        const auto scheduled=KeyposeCollision::Intervals(keys,input.SegmentIndices);
        for(const auto& job:scheduled)for(const auto frame:{job.First,job.Last})if(std::find(input.KeypointFrames.begin(),input.KeypointFrames.end(),frame)==input.KeypointFrames.end()){
            input.KeypointFrames.push_back(frame);input.KeypointSegments.push_back({frames.at(frame),frames.at(frame),input.Query.Segments.at(job.Rows.front()).Clearance,true});
        }
        input.KeyposeChecks.resize(input.KeypointFrames.size());
        for(const auto& job:scheduled)input.InterpolationStarts.push_back(input.SegmentIndices.at(job.Rows.front()));
        input.InterpolationChecks.resize(scheduled.size());
        struct Shared {
            std::mutex Mutex;
            std::map<std::size_t,std::vector<double>> Solutions;
            std::map<std::size_t,Text> Errors;
            std::map<std::size_t,Collision::RigidMotionCollisionSegment> Verdicts;
            std::map<std::size_t,Map> Poses;
            std::atomic_size_t Solves=0;
        };
        auto shared=std::make_shared<Shared>();auto* output=&input;
        input.CheckFullHead=[machine,boxes,obstacles,kinematics,built,task,distanceService,output,frames,keys,fractions,scheduled,shared]
            (const auto& segments,bool keypoints,std::optional<std::size_t> singleFrame,const auto& cancelled,const auto& started,const auto& completed){
            Result result;result.Segments.resize(segments.size());std::atomic_size_t queries=0;
            const auto diagnostic=[&](const auto& joints,const auto& pose){
                const auto world=Motion::Forward(machine.Nodes,joints);const auto supportInverse=Motion::Inverse(world.at(machine.Target.Support));
                Array transforms,placed;for(const auto& node:machine.HeadMotion.at("headNodes").To<Array>())transforms.emplace_back(MachineNumbers(Motion::Multiply(supportInverse,world.at(node.To<int>()))));
                const R::Quaternion inverse{-pose.Rotation[0],-pose.Rotation[1],-pose.Rotation[2],pose.Rotation[3]};
                for(std::size_t i=0;i<boxes.size();++i){const auto& b=boxes[i];const auto transform=Motion::Multiply(supportInverse,world[b.Node]);Array axes;
                    for(const auto& axis:b.Box.Axes)axes.emplace_back(GenPoint(R::Rotate(inverse,Motion::Transform(transform,axis,true))));
                    placed.emplace_back(Map{{"index",std::to_string(i)},{"center",GenPoint(R::Rotate(inverse,R::Sub(Motion::Transform(transform,b.Box.Center),pose.Position)))},
                        {"halfExtents",GenPoint(b.Box.HalfExtents)},{"axes",axes},{"source",Text(i<output->BaseHeadBoxCount?"HeadAssembly":"AdditionalEnvelope")}});
                }
                return Map{{"partTransforms",transforms},{"boxes",placed},{"frame",Text("Setup")},{"joints",MachineNumbers(joints)}};
            };
            std::vector<KeyposeCollision::Interval> jobs;
            if(singleFrame)jobs.push_back({*singleFrame,std::min(*singleFrame+1,frames.size()-1),{0}});
            else if(keypoints)for(std::size_t i=0;i<segments.size();++i)jobs.push_back({output->KeypointFrames[i],output->KeypointFrames[i],{i}});
            else jobs=scheduled;
            const auto run=[&](std::size_t jobIndex){
                const auto& job=jobs[jobIndex];const auto row=job.Rows.front();
                if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");if(started)started(row);
                Collision::RigidMotionCollisionSegment verdict;Map hitPose;double hitFraction=0;bool skippedEndpoint=false;
                double clearance=0;bool connected=true;
                for(const auto i:job.Rows){clearance=std::max(clearance,segments[i].Clearance);connected&=segments[i].Connected;}
                std::vector<double> knots(job.Last-job.First+1);double length=0,radius=0;
                for(auto i=job.First;i<=job.Last;++i){radius=std::max(radius,R::Length(frames[i].Position));if(i>job.First){length+=R::Length(R::Sub(frames[i].Position,frames[i-1].Position));knots[i-job.First]=length;}}
                for(std::size_t i=1;i<knots.size();++i)knots[i]=length>1e-12?knots[i]/length:double(i)/(knots.size()-1);
                const auto location=[&](double t){
                    if(job.First==job.Last)return std::pair{job.First,0.};
                    const auto upper=std::upper_bound(knots.begin(),knots.end(),t);
                    const auto i=std::min<std::size_t>(std::max<std::size_t>(1,upper-knots.begin())-1,knots.size()-2);
                    return std::pair{job.First+i,knots[i+1]>knots[i]?(t-knots[i])/(knots[i+1]-knots[i]):0.};
                };
                const auto at=[&](double t){const auto [i,f]=location(t);const auto j=std::min(i+1,job.Last);Pose pose;
                    pose.Position=R::Add(frames[i].Position,R::Scale(R::Sub(frames[j].Position,frames[i].Position),f));
                    pose.Rotation=R::Interpolate(frames[i].Rotation,frames[j].Rotation,f);return pose;};
                try{
                    if(!connected)throw std::runtime_error("MOTION_PATH_DISCONNECTED");
                    // Keypoint results are immutable once the first phase joins.
                    if(!keypoints&&!singleFrame){
                        std::lock_guard lock(shared->Mutex);
                        for(const auto key:{job.First,job.Last})if(const auto found=shared->Verdicts.find(key);found!=shared->Verdicts.end()&&found->second.Status!="Clear"){
                            verdict=found->second;hitFraction=key==job.First?0:1;hitPose=shared->Poses.at(key);skippedEndpoint=true;break;
                        }
                    }
                    const bool skipped=skippedEndpoint;
                    if(!skipped){
                        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(output->Query.MaxSegmentTimeMs);
                        const auto checkStop=[&]{if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("CAM_HEAD_COLLISION_BUDGET");};
                        const auto solveKey=[&](std::size_t key){
                            {std::lock_guard lock(shared->Mutex);if(shared->Errors.contains(key))throw std::runtime_error(shared->Errors.at(key));if(shared->Solutions.contains(key))return shared->Solutions.at(key);}
                            checkStop();K::Request request;request.TaskDefinition=task;request.Goal.Direction=R::Unit(R::Rotate(frames.at(key).Rotation,{0,0,-1}));
                            request.MaxTimeMs=std::min(500.,std::chrono::duration<double,std::milli>(deadline-std::chrono::steady_clock::now()).count());
                            request.MaxSeeds=8;request.MaxCandidates=1;request.PositionToleranceMm=.0001;request.DirectionToleranceRad=1e-7;
                            auto seed=MachineJointValues(machine);const auto initial=Motion::Forward(machine.Nodes,seed);
                            const auto tcp=Motion::Transform(initial.at(machine.Target.Tool),task.TCP),origin=Motion::Transform(initial.at(machine.Target.Support),{0,0,0});
                            for(int k=0;k<3;++k)seed.at(1+k)+=origin[k]-tcp[k];request.Seeds={seed};++shared->Solves;
                            const auto solved=kinematics->InverseCandidates(built,request,[&]{return (cancelled&&cancelled())||std::chrono::steady_clock::now()>=deadline;});
                            std::lock_guard lock(shared->Mutex);
                            if(solved.Candidates.empty()){shared->Errors[key]=solved.State==K::Status::BudgetExhausted?"CAM_HEAD_IK_BUDGET":solved.State==K::Status::NoCandidate?"CAM_HEAD_LIMIT_NO_SOLUTION":"CAM_HEAD_JOINT_POSE_UNRESOLVED";throw std::runtime_error(shared->Errors.at(key));}
                            return shared->Solutions.emplace(key,solved.Candidates.front().Positions).first->second;
                        };
                        const auto interpolated=[&](std::size_t frame){const auto upper=std::lower_bound(keys.begin(),keys.end(),frame);
                            if(upper==keys.end())return solveKey(keys.back());if(*upper==frame||upper==keys.begin())return solveKey(*upper);
                            auto a=solveKey(*std::prev(upper));const auto b=solveKey(*upper);for(std::size_t k=0;k<a.size();++k)a[k]+=(b[k]-a[k])*fractions[frame];return a;};
                        auto first=interpolated(job.First),last=interpolated(job.Last);for(int k=1;k<=3;++k)first[k]=last[k]=0;
                        const auto placed=[&](std::vector<double> q,double t){for(int k=1;k<=3;++k)q[k]=0;const auto world=Motion::Forward(machine.Nodes,q);
                            const auto requested=Motion::Transform(world.at(machine.Target.Support),at(t).Position),tcp=Motion::Transform(world.at(machine.Target.Tool),task.TCP);
                            for(int k=0;k<3;++k){q[k+1]=requested[k]-tcp[k];if(q[k+1]<machine.Nodes[k+1].Lower||q[k+1]>machine.Nodes[k+1].Upper)throw std::runtime_error("CAM_HEAD_JOINT_POSE_UNRESOLVED");}return q;};
                        auto sweptFirst=first,sweptLast=last;sweptFirst.push_back(0);sweptLast.push_back(1);
                        bool unresolved=false;
                        for(std::size_t boxIndex=0;boxIndex<boxes.size()&&verdict.Status!="Collision";++boxIndex){const auto& box=boxes[boxIndex];
                            for(const auto& obstacle:obstacles){checkStop();
                                // Arc-length parameterization bounds the original TCP polyline,
                                // including bends that leave the endpoint chord.
                                const auto bound=Motion::RelativeSweepBound(machine.Nodes,box.Node,box.Radius,machine.Target.Support,obstacle.Radius,first,last)+length
                                    +Motion::SweepBound(machine.Nodes,machine.Target.Tool,Motion::Length(task.TCP),first,last)
                                    +Motion::SweepBound(machine.Nodes,machine.Target.Support,radius,first,last);
                                struct ContactFound{};bool certified=false;
                                try{certified=Motion::CertifySweep(sweptFirst,sweptLast,bound,clearance,[&](auto values){
                                    const auto t=values.back();values.pop_back();const auto joints=placed(std::move(values),t);checkStop();const auto world=Motion::Forward(machine.Nodes,joints);
                                    const auto fixed=Motion::Multiply(world.at(machine.Target.Support),obstacle.Local),relative=Motion::Multiply(Motion::Inverse(fixed),world.at(box.Node));
                                    const auto center=Motion::Transform(relative,box.Box.Center);Motion::Vector extent{};
                                    for(int k=0;k<3;++k)for(int j=0;j<3;++j)extent[k]+=std::abs(Motion::Transform(relative,box.Box.Axes[j],true)[k])*box.Box.HalfExtents[j];
                                    double square=0;for(int k=0;k<3;++k){const auto gap=std::max({obstacle.Min[k]-center[k]-extent[k],center[k]-extent[k]-obstacle.Max[k],0.});square+=gap*gap;}
                                    const auto gap=std::sqrt(square);if(gap>clearance)return gap;++queries;
                                    const auto distance=distanceService->Separation(obstacle.Model,box.Model,relative,[&]{checkStop();return false;});
                                    if(distance.Overlapping||distance.UpperBound<=clearance){
                                        verdict.Status="Collision";verdict.Reason="CAM_HEAD_ASSEMBLY_COLLISION";verdict.Obstacle=obstacle.ID;hitFraction=t;hitPose=diagnostic(joints,at(t));
                                        if(distance.Witness){const auto& w=*distance.Witness;Collision::RigidMotionCollisionWitness out;out.BoxIndex=boxIndex;
                                            out.BodyPoint=Motion::Transform(obstacle.Local,w.PointOnMoving);out.ObstaclePoint=Motion::Transform(obstacle.Local,w.PointOnFixed);
                                            out.Separation=w.Separation;out.SurfaceDistance=w.SurfaceDistance;out.RequiredClearance=clearance;out.Containment=w.Containment;verdict.Witness=out;}
                                        throw ContactFound{};
                                    }return distance.LowerBound;
                                }).Clear;}catch(const ContactFound&){}
                                if(verdict.Status=="Collision")break;if(!certified)unresolved=true;
                            }
                        }
                        if(verdict.Status!="Collision"){verdict.Status=unresolved?"Unchecked":"Clear";verdict.Reason=unresolved?"CAM_HEAD_SWEEP_UNRESOLVED":"";verdict.CertifiedClearance=unresolved?0:clearance;}
                        if(keypoints){if(hitPose.empty())hitPose=diagnostic(placed(first,0),frames[job.First]);}
                        else for(const auto i:job.Rows){const auto global=singleFrame?*singleFrame:output->SegmentIndices[i];const auto t=knots.at(global-job.First);
                            auto q=first;for(std::size_t k=0;k<q.size();++k)q[k]+=(last[k]-q[k])*t;
                            auto pose=diagnostic(placed(q,t),frames[global]);auto end=q;const auto t2=knots.at(std::min(global+1,job.Last)-job.First);
                            for(std::size_t k=0;k<end.size();++k)end[k]=first[k]+(last[k]-first[k])*t2;
                            pose["motion"]=Array{Map{{"fraction",0.},{"joints",MachineNumbers(q)}},Map{{"fraction",1.},{"joints",MachineNumbers(end)}}};output->HeadDiagnostics.at(global)=std::move(pose);
                        }
                    }
                    if(skipped)verdict.Reason=verdict.Status=="Collision"?"CAM_HEAD_KEYPOSE_COLLISION":"CAM_HEAD_KEYPOSE_UNVERIFIED";
                }catch(const std::exception& error){if(cancelled&&cancelled())throw;verdict.Status="Unchecked";verdict.Reason=error.what();}
                const auto [hitFrame,localFraction]=location(hitFraction);verdict.Fraction=localFraction;
                if(keypoints){
                    result.Segments[row]=verdict;auto record=CollisionSegmentState(job.First,verdict);record["location"]=output->KeyposeLocations.at(job.First);record["frameIndex"]=std::to_string(job.First);if(!hitPose.empty())record["headPose"]=hitPose;
                    output->KeyposeChecks[row]=record;std::lock_guard lock(shared->Mutex);shared->Verdicts[job.First]=verdict;shared->Poses[job.First]=hitPose;
                }else{
                    for(const auto i:job.Rows){auto copy=verdict;const auto global=singleFrame?*singleFrame:output->SegmentIndices[i];
                        if(verdict.Status=="Collision"&&global!=hitFrame){copy.Status="Unchecked";copy.Reason=verdict.Reason=="CAM_HEAD_KEYPOSE_COLLISION"?"CAM_HEAD_INTERVAL_SKIPPED_KEYPOSE":"CAM_HEAD_INTERVAL_COLLISION";copy.Witness.reset();copy.Obstacle.clear();copy.Fraction=0;}
                        result.Segments[i]=copy;if(global==hitFrame&&!hitPose.empty())output->HeadDiagnostics.at(global)=hitPose;
                    }
                    if(!singleFrame){auto record=CollisionSegmentState(hitFrame,verdict);record["first"]=output->KeyposeLocations.at(job.First);record["last"]=output->KeyposeLocations.at(job.Last);
                        record["firstFrameIndex"]=std::to_string(job.First);record["lastFrameIndex"]=std::to_string(job.Last);
                        if(skippedEndpoint){record["status"]=Text("Skipped");record["reason"]=Text("CAM_HEAD_INTERVAL_SKIPPED_KEYPOSE");record.erase("witness");}
                        if(!hitPose.empty())record["headPose"]=hitPose;output->InterpolationChecks[jobIndex]=std::move(record);}
                }
                if(completed)completed(row,verdict);
            };
            KeyposeCollision::ParallelKeyposes(jobs.size(),keypoints?output->ParallelWorkers:1u,cancelled,run);
            output->HeadCoverage["inverseSolveCount"]=std::to_string(shared->Solves.load());
            output->HeadCoverage["interpolationTaskCount"]=std::to_string(scheduled.size());
            Array motion;for(const auto frame:keys){Map knot{{"frameIndex",std::to_string(frame)}};if(shared->Solutions.contains(frame))knot["joints"]=MachineNumbers(shared->Solutions.at(frame));else if(shared->Errors.contains(frame))knot["reason"]=shared->Errors.at(frame);motion.emplace_back(std::move(knot));}
            output->HeadCoverage["keyposes"]=std::move(motion);
            result.DistanceQueries=queries.load();return result;
        };
        };
        input.ConfigureFullHead=configure;configure(input);
    }
}
