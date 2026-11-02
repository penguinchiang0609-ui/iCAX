// Per-instance overrides retain the immutable imported/catalog definition.
Array NormalizeHeadCollisionOverrides(const Map& model,const Array& input){
    const auto nodes=model.contains("assembly")?model.at("assembly").To<Map>().at("nodes").To<Array>():Array{};
    if(input.size()>std::max<std::size_t>(1,nodes.size()))throw std::invalid_argument("CAM_HEAD_COLLISION_PART_INVALID");
    Array result;std::set<int> parts;std::size_t total=0;
    for(const auto& value:input){
        const auto group=value.To<Map>();CutFields(group,{"partIndex","boxes"});const auto number=FiniteNumber(group.at("partIndex"),"CAM_HEAD_COLLISION_PART_INVALID");
        if(number!=std::floor(number)||number< -1||number>=static_cast<double>(nodes.size())||nodes.empty()!=(number== -1))throw std::invalid_argument("CAM_HEAD_COLLISION_PART_INVALID");
        const auto part=static_cast<int>(number);if(!parts.insert(part).second)throw std::invalid_argument("CAM_HEAD_COLLISION_PART_INVALID");
        Array boxes;std::set<Text> ids;
        for(const auto& entry:group.at("boxes").To<Array>()){
            if(++total>256)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_LIMIT");
            const auto box=entry.To<Map>();CutFields(box,{"id","name","enabled","pose","geometry"});
            const auto id=String(box,"id"),name=String(box,"name");if(id.empty()||id.size()>256||name.empty()||name.size()>256||!ids.insert(id).second)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_INVALID");
            const auto pose=box.at("pose").To<Array>();if(pose.size()!=6)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_INVALID");Array normalizedPose;
            for(int k=0;k<6;++k){const auto n=FiniteNumber(pose[k],"CAM_HEAD_COLLISION_BOX_INVALID");if(std::abs(n)>1000.)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_INVALID");normalizedPose.emplace_back(n);}
            const auto geometry=box.at("geometry").To<Map>();CutFields(geometry,{"type","size"});if(String(geometry,"type")!="box")throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_REQUIRED");
            const auto size=geometry.at("size").To<Array>();if(size.size()!=3)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_SIZE");Array normalizedSize;
            for(const auto& v:size){const auto n=FiniteNumber(v,"CAM_HEAD_COLLISION_BOX_SIZE");if(n<=0||n>1000.)throw std::invalid_argument("CAM_HEAD_COLLISION_BOX_SIZE: 碰撞盒尺寸必须大于零。");normalizedSize.emplace_back(n);}
            boxes.emplace_back(Map{{"id",id},{"name",name},{"enabled",box.at("enabled").To<bool>()},{"pose",normalizedPose},{"geometry",Map{{"type",Text("box")},{"size",normalizedSize}}}});
        }
        result.emplace_back(Map{{"partIndex",part},{"boxes",boxes}});
    }return result;
}
void ApplyHeadCollisionOverrides(Map& model,const Map& parameters){
    if(!parameters.contains("collisionOverrides"))return;
    auto assembly=model.contains("assembly")?model.at("assembly").To<Map>():Map{};
    auto nodes=assembly.empty()?Array{}:assembly.at("nodes").To<Array>();
    for(const auto& value:parameters.at("collisionOverrides").To<Array>()){
        const auto group=value.To<Map>();const auto part=group.at("partIndex").To<int>();auto boxes=group.at("boxes").To<Array>();
        if(part<0){if(model.contains("collisionBoxes"))for(const auto& v:model.at("collisionBoxes").To<Array>())if(String(v.To<Map>().at("geometry").To<Map>(),"type")!="box")boxes.push_back(v);model["collisionBoxes"]=boxes;continue;}
        auto node=nodes.at(part).To<Map>();
        // Non-box declarations are never silently deleted by the box editor.
        if(node.contains("collisionBoxes"))for(const auto& v:node.at("collisionBoxes").To<Array>())if(String(v.To<Map>().at("geometry").To<Map>(),"type")!="box")boxes.push_back(v);
        node["collisionBoxes"]=boxes;nodes[part]=node;
        if(part==assembly.at("tool").To<int>())model["collisionBoxes"]=boxes;
    }
    if(!assembly.empty()){assembly["nodes"]=nodes;model["assembly"]=assembly;}
}
