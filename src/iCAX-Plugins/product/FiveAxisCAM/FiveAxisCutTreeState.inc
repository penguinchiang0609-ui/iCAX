namespace
{
    Array CutTreeOrder(Scene& scene,const ID& setupID,const Map* generatedSnapshot=nullptr);
    Map GeneratedDependencyPatch(Scene&,const ID&);
    Text CutGroupParent(const Map& group){return group.contains("parentId")?String(group,"parentId"):Text();}
    std::vector<Map> CutGroupAncestors(Scene& scene,const ID& setupID,const Text& groupID)
    {
        std::vector<Map> result;auto parent=groupID;std::map<Text,Map> groups;std::set<Text> visited;
        for(const auto& value:IntentCutGroups(scene,setupID)){
            const auto group=value.To<Map>();groups.emplace(String(group,"id"),group);
        }
        while(!parent.empty()){
            if(!visited.insert(parent).second||visited.size()>64)throw std::invalid_argument("CAM_CUT_GROUP_CYCLE");
            if(!groups.contains(parent))throw std::logic_error("CAM_CUT_GROUP_MISSING");
            const auto& group=groups.at(parent);result.push_back(group);parent=CutGroupParent(group);
        }return result;
    }
    template<class Feature> std::vector<Map> CutGroupAncestors(Scene& scene,const Feature& feature)
    {return CutGroupAncestors(scene,feature.GetSetupID(),feature.GetGroupID());}
    bool CutLocked(Scene& scene,const ID& id)
    {
        return ReadIntentPath(scene,id).Locked;
    }
    template<class Cut> bool CutEnabledForView(Scene& scene,const Cut& cut)
    {
        const auto placement=Require<CFiveAxisPlacementComponent>(scene.Database(),cut.GetPlacementID());
        return Require<CFiveAxisSetupComponent>(scene.Database(),cut.GetSetupID())->GetEnabled()
            &&Require<CFiveAxisWorkpieceComponent>(scene.Database(),placement->GetWorkpieceID())->GetEnabled()
            &&cut.GetEffectiveEnabled();
    }
    bool CutEnabled(Scene& scene,const ID& id){return CutEnabledForView(scene,*ReadIntentCut(scene,id));}
    void RequireCutUnlocked(Scene& scene,const ID& id)
    {if(CutLocked(scene,id))throw std::invalid_argument("CAM_CUT_LOCKED");}
    // Return authoritative metadata with the transaction receipt. Tree switches
    // and names do not require serializing all generated cutting/rapid points.
    Map CutMetadataPatch(Scene& scene,const ID& setupID,bool invalidatesGeometry)
    {
        const auto runtime=Runtime(scene);const auto setup=Require<CFiveAxisSetupComponent>(scene.Database(),setupID);
        const auto job=Require<CFiveAxisJobComponent>(scene.Database(),setup->GetJobID());Array cuts;
        for(const auto& feature:IntentCuts(scene,setupID)){
                cuts.emplace_back(Map{{"id",iCAX::Data::to_string(feature->GetID())},{"name",feature->GetName()},
                    {"enabled",CutEnabledForView(scene,*feature)},{"ownEnabled",feature->GetEnabled()},
                    {"locked",feature->GetEffectiveLocked()},{"ownLocked",feature->GetLocked()},{"groupId",feature->GetGroupID()},{"layers",feature->Path.Layers}});
            }
        Map state{{"id",iCAX::Data::to_string(setupID)},{"inputRevision",std::to_string(setup->GetInputRevision())},
            {"planValidity",setup->GetPlanValidity()},{"cutGroups",IntentCutGroups(scene,setupID)}};
        const auto ordering=IntentCutOrderPolicy(scene,setupID);
        Map geometry{{"ordering",ordering.empty()?Map{{"mode",Text("Automatic")}}:ordering}};
        if(invalidatesGeometry)geometry["status"]=Text(setup->GetGeneratedToolpaths().empty()?"Missing":"Stale");
        state["generatedGeometry"]=geometry;
        // Compact edit receipts must invalidate the independent optical proof
        // immediately, just as the full scene projection does.
        Map patch{{"layers",IntentDocument(scene).QueryLayers()},{"expected",Map{{"epoch",runtime->Epoch},{"mutationSequence",std::to_string(runtime->Sequence)}}},
            {"inputRevision",std::to_string(job->GetInputRevision())},{"setup",state},{"intentToolpaths",cuts},{"fixtureValidity",FixtureValidityPatch(scene,setupID)}};
        if(invalidatesGeometry){const auto dependency=GeneratedDependencyPatch(scene,setupID);
            for(const auto& [key,value]:dependency.at("geometry").To<Map>())geometry[key]=value;
            for(const auto* key:{"transitionStates","operationStates","operationOrder"})patch[key]=dependency.at(key);
            state["generatedGeometry"]=geometry;patch["setup"]=state;}
        return patch;
    }
    ID CutID(const Variant& value){return Identifier(Map{{"id",value}},"id");}
    // A group is one ordering unit. A locked child also prevents moving its group.
    std::vector<Array> CutTreeUnits(Scene& scene,const Array& order)
    {
        std::vector<Array> units;std::map<Text,std::size_t> groups;
        for(const auto& id:order){const auto ancestors=CutGroupAncestors(scene,*ReadIntentCut(scene,CutID(id)));
            const auto group=ancestors.empty()?Text():String(ancestors.back(),"id");
            if(group.empty()){units.push_back({id});continue;}
            if(!groups.contains(group)){groups[group]=units.size();units.push_back({});}
            units[groups.at(group)].push_back(id);
        }return units;
    }
    bool CutUnitLocked(Scene& scene,const Array& unit)
    {return std::any_of(unit.begin(),unit.end(),[&](const auto& id){return CutLocked(scene,CutID(id));});}
    Array FlattenCutUnits(const std::vector<Array>& units)
    {Array order;for(const auto& unit:units)order.insert(order.end(),unit.begin(),unit.end());return order;}
    // Manual ordering validates every hierarchy level, not only root groups.
    Array ConstrainedCutOrder(Scene& scene,const Array& current,const Array& desired)
    {
        std::map<Text,std::size_t> ranks;std::map<Text,std::vector<Text>> ancestors;
        for(const auto& id:current){const auto chain=CutGroupAncestors(scene,*ReadIntentCut(scene,CutID(id)));
            for(auto i=chain.rbegin();i!=chain.rend();++i)ancestors[id.To<Text>()].push_back(String(*i,"id"));}
        for(std::size_t i=0;i<desired.size();++i)ranks[desired[i].To<Text>()]=i;
        const auto rank=[&](const Array& unit){auto r=desired.size();for(const auto& id:unit)
            if(ranks.contains(id.To<Text>()))r=std::min(r,ranks.at(id.To<Text>()));return r;};
        std::function<Array(const Array&,std::size_t)> sort=[&](const Array& members,std::size_t depth){
            std::vector<Array> units;std::map<Text,std::size_t> positions;
            for(const auto& id:members){const auto& chain=ancestors[id.To<Text>()];const auto key=depth<chain.size()?Text("g:")+chain[depth]:Text("c:")+id.To<Text>();
                if(!positions.contains(key)){positions[key]=units.size();units.push_back({});}units[positions.at(key)].push_back(id);}
            for(auto& unit:units)if(depth<ancestors[unit.front().To<Text>()].size())unit=sort(unit,depth+1);
            for(std::size_t begin=0;begin<units.size();){
                if(CutUnitLocked(scene,units[begin])){++begin;continue;}
                auto end=begin;while(end<units.size()&&!CutUnitLocked(scene,units[end]))++end;
                std::stable_sort(units.begin()+begin,units.begin()+end,[&](const auto& a,const auto& b){return rank(a)<rank(b);});begin=end;
            }return FlattenCutUnits(units);
        };return sort(current,0);
    }
}
