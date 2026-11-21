#include "ordinary_multi_continuation_mapped.hpp"
#include "ordinary_predefined_validation.hpp"
#include "recovered_multi_continuation.hpp"
#include <set>

namespace study {
namespace {
bool unchangedGeometry(const PartInput &a,const PartInput &b){
    if(a.polygon.size()!=b.polygon.size()||a.pivot.x!=b.pivot.x||a.pivot.y!=b.pivot.y||a.poses.size()!=b.poses.size()||a.protection!=b.protection||!a.holes.empty()||!b.holes.empty())return false;
    for(size_t n=0;n<a.polygon.size();++n)if(a.polygon[n].x!=b.polygon[n].x||a.polygon[n].y!=b.polygon[n].y)return false;
    for(size_t n=0;n<a.poses.size();++n)if(a.poses[n].angle!=b.poses[n].angle||a.poses[n].upper!=b.poses[n].upper||a.poses[n].flip!=b.poses[n].flip)return false;
    return true;
}
void transferConcrete(OrdinaryMultiPredefinedScene &scene,recovered::PlacementLifecycle &lifecycle){std::swap(scene.placementCatalog,lifecycle.catalog);std::swap(scene.instances,lifecycle.instances);}
class MappedBoardContinuation {
    OrdinaryMultiPredefinedScene &scene;
    OrdinaryMultiPredefinedBoard &board;
public:
    OrdinaryState state;
    MappedBoardContinuation(OrdinaryMultiPredefinedScene &s,OrdinaryMultiPredefinedBoard &b):scene(s),board(b){
        state.placementHost=std::make_unique<OrdinaryState::PlacementHost>(std::span<const size_t>{});state.placementHost->modules=scene.modules;
        for(const auto &part:scene.input.parts)state.placementHost->requiredArea+=area(part.polygon)*part.quantity;
        state.catalog.swap(scene.catalog);state.output=std::move(board.output);state.placementHost->nesting=std::move(board.nesting);transferConcrete(scene,state.placementHost->lifecycle);
        for(const auto &p:state.placementHost->nesting.placements){state.placementHost->lifecycle.instances[size_t(p->instance18)].owner08=&state.placementHost->nesting;p->group08=reinterpret_cast<uintptr_t>(&state.placementHost->nesting);state.placementHost->informerArea+=p->area58;}
        state.placementHost->queueAggregate20=state.placementHost->informerArea;
    }
    ~MappedBoardContinuation(){
        transferConcrete(scene,state.placementHost->lifecycle);state.catalog.swap(scene.catalog);board.nesting=std::move(state.placementHost->nesting);board.output=std::move(state.output);
        for(const auto &p:board.nesting.placements){scene.instances[size_t(p->instance18)].owner08=&board.nesting;p->group08=reinterpret_cast<uintptr_t>(&board.nesting);}
    }
};
void updateUnplaced(OrdinaryMultiPredefinedScene &scene){scene.unplaced.clear();if(scene.catalog)for(size_t n=0;n<scene.sdkIdentities.size();++n)if(!scene.catalog->instances[n].placed)scene.unplaced.push_back(scene.sdkIdentities[n]);}
}
std::unique_ptr<OrdinaryMultiPredefinedScene> ordinaryMultiNestMoreMapped(
    const OrdinaryMultiPredefinedScene &original,const Input &joined,
    std::span<const OrdinaryMultiAddedInstance> retained,std::span<const OrdinaryMultiAddedInstance> added,
    std::span<const OrdinaryMultiPredefinedSheet> stocks,const ReverseRunnerFactory &factory,
    Budget &budget,OrdinaryMultiContinuationTrace *record){
    require(bool(factory),PN2S_INVALID,"Multi continuation requires its actual native strategy");
    require(joined.gap==0&&!requires_general(joined)&&joined.pointGroups.empty(),PN2S_UNSUPPORTED,"Mapped multi continuation requires finite grid-aligned single rings without spacing or point groups");
    std::vector<std::vector<uintptr_t>> oldPlaced;std::set<uintptr_t> oldIds;for(const auto &board:original.boards){oldPlaced.emplace_back();for(const auto &p:board->nesting.placements){const auto id=original.sdkIdentities.at(size_t(p->instance18));require(oldIds.insert(id).second,PN2S_UNSUPPORTED,"Repeated old placed SDK IDs are unsupported");oldPlaced.back().push_back(id);}}
    std::vector<uintptr_t> additions;for(const auto &mapping:added)additions.push_back(mapping.sdkIdentity);
    const auto demand=recovered::multiContinuationDemand(additions,oldPlaced);require(demand.repeated==0,PN2S_UNSUPPORTED,"Repeated retained or added SDK IDs are unsupported");
    std::map<uintptr_t,OrdinaryMultiAddedInstance> mappingBySdk;std::set<std::pair<size_t,size_t>> concrete;std::set<uintptr_t> retainedIds;
    const auto bindMapping=[&](const OrdinaryMultiAddedInstance &mapping){require(mapping.sdkIdentity!=0&&mapping.group<joined.parts.size()&&joined.parts[mapping.group].quantity>=0&&mapping.ordinal<size_t(joined.parts[mapping.group].quantity),PN2S_INVALID,"Invalid mapped SDK instance");require(mappingBySdk.emplace(mapping.sdkIdentity,mapping).second&&concrete.emplace(mapping.group,mapping.ordinal).second,PN2S_UNSUPPORTED,"Repeated mapped SDK or native concrete identity");};
    for(const auto &mapping:retained){require(oldIds.contains(mapping.sdkIdentity),PN2S_INVALID,"Only actual previous placed demand may be retained");retainedIds.insert(mapping.sdkIdentity);bindMapping(mapping);const auto oldIndex=original.instanceBySdk.at(mapping.sdkIdentity),oldGroup=original.catalog->instances.at(oldIndex).group;require(unchangedGeometry(original.input.parts.at(oldGroup),joined.parts[mapping.group]),PN2S_UNSUPPORTED,"Retained SDK material must preserve its original geometry and poses");}
    require(retainedIds==oldIds,PN2S_INVALID,"Every actual previous placed SDK ID requires one new group and ordinal mapping");for(const auto &mapping:added)bindMapping(mapping);
    size_t total=0;for(const auto &part:joined.parts){require(part.quantity>=0&&!part.poses.empty(),PN2S_INVALID,"Invalid joined global demand");total+=size_t(part.quantity);}require(total==mappingBySdk.size()&&total==demand.identities.size(),PN2S_INVALID,"Joined quantities must exactly match current additions and previous placed demand");
    for(const auto &stock:stocks)require(stock.sheetIdentity!=0&&std::isfinite(stock.width)&&std::isfinite(stock.height)&&stock.width>0&&stock.height>0&&stock.predefined.records.empty(),PN2S_INVALID,"New continuation stock must be a valid empty physical board");
    budget.check(true);OrdinaryMultiContinuationTrace local;auto &trace=record?*record:local;trace={};auto scene=std::make_unique<OrdinaryMultiPredefinedScene>();scene->input=joined;
    if(total==0){scene->pitchX=original.pitchX;for(const auto &source:original.boards){auto board=std::make_unique<OrdinaryMultiPredefinedBoard>(source->source);board->output.pitchX=scene->pitchX;scene->boards.push_back(std::move(board));++trace.originalBoards;}return scene;}
    for(const auto &board:original.boards){scene->input.width=std::max(scene->input.width,board->source.width);scene->input.height=std::max(scene->input.height,board->source.height);}for(const auto &stock:stocks){scene->input.width=std::max(scene->input.width,stock.width);scene->input.height=std::max(scene->input.height,stock.height);}
    require(total>0&&scene->input.width>0&&scene->input.height>0,PN2S_INVALID,"Multi continuation requires concrete demand and usable stock");
    OrdinaryState prepared;OrdinaryControl initialize;initialize.placementLimit=0;for(const auto &part:scene->input.parts)initialize.values.push_back(area(part.polygon));ordinaryRun(scene->input,budget,prepared,&initialize);++trace.globalCatalogBuilds;
    scene->catalog=std::move(prepared.catalog);scene->modules=std::move(prepared.placementHost->modules);scene->placementCatalog=std::move(prepared.placementHost->lifecycle.catalog);scene->instances=std::move(prepared.placementHost->lifecycle.instances);scene->pitchX=prepared.output.pitchX;scene->sdkIdentities.resize(total);
    for(const auto &[id,mapping]:mappingBySdk){const auto directory=scene->catalog->groups.at(mapping.group).at(0),instance=scene->catalog->records[directory].instances.at(mapping.ordinal);scene->sdkIdentities[instance]=id;scene->instanceBySdk.emplace(id,instance);}
    for(const auto &mapping:retained){const auto oldIndex=original.instanceBySdk.at(mapping.sdkIdentity),oldGroup=original.catalog->instances.at(oldIndex).group;for(size_t p=0;p<scene->modules[mapping.group].size();++p)scene->modules[mapping.group][p].geometryOwner=original.modules[oldGroup][p].geometryOwner;}
    for(const auto &source:original.boards){auto board=std::make_unique<OrdinaryMultiPredefinedBoard>(source->source);board->output.pitchX=scene->pitchX;
        for(size_t n=0;n<source->output.placements.size();++n){const auto id=original.sdkIdentities.at(size_t(source->nesting.placements[n]->instance18)),instance=scene->instanceBySdk.at(id);const auto &mapping=mappingBySdk.at(id);auto value=source->output.placements[n];value.group=mapping.group;value.ordinal=mapping.ordinal;
            require(source->nesting.placements[n]->edges.empty()&&!source->nesting.placements[n]->group58&&!source->nesting.placements[n]->groupFirst,PN2S_UNSUPPORTED,"Multi continuation process-relation cloning is outside the ordinary manual domain");
            recovered::PlacementCloneInput options;options.pitch=scene->pitchX;options.instance18=int32_t(instance);options.instance40=instance;
            transferConcrete(*scene,board->lifecycle);try{recovered::clonePlacement(*source->nesting.placements[n],options,[](uintptr_t)->std::optional<std::shared_ptr<recovered::ProcessPlacement>>{return {};},board->lifecycle.validateOnCommit,[&](const auto &p){require(board->lifecycle.commit(p),PN2S_INTERNAL,"Mapped original multi instance clone ownership");});}catch(...){transferConcrete(*scene,board->lifecycle);throw;}transferConcrete(*scene,board->lifecycle);
            scene->catalog->setPlaced(instance,true);board->output.placements.push_back(value);++trace.clonedOriginal;
        }
        scene->boards.push_back(std::move(board));++trace.originalBoards;
    }
    const auto fill=[&](OrdinaryMultiPredefinedBoard &board){budget.check();Input material=scene->input;material.width=board.source.width;material.height=board.source.height;material.pitch=double(scene->pitchX)/1e6;const auto prefix=board.output;const auto runner=factory(material);require(bool(runner),PN2S_INVALID,"Missing concrete mapped multi continuation runner");++trace.boardAttempts;
        MappedBoardContinuation current(*scene,board);runner(material,budget,current.state);if(!validatePredefinedContinuation(material,prefix,current.state.output).valid){++trace.rejectedGeometry;ordinaryRestore(current.state,prefix.placements.size());}trace.added+=current.state.output.placements.size()-prefix.placements.size();
    };
    updateUnplaced(*scene);for(auto &board:scene->boards){if(scene->unplaced.empty())break;fill(*board);updateUnplaced(*scene);}for(const auto &stock:stocks){if(scene->unplaced.empty())break;auto board=std::make_unique<OrdinaryMultiPredefinedBoard>(stock);board->output.pitchX=scene->pitchX;fill(*board);if(!board->output.placements.empty()){scene->boards.push_back(std::move(board));++trace.newBoards;}updateUnplaced(*scene);}
    updateUnplaced(*scene);return scene;
}
}
