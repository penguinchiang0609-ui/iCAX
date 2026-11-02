// Exact inverse candidates for a serial 3P-2R task, recognized from neutral
// topology and axes. No product, asset name, mesh, CAM or controller semantics.
// The translational Jacobian spans R3; the two downstream rotations determine
// a signed beam direction. All results still pass the provider's independent FK.
bool SerialThreePTwoR(Search& s)
{
    const auto tool=s.M.IDs.at(s.R.TaskDefinition.ToolFrameID);
    std::vector<std::size_t> chain;
    for(int at=static_cast<int>(tool);at>=0;at=s.M.Source.Nodes[at].Parent)
        if(s.M.QIndices[at]>=0)chain.push_back(static_cast<std::size_t>(at));
    std::reverse(chain.begin(),chain.end());
    if(chain.size()!=5)return false;
    for(std::size_t i=0;i<3;++i)if(s.M.Source.Nodes[chain[i]].Type!=JointType::Prismatic)return false;
    for(std::size_t i=3;i<5;++i)if(s.M.Source.Nodes[chain[i]].Type!=JointType::Revolute&&s.M.Source.Nodes[chain[i]].Type!=JointType::Continuous)return false;
    for(std::size_t i=0;i<s.M.QIndices.size();++i)
        if(s.M.QIndices[i]>=0&&std::find(chain.begin(),chain.end(),i)==chain.end())return false;
    if(!s.R.TaskDefinition.ReferenceFrameID.empty())
        for(int at=static_cast<int>(s.M.IDs.at(s.R.TaskDefinition.ReferenceFrameID));at>=0;at=s.M.Source.Nodes[at].Parent)
            if(s.M.QIndices[at]>=0)return false;
    const auto outer=chain[3],inner=chain[4];
    const auto rotation=[](const Matrix& m){Eigen::Matrix3d r;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r(i,j)=m[i*4+j];return r;};
    const auto vec=[](const Vector& v){return Eigen::Vector3d(v[0],v[1],v[2]);};
    std::vector<double> zero(s.Base.size(),0.);
    std::vector<std::string> ids{s.M.Source.Nodes[outer].ID,s.M.Source.Nodes[inner].ID};
    if(!s.R.TaskDefinition.ReferenceFrameID.empty())ids.push_back(s.R.TaskDefinition.ReferenceFrameID);
    const auto origin=Evaluate(s.M,zero,s.R.TaskDefinition,true,ids);
    const Eigen::Matrix3d reference=ids.size()==3?rotation(origin.Pose.Frames[2].World):Eigen::Matrix3d::Identity();
    const Eigen::Vector3d c=(reference.transpose()*rotation(origin.Pose.Frames[0].World)*vec(s.M.Source.Nodes[outer].Axis)).normalized();
    const Eigen::Vector3d a=(reference.transpose()*rotation(origin.Pose.Frames[1].World)*vec(s.M.Source.Nodes[inner].Axis)).normalized();
    const Eigen::Vector3d b=vec(origin.Pose.Direction),goal=vec(s.Direction);
    Eigen::Matrix3d linear;for(int k=0;k<3;++k)linear.col(k)=origin.Jacobian.block<3,1>(0,chain[k]);
    const auto inverse=linear.fullPivLu();if(inverse.rank()!=3)return false;
    const double ac=a.dot(c),ab=a.dot(b),cosine=c.dot(b)-ac*ab,sine=c.dot(a.cross(b));
    const double radius=std::hypot(cosine,sine),rhs=c.dot(goal)-ac*ab;
    if(radius<1e-10)return false; // direction has fewer than two controllable axes
    s.Output.Coverage="AnalyticSerialThreePrismaticTwoRotary";
    if(std::abs(rhs)>radius+1e-10)return true;
    const auto seed=s.R.Seeds.empty()?s.Base:s.R.Seeds.front();
    constexpr double tau=6.283185307179586476925286766559;
    const auto turns=[&](double angle,std::size_t axis){
        std::vector<double> values;const double first=std::ceil((s.Lower[axis]-angle-1e-10)/tau),last=std::floor((s.Upper[axis]-angle+1e-10)/tau);
        if(first>last)return values;
        const double nearest=std::clamp(std::round((seed[axis]-angle)/tau),first,last);
        const std::size_t budget=std::min<std::size_t>(256,std::max<std::size_t>(4,s.R.MaxCandidates*2));
        for(std::size_t k=0;values.size()<budget&&k<=budget;++k){
            const double offset=k?double((k+1)/2)*(k%2?1:-1):0,turn=nearest+offset;
            if(turn>=first&&turn<=last)values.push_back(std::clamp(angle+turn*tau,s.Lower[axis],s.Upper[axis]));
        }return values;
    };
    const double phase=std::atan2(sine,cosine),opening=std::acos(std::clamp(rhs/radius,-1.,1.));
    std::vector<std::pair<double,double>> angles;
    for(const double base:{phase+opening,phase-opening})for(const double phi:turns(base,inner)){
        const Eigen::Vector3d v=Eigen::AngleAxisd(phi,a)*b;
        const Eigen::Vector3d vp=v-c*c.dot(v),gp=goal-c*c.dot(goal);
        if(vp.norm()<1e-9&&gp.norm()<1e-9){
            // At a direction pole the outer rotary coordinate is free. Retain
            // caller branches first, which preserves continuity through A=0.
            std::vector<double> free{seed[outer]};for(const auto& prior:s.R.Seeds)free.push_back(prior[outer]);free.push_back(s.Base[outer]);
            for(double theta:free)if(theta>=s.Lower[outer]&&theta<=s.Upper[outer])angles.emplace_back(theta,phi);
        }else if(vp.norm()>1e-9&&gp.norm()>1e-9){
            const double theta=std::atan2(c.dot(vp.cross(gp)),vp.dot(gp));
            for(double turn:turns(theta,outer))angles.emplace_back(turn,phi);
        }
    }
    std::stable_sort(angles.begin(),angles.end(),[&](const auto& x,const auto& y){
        return std::hypot(x.first-seed[outer],x.second-seed[inner])<std::hypot(y.first-seed[outer],y.second-seed[inner]);
    });
    std::size_t branch=0;
    for(const auto& [theta,phi]:angles){
        if(s.Stop())break;auto q=zero;q[outer]=theta;q[inner]=phi;
        const auto pose=Evaluate(s.M,q,s.R.TaskDefinition,false).Pose;
        const Eigen::Vector3d translation=inverse.solve(vec(s.R.Goal.PositionMm)-vec(pose.PositionMm));
        for(int k=0;k<3;++k){const auto axis=chain[k];q[axis]=translation[k];
            if(s.Lower[axis]==s.Upper[axis]&&std::abs(q[axis]-s.Lower[axis])<=1e-8)q[axis]=s.Lower[axis];}
        if(Accept(s,q,branch++,"AnalyticSerial3P2R",seed))s.Output.Candidates.back().BranchKey="serial-3p2r:"+std::to_string(theta)+":"+std::to_string(phi);
        if(s.Output.Candidates.size()>=s.R.MaxCandidates)break;
    }
    return true;
}
