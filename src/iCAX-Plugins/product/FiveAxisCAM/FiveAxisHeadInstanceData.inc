// Included in the domain's owner namespace. No UI or service owns these inputs.
void CutFields(const Map&,std::initializer_list<const char*>);
std::array<double,16> RigidMatrix(const Array&);
Array HeadNumbers(const auto& values){Array result;for(const auto x:values)result.emplace_back(x);return result;}
iCAX::CAM::Motion::Vector HeadParameterVector(const Variant& value,bool unit=false){
    const auto a=value.To<Array>();if(a.size()!=3)throw std::invalid_argument("CAM_HEAD_VECTOR_REQUIRED");
    iCAX::CAM::Motion::Vector result{};for(int i=0;i<3;++i){result[i]=FiniteNumber(a[i],"CAM_HEAD_PARAMETER_INVALID");if(std::abs(result[i])>100000)throw std::invalid_argument("CAM_HEAD_PARAMETER_RANGE");}
    return unit?iCAX::CAM::Motion::Unit(result):result;
}
Map DefaultHeadParameters(const Map& model){
    Array axes;
    if(model.contains("assembly")){
        const auto nodes=model.at("assembly").To<Map>().at("nodes").To<Array>();
        for(std::size_t i=0;i<nodes.size();++i){const auto n=nodes[i].To<Map>();if(String(n,"type")=="fixed")continue;
            axes.emplace_back(Map{{"index",static_cast<UInt>(i)},{"name",n.at("joint")},{"target",Text("Head")},{"velocityDegS",60.},{"origin",n.at("origin")},{"axis",n.at("axis")},{"zeroOffsetDeg",0.},{"sign",1.},
                {"lowerDeg",FiniteNumber(n.at("lower"),"CAM_HEAD_LIMIT_INVALID")*180/std::numbers::pi},{"upperDeg",FiniteNumber(n.at("upper"),"CAM_HEAD_LIMIT_INVALID")*180/std::numbers::pi}});
        }
        const auto assembly=model.at("assembly").To<Map>();
        const bool hasHeadAxes=!axes.empty();int workSlot=0;if(assembly.contains("workpieceAxes"))for(const auto& name:assembly.at("workpieceAxes").To<Array>()){
            axes.emplace_back(Map{{"index",static_cast<UInt>(nodes.size()+axes.size())},{"name",name},{"target",Text("Workpiece")},{"velocityDegS",60.},
                {"origin",HeadNumbers(iCAX::CAM::Motion::Identity())},{"axis",workSlot++==0&&!hasHeadAxes?Array{1.,0.,0.}:Array{0.,0.,1.}},{"zeroOffsetDeg",0.},{"sign",1.},{"lowerDeg",-180.},{"upperDeg",180.}});
        }
    }
    return {{"tcpMode",Text("Coordinates")},{"tcp",model.at("tcpLocalPosition")},{"beam",model.at("beamLocalDirection")},
        {"gaugeOrigin",Array{0.,0.,0.}},{"lengthMm",0.},{"axes",axes}};
}
#include "FiveAxisHeadColliders.inc"
Map NormalizeHeadParameters(const Map& model,const Map& input){
    CutFields(input,{"tcpMode","tcp","beam","gaugeOrigin","lengthMm","axes","collisionOverrides"});
    Map result=input;const auto mode=String(input,"tcpMode");if(mode!="Coordinates"&&mode!="Gauge")throw std::invalid_argument("CAM_HEAD_TCP_MODE_REQUIRED");
    result["tcp"]=HeadNumbers(HeadParameterVector(input.at("tcp")));result["beam"]=HeadNumbers(HeadParameterVector(input.at("beam"),true));result["gaugeOrigin"]=HeadNumbers(HeadParameterVector(input.at("gaugeOrigin")));
    const auto length=FiniteNumber(input.at("lengthMm"),"CAM_HEAD_LENGTH_REQUIRED");if(length<0||length>100000||mode=="Gauge"&&length<=0)throw std::invalid_argument("CAM_HEAD_LENGTH_REQUIRED: 请填写从安装基准到喷嘴端点的正刀长。");
    result["lengthMm"]=length;
    const auto requested=input.at("axes").To<Array>();
    if(requested.size()>6)throw std::invalid_argument("CAM_HEAD_TOPOLOGY_MISMATCH");
    Array axes;std::set<UInt> used;
    const auto nodes=model.contains("assembly")?model.at("assembly").To<Map>().at("nodes").To<Array>():Array{};
    std::set<Text> names;
    for(const auto& v:requested){auto a=v.To<Map>();CutFields(a,{"index","name","target","velocityDegS","origin","axis","zeroOffsetDeg","sign","lowerDeg","upperDeg"});
        const auto number=FiniteNumber(a.at("index"),"CAM_HEAD_TOPOLOGY_MISMATCH");
        if(number<0||number>=nodes.size()+6||std::floor(number)!=number)throw std::invalid_argument("CAM_HEAD_TOPOLOGY_MISMATCH");
        const auto index=static_cast<UInt>(number);if(!used.insert(index).second||index<nodes.size()&&String(nodes[index].To<Map>(),"type")=="fixed")throw std::invalid_argument("CAM_HEAD_TOPOLOGY_MISMATCH");a["index"]=index;
        const auto target=String(a,"target"),name=String(a,"name");
        if(target!="Head"&&target!="Workpiece"||name.empty()||name.size()>32||!names.insert(name).second)throw std::invalid_argument("CAM_HEAD_AXIS_REQUIRED: 请填写不同的轴名称，并选择带动对象。");
        const auto velocity=FiniteNumber(a.at("velocityDegS"),"CAM_HEAD_VELOCITY_REQUIRED");if(velocity<=0||velocity>36000)throw std::invalid_argument("CAM_HEAD_VELOCITY_REQUIRED");a["velocityDegS"]=velocity;
        a["origin"]=HeadNumbers(RigidMatrix(a.at("origin").To<Array>()));a["axis"]=HeadNumbers(HeadParameterVector(a.at("axis"),true));
        for(const auto* key:{"zeroOffsetDeg","sign","lowerDeg","upperDeg"})a[key]=FiniteNumber(a.at(key),"CAM_HEAD_PARAMETER_INVALID");
        const auto sign=a.at("sign").To<double>(),lower=a.at("lowerDeg").To<double>(),upper=a.at("upperDeg").To<double>();
        if(sign!=1&&sign!=-1||lower>upper||lower< -36000||upper>36000||std::abs(a.at("zeroOffsetDeg").To<double>())>36000)throw std::invalid_argument("CAM_HEAD_LIMIT_INVALID: 请核对轴正向、零偏和限位。");
        axes.emplace_back(a);
    }result["axes"]=axes;if(input.contains("collisionOverrides"))result["collisionOverrides"]=NormalizeHeadCollisionOverrides(model,input.at("collisionOverrides").To<Array>());return result;
}
#include "FiveAxisHeadMotion.inc"
Map CalibratedHeadModel(const Map& instance){
    auto model=instance.at("model").To<Map>();const auto p=instance.at("parameters").To<Map>();
    ApplyHeadCollisionOverrides(model,p);
    auto tcp=HeadParameterVector(p.at("tcp"));const auto beam=HeadParameterVector(p.at("beam"),true);
    if(String(p,"tcpMode")=="Gauge"){tcp=HeadParameterVector(p.at("gaugeOrigin"));for(int i=0;i<3;++i)tcp[i]+=beam[i]*p.at("lengthMm").To<double>();}
    model["tcpLocalPosition"]=HeadNumbers(tcp);model["beamLocalDirection"]=HeadNumbers(beam);
    if(instance.contains("calibrationConfirmed")&&instance.at("calibrationConfirmed").To<bool>())model["calibrated"]=true;
    if(model.contains("assembly")){
        auto assembly=model.at("assembly").To<Map>();auto nodes=assembly.at("nodes").To<Array>();
        const auto sourceNodes=nodes;for(auto& v:nodes){auto n=v.To<Map>();n["type"]=Text("fixed");n["position"]=0.;v=n;}
        Array headAxes,workAxes;
        for(const auto& v:p.at("axes").To<Array>()){
            const auto a=v.To<Map>();const auto index=a.at("index").To<UInt>();
            if(String(a,"target")=="Workpiece"){workAxes.emplace_back(a.at("name"));continue;}
            headAxes.emplace_back(a.contains("name")?a.at("name"):sourceNodes.at(index).To<Map>().at("joint"));
            if(index>=nodes.size())continue;
            auto node=sourceNodes.at(index).To<Map>();const auto sign=a.at("sign").To<double>();
            if(a.contains("name"))node["joint"]=a.at("name");
            const auto axis=HeadParameterVector(a.at("axis"),true);iCAX::CAM::Motion::Node rotation;rotation.Type="revolute";rotation.Axis=axis;
            node["origin"]=HeadNumbers(iCAX::CAM::Motion::Multiply(RigidMatrix(a.at("origin").To<Array>()),iCAX::CAM::Motion::Joint(rotation,-sign*a.at("zeroOffsetDeg").To<double>()*std::numbers::pi/180)));
            auto effectiveAxis=axis;for(auto& x:effectiveAxis)x*=sign;node["axis"]=HeadNumbers(effectiveAxis);
            node["lower"]=a.at("lowerDeg").To<double>()*std::numbers::pi/180;node["upper"]=a.at("upperDeg").To<double>()*std::numbers::pi/180;
            node["position"]=std::clamp(FiniteNumber(node.at("position"),"CAM_HEAD_PARAMETER_INVALID"),node.at("lower").To<double>(),node.at("upper").To<double>());
            node["zeroOffsetDeg"]=a.at("zeroOffsetDeg");node["directionSign"]=sign;nodes[index]=node;
        }
        assembly["nodes"]=nodes;assembly["tcp"]=HeadNumbers(tcp);assembly["beam"]=HeadNumbers(beam);assembly["headAxes"]=headAxes;assembly["workpieceAxes"]=workAxes;model["assembly"]=assembly;
    }
    if(instance.at("binding").To<Map>().empty()){model["motion"]=CompileHeadMotion(model,p,String(instance,"id"));model["motionType"]=Text(p.at("axes").To<Array>().empty()?"Rigid":"Articulated");}
    return model;
}
Map ProjectHeadInstance(Scene& scene,const CFiveAxisSetupComponent& setup){
    if(setup.GetHeadInstanceID().is_nil())return {};
    const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup.GetJobID());const auto instances=job->GetHeadInstances();const auto key=iCAX::Data::to_string(setup.GetHeadInstanceID());
    const auto it=instances.find(key);if(it==instances.end())throw std::invalid_argument("CAM_HEAD_INSTANCE_MISSING");return it->second.To<Map>();
}
Map HeadDefinition(Scene& scene,const CFiveAxisSetupComponent& setup){
    const auto instance=ProjectHeadInstance(scene,setup);if(!instance.empty())return CalibratedHeadModel(instance);
    return {{"kind",Text("Builtin")},{"motionType",Text("Rigid")},{"rotaryAxisCount",UInt(0)},{"name",Text("内置示意头")},{"calibrated",true},{"tcpLocalPosition",Array{0.,0.,0.}},{"beamLocalDirection",Array{0.,0.,-1.}}};
}
Text HeadInstanceName(Text name){
    const auto first=name.find_first_not_of(" \t\r\n"),last=name.find_last_not_of(" \t\r\n");if(first==Text::npos||name.size()>256)throw std::invalid_argument("CAM_HEAD_NAME_REQUIRED: 请填写刀头实例名称。");return name.substr(first,last-first+1);
}
// Called inside the same transaction as import/model selection. Resource refs
// are immutable, embedded snapshots; copying an instance shares their bytes.
ID AttachProjectHead(Scene& scene,iCAX::Database::ITransaction& tx,const ID& setupID,const Map& model,const Text& requestedName={},const Map& parameters={},const Map& profile={},const Map& binding={},bool confirmed=false){
    const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());auto instances=job->GetHeadInstances();
    const auto id=iCAX::Data::GenerateNewUUID();Text name=requestedName;
    if(name.empty()){name=model.contains("assembly")?String(model.at("assembly").To<Map>(),"kind"):Text("刀头");if(name=="Articulated"||name=="Rigid")name="刀头";name+=" "+std::to_string(instances.size()+1);}
    const auto normalized=NormalizeHeadParameters(model,parameters.empty()?DefaultHeadParameters(model):parameters);
    if(binding.empty()&&!setup->GetMachineSetup().empty())throw std::invalid_argument("CAM_HEAD_MACHINE_BOUND");
    instances[iCAX::Data::to_string(id)]=Map{{"id",iCAX::Data::to_string(id)},{"name",HeadInstanceName(name)},{"model",model},{"parameters",normalized},{"profile",profile},{"binding",binding},{"calibrationConfirmed",confirmed},{"revision",UInt(1)}};
    tx.ModifyComponent(setup->GetJobID(),CFiveAxisJobComponent::S_ClassName,{{"HeadInstances",instances},{"InputRevision",Next(job->GetInputRevision())}});
    tx.ModifyComponent(setupID,CFiveAxisSetupComponent::S_ClassName,{{"HeadInstanceID",id},{"PlanValidity",Text(setup->GetLastSuccessfulPlanRef().empty()?"Missing":"Stale")}});return id;
}
Map HeadMachineConfiguration(Scene& scene,Map configuration){
    if(!configuration.contains("headInstanceId"))return configuration;
    const auto job=Require<CFiveAxisJobComponent>(scene.Database(),scene.Database().GetMetaEntity()->GetID());const auto instances=job->GetHeadInstances();const auto key=String(configuration,"headInstanceId");
    const auto it=instances.find(key);if(it==instances.end())throw std::invalid_argument("CAM_HEAD_INSTANCE_MISSING");const auto instance=it->second.To<Map>(),binding=instance.at("binding").To<Map>();
    if(String(binding,"machineId")!=String(configuration,"machineId")||String(binding,"toolId")!=String(configuration,"toolId"))throw std::invalid_argument("CAM_HEAD_MACHINE_MISMATCH");
    const auto head=CalibratedHeadModel(instance);configuration["tcp"]=head.at("tcpLocalPosition");configuration["beam"]=head.at("beamLocalDirection");return configuration;
}
void AttachBoundProjectHead(Scene&,iCAX::Database::ITransaction&,const ID&,Map&);
