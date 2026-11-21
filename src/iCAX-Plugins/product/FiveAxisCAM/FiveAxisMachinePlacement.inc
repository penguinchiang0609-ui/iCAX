namespace {
    namespace PlacementMath=iCAX::GeometryServices::RigidMotion;
    struct PlacementBox { Motion::Vector Min,Max;Motion::Matrix Transform;Text ID; };
    PlacementMath::Quaternion PlacementRotation(const Motion::Matrix& m){
        PlacementMath::Quaternion q{};const double trace=m[0]+m[5]+m[10];
        if(trace>0){const double s=2*std::sqrt(trace+1);q={(m[9]-m[6])/s,(m[2]-m[8])/s,(m[4]-m[1])/s,s/4};}
        else {int i=0;if(m[5]>m[0])i=1;if(m[10]>m[i*4+i])i=2;const int j=(i+1)%3,k=(i+2)%3;const double s=2*std::sqrt(1+m[i*4+i]-m[j*4+j]-m[k*4+k]);q[i]=s/4;q[j]=(m[j*4+i]+m[i*4+j])/s;q[k]=(m[k*4+i]+m[i*4+k])/s;q[3]=(m[k*4+j]-m[j*4+k])/s;}
        return PlacementMath::Unit(q);
    }
    struct PlacementSweep {
        Motion::Matrix From,To;Motion::Vector Axis{0,0,1},Anchor{},Start{},End{};double Angle=0;
        PlacementSweep(Motion::Matrix from,Motion::Matrix to,Motion::Vector anchor):From(from),To(to),Anchor(anchor),Start(Motion::Transform(from,anchor)),End(Motion::Transform(to,anchor)){
            auto delta=PlacementRotation(Motion::Multiply(to,Motion::Inverse(from)));if(delta[3]<0)for(auto& v:delta)v=-v;
            const double sine=std::hypot(delta[0],delta[1],delta[2]);Angle=2*std::atan2(sine,delta[3]);
            if(sine>1e-12)Axis={delta[0]/sine,delta[1]/sine,delta[2]/sine};
        }
        Motion::Matrix At(double t)const{
            Motion::Node n;n.Type="revolute";n.Axis=Axis;auto m=Motion::Multiply(Motion::Joint(n,Angle*t),From);
            const auto offset=Motion::Transform(m,Anchor,true);for(int k=0;k<3;++k)m[k*4+3]=Start[k]+t*(End[k]-Start[k])-offset[k];return m;
        }
        // Exact coordinate extrema for linear translation plus shortest rigid
        // rotation. Table contact must not cause sampled-motion false alarms.
        std::pair<double,double> Range(Motion::Vector point,int k)const{
            const auto p=Motion::Transform(From,PlacementMath::Sub(point,Anchor),true),a=PlacementMath::Scale(Axis,PlacementMath::Dot(Axis,p));
            const auto c=PlacementMath::Sub(p,a),d=PlacementMath::Cross(Axis,p);const double b=End[k]-Start[k];
            const auto f=[&](double t){return Start[k]+b*t+a[k]+c[k]*std::cos(Angle*t)+d[k]*std::sin(Angle*t);};
            double lo=std::min(f(0),f(1)),hi=std::max(f(0),f(1));const double r=Angle*std::hypot(c[k],d[k]);
            if(r>1e-12&&std::abs(b)<=r){const double phase=std::atan2(-c[k],d[k]),root=std::acos(std::clamp(-b/r,-1.,1.));
                for(int turn=-2;turn<=static_cast<int>(std::ceil(Angle/(2*std::numbers::pi)))+1;++turn)for(double sign:{-1.,1.}){const double t=(phase+sign*root+turn*2*std::numbers::pi)/Angle;if(t>0&&t<1){lo=std::min(lo,f(t));hi=std::max(hi,f(t));}}}
            return {lo,hi};
        }
    };
    std::vector<Motion::Vector> PlacementCorners(const PlacementBox& box){
        std::vector<Motion::Vector> result;for(int b=0;b<8;++b)result.push_back(Motion::Transform(box.Transform,{b&1?box.Max[0]:box.Min[0],b&2?box.Max[1]:box.Min[1],b&4?box.Max[2]:box.Min[2]}));return result;
    }
    // A normalized separating axis supplies a conservative signed gap. The
    // sweep bound certifies the interval, including thin intervening obstacles.
    double PlacementBoxGap(const PlacementBox& a,const PlacementBox& b,const Motion::Matrix& placement){
        auto moving=a;moving.Transform=Motion::Multiply(placement,a.Transform);const auto ca=PlacementCorners(moving),cb=PlacementCorners(b);
        std::vector<Motion::Vector> axes;for(int k=0;k<3;++k){axes.push_back({moving.Transform[k],moving.Transform[4+k],moving.Transform[8+k]});axes.push_back({b.Transform[k],b.Transform[4+k],b.Transform[8+k]});}
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)axes.push_back(PlacementMath::Cross(axes[i*2],axes[j*2+1]));
        double gap=-1e100;for(auto axis:axes){if(Motion::Length(axis)<1e-10)continue;axis=Motion::Unit(axis);double al=1e100,ah=-1e100,bl=1e100,bh=-1e100;
            for(const auto& p:ca){const double v=PlacementMath::Dot(axis,p);al=std::min(al,v);ah=std::max(ah,v);}for(const auto& p:cb){const double v=PlacementMath::Dot(axis,p);bl=std::min(bl,v);bh=std::max(bh,v);}gap=std::max(gap,std::max(bl-ah,al-bh));}return gap;
    }
    std::vector<PlacementBox> PlacementAssemblyBoxes(Scene& scene,const ID& id){
        const auto assembly=RigidMatrix(OperationAssemblyTransform(scene,id));
        std::vector<PlacementBox> boxes;
        const auto append=[&](const ID& entity,const Map& geometry,const Array& transform){
            const auto mesh=CollisionMeshFor(scene,geometry);PlacementBox box{{1e100,1e100,1e100},{-1e100,-1e100,-1e100},Motion::Multiply(assembly,RigidMatrix(transform)),iCAX::Data::to_string(entity)};
            if(!mesh||mesh->Vertices.empty())throw std::invalid_argument("CAM_MACHINE_MOUNT_GEOMETRY_REQUIRED");
            for(const auto& p:mesh->Vertices)for(int k=0;k<3;++k){box.Min[k]=std::min(box.Min[k],p[k]);box.Max[k]=std::max(box.Max[k],p[k]);}boxes.push_back(box);
        };
        for(const auto& e:scene.Database().FilterEntities([](auto){return true;})){
            if(const auto p=Component<CFiveAxisPlacementComponent>(e);p&&p->GetSetupID()==id){const auto w=Require<CFiveAxisWorkpieceComponent>(scene.Database(),p->GetWorkpieceID());if(w->GetEnabled())append(e->GetID(),w->GetGeometryRef(),p->GetTransform());}
            // Enabled tooling belongs to the assembly even if its machining
            // clearance check has been disabled in the fixture page.
            if(const auto f=Component<CFiveAxisFixtureComponent>(e);f&&f->GetSetupID()==id&&f->GetEnabled())append(e->GetID(),f->GetGeometryRef(),f->GetTransform());
        }return boxes;
    }
    void ValidateMachinePlacement(Scene& scene,const ID& id,const MachineSnapshot& machine,const Motion::Matrix& from,Motion::Vector pivot={},const Map& rotation={}){
        const auto boxes=PlacementAssemblyBoxes(scene,id);if(boxes.empty())throw std::invalid_argument("CAM_MACHINE_MOUNT_WORKPIECE_REQUIRED: 请先启用工件。");
        const auto world=Motion::Forward(machine.Nodes,MachineJointValues(machine));const auto inverse=Motion::Inverse(world.at(machine.Target.Support));
        std::vector<PlacementBox> obstacles;Motion::Vector tableLo{1e100,1e100,1e100},tableHi{-1e100,-1e100,-1e100};bool table=false;
        for(size_t i=0;i<machine.Nodes.size();++i){const auto entity=scene.Database().GetEntity(*ID::from_string(machine.Nodes[i].ID));const auto collision=Component<iCAX::CAM::CMachineCollisionComponent>(entity);if(!collision)continue;
            for(const auto& v:collision->GetCollisions()){const auto c=v.To<Map>();if(c.contains("enabled")&&!c.at("enabled").To<bool>())continue;const auto g=c.at("geometry").To<Map>();if(String(g,"type")!="box")throw std::invalid_argument("CAM_MACHINE_BOX_INVALID");
                const auto p=c.at("pose").To<Array>(),size=g.at("size").To<Array>();if(p.size()!=6||size.size()!=3)throw std::invalid_argument("CAM_MACHINE_BOX_INVALID");
                const auto local=iCAX::Transform::MakeLocalMatrix(FiniteNumber(p[0],"CAM_MACHINE_BOX_INVALID")*1000,FiniteNumber(p[1],"CAM_MACHINE_BOX_INVALID")*1000,FiniteNumber(p[2],"CAM_MACHINE_BOX_INVALID")*1000,FiniteNumber(p[5],"CAM_MACHINE_BOX_INVALID"),FiniteNumber(p[4],"CAM_MACHINE_BOX_INVALID"),FiniteNumber(p[3],"CAM_MACHINE_BOX_INVALID"),1,1,1);
                Motion::Matrix transform{};for(int k=0;k<16;++k)transform[k]=local(k/4,k%4);PlacementBox b{{},{},Motion::Multiply(Motion::Multiply(inverse,world[i]),transform),machine.Nodes[i].ID};
                for(int k=0;k<3;++k){const double half=FiniteNumber(size[k],"CAM_MACHINE_BOX_INVALID")*500;if(half<=0)throw std::invalid_argument("CAM_MACHINE_BOX_INVALID");b.Min[k]=-half;b.Max[k]=half;}
                if(static_cast<int>(i)==machine.Target.Support){table=true;for(const auto& corner:PlacementCorners(b))for(int k=0;k<3;++k){tableLo[k]=std::min(tableLo[k],corner[k]);tableHi[k]=std::max(tableHi[k],corner[k]);}}
                else obstacles.push_back(b);
            }
        }
        if(!table)throw std::invalid_argument("CAM_MACHINE_SUPPORT_SURFACE_REQUIRED: 工作台须有有效碰撞盒。");
        PlacementSweep sweep(from,machine.Placement,pivot);double radius=0;
        if(!rotation.empty()){
            CutFields(rotation,{"axis","angle"});const auto axis=Motion::Unit(RegistrationPoint(rotation.at("axis")));const double angle=FiniteNumber(rotation.at("angle"),"CAM_MACHINE_PLACEMENT_ROTATION_INVALID");
            if(std::abs(angle)>2000*std::numbers::pi)throw std::invalid_argument("CAM_MACHINE_PLACEMENT_ROTATION_INVALID");
            Motion::Node n;n.Type="revolute";n.Axis=axis;const auto target=Motion::Multiply(Motion::Joint(n,angle),from);
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(std::abs(target[i*4+j]-machine.Placement[i*4+j])>1e-7)throw std::invalid_argument("CAM_MACHINE_PLACEMENT_ROTATION_MISMATCH");
            sweep.Axis=PlacementMath::Scale(axis,angle<0?-1.:1.);sweep.Angle=std::abs(angle);
        }
        for(const auto& box:boxes)for(const auto& corner:PlacementCorners(box)){radius=std::max(radius,Motion::Length(PlacementMath::Sub(corner,pivot)));for(int k=0;k<3;++k){const auto [lo,hi]=sweep.Range(corner,k);
            if(k==2&&lo<tableHi[2]-.01)throw std::invalid_argument("CAM_MACHINE_PLACEMENT_BELOW_TABLE: 工件或工装不能穿入工作台。");
            if(k<2&&(lo<tableLo[k]-.01||hi>tableHi[k]+.01))throw std::invalid_argument("CAM_MACHINE_PLACEMENT_EXCEEDS_TABLE: 工件与工装整体不能超出工作台边界。");}}
        const double travel=Motion::Length(PlacementMath::Sub(sweep.End,sweep.Start))+sweep.Angle*radius;
        for(const auto& box:boxes)for(const auto& obstacle:obstacles){const auto checked=Motion::CertifySweep({0.},{1.},travel,-.01,[&](const auto& t){if(scene.IsStopRequested())throw std::runtime_error("CAM_MACHINE_PLACEMENT_CANCELLED");return PlacementBoxGap(box,obstacle,sweep.At(t[0]));});
            if(!checked.Clear)throw std::invalid_argument("CAM_MACHINE_PLACEMENT_BLOCKED: 工件与工装的平移或旋转受到机床碰撞盒限制。");}
    }
}
Map CheckMachinePlacement(Scene& scene,const Map& request){
    CutFields(request,{"expected","setupId","configuration","placementPivot","placementRotation"});CheckExpected(*Runtime(scene),request);const auto id=Identifier(request,"setupId");RequireSetupUnlocked(scene,id);
    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),id);const auto machine=CaptureMachine(scene,request.at("configuration").To<Map>());const auto previous=setup->GetMachineSetup();
    const bool same=!previous.empty()&&String(previous,"machineId")==String(machine.Configuration,"machineId")&&String(previous,"supportId")==String(machine.Configuration,"supportId");
    ValidateMachinePlacement(scene,id,machine,same?RigidMatrix(previous.at("placement").To<Array>()):machine.Placement,request.contains("placementPivot")?RegistrationPoint(request.at("placementPivot")):Motion::Vector{},request.contains("placementRotation")?request.at("placementRotation").To<Map>():Map{});
    return {{"status",Text("Ready")},{"collisionStatus",Text("Clear")},{"placement",MachineNumbers(machine.Placement)}};
}
