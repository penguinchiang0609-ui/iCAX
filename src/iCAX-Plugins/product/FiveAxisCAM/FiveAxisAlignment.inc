// Geometric references are resolved again for every preview and commit. The
// browser supplies intent, never a placement matrix or a solved constraint.
namespace {
    using AlignVector=std::array<double,3>;
    using AlignMatrix=std::array<double,16>;
    AlignVector AlignAdd(AlignVector a,AlignVector b,double scale=1){for(int i=0;i<3;++i)a[i]+=b[i]*scale;return a;}
    double AlignDot(AlignVector a,AlignVector b){double s=0;for(int i=0;i<3;++i)s+=a[i]*b[i];return s;}
    AlignVector AlignCross(AlignVector a,AlignVector b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
    AlignVector AlignUnit(AlignVector a){const auto l=std::sqrt(AlignDot(a,a));if(l<1e-10)throw std::invalid_argument("CAM_ALIGNMENT_DIRECTION_REQUIRED");for(auto& v:a)v/=l;return a;}
    AlignVector AlignMap(const AlignMatrix& m,AlignVector p,bool direction=false){AlignVector r{};for(int i=0;i<3;++i){r[i]=direction?0:m[i*4+3];for(int j=0;j<3;++j)r[i]+=m[i*4+j]*p[j];}return r;}
    void AlignRotate(AlignMatrix& m,AlignVector axis,double angle,AlignVector pivot){
        axis=AlignUnit(axis);const double c=std::cos(angle),s=std::sin(angle);
        auto rotate=[&](AlignVector p){AlignVector r=AlignAdd(AlignAdd({},p,c),AlignCross(axis,p),s);return AlignAdd(r,axis,AlignDot(axis,p)*(1-c));};
        const auto origin=AlignAdd(pivot,rotate(AlignAdd(AlignMap(m,{}),pivot,-1)));
        for(int j=0;j<3;++j){auto r=rotate({m[j],m[4+j],m[8+j]});for(int i=0;i<3;++i)m[i*4+j]=r[i];}
        for(int i=0;i<3;++i)m[i*4+3]=origin[i];
    }
    struct AlignmentConstraint{Text Kind;AlignVector Source,Target,Point;};
    Map ComputeAlignment(Scene& scene,const Map& request){
        const auto placementID=Identifier(request,"placementId"),setup=OptionalIdentifier(request,"setupId");
        const auto object=ReadPickObject(scene,placementID,setup);if(!object.Workpiece)throw std::invalid_argument("CAM_ALIGNMENT_WORKPIECE_REQUIRED");
        const auto specification=request.at("registration").To<Map>();
        if(String(specification,"modelFrame")!="Workpiece"||String(specification,"targetFrame")!="Setup"||String(specification,"unit")!="mm")throw std::invalid_argument("CAM_REGISTRATION_FRAME_MISMATCH");
        if(specification.contains("frame")){
            if(specification.contains("steps"))throw std::invalid_argument("CAM_ALIGNMENT_FRAME_INVALID");
            const auto frame=specification.at("frame").To<Map>();
            if(frame.size()!=4)throw std::invalid_argument("CAM_ALIGNMENT_FRAME_INVALID");
            const auto origin=RegistrationPoint(frame.at("origin"));
            const std::array<AlignVector,3> axes{RegistrationPoint(frame.at("x")),RegistrationPoint(frame.at("y")),RegistrationPoint(frame.at("z"))};
            for(int i=0;i<3;++i){
                if(std::abs(AlignDot(axes[i],axes[i])-1)>1e-8)throw std::invalid_argument("CAM_ALIGNMENT_FRAME_INVALID");
                for(int j=0;j<i;++j)if(std::abs(AlignDot(axes[i],axes[j]))>1e-8)throw std::invalid_argument("CAM_ALIGNMENT_FRAME_INVALID");
            }
            if(AlignDot(AlignCross(axes[0],axes[1]),axes[2])<1-1e-8)throw std::invalid_argument("CAM_ALIGNMENT_FRAME_INVALID");
            // Rebuild the transform from the editable frame. A client-supplied
            // preview matrix is never used to rebase the persisted geometry.
            Array transform;for(int i=0;i<3;++i){for(int j=0;j<3;++j)transform.emplace_back(axes[i][j]);transform.emplace_back(-AlignDot(axes[i],origin));}
            for(double v:{0.,0.,0.,1.})transform.emplace_back(v);
            if(request.contains("resetWorkpieceFrame")&&request.at("resetWorkpieceFrame").To<bool>()){
                const auto previous=object.Workpiece->GetCoordinateFrame();
                AlignMatrix pose{};pose[15]=1;
                const auto oldOrigin=RegistrationPoint(previous.at("origin"));
                for(int i=0;i<3;++i){pose[i*4+3]=oldOrigin[i];for(int j=0;j<3;++j)pose[i*4+j]=RegistrationPoint(previous.at(std::array<Text,3>{"x","y","z"}[j]))[i];}
                const auto target=RigidMatrix(transform);Array relative;
                for(int i=0;i<4;++i)for(int j=0;j<4;++j){double value=0;for(int k=0;k<4;++k)value+=target[i*4+k]*pose[k*4+j];relative.emplace_back(value);}
                transform=std::move(relative);
            }
            (void)RigidMatrix(transform);
            return {{"method",Text("Alignment")},{"input",specification},{"transform",transform},{"qualified",true},{"resolvedSteps",Array{}},{"geometryRef",WireRef(object.GeometryRef)},
                {"placementId",request.at("placementId")},{"setupId",request.at("setupId")},{"expected",request.at("expected")},{"rmsMm",0.},{"maximumResidualMm",0.},{"modelConditionRatio",1.},{"measuredConditionRatio",1.}};
        }
        const auto steps=specification.at("steps").To<Array>();if(steps.empty()||steps.size()>6)throw std::invalid_argument("CAM_ALIGNMENT_STEP_COUNT");
        auto matrix=object.Transform;std::vector<AlignmentConstraint> constraints;Array resolved;
        AlignVector low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
        for(const auto& mesh:object.Geometry->Triangulations3)for(const auto& p:mesh.Geometry.Vertices){const AlignVector point{p.X,p.Y,p.Z};for(int i=0;i<3;++i){low[i]=std::min(low[i],point[i]);high[i]=std::max(high[i],point[i]);}}
        AlignVector localCenter{};for(int i=0;i<3;++i){localCenter[i]=(low[i]+high[i])*.5;if(!std::isfinite(localCenter[i]))throw std::runtime_error("CAM_WORKPIECE_BOUNDS_MISSING");}
        auto check=[&]{for(const auto& c:constraints){
            const auto p=AlignMap(matrix,c.Source,c.Kind=="direction");
            const auto difference=c.Kind=="plane"?std::abs(AlignDot(AlignAdd(p,c.Point,-1),c.Target)):std::sqrt(AlignDot(AlignAdd(p,c.Target,-1),AlignAdd(p,c.Target,-1)));
            if(difference>(c.Kind=="direction"?1e-6:1e-5))throw std::invalid_argument("CAM_ALIGNMENT_CONFLICT");
        }};
        for(const auto& value:steps){
            const auto step=value.To<Map>();const auto kind=String(step,"kind");
            auto source=PickReference(scene,object,step.at("source").To<Map>());const auto mode=String(source.at("query").To<Map>(),"mode");
            const auto sourcePoint=RegistrationPoint(source.at("point"));AlignVector targetPoint{},targetDirection{};
            const auto target=step.at("target").To<Map>();Map targetResult;
            if(target.contains("objectId")){
                const auto id=Identifier(target,"objectId");if(id==placementID)throw std::invalid_argument("CAM_ALIGNMENT_SELF_TARGET");
                const auto other=ReadPickObject(scene,id,setup);targetResult=PickReference(scene,other,target.at("query").To<Map>());
                const auto otherMode=String(targetResult.at("query").To<Map>(),"mode");
                if(kind=="plane"&&otherMode!="plane")throw std::invalid_argument("CAM_ALIGNMENT_TARGET_KIND");
                if(kind=="direction"&&otherMode!="line"&&otherMode!="center"&&otherMode!="axis"&&otherMode!="direction")throw std::invalid_argument("CAM_ALIGNMENT_TARGET_KIND");
                if(kind=="point"&&(otherMode=="line"||otherMode=="plane"||otherMode=="axis"||otherMode=="direction"))throw std::invalid_argument("CAM_ALIGNMENT_TARGET_KIND");
                targetPoint=AlignMap(other.Transform,RegistrationPoint(targetResult.at("point")));
                targetDirection=AlignMap(other.Transform,RegistrationPoint(targetResult.at("direction")),true);
            }else if(kind!="point"){
                const auto axis=String(target,"axis");if(axis!="X"&&axis!="Y"&&axis!="Z")throw std::invalid_argument("CAM_ALIGNMENT_TARGET_KIND");targetDirection[axis=="X"?0:axis=="Y"?1:2]=1;
            }else if(String(target,"point")!="origin")throw std::invalid_argument("CAM_ALIGNMENT_TARGET_KIND");
            if(kind=="plane"||kind=="direction"){
                if(kind=="plane"?mode!="plane":mode!="line"&&mode!="center"&&mode!="axis"&&mode!="direction")throw std::invalid_argument("CAM_ALIGNMENT_SOURCE_KIND");
                auto sourceDirection=AlignUnit(RegistrationPoint(source.at("direction")));targetDirection=AlignUnit(targetDirection);
                if(step.contains("projectToPlane")&&step.at("projectToPlane").To<bool>()){
                    const auto plane=std::find_if(constraints.begin(),constraints.end(),[](const auto& c){return c.Kind=="direction";});
                    if(plane==constraints.end())throw std::invalid_argument("CAM_ALIGNMENT_PLANE_REQUIRED");
                    sourceDirection=AlignUnit(AlignAdd(sourceDirection,plane->Source,-AlignDot(sourceDirection,plane->Source)));
                }
                if(step.contains("flip")&&step.at("flip").To<bool>())for(auto& x:targetDirection)x=-x;
                const auto from=AlignUnit(AlignMap(matrix,sourceDirection,true));auto pivot=AlignMap(matrix,localCenter);
                for(const auto& c:constraints)if(c.Kind=="point")pivot=c.Target;
                const auto locked=std::find_if(constraints.begin(),constraints.end(),[](const auto& c){return c.Kind=="direction";});
                if(locked!=constraints.end()){
                    const auto axis=locked->Target;if(std::abs(AlignDot(from,axis)-AlignDot(targetDirection,axis))>1e-6)throw std::invalid_argument("CAM_ALIGNMENT_CONFLICT");
                    const auto a=AlignAdd(from,axis,-AlignDot(from,axis)),b=AlignAdd(targetDirection,axis,-AlignDot(targetDirection,axis));
                    if(AlignDot(a,a)>1e-12)AlignRotate(matrix,axis,std::atan2(AlignDot(axis,AlignCross(a,b)),AlignDot(a,b)),pivot);
                }else{
                    auto axis=AlignCross(from,targetDirection);const double sine=std::sqrt(AlignDot(axis,axis)),cosine=std::clamp(AlignDot(from,targetDirection),-1.,1.);
                    if(sine>1e-10)AlignRotate(matrix,axis,std::atan2(sine,cosine),pivot);
                    else if(cosine<0){AlignVector basis{};basis[std::min_element(from.begin(),from.end(),[](double a,double b){return std::abs(a)<std::abs(b);})-from.begin()]=1;AlignRotate(matrix,AlignCross(from,basis),std::acos(-1.),pivot);}
                }
                const auto quarter=step.contains("quarter")?FiniteNumber(step.at("quarter"),"CAM_ALIGNMENT_ANGLE_INVALID"):0.;
                if(quarter!=std::floor(quarter)||std::abs(quarter)>3)throw std::invalid_argument("CAM_ALIGNMENT_ANGLE_INVALID");
                if(quarter)AlignRotate(matrix,targetDirection,quarter*std::acos(-1.)*.5,pivot);
                constraints.push_back({"direction",sourceDirection,targetDirection,{}});
                if(kind=="plane"&&step.contains("contact")&&step.at("contact").To<bool>()){
                    auto freeDirection=targetDirection;std::vector<AlignVector> normals;
                    for(const auto& c:constraints)if(c.Kind=="plane"){
                        auto n=c.Target;for(const auto& u:normals)n=AlignAdd(n,u,-AlignDot(n,u));
                        if(AlignDot(n,n)>1e-12){n=AlignUnit(n);normals.push_back(n);freeDirection=AlignAdd(freeDirection,n,-AlignDot(freeDirection,n));}}
                    const double gap=AlignDot(AlignAdd(targetPoint,AlignMap(matrix,sourcePoint),-1),targetDirection),denom=AlignDot(freeDirection,targetDirection);
                    if(std::abs(gap)>1e-5){if(std::abs(denom)<1e-10)throw std::invalid_argument("CAM_ALIGNMENT_CONFLICT");for(int i=0;i<3;++i)matrix[i*4+3]+=freeDirection[i]*gap/denom;}
                    constraints.push_back({"plane",sourcePoint,targetDirection,targetPoint});
                }
            }else if(kind=="point"){
                if(mode=="line"||mode=="plane"||mode=="axis"||mode=="direction")throw std::invalid_argument("CAM_ALIGNMENT_SOURCE_KIND");
                const auto delta=AlignAdd(targetPoint,AlignMap(matrix,sourcePoint),-1);for(int i=0;i<3;++i)matrix[i*4+3]+=delta[i];constraints.push_back({"point",sourcePoint,targetPoint,{}});
            }else throw std::invalid_argument("CAM_ALIGNMENT_SOURCE_KIND");
            check();resolved.emplace_back(Map{{"source",source},{"target",targetResult}});
        }
        Array transform;for(auto v:matrix)transform.emplace_back(v==0?0.:v);(void)RigidMatrix(transform);
        return {{"method",Text("Alignment")},{"input",specification},{"transform",transform},{"qualified",true},{"resolvedSteps",resolved},{"geometryRef",WireRef(object.GeometryRef)},
            {"placementId",request.at("placementId")},{"setupId",request.at("setupId")},{"expected",request.at("expected")},{"rmsMm",0.},{"maximumResidualMm",0.},{"modelConditionRatio",1.},{"measuredConditionRatio",1.}};
    }
}
