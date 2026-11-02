namespace
{
    Array CutTreeOrder(Scene& scene,const ID& setupID,const Map* generatedSnapshot)
    {
        (void)generatedSnapshot;return ReadOrderStructure(scene,setupID).Order;
    }
}
namespace
{
    // Mutable command-local tree. Persist only group parents, cut membership and
    // the flattened machining order, together in one Scene transaction.
    struct CutTreeDraft
    {
        std::map<Text,Map> Groups;
        std::map<Text,Text> Parent;
        std::map<Text,std::vector<Text>> Children;
        std::map<Text,bool> Enabled,Locked;
        static Text GroupKey(const Text& id){return "g:"+id;}
        static Text CutKey(const Text& id){return "c:"+id;}
        static bool IsGroup(const Text& key){return key.starts_with("g:");}
        static Text Raw(const Text& key){return key.substr(2);}
        CutTreeDraft(Scene& scene,const ID& setupID,const Array& order)
        {
            const auto& snapshot=ReadOrderStructure(scene,setupID);
            for(const auto& value:snapshot.Groups){
                const auto g=value.To<Map>();const auto key=GroupKey(String(g,"id"));Groups[key]=g;
                const auto parent=CutGroupParent(g);Parent[key]=parent.empty()?Text():GroupKey(parent);
                Enabled[key]=g.at("enabled").To<bool>();Locked[key]=g.at("locked").To<bool>();
            }
            for(const auto& value:order){const auto id=CutID(value);const auto& f=snapshot.Paths.at(id);
                const auto key=CutKey(value.To<Text>()),parent=f.GroupId;Parent[key]=parent.empty()?Text():GroupKey(parent);
                Enabled[key]=f.OwnParticipating;Locked[key]=f.OwnLocked;
                auto chain=Ancestors(key);std::reverse(chain.begin(),chain.end());chain.push_back(key);
                for(const auto& n:chain){auto& siblings=Children[Parent.at(n)];if(std::find(siblings.begin(),siblings.end(),n)==siblings.end())siblings.push_back(n);}
            }
        }
        std::vector<Text> Ancestors(const Text& key) const
        {
            std::vector<Text> result;std::set<Text> seen{key};auto p=Parent.at(key);
            while(!p.empty()){
                if(!Groups.contains(p))throw std::invalid_argument("CAM_CUT_GROUP_MISSING");
                if(!seen.insert(p).second||seen.size()>64)throw std::invalid_argument("CAM_CUT_GROUP_CYCLE");
                result.push_back(p);p=Parent.at(p);
            }return result;
        }
        bool Under(const Text& key,const Text& ancestor) const
        {if(key==ancestor)return true;const auto parents=Ancestors(key);return std::find(parents.begin(),parents.end(),ancestor)!=parents.end();}
        bool Effective(const Text& key,bool enabled) const
        {
            bool state=(enabled?Enabled:Locked).at(key);
            for(const auto& p:Ancestors(key))state=enabled?(state&&Enabled.at(p)):(state||Locked.at(p));return state;
        }
        bool AncestorLocked(const Text& key) const
        {for(const auto& p:Ancestors(key))if(Locked.at(p))return true;return false;}
        std::vector<Text> Walk(const Text& parent=Text()) const
        {
            std::vector<Text> result;if(!Children.contains(parent))return result;
            for(const auto& n:Children.at(parent)){result.push_back(n);if(IsGroup(n)){auto sub=Walk(n);result.insert(result.end(),sub.begin(),sub.end());}}return result;
        }
        std::vector<Text> Leaves(const Text& node) const
        {if(!IsGroup(node))return {node};auto nodes=Walk(node);std::erase_if(nodes,[](const Text& n){return IsGroup(n);});return nodes;}
        bool ContainsLocked(const Text& node) const
        {if(Effective(node,false))return true;for(const auto& n:Walk(node))if(Locked.at(n))return true;return false;}
        Array Order() const
        {Array result;for(const auto& key:Walk())if(!IsGroup(key))result.emplace_back(Raw(key));return result;}
        void Detach(const Text& key){std::erase(Children[Parent.at(key)],key);}
        void Prune()
        {
            bool changed=true;while(changed){changed=false;for(auto i=Groups.begin();i!=Groups.end();){
                if(Children[i->first].empty()){Detach(i->first);Parent.erase(i->first);Enabled.erase(i->first);Locked.erase(i->first);i=Groups.erase(i);changed=true;}
                else ++i;
            }}
        }
        Array GroupRecords() const
        {
            Array result;for(const auto& [key,record]:Groups){auto g=record;const auto p=Parent.at(key);g["parentId"]=p.empty()?Text():Raw(p);
                g["enabled"]=Enabled.at(key);g["locked"]=Locked.at(key);result.emplace_back(g);}return result;
        }
    };
}
Map ManageCutTree(Scene& scene,const Map& request)
{
    CutFields(request,{"commandId","expected","setupId","intentIds","groupIds","groupId","action","target"});
    const auto runtime=Runtime(scene);const auto command=iCAX::Data::to_string(Identifier(request,"commandId"));
    const auto payload=Text("ManageCutTree:")+iCAX::Data::VariantSerializer::Serialize(request);
    if(const auto prior=runtime->Receipts.find(command);prior!=runtime->Receipts.end()){
        if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}
    for(const auto& [_,task]:runtime->Tasks)if(task.CommandID==command)throw std::invalid_argument("CAM_COMMAND_REUSED");
    CheckExpected(*runtime,request);CheckCapacity(*runtime);
    const auto setupID=Identifier(request,"setupId");const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
    const auto action=String(request,"action");
    if(!std::set<Text>{"Delete","Enable","Disable","Up","Down","Lock","Unlock","Group","Ungroup","Move"}.contains(action))throw std::invalid_argument("CAM_TREE_ACTION_INVALID");
    if((action=="Move")!=request.contains("target"))throw std::invalid_argument("CAM_TREE_TARGET_REQUIRED");
    const auto previous=CutTreeOrder(scene,setupID);CutTreeDraft tree(scene,setupID,previous);const auto before=tree;
    auto groupIDs=request.contains("groupIds")?request.at("groupIds").To<Array>():Array();
    if(request.contains("groupId")){
        if(!groupIDs.empty()||!request.at("intentIds").To<Array>().empty())throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
        groupIDs.push_back(request.at("groupId"));
    }
    std::set<Text> selection;
    for(const auto& id:request.at("intentIds").To<Array>()){
        if(!tree.Parent.contains(CutTreeDraft::CutKey(id.To<Text>())))throw std::invalid_argument("CAM_CUT_SETUP_MISMATCH");
        if(!selection.insert(CutTreeDraft::CutKey(id.To<Text>())).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");
    }
    for(const auto& id:groupIDs){const auto key=CutTreeDraft::GroupKey(id.To<Text>());
        if(!tree.Groups.contains(key))throw std::invalid_argument("CAM_CUT_GROUP_MISSING");
        if(!selection.insert(key).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");}
    if(selection.empty()||selection.size()>4096)throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    // A selected parent already carries its entire subtree; never flatten it.
    std::vector<Text> chosen;
    for(const auto& node:tree.Walk())if(selection.contains(node)){
        const auto ancestors=tree.Ancestors(node);
        if(std::none_of(ancestors.begin(),ancestors.end(),[&](const auto& p){return selection.contains(p);}))chosen.push_back(node);
    }
    if(chosen.empty())throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");
    std::set<Text> selected(chosen.begin(),chosen.end());Array affected;
    for(const auto& node:chosen){
        if(tree.AncestorLocked(node))throw std::invalid_argument("CAM_CUT_GROUP_LOCKED");
        if(action!="Lock"&&action!="Unlock"&&tree.ContainsLocked(node))throw std::invalid_argument("CAM_CUT_LOCKED");
        for(const auto& leaf:tree.Leaves(node))affected.emplace_back(CutTreeDraft::Raw(leaf));
    }
    Text createdGroup;
    if(action=="Group"){
        if(chosen.size()<2)throw std::invalid_argument("CAM_GROUP_REQUIRES_MULTIPLE_CUTS");
        auto candidates=tree.Ancestors(chosen.front());candidates.push_back(Text());Text parent;
        for(const auto& candidate:candidates){if(std::all_of(chosen.begin(),chosen.end(),[&](const auto& n){return candidate.empty()||tree.Under(n,candidate);})) {parent=candidate;break;}}
        auto first=chosen.front();while(tree.Parent.at(first)!=parent)first=tree.Parent.at(first);
        const auto oldSiblings=tree.Children[parent];const auto firstIndex=std::find(oldSiblings.begin(),oldSiblings.end(),first)-oldSiblings.begin();
        size_t insertion=0;for(size_t i=0;i<static_cast<size_t>(firstIndex);++i)if(!selected.contains(oldSiblings[i]))++insertion;
        for(const auto& node:chosen)tree.Detach(node);
        createdGroup=command;const auto key=CutTreeDraft::GroupKey(createdGroup);unsigned number=1;std::set<Text> names;
        for(const auto& [_,g]:tree.Groups)names.insert(String(g,"name"));while(names.contains("组 "+std::to_string(number)))++number;
        tree.Groups[key]={{"id",createdGroup},{"name",Text("组 ")+std::to_string(number)}};tree.Enabled[key]=true;tree.Locked[key]=false;
        tree.Parent[key]=parent;tree.Children[parent].insert(tree.Children[parent].begin()+insertion,key);tree.Children[key]=chosen;
        for(const auto& node:chosen)tree.Parent[node]=key;
    }
    if(action=="Ungroup"){
        if(std::any_of(chosen.begin(),chosen.end(),[](const auto& n){return !CutTreeDraft::IsGroup(n);}))throw std::invalid_argument("CAM_CUT_GROUP_REQUIRED");
        for(const auto& node:chosen){auto& siblings=tree.Children[tree.Parent.at(node)];const auto at=std::find(siblings.begin(),siblings.end(),node)-siblings.begin();
            siblings.erase(siblings.begin()+at);const auto children=tree.Children[node];siblings.insert(siblings.begin()+at,children.begin(),children.end());
            for(const auto& child:children){tree.Parent[child]=tree.Parent.at(node);if(!tree.Enabled[node])tree.Enabled[child]=false;}
            tree.Children[node].clear();tree.Groups.erase(node);tree.Parent.erase(node);tree.Enabled.erase(node);tree.Locked.erase(node);
        }
    }
    if(action=="Up"||action=="Down")for(auto& [_,siblings]:tree.Children){
        const auto movable=[&](const Text& n){return !tree.ContainsLocked(n);};
        if(action=="Up")for(size_t i=1;i<siblings.size();++i){if(selected.contains(siblings[i])&&!selected.contains(siblings[i-1])&&movable(siblings[i-1]))std::swap(siblings[i],siblings[i-1]);}
        else for(size_t i=siblings.size();i>1;--i){if(selected.contains(siblings[i-2])&&!selected.contains(siblings[i-1])&&movable(siblings[i-1]))std::swap(siblings[i-2],siblings[i-1]);}
    }
    if(action=="Move"){
        const auto target=request.at("target").To<Map>();CutFields(target,{"kind","id","position"});
        const auto kind=String(target,"kind"),position=String(target,"position"),id=String(target,"id");Text key,parent;
        if(kind=="Root"){if(!id.empty()||position!="Inside")throw std::invalid_argument("CAM_TREE_TARGET_INVALID");}
        else{
            if(kind!="Cut"&&kind!="Group")throw std::invalid_argument("CAM_TREE_TARGET_INVALID");
            key=kind=="Group"?CutTreeDraft::GroupKey(id):CutTreeDraft::CutKey(id);
            if(!tree.Parent.contains(key))throw std::invalid_argument("CAM_TREE_TARGET_MISSING");
            if(position!="Before"&&position!="After"&&!(position=="Inside"&&kind=="Group"))throw std::invalid_argument("CAM_TREE_TARGET_INVALID");
            for(const auto& n:chosen)if(tree.Under(key,n))throw std::invalid_argument("CAM_CUT_GROUP_CYCLE");
            if(tree.Effective(key,false))throw std::invalid_argument("CAM_CUT_LOCKED");
            parent=position=="Inside"?key:tree.Parent.at(key);
        }
        for(const auto& n:chosen)tree.Detach(n);
        auto& siblings=tree.Children[parent];auto insertion=siblings.size();
        if(position!="Inside")insertion=std::find(siblings.begin(),siblings.end(),key)-siblings.begin()+(position=="After"?1:0);
        siblings.insert(siblings.begin()+insertion,chosen.begin(),chosen.end());for(const auto& n:chosen)tree.Parent[n]=parent;
    }
    if(action=="Delete")for(const auto& n:chosen){const auto nodes=tree.Walk(n);tree.Detach(n);for(const auto& d:nodes){tree.Groups.erase(d);tree.Parent.erase(d);tree.Enabled.erase(d);tree.Locked.erase(d);tree.Children.erase(d);}
        tree.Groups.erase(n);tree.Parent.erase(n);tree.Enabled.erase(n);tree.Locked.erase(n);tree.Children.erase(n);}
    if(action=="Enable"||action=="Disable"||action=="Lock"||action=="Unlock")for(const auto& n:chosen)
        (action=="Enable"||action=="Disable"?tree.Enabled:tree.Locked)[n]=action=="Enable"||action=="Lock";
    tree.Prune();
    for(const auto& [node,_]:tree.Parent)tree.Ancestors(node); // Validate depth, references and cycles before commit.
    const auto order=tree.Order(),groups=tree.GroupRecords();
    if(action=="Group"||action=="Move")for(size_t i=0;i<previous.size();++i)
        if(before.Effective(CutTreeDraft::CutKey(previous[i].To<Text>()),false)&&order[i]!=previous[i])throw std::invalid_argument("CAM_CUT_LOCKED");
    const bool changed=order!=previous||groups!=before.GroupRecords()||tree.Parent!=before.Parent||tree.Enabled!=before.Enabled||tree.Locked!=before.Locked;
    // A permutation needs only execution constraints. Do not resolve path
    // geometry, republish the machining plan, or build a full scene receipt.
    if((action=="Up"||action=="Down"||action=="Move")&&tree.Parent==before.Parent&&groups==before.GroupRecords()){
        if(changed){auto ordering=request;ordering["mode"]=Text("Manual");CommitToolpathOrder(scene,ordering,order,{});}
        Map result{{"changed",changed},{"orderOnly",true},{"setupId",iCAX::Data::to_string(setupID)},
            {"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}}};
        runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
    }
    bool machiningChanged=action=="Delete";
    for(const auto& id:order){const auto key=CutTreeDraft::CutKey(id.To<Text>());machiningChanged|=tree.Effective(key,true)!=before.Effective(key,true);}
    if(changed)
    {
        using namespace iCAX::CAM::Intent;
        const auto plan=IntentPlan(scene,setupID);iCAX::Data::uuid_name_generator names(Identifier(request,"commandId"));std::vector<Map> commands;
        const auto append=[&](const Text& kind,const Text& role,Map values)
        {values["kind"]=kind;values["commandId"]=names("fiveaxis.tree/"+role);values["planId"]=plan;commands.push_back(std::move(values));};
        const auto targets=[&](const Array& ids)
        {
            Array ports;Map fingerprints,producers;
            for(const auto& id:ids){const auto path=ReadIntentPath(scene,CutID(id));ports.emplace_back(ToData(path.Port));
                const auto key=ToolpathPortKey(path.Port);
                if(path.Fingerprint.empty())producers[key]=IntentOperationDefinition(scene,path.Port.OperationId).at("definitionFingerprint");
                else fingerprints[key]=path.Fingerprint;}
            return Map{{"targets",ports},{"expectedInputs",fingerprints},{"expectedProducers",producers},{"allowStaleTargets",true}};
        };
        if(action=="Delete")append("DeletePaths","delete",targets(affected));
        for(const auto& id:order)
        {
            const auto key=CutTreeDraft::CutKey(id.To<Text>());
            if(tree.Enabled.at(key)!=before.Enabled.at(key))
            {auto command=targets({id});command["enabled"]=tree.Enabled.at(key);append("SetParticipation","participation/"+id.To<Text>(),std::move(command));}
            if(tree.Locked.at(key)!=before.Locked.at(key))
                append("SetPathLocked","locked/"+id.To<Text>(),{{"pathIds",Array{CutID(id)}},{"locked",tree.Locked.at(key)}});
        }
        const bool organizationChanged=groups!=before.GroupRecords()||tree.Parent!=before.Parent;
        if(organizationChanged)
        {
            const bool membershipChanged=tree.Parent!=before.Parent;
            // Clear old direct memberships first, so moving a current path
            // between groups never creates transient conflicting ownership.
            // A flag-only edit must retain members, including when unlocking.
            if(membershipChanged)for(const auto& [key,record]:before.Groups)
                append("Group","clear-group/"+CutTreeDraft::Raw(key),{{"groupId",CutTreeDraft::Raw(key)},{"members",Array{}}});
            for(const auto& [key,record]:before.Groups)if(!tree.Groups.contains(key))
                append("Ungroup","remove-group/"+CutTreeDraft::Raw(key),{{"groupId",CutTreeDraft::Raw(key)}});
            std::vector<Text> groupKeys;for(const auto& [key,record]:tree.Groups)groupKeys.push_back(key);
            std::stable_sort(groupKeys.begin(),groupKeys.end(),[&](const auto& a,const auto& b){return tree.Ancestors(a).size()<tree.Ancestors(b).size();});
            for(const auto& key:groupKeys)
            {
                if(!membershipChanged&&tree.Enabled.at(key)==before.Enabled.at(key)&&tree.Locked.at(key)==before.Locked.at(key)&&tree.Groups.at(key)==before.Groups.at(key))continue;
                Array members;for(const auto& id:order)if(tree.Parent.at(CutTreeDraft::CutKey(id.To<Text>()))==key)members.emplace_back(CutID(id));
                const auto parent=tree.Parent.at(key);
                append("Group","write-group/"+CutTreeDraft::Raw(key),{{"groupId",CutTreeDraft::Raw(key)},{"name",tree.Groups.at(key).at("name")},
                    {"parentGroupId",parent.empty()?Text():CutTreeDraft::Raw(parent)},{"members",members},{"enabled",tree.Enabled.at(key)},{"locked",tree.Locked.at(key)}});
            }
        }
        if(order!=previous||action=="Group"||action=="Lock"||action=="Move"||((action=="Enable"||action=="Disable")&&IntentCutOrderPolicy(scene,setupID).empty()))
        {
            const auto policy=IntentCutOrderPolicy(scene,setupID);
            const auto strategy=policy.contains("strategy")?String(policy,"strategy"):Text("MarkHoleTrim");
            auto constraints=IntentDocument(scene).ReadPlanDefinition(plan).at("executionConstraints").To<Map>();
            auto blocks=constraints.contains("blocks")?constraints.at("blocks").To<Array>():Array{};
            std::erase_if(blocks,[](const auto& item){const auto id=String(item.To<Map>(),"id");return id=="scene.manual-order"||id=="scene.automatic-order";});
            Array members;for(const auto& id:order)members.emplace_back(ToData(ReadIntentPath(scene,CutID(id)).Port));
            if(!members.empty()){Map block{{"id",Text("scene.manual-order")},{"members",members},{"atomic",false},{"strategy",strategy}};if(policy.contains("options"))block["options"]=policy.at("options");blocks.emplace_back(std::move(block));}
            constraints["blocks"]=std::move(blocks);append("SetExecutionConstraints","order",{{"constraints",constraints}});
        }
        CommitIntentCommands(scene,commands,{setupID},"编辑刀路树",false);
    }
    Map result{{"changed",changed},{"intentIds",affected},{"order",order},{"groupId",createdGroup}};
    {
        auto patch=CutMetadataPatch(scene,setupID,changed&&machiningChanged),state=patch.at("setup").To<Map>();state["definitionOrder"]=order;patch["setup"]=state;result["statePatch"]=patch;
        if(action=="Delete"&&changed)std::get<Map>(result.at("statePatch").m_Value)["removedIntentIds"]=affected;
    }
    runtime->Receipts.emplace(command,std::make_pair(payload,result));return result;
}
