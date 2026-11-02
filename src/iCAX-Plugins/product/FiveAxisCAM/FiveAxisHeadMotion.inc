// Product-owned split chain: names are labels; target and parent frames define
// the kinematics. This same graph is consumed by preview, IK and CAM playback.
Map CompileHeadMotion(const Map& model,const Map& parameters,const Text& instanceID){
    namespace M=iCAX::CAM::Motion;
    Array nodes,headNodes,axisNodes,collisions;
    const auto prefix="head:"+instanceID+":";
    const auto append=[&](Text suffix,int parent,Text type,Array origin,Array axis,double lower,double upper,double velocity,Text name,int parameter=-1){
        const auto index=static_cast<int>(nodes.size());
        nodes.emplace_back(Map{{"id",prefix+suffix},{"name",name},{"parent",parent},{"type",type},{"origin",origin},{"axis",axis},
            {"lower",lower},{"upper",upper},{"position",std::clamp(0.,lower,upper)},{"velocity",velocity},{"parameterIndex",parameter}});return index;
    };
    const auto identity=HeadNumbers(M::Identity());
    append("root",-1,"fixed",identity,Array{0.,0.,1.},0,0,0,"基准");
    int headParent=0;
    for(int k=0;k<3;++k){Array axis{0.,0.,0.};axis[k]=1.;headParent=append(Text(1,"XYZ"[k]),headParent,"prismatic",identity,axis,-1000000,1000000,1000,Text(1,"XYZ"[k]));}
    const auto source=model.contains("assembly")?model.at("assembly").To<Map>().at("nodes").To<Array>():Array{};
    const auto axes=parameters.at("axes").To<Array>();
    const auto addAxis=[&](const Map& a,int parent,int parameter){
        const auto sign=a.at("sign").To<double>();auto axis=HeadParameterVector(a.at("axis"),true);
        M::Node rotation;rotation.Type="revolute";rotation.Axis=axis;
        const auto origin=M::Multiply(RigidMatrix(a.at("origin").To<Array>()),M::Joint(rotation,-sign*a.at("zeroOffsetDeg").To<double>()*std::numbers::pi/180));
        for(auto& x:axis)x*=sign;
        return append("axis"+std::to_string(a.at("index").To<UInt>()),parent,"revolute",HeadNumbers(origin),HeadNumbers(axis),
            a.at("lowerDeg").To<double>()*std::numbers::pi/180,a.at("upperDeg").To<double>()*std::numbers::pi/180,
            (a.contains("velocityDegS")?a.at("velocityDegS").To<double>():60.)*std::numbers::pi/180,String(a,"name"),parameter);
    };
    // Additional head axes precede the physical head. Model-backed head axes
    // retain their real parent topology; reassigned axes are fixed on that side.
    for(int i=0;i<static_cast<int>(axes.size());++i){const auto a=axes[i].To<Map>();
        if(String(a,"target")!="Workpiece"&&a.at("index").To<UInt>()>=source.size()){headParent=addAxis(a,headParent,i);axisNodes.emplace_back(headParent);}}
    for(int i=0;i<static_cast<int>(source.size());++i){const auto n=source[i].To<Map>();const auto parent=n.at("parent").To<int>();
        const auto mappedParent=parent<0?headParent:headNodes.at(parent).To<int>();int parameter=-1;
        for(int j=0;j<static_cast<int>(axes.size());++j){const auto a=axes[j].To<Map>();if(a.at("index").To<UInt>()==i&&String(a,"target")!="Workpiece")parameter=j;}
        const auto index=append("part"+std::to_string(i),mappedParent,String(n,"type"),n.at("origin").To<Array>(),n.at("axis").To<Array>(),
            String(n,"type")=="fixed"?0:n.at("lower").To<double>(),String(n,"type")=="fixed"?0:n.at("upper").To<double>(),parameter<0?0:(axes[parameter].To<Map>().contains("velocityDegS")?axes[parameter].To<Map>().at("velocityDegS").To<double>():60.)*std::numbers::pi/180,String(n,"name"),parameter);
        headNodes.emplace_back(index);if(parameter>=0)axisNodes.emplace_back(index);
        if(n.contains("collisionBoxes")&&!n.at("collisionBoxes").To<Array>().empty())collisions.emplace_back(Map{{"id",prefix+"part"+std::to_string(i)},{"name",n.at("name")},{"geometry",n.at("collisionBoxes")}});
    }
    int tool;
    if(source.empty()){
        tool=append("tool",headParent,"fixed",identity,Array{0.,0.,1.},0,0,0,String(model,"name"));headNodes.emplace_back(tool);
        if(model.contains("collisionBoxes"))collisions.emplace_back(Map{{"id",prefix+"tool"},{"name",model.at("name")},{"geometry",model.at("collisionBoxes")}});
    }else tool=headNodes.at(model.at("assembly").To<Map>().at("tool").To<int>()).To<int>();
    int support=0;
    for(int i=0;i<static_cast<int>(axes.size());++i){const auto a=axes[i].To<Map>();if(String(a,"target")=="Workpiece"){support=addAxis(a,support,i);axisNodes.emplace_back(support);}}
    support=append("support",support,"fixed",identity,Array{0.,0.,1.},0,0,0,"当前工件与夹具");
    return {{"nodes",nodes},{"headNodes",headNodes},{"axisNodes",axisNodes},{"tool",tool},{"support",support},
        {"tcp",model.at("tcpLocalPosition")},{"beam",model.at("beamLocalDirection")},{"collisionDefinitions",collisions},{"independentHead",true}};
}
