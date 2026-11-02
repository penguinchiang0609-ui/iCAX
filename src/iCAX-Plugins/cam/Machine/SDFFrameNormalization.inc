// SDF stores link poses in the model frame and joint poses in the child frame
// by default. The runtime uses one tree with each part rooted at its incoming
// joint. Resolve the SDF frame graph, then rebase geometry and axes exactly once.
namespace SDFFrames {
    namespace M=iCAX::CAM::Motion;
    using Pose=iCAX::CAM::MachinePose6;
    M::Matrix Matrix(const Pose& p){
        M::Node n;n.Type="revolute";n.Axis={0,0,1};auto r=M::Joint(n,p[5]);
        n.Axis={0,1,0};r=M::Multiply(r,M::Joint(n,p[4]));n.Axis={1,0,0};r=M::Multiply(r,M::Joint(n,p[3]));
        for(int k=0;k<3;++k)r[k*4+3]=p[k];return r;
    }
    Pose ToPose(const M::Matrix& m){
        const double pitch=std::asin(std::clamp(-m[8],-1.,1.));
        if(std::abs(std::cos(pitch))>1e-9)return {m[3],m[7],m[11],std::atan2(m[9],m[10]),pitch,std::atan2(m[4],m[0])};
        return {m[3],m[7],m[11],0,pitch,std::atan2(-m[1],m[5])};
    }
    std::string Relative(const SXmlNode& node,const std::string& defaultFrame){
        const auto* pose=_FirstChild(node,"pose");const auto frame=pose?_Attr(*pose,"relative_to"):std::string();return frame.empty()?defaultFrame:frame;
    }
    struct Frame {std::string Parent;M::Matrix Local=M::Identity(),World=M::Identity();int State=0;};
    struct Graph {
        std::map<std::string,Frame> Frames{{"world",{{},M::Identity(),M::Identity(),2}}};
        void Add(const std::string& name,const std::string& parent,const SXmlNode& node){
            if(name.empty()||Frames.contains(name)||name.find("::")!=std::string::npos)throw std::runtime_error("SDF duplicate or unsupported frame name: "+name);
            Frames.emplace(name,Frame{parent,Matrix(_ReadPose6(node))});
        }
        const M::Matrix& World(const std::string& name){
            const auto found=Frames.find(name);if(found==Frames.end())throw std::runtime_error("SDF unknown frame: "+name);
            auto& f=found->second;if(f.State==1)throw std::runtime_error("SDF cyclic pose frame: "+name);
            if(f.State==0){f.State=1;f.World=M::Multiply(World(f.Parent),f.Local);f.State=2;}return f.World;
        }
        M::Matrix PoseWorld(const SXmlNode& node,const std::string& defaultFrame){return M::Multiply(World(Relative(node,defaultFrame)),Matrix(_ReadPose6(node)));}
    };
}
void _NormalizeSDFFrames(const SXmlNode& sdf,const SXmlNode& model,iCAX::CAM::CMachineDescriptionResource& result){
    using namespace SDFFrames;
    if(_Children(sdf,"model").size()!=1||!_Children(sdf,"world").empty()||!_Children(model,"model").empty()||!_Children(model,"include").empty())
        throw std::runtime_error("SDF supports one self-contained model; world, nested model and include are not supported");
    if(!_Attr(model,"placement_frame").empty())throw std::runtime_error("SDF placement_frame is not supported");
    Graph graph;graph.Add("__model__",Relative(model,"world"),model);
    std::map<std::string,const SXmlNode*> links,joints,incoming;
    std::map<std::string,std::string> parents;
    for(const auto* link:_Children(model,"link")){const auto name=_Attr(*link,"name");graph.Add(name,Relative(*link,"__model__"),*link);links.emplace(name,link);}
    if(links.empty())throw std::runtime_error("SDF model has no links");
    for(const auto* frame:_Children(model,"frame")){const auto attached=_Attr(*frame,"attached_to");graph.Add(_Attr(*frame,"name"),Relative(*frame,attached.empty()?"__model__":attached),*frame);}
    for(const auto* joint:_Children(model,"joint")){
        const auto name=_Attr(*joint,"name"),child=_ChildText(*joint,"child"),parent=_ChildText(*joint,"parent"),type=_Attr(*joint,"type");
        if(!links.contains(child)||(!links.contains(parent)&&parent!="world"))throw std::runtime_error("SDF joint has an unknown parent or child: "+name);
        if(!incoming.emplace(child,joint).second)throw std::runtime_error("SDF link has multiple parent joints: "+child);
        if(type!="fixed"&&type!="revolute"&&type!="prismatic")throw std::runtime_error("SDF joint type is not supported: "+type);
        if(_FirstChild(*joint,"axis2"))throw std::runtime_error("SDF multi-axis joints are not supported: "+name);
        parents[child]=parent;graph.Add(name,Relative(*joint,child),*joint);joints.emplace(name,joint);
    }
    for(const auto& [name,frame]:graph.Frames)graph.World(name);
    for(const auto& [name,link]:links){std::set<std::string> visited;auto p=name;while(parents.contains(p)){if(!visited.insert(p).second)throw std::runtime_error("SDF cyclic mechanical tree: "+name);p=parents.at(p);}}
    // attached_to has its own graph, independent of pose relative_to.
    std::map<std::string,std::string> attached;
    for(const auto* frame:_Children(model,"frame")){auto target=_Attr(*frame,"attached_to");if(target.empty())target="__model__";graph.World(target);attached[_Attr(*frame,"name")]=target;}
    for(const auto& [name,target]:attached){std::set<std::string> visited;auto p=name;while(attached.contains(p)){if(!visited.insert(p).second)throw std::runtime_error("SDF cyclic attached_to frame: "+name);p=attached.at(p);}}
    const auto canonical=[&](const std::string& link)->M::Matrix {if(link=="world")return M::Identity();return graph.World(incoming.contains(link)?_Attr(*incoming.at(link),"name"):link);};
    for(auto& element:result.Elements){
        if(element.ElementID==result.RootElementID){element.Pose={};continue;}
        const auto& name=element.OriginalName;const auto& link=*links.at(name);const auto frame=canonical(name),inverse=M::Inverse(frame);
        const auto parent=parents.contains(name)?canonical(parents.at(name)):M::Identity();
        element.Pose=ToPose(M::Multiply(M::Inverse(parent),frame));
        if(element.JointToParent.bValid){
            auto& joint=element.JointToParent;joint.Pose=element.Pose;
            if(joint.JointType!="fixed"){
                const auto& xml=*incoming.at(name);const auto* axis=_FirstChild(xml,"axis");
                if(!axis)throw std::runtime_error("SDF movable joint requires an axis: "+joint.Name);
                const auto xyz=_ParseDoubles(_ChildText(*axis,"xyz","0 0 1"));if(xyz.size()!=3)throw std::runtime_error("SDF axis requires three values: "+joint.Name);
                auto reference=joint.Axis.ExpressedIn;if(reference.empty()&&joint.Axis.bUseParentModelFrame)reference="__model__";
                auto direction=M::Unit(M::Vector{xyz[0],xyz[1],xyz[2]});
                if(!reference.empty())direction=M::Unit(M::Transform(M::Multiply(inverse,graph.World(reference)),direction,true));
                joint.Axis.XYZ={direction[0],direction[1],direction[2]};joint.Axis.ExpressedIn.clear();joint.Axis.bUseParentModelFrame=false;
                if(!std::isfinite(joint.Axis.dLowerLimit)||!std::isfinite(joint.Axis.dUpperLimit)||joint.Axis.dLowerLimit>joint.Axis.dUpperLimit)
                    throw std::runtime_error("SDF invalid joint limits: "+joint.Name);
                joint.dLowerLimit=joint.Axis.dLowerLimit;joint.dUpperLimit=joint.Axis.dUpperLimit;
                if(!std::isfinite(joint.Axis.dVelocity)||joint.Axis.dVelocity<0)throw std::runtime_error("SDF invalid joint velocity: "+joint.Name);
            }
        }
        const auto visuals=_Children(link,"visual"),collisions=_Children(link,"collision");
        for(size_t i=0;i<visuals.size();++i)element.Visuals[i].Pose=ToPose(M::Multiply(inverse,graph.PoseWorld(*visuals[i],name)));
        for(size_t i=0;i<collisions.size();++i)element.Collisions[i].Pose=ToPose(M::Multiply(inverse,graph.PoseWorld(*collisions[i],name)));
        if(const auto* inertial=_FirstChild(link,"inertial")){const auto p=ToPose(M::Multiply(inverse,graph.PoseWorld(*inertial,name)));VariantArray values;for(auto v:p)values.emplace_back(v);element.Inertial["pose"]=values;}
        if(element.Tool.bValid){
            const auto offset=M::Multiply(inverse,graph.World(name));auto& t=element.Tool;
            const auto tcp=M::Transform(offset,{t.TcpLocalPosition[0].To<double>()/1000,t.TcpLocalPosition[1].To<double>()/1000,t.TcpLocalPosition[2].To<double>()/1000});
            const auto beam=M::Unit(M::Transform(offset,{t.BeamLocalDirection[0].To<double>(),t.BeamLocalDirection[1].To<double>(),t.BeamLocalDirection[2].To<double>()},true));
            t.TcpLocalPosition={tcp[0]*1000,tcp[1]*1000,tcp[2]*1000};t.BeamLocalDirection={beam[0],beam[1],beam[2]};
        }
    }
}
