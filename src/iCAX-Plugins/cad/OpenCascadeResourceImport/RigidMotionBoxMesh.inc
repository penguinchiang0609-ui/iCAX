    struct PrimitiveMeshState {
        CollisionDistanceModelRef Body;gp_Trsf Calibration;Bnd_Box BodyBounds;double Radius=0;
        struct Obstacle{std::string Id;CollisionDistanceModelRef Model;gp_Trsf Inverse;Bnd_Box Bounds;};
        std::vector<Obstacle> Obstacles;
        struct Distance{double Lower=0,Upper=std::numeric_limits<double>::infinity();std::string Id;bool Exact=false;std::optional<RigidMotionCollisionWitness> Witness;};
        std::map<std::array<double,7>,Distance> Cache;
    };
    std::unique_ptr<PrimitiveMeshState> PreparePrimitiveMesh(const RigidMotionCollisionQuery& q,const ICollisionDistanceService& collision,
        const std::function<CollisionDistanceModelRef(const std::shared_ptr<const CollisionMesh>&,const std::function<bool()>&)>& prepareMesh,
        const std::function<bool()>& cancelled)
    {
        if(q.Body||q.Boxes.empty()==q.Solids.empty())throw std::invalid_argument("MOTION_COLLIDER_REPRESENTATION_INVALID");
        if(q.Obstacles.empty()||q.Obstacles.size()>128)throw std::invalid_argument("MOTION_INPUT_BUDGET");
        const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");};
        auto state=std::make_unique<PrimitiveMeshState>();
        std::vector<CollisionFrustum> frusta;for(const auto& s:q.Solids)frusta.push_back({s.Z,s.Height,s.LowerRadius,s.UpperRadius});
        state->Body=frusta.empty()?collision.PrepareBoxes(q.Boxes,cancelled):collision.PrepareFrusta(frusta);state->Calibration=Transform(q.BodyCalibration);
        for(const auto& box:q.Boxes)for(int bits=0;bits<8;++bits){auto point=box.Center;
            for(int axis=0;axis<3;++axis)point=M::Add(point,M::Scale(box.Axes[axis],std::max(box.HalfExtents[axis],1e-9)*(bits&(1<<axis)?1:-1)));
            state->BodyBounds.Add(gp_Pnt(point[0],point[1],point[2]));const auto transformed=gp_Pnt(point[0],point[1],point[2]).Transformed(state->Calibration);
            state->Radius=std::max(state->Radius,M::Length({transformed.X(),transformed.Y(),transformed.Z()}));
        }
        for(const auto& s:q.Solids){const auto radial=std::max(s.LowerRadius,s.UpperRadius);
            for(double x:{-radial,radial})for(double y:{-radial,radial})for(double z:{s.Z,s.Z+s.Height})state->BodyBounds.Add(gp_Pnt(x,y,z));
            state->Radius=std::max(state->Radius,std::hypot(std::max(std::abs(s.Z),std::abs(s.Z+s.Height)),radial)+M::Length(q.BodyCalibration.Position));}
        for(const auto& source:q.Obstacles){check();if(!source.Mesh)throw std::invalid_argument("MOTION_COLLISION_MESH_REQUIRED");
            PrimitiveMeshState::Obstacle obstacle{source.Id,prepareMesh(source.Mesh,cancelled),Transform(source.Transform).Inverted(),{}};
            for(const auto& p:source.Mesh->Vertices)obstacle.Bounds.Add(gp_Pnt(p[0],p[1],p[2]));obstacle.Bounds.Enlarge(source.Mesh->Padding);state->Obstacles.push_back(std::move(obstacle));
        }
        return state;
    }
    RigidMotionCollisionResult CalculatePrimitiveMesh(const RigidMotionCollisionQuery& q,const ICollisionDistanceService& collision,
        const std::function<CollisionDistanceModelRef(const std::shared_ptr<const CollisionMesh>&,const std::function<bool()>&)>& prepareMesh,
        const std::function<bool()>& cancelled,const std::function<void(std::size_t,std::size_t)>& progress,
        const std::function<std::optional<std::size_t>()>& nextSegment,
        const std::function<void(std::size_t,const RigidMotionCollisionSegment&)>& receive,
        PrimitiveMeshState* prepared=nullptr,bool stopAtFirstFailure=false)
    {
        if(q.Body||q.Boxes.empty()==q.Solids.empty())throw std::invalid_argument("MOTION_COLLIDER_REPRESENTATION_INVALID");
        if(q.Obstacles.empty()||q.Obstacles.size()>128||q.Segments.empty()||q.Segments.size()>100000)throw std::invalid_argument("MOTION_INPUT_BUDGET");
        if(q.MaxSegmentTimeMs<1||q.MaxSegmentTimeMs>60000)throw std::invalid_argument("MOTION_TIME_BUDGET_INVALID");
        auto deadline=std::chrono::steady_clock::time_point::max();
        const auto cancelCheck=[&]{if(cancelled&&cancelled())throw std::runtime_error("MOTION_CANCELLED");};
        const auto check=[&]{cancelCheck();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("MOTION_TIME_BUDGET");};
        const auto cancellation=[&]{check();return false;};
        auto owned=prepared?std::unique_ptr<PrimitiveMeshState>{}:PreparePrimitiveMesh(q,collision,prepareMesh,cancellation);auto& state=prepared?*prepared:*owned;
        const auto& body=state.Body;const auto& calibration=state.Calibration;const auto radius=state.Radius;const auto& obstacles=state.Obstacles;auto& cache=state.Cache;
        using Distance=PrimitiveMeshState::Distance;RigidMotionCollisionResult result;
        const auto measure=[&](const RigidMotionPose& pose,double clearance){
            check();const std::array<double,7> key{pose.Position[0],pose.Position[1],pose.Position[2],pose.Rotation[0],pose.Rotation[1],pose.Rotation[2],pose.Rotation[3]};
            if(cache.contains(key)){const auto& prior=cache.at(key);if(prior.Exact||prior.Lower>clearance+1e-7||prior.Upper<=clearance+1e-7)return prior;}
            const auto world=Transform(pose)*calibration;Distance value;value.Lower=std::numeric_limits<double>::infinity();value.Exact=true;
            for(std::size_t i=0;i<obstacles.size();++i){const auto& obstacle=obstacles[i];check();CollisionTransform relative{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};const auto transform=obstacle.Inverse*world;
                for(int row=0;row<3;++row)for(int col=0;col<4;++col)relative[row*4+col]=transform.Value(row+1,col+1);
                // Disjoint complete AABBs also exclude occupied-volume
                // containment. This is only a conservative distance bound;
                // the unchanged interval proof still subtracts body motion.
                const auto coarse=std::max(0.,state.BodyBounds.Transformed(transform).Distance(obstacle.Bounds)-1e-6);
                if(coarse>clearance+1e-6){value.Lower=std::min(value.Lower,coarse);value.Exact=false;continue;}
                const auto bound=collision.LowerBound(obstacle.Model,body,relative,clearance+1e-6,cancellation);result.DistanceQueries+=bound.NarrowPhaseQueries;
                double lower=bound.LowerBound;
                if(lower<=clearance+1e-6){const auto separation=collision.Separation(obstacle.Model,body,relative,cancellation);result.DistanceQueries+=separation.NarrowPhaseQueries;lower=separation.LowerBound;
                    if(separation.UpperBound<value.Upper){value.Upper=separation.UpperBound;value.Id=obstacle.Id;value.Witness.reset();
                        if(separation.Witness){const auto& witness=*separation.Witness;const auto forward=obstacle.Inverse.Inverted();
                            const auto bodyPoint=gp_Pnt(witness.PointOnMoving[0],witness.PointOnMoving[1],witness.PointOnMoving[2]).Transformed(forward),obstaclePoint=gp_Pnt(witness.PointOnFixed[0],witness.PointOnFixed[1],witness.PointOnFixed[2]).Transformed(forward);
                            value.Witness=RigidMotionCollisionWitness{witness.MovingPartIndex,{bodyPoint.X(),bodyPoint.Y(),bodyPoint.Z()},{obstaclePoint.X(),obstaclePoint.Y(),obstaclePoint.Z()},witness.Separation,witness.SurfaceDistance,0,witness.Containment};
                        }
                    }
                }else value.Exact=false;
                value.Lower=std::min(value.Lower,lower);
                // One attained occupied-material witness already rejects this
                // pose. Unvisited obstacles cannot make it clear; report a
                // zero lower bound instead of claiming their global minimum.
                if(value.Upper<=clearance+1e-7){value.Lower=0;value.Exact=false;break;}
            }
            if(!std::isfinite(value.Lower))throw std::runtime_error("MOTION_DISTANCE_FAILED");
            if(cache.size()<100000)cache[key]=value;return value;
        };
        std::size_t nextIndex=0;
        for(;;){deadline=std::chrono::steady_clock::time_point::max();cancelCheck();
            const auto requested=nextSegment?nextSegment():nextIndex<q.Segments.size()?std::optional<std::size_t>(nextIndex++):std::nullopt;
            if(!requested)break;if(*requested>=q.Segments.size())throw std::invalid_argument("MOTION_SEGMENT_OUTSIDE_QUERY");
            const auto& segment=q.Segments[*requested];Validate(segment.Start);Validate(segment.End);
            if(!std::isfinite(segment.Clearance)||segment.Clearance<0||segment.Clearance>1e6)throw std::invalid_argument("MOTION_CLEARANCE_INVALID");
            deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(q.MaxSegmentTimeMs);
            RigidMotionCollisionSegment status;status.CertifiedClearance=std::numeric_limits<double>::infinity();
            if(!segment.Connected){status.Reason="MOTION_DISCONNECTED";status.CertifiedClearance=0;}
            else try{
                const auto test=[&](const RigidMotionPose& pose,double fraction){const auto distance=measure(pose,segment.Clearance);
                    if(distance.Upper<=segment.Clearance+1e-7){status.Status="Collision";status.Reason="MOTION_CLEARANCE_VIOLATION";status.Obstacle=distance.Id;status.Fraction=fraction;status.Witness=distance.Witness;if(status.Witness)status.Witness->RequiredClearance=segment.Clearance;}return distance.Lower;};
                const auto first=test(segment.Start,0);double last=0;if(status.Status!="Collision")last=test(segment.End,1);
                const auto translation=M::Length(M::Sub(segment.End.Position,segment.Start.Position)),angle=M::Angle(segment.Start.Rotation,segment.End.Rotation);
                const auto displacement=translation+2*radius*std::sin(angle*.5),endpoint=std::max(first,last)-displacement-1e-6;
                if(status.Status!="Collision"&&endpoint>=segment.Clearance){status.Status="Clear";status.CertifiedClearance=endpoint;}
                std::size_t intervals=0;
                std::function<bool(double,double,int)> certify=[&](double a,double b,int depth){check();if(++intervals>8192)throw std::runtime_error("MOTION_INTERVAL_BUDGET");
                    const auto middle=(a+b)*.5,distance=test(Interpolate(segment.Start,segment.End,middle),middle);
                    if(status.Status=="Collision")return false;
                    const auto motion=translation*(b-a)*.5+2*radius*std::sin(angle*(b-a)*.25),lower=distance-motion-1e-6;
                    if(lower>=segment.Clearance){status.CertifiedClearance=std::min(status.CertifiedClearance,lower);return true;}
                    if(depth>=18){status.Reason="MOTION_INTERVAL_UNRESOLVED";return false;}
                    return certify(a,middle,depth+1)&&certify(middle,b,depth+1);
                };
                if(status.Status!="Collision"&&status.Status!="Clear"&&certify(0,1,0))status.Status="Clear";
            }catch(const std::exception& error){cancelCheck();status.Status="Unchecked";status.Reason=error.what();}
            if(status.Status!="Clear"||!std::isfinite(status.CertifiedClearance))status.CertifiedClearance=0;
            if(status.Status!="Collision")status.Witness.reset();
            result.Segments.push_back(status);if(receive)receive(*requested,status);if(progress)progress(result.Segments.size(),q.Segments.size());
            if(stopAtFirstFailure&&status.Status!="Clear")break;
        }
        return result;
    }
