        RigidMotionCollisionQuery BoxMotionQuery(const RapidPathPlanningQuery& query)const{
            const auto& environment=*query.Collision;RigidMotionCollisionQuery collision;
            collision.Boxes=environment.Boxes;collision.Obstacles=environment.Obstacles;collision.MaxSegmentTimeMs=5000;
            const auto calibration=Transform(environment.Calibration);const auto rotation=calibration.GetRotation();
            collision.BodyCalibration.Position={environment.Calibration[3],environment.Calibration[7],environment.Calibration[11]};
            collision.BodyCalibration.Rotation={rotation.X(),rotation.Y(),rotation.Z(),rotation.W()};return collision;
        }
        RapidPathPlanningResult PlanBoxes(const RapidPathPlanningQuery& query,const std::function<bool()>& cancelled,
            std::unique_ptr<IRigidMotionCollisionSession> session={})const{
            namespace M=RigidMotion;const auto check=[&]{if(cancelled&&cancelled())throw std::runtime_error("TRAVEL_CANCELLED");};
            const auto& environment=*query.Collision;
            if(environment.Body||!environment.Solids.empty()||environment.Boxes.empty()||environment.Obstacles.empty()||!std::isfinite(query.Clearance)||query.Clearance<0)throw std::invalid_argument("RAPID_COLLISION_INPUT_REQUIRED");
            const auto collision=BoxMotionQuery(query);if(!session)session=MotionCollision->CreatePrimitiveMeshSession(collision,cancelled);
            const auto calibration=Transform(environment.Calibration);
            const auto poses=[&](const std::vector<TravelPose>& points){std::vector<RigidMotionPose> result;result.reserve(points.size());M::Vector previous{0,0,1};M::Quaternion orientation{0,0,0,1};
                for(const auto& point:points){const auto direction=M::Unit(point.Direction);orientation=M::Multiply(M::Between(previous,direction),orientation);previous=direction;result.push_back({point.Point,orientation});}return result;};
            RapidPathPlanningResult result;auto candidate=query.Geometry;const auto original=Construction->Generate({candidate},cancelled).front();
            if(!original.Generated){result.Reason=original.Reason;return result;}
            // Keep the authored construction visible and editable even when
            // endpoint checks or every avoidance candidate fail. Qualification
            // remains false until a complete route is independently cleared.
            static_cast<RapidPathGeometryResult&>(result)=original;result.Generated=false;result.DetourOffset=candidate.LateralOffset;
            const auto initial=poses(original.Poses);if(initial.empty())throw std::invalid_argument("RAPID_POSES_REQUIRED");
            const auto endpoints=session->Check({{initial.front(),initial.front(),query.Clearance,true},{initial.back(),initial.back(),query.Clearance,true}},cancelled,true);
            for(const auto& status:endpoints.Segments)if(status.Status!="Clear"){result.Obstacle=status.Obstacle;result.Reason=status.Status=="Collision"?"RAPID_ENDPOINT_BLOCKED":status.Reason;return result;}
            if(endpoints.Segments.size()!=2)throw std::runtime_error("MOTION_RESULT_INCOMPLETE");
            const auto better=[](const RapidPathGeometryResult& a,const RapidPathGeometryResult& b){return a.Length<b.Length-1e-7||(std::abs(a.Length-b.Length)<=1e-7&&a.EstimatedSeconds<b.EstimatedSeconds-1e-9);};
            const auto attempt=[&](const RapidPathGeometryResult& built,const V& offset){check();++result.Candidates;
                if(!built.Generated){if(!result.Generated)result.Reason=built.Reason;return false;}
                const auto path=poses(built.Poses);if(path.empty())throw std::invalid_argument("RAPID_POSES_REQUIRED");std::vector<RigidMotionSegment> segments;segments.reserve(path.size());
                for(std::size_t i=1;i<path.size();++i)segments.push_back({path[i-1],path[i],query.Clearance,true});
                if(segments.empty())segments.push_back({path.front(),path.front(),query.Clearance,true});
                const auto checked=session->Check(segments,cancelled,true);double minimum=std::numeric_limits<double>::infinity();
                for(const auto& status:checked.Segments){if(status.Status!="Clear"){if(!result.Generated){result.Obstacle=status.Obstacle;result.Reason=status.Reason;}return false;}minimum=std::min(minimum,status.CertifiedClearance);}
                if(checked.Segments.size()!=segments.size())throw std::runtime_error("MOTION_RESULT_INCOMPLETE");
                if(!result.Generated||better(built,result)){
                    static_cast<RapidPathGeometryResult&>(result)=built;result.Status="Clear";result.Reason.clear();result.Obstacle.clear();result.CertifiedClearance=minimum;
                    result.DetourOffset=offset;result.Method=candidate.Manual?"Manual":built.Construction&&built.Construction->TurnPoint?"CornerBridge":Norm(offset)>1e-9?"LateralDetour":"Direct";
                }return true;
            };
            if(attempt(original,candidate.LateralOffset))return result;
            if(candidate.Manual){result.Reason="RAPID_MANUAL_PATH_BLOCKED";return result;}
            if(candidate.Kind==RapidPathKind::Retract){
                // Rotate while travelling to a corner of the ORIGINAL lifted
                // anchor box. No coordinate goes outside those lift anchors,
                // and the neutral constructor owns every returned pose/axis.
                const auto a=original.Poses.at(1).Point,b=original.Poses.at(original.Poses.size()-2).Point;
                struct Corner{V Point;RapidPathGeometryResult Built;};std::vector<Corner> corners;
                for(unsigned bits=0;bits<8;++bits){V point;for(int axis=0;axis<3;++axis)point[axis]=bits&(1u<<axis)?b[axis]:a[axis];
                    if(point==a||point==b||std::any_of(corners.begin(),corners.end(),[&](const auto& c){return c.Point==point;}))continue;
                    check();candidate.TurnPoint=point;candidate.LateralOffset={};corners.push_back({point,Construction->Generate({candidate},cancelled).front()});
                }
                std::stable_sort(corners.begin(),corners.end(),[](const auto& x,const auto& y){if(x.Built.Generated!=y.Built.Generated)return x.Built.Generated;
                    if(x.Built.Length!=y.Built.Length)return x.Built.Length<y.Built.Length;return x.Built.EstimatedSeconds<y.Built.EstimatedSeconds;});
                for(const auto& corner:corners){candidate.TurnPoint=corner.Point;if(attempt(corner.Built,{}))break;}
                candidate.TurnPoint.reset();candidate.LateralOffset={};
            }
            if(result.Generated&&Norm(Add(candidate.Start.Direction,candidate.End.Direction))<1e-9)return result;
            const auto up=Unit(Add(candidate.Start.Direction,candidate.End.Direction));auto forward=Sub(candidate.End.Point,candidate.Start.Point);forward=Sub(forward,Mul(up,Dot(forward,up)));
            if(Norm(forward)<1e-9){const V helper=std::abs(up[0])<.8?V{1,0,0}:V{0,1,0};forward=Sub(helper,Mul(up,Dot(helper,up)));}forward=Unit(forward);
            const V side{up[1]*forward[2]-up[2]*forward[1],up[2]*forward[0]-up[0]*forward[2],up[0]*forward[1]-up[1]*forward[0]};
            double radial=1;for(const auto& box:environment.Boxes)for(int bits=0;bits<8;++bits){auto corner=box.Center;
                for(int axis=0;axis<3;++axis)corner=M::Add(corner,M::Scale(box.Axes[axis],box.HalfExtents[axis]*(bits&(1<<axis)?1:-1)));
                const auto point=gp_Pnt(corner[0],corner[1],corner[2]).Transformed(calibration);radial=std::max(radial,std::hypot(point.X(),point.Y()));}
            Bnd_Box bounds;for(const auto& obstacle:environment.Obstacles){check();if(!obstacle.Mesh)throw std::invalid_argument("MOTION_COLLISION_MESH_REQUIRED");const auto transform=Transform(obstacle.Transform);
                for(const auto& point:obstacle.Mesh->Vertices)bounds.Add(gp_Pnt(point[0],point[1],point[2]).Transformed(transform));}
            double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);const auto span=Norm({x1-x0,y1-y0,z1-z0});
            std::vector<double> radii{std::max(2.,radial*1.5),std::max(2.,span*.125),std::max(2.,span*.25),std::max(2.,span*.5),std::max(2.,span)};
            std::sort(radii.begin(),radii.end());radii.erase(std::unique(radii.begin(),radii.end()),radii.end());
            struct Alternative{V Offset;RapidPathGeometryResult Built;};std::vector<Alternative> alternatives;
            for(const auto radius:radii)for(const auto direction:{0,4,1,7,3,5,2,6})alternatives.push_back({Mul(Add(Mul(side,std::cos(direction*Pi/4)),Mul(forward,std::sin(direction*Pi/4))),radius),{}});
            // Retract construction is algebraic and cheap. Follow construction
            // projects to exact CAD, so those candidates stay demand-driven.
            if(candidate.Kind==RapidPathKind::Retract){
                for(auto& alternative:alternatives){check();candidate.LateralOffset=alternative.Offset;alternative.Built=Construction->Generate({candidate},cancelled).front();}
                std::stable_sort(alternatives.begin(),alternatives.end(),[](const auto& a,const auto& b){if(a.Built.Generated!=b.Built.Generated)return a.Built.Generated;if(a.Built.Length!=b.Built.Length)return a.Built.Length<b.Built.Length;return a.Built.EstimatedSeconds<b.Built.EstimatedSeconds;});
            }
            for(auto& alternative:alternatives){candidate.LateralOffset=alternative.Offset;
                if(candidate.Kind!=RapidPathKind::Retract)alternative.Built=Construction->Generate({candidate},cancelled).front();
                if(result.Generated&&!better(alternative.Built,result))continue;
                if(attempt(alternative.Built,alternative.Offset))break;
            }
            if(!result.Generated){result.Status="Unchecked";result.Reason="RAPID_NO_FEASIBLE_ROUTE_WITHIN_PARAMETERS";return result;}
            if(result.Method=="CornerBridge")return result;
            // Feasibility need not be monotone. Keep the last fully certified
            // route and replace it only with a shorter, independently checked
            // construction; a failed probe never discards that safe route.
            const auto seed=result.DetourOffset;double low=0,high=1;
            for(int step=0;step<6;++step){check();const auto scale=(low+high)*.5;candidate.LateralOffset=Mul(seed,scale);
                const auto built=Construction->Generate({candidate},cancelled).front();
                if(!built.Generated||built.Length>=result.Length-1e-7){low=scale;continue;}
                if(attempt(built,candidate.LateralOffset))high=scale;else low=scale;
            }
            return result;
        }
