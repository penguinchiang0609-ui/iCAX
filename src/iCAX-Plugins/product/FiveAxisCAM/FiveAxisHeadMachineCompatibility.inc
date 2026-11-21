// Compatibility follows the declared tool ancestry, never a catalog label or
// a stale rotaryAxisCount. Work-support axes are not part of this tool branch.
std::optional<unsigned> HeadToolRotations(const Map& model){
    try{
        if(!model.contains("assembly")){
            const auto kind=model.contains("kind")?String(model,"kind"):Text();
            const auto source=[&](const char* key){if(!model.contains(key))return false;const auto ref=model.at(key).To<Map>();return ref.contains("headAsset")||iCAX::ProjectFile::TryGetResourceReferenceValue(ref).has_value();};
            if((kind=="Imported"||kind=="Builtin")&&model.contains("motionType")&&String(model,"motionType")=="Rigid"
                &&model.contains("rotaryAxisCount")&&FiniteNumber(model.at("rotaryAxisCount"),"CAM_HEAD_TOPOLOGY_MISMATCH")==0
                &&(source("geometryRef")||source("previewRef")))return 0;
            return std::nullopt;
        }
        const auto assembly=model.at("assembly").To<Map>();const auto nodes=assembly.at("nodes").To<Array>();
        const auto tool=FiniteNumber(assembly.at("tool"),"CAM_HEAD_TOPOLOGY_MISMATCH");
        if(nodes.empty()||tool<0||tool>=nodes.size()||std::floor(tool)!=tool)return std::nullopt;
        std::set<std::size_t> visited;unsigned rotations=0;double index=tool;
        while(index>=0){
            if(index>=nodes.size()||std::floor(index)!=index||!visited.insert(static_cast<std::size_t>(index)).second)return std::nullopt;
            const auto node=nodes[static_cast<std::size_t>(index)].To<Map>();const auto type=String(node,"type");
            if(type=="revolute"||type=="continuous")++rotations;
            else if(type!="fixed"&&type!="prismatic")return std::nullopt;
            index=FiniteNumber(node.at("parent"),"CAM_HEAD_TOPOLOGY_MISMATCH");if(index< -1||std::floor(index)!=index)return std::nullopt;
        }
        return rotations;
    }catch(const std::exception&){return std::nullopt;}
}
Array HeadMachineTypes(const Map& model){
    const auto count=HeadToolRotations(model);if(!count||*count>2)return {};
    return {Text(*count==2?"HeadHead":*count==1?"HeadTable":"TableTable")};
}
Array HeadInstanceMachineTypes(const Map& instance){
    try{
        const auto model=instance.at("model").To<Map>();const auto source=HeadToolRotations(model);
        if(!source||*source>2)return {};
        if(!instance.at("binding").To<Map>().empty()&&!model.contains("assembly"))return {};
        const auto effective=model.contains("assembly")?HeadToolRotations(CalibratedHeadModel(instance)):source;
        if(!effective||*source!=*effective)return {};
        // Parameters cannot turn an unused side branch into the tool mechanism
        // or reclassify an actual head joint as a workpiece axis.
        if(model.contains("assembly")){
            const auto assembly=model.at("assembly").To<Map>();const auto nodes=assembly.at("nodes").To<Array>();
            std::set<std::size_t> chain;auto index=FiniteNumber(assembly.at("tool"),"CAM_HEAD_TOPOLOGY_MISMATCH");
            while(index>=0){const auto n=static_cast<std::size_t>(index);if(!chain.insert(n).second)return {};index=FiniteNumber(nodes.at(n).To<Map>().at("parent"),"CAM_HEAD_TOPOLOGY_MISMATCH");}
            for(const auto& value:instance.at("parameters").To<Map>().at("axes").To<Array>()){
                const auto axis=value.To<Map>();const auto n=axis.at("index").To<UInt>();const auto target=String(axis,"target");
                if(target=="Head"&&!chain.contains(static_cast<std::size_t>(n)))return {};
                if(target=="Workpiece"&&chain.contains(static_cast<std::size_t>(n))){const auto type=String(nodes.at(n).To<Map>(),"type");if(type=="revolute"||type=="continuous")return {};}
            }
        }else for(const auto& value:instance.at("parameters").To<Map>().at("axes").To<Array>())if(String(value.To<Map>(),"target")=="Head")return {};
        return {Text(*source==2?"HeadHead":*source==1?"HeadTable":"TableTable")};
    }catch(const std::exception&){return {};}
}
bool HeadFitsMachineType(const Map& instance,const Text& type){
    const auto types=HeadInstanceMachineTypes(instance);return std::any_of(types.begin(),types.end(),[&](const auto& v){return v.template To<Text>()==type;});
}
void RequireHeadFitsMachineType(const Map& instance,const Text& type){
    if(type.empty())throw std::invalid_argument("CAM_OPERATION_MACHINE_TYPE_REQUIRED: 请先在创建工序时选择机型。");
    if(!HeadFitsMachineType(instance,type))throw std::invalid_argument("CAM_HEAD_MACHINE_TYPE_MISMATCH: 切割头的转轴结构与当前工序机型不匹配，请选择适配的切割头。");
}
void RequireHeadFitsOperation(Scene& scene,const ID& setupID,const Map& instance){RequireHeadFitsMachineType(instance,EffectiveMachineTypeValue(scene,setupID));}
void RequireHeadCatalogFitsOperation(Scene& scene,const ID& setupID,const Array& types){
    const auto type=EffectiveMachineTypeValue(scene,setupID);
    if(type.empty())throw std::invalid_argument("CAM_OPERATION_MACHINE_TYPE_REQUIRED: 请先在创建工序时选择机型。");
    if(std::none_of(types.begin(),types.end(),[&](const auto& value){return value.template To<Text>()==type;}))throw std::invalid_argument("CAM_HEAD_MACHINE_TYPE_MISMATCH: 切割头的转轴结构与当前工序机型不匹配，请选择适配的切割头。");
}
