#include "ordinary_multi_continuation.hpp"
#include "ordinary_predefined_validation.hpp"
#include <set>

namespace study {
namespace {
bool sameGeometry(const PartInput &a,const PartInput &b){
    if(a.polygon.size()!=b.polygon.size()||a.pivot.x!=b.pivot.x||a.pivot.y!=b.pivot.y||a.poses.size()!=b.poses.size()||a.protection!=b.protection||a.holes.size()!=b.holes.size())return false;
    for(size_t n=0;n<a.polygon.size();++n)if(a.polygon[n].x!=b.polygon[n].x||a.polygon[n].y!=b.polygon[n].y)return false;
    for(size_t n=0;n<a.poses.size();++n)if(a.poses[n].angle!=b.poses[n].angle||a.poses[n].upper!=b.poses[n].upper||a.poses[n].flip!=b.poses[n].flip)return false;
    return a.holes.empty();
}
void swapShared(OrdinaryMultiPredefinedScene &scene,recovered::PlacementLifecycle &lifecycle){std::swap(scene.placementCatalog,lifecycle.catalog);std::swap(scene.instances,lifecycle.instances);}
void attachOwners(OrdinaryMultiPredefinedScene &scene,recovered::WarmNesting &nesting){
    for(const auto &p:nesting.placements){scene.instances[size_t(p->instance18)].owner08=&nesting;p->group08=reinterpret_cast<uintptr_t>(&nesting);}
}
// The ordinary strategy consumes an actual OrdinaryState. Move the current
// board into that host for its call while transferring, rather than copying,
// the one global catalog/state tables. Other boards keep their stable owners.
class BoardContinuation {
    OrdinaryMultiPredefinedScene &scene;
    OrdinaryMultiPredefinedBoard &board;
public:
    OrdinaryState state;
    BoardContinuation(OrdinaryMultiPredefinedScene &s,OrdinaryMultiPredefinedBoard &b):scene(s),board(b){
        state.placementHost=std::make_unique<OrdinaryState::PlacementHost>(std::span<const size_t>{});
        state.placementHost->modules=scene.modules;for(const auto &part:scene.input.parts)state.placementHost->requiredArea+=area(part.polygon)*part.quantity;
        state.catalog.swap(scene.catalog);state.output=std::move(board.output);state.placementHost->nesting=std::move(board.nesting);
        swapShared(scene,state.placementHost->lifecycle);
        for(const auto &p:state.placementHost->nesting.placements){auto &instance=state.placementHost->lifecycle.instances[size_t(p->instance18)];instance.owner08=&state.placementHost->nesting;p->group08=reinterpret_cast<uintptr_t>(&state.placementHost->nesting);state.placementHost->informerArea+=p->area58;}
        state.placementHost->queueAggregate20=state.placementHost->informerArea;
    }
    ~BoardContinuation(){
        swapShared(scene,state.placementHost->lifecycle);state.catalog.swap(scene.catalog);board.nesting=std::move(state.placementHost->nesting);board.output=std::move(state.output);attachOwners(scene,board.nesting);
    }
};
void refreshMissing(OrdinaryMultiPredefinedScene &scene){scene.unplaced.clear();if(scene.catalog)for(size_t n=0;n<scene.sdkIdentities.size();++n)if(!scene.catalog->instances[n].placed)scene.unplaced.push_back(scene.sdkIdentities[n]);}
}
std::unique_ptr<OrdinaryMultiPredefinedScene> ordinaryMultiNestMore(
    const OrdinaryMultiPredefinedScene &original,const Input &expanded,
    std::span<const OrdinaryMultiAddedInstance> added,std::span<const OrdinaryMultiPredefinedSheet> stocks,
    const ReverseRunnerFactory &factory,Budget &budget,OrdinaryMultiContinuationTrace *record){
    require(bool(factory),PN2S_INVALID,"Multi continuation requires its actual native strategy");
    require(original.unplaced.empty(),PN2S_UNSUPPORTED,"Multi continuation of a partially rejected manual input needs an explicit previous-demand policy");
    require(expanded.gap==0&&!requires_general(expanded)&&expanded.pointGroups.empty(),PN2S_UNSUPPORTED,"Multi continuation requires finite grid-aligned single rings without spacing or point groups");
    require(expanded.parts.size()>=original.input.parts.size(),PN2S_INVALID,"Continuation cannot remove original native groups");
    for(size_t n=0;n<original.input.parts.size();++n)require(sameGeometry(original.input.parts[n],expanded.parts[n])&&expanded.parts[n].quantity>=original.input.parts[n].quantity,PN2S_UNSUPPORTED,"Continuation must preserve original group geometry, poses and ordinals");
    std::set<uintptr_t> identities(original.sdkIdentities.begin(),original.sdkIdentities.end());std::set<std::pair<size_t,size_t>> concrete;
    if(original.catalog)for(const auto &part:original.catalog->instances)concrete.emplace(part.group,part.ordinal);
    for(const auto &part:added){require(part.sdkIdentity!=0&&part.group<expanded.parts.size()&&part.ordinal<size_t(expanded.parts[part.group].quantity),PN2S_INVALID,"Invalid added global SDK instance");require(identities.insert(part.sdkIdentity).second&&concrete.emplace(part.group,part.ordinal).second,PN2S_UNSUPPORTED,"Repeated old or new concrete SDK demand is unsupported");}
    size_t total=0;for(const auto &part:expanded.parts){require(part.quantity>=0&&!part.poses.empty(),PN2S_INVALID,"Invalid expanded global demand");total+=size_t(part.quantity);}require(total==concrete.size(),PN2S_INVALID,"Expanded quantities must match original and added concrete demand");
    for(const auto &stock:stocks)require(stock.sheetIdentity!=0&&std::isfinite(stock.width)&&std::isfinite(stock.height)&&stock.width>0&&stock.height>0&&stock.predefined.records.empty(),PN2S_INVALID,"New continuation stock must be a valid empty physical board");
    budget.check(true);OrdinaryMultiContinuationTrace local;auto &trace=record?*record:local;trace={};auto scene=std::make_unique<OrdinaryMultiPredefinedScene>();scene->input=expanded;
    for(const auto &board:original.boards){scene->input.width=std::max(scene->input.width,board->source.width);scene->input.height=std::max(scene->input.height,board->source.height);}for(const auto &stock:stocks){scene->input.width=std::max(scene->input.width,stock.width);scene->input.height=std::max(scene->input.height,stock.height);}
    require(total>0&&scene->input.width>0&&scene->input.height>0,PN2S_INVALID,"Multi continuation requires concrete demand and usable stock");
    OrdinaryState prepared;OrdinaryControl initialize;initialize.placementLimit=0;for(const auto &part:scene->input.parts)initialize.values.push_back(area(part.polygon));ordinaryRun(scene->input,budget,prepared,&initialize);++trace.globalCatalogBuilds;
    scene->catalog=std::move(prepared.catalog);scene->modules=std::move(prepared.placementHost->modules);scene->placementCatalog=std::move(prepared.placementHost->lifecycle.catalog);scene->instances=std::move(prepared.placementHost->lifecycle.instances);scene->pitchX=prepared.output.pitchX;scene->sdkIdentities.resize(total);
    const auto bind=[&](size_t group,size_t ordinal,uintptr_t id){const auto directory=scene->catalog->groups.at(group).at(0);const auto instance=scene->catalog->records[directory].instances.at(ordinal);scene->sdkIdentities[instance]=id;scene->instanceBySdk.emplace(id,instance);};
    if(original.catalog)for(size_t n=0;n<original.sdkIdentities.size();++n){const auto &part=original.catalog->instances[n];bind(part.group,part.ordinal,original.sdkIdentities[n]);}for(const auto &part:added)bind(part.group,part.ordinal,part.sdkIdentity);
    for(size_t g=0;g<original.modules.size();++g)for(size_t p=0;p<original.modules[g].size();++p)scene->modules[g][p].geometryOwner=original.modules[g][p].geometryOwner;
    for(const auto &source:original.boards){auto board=std::make_unique<OrdinaryMultiPredefinedBoard>(source->source);board->output.pitchX=scene->pitchX;
        for(size_t n=0;n<source->output.placements.size();++n){const auto &value=source->output.placements[n];const auto instance=scene->instanceBySdk.at(original.sdkIdentities[size_t(source->nesting.placements[n]->instance18)]);
            require(source->nesting.placements[n]->edges.empty()&&!source->nesting.placements[n]->group58&&!source->nesting.placements[n]->groupFirst,PN2S_UNSUPPORTED,"Multi continuation process-relation cloning is outside the ordinary manual domain");
            recovered::PlacementCloneInput options;options.pitch=scene->pitchX;options.instance18=int32_t(instance);options.instance40=instance;
            swapShared(*scene,board->lifecycle);try{recovered::clonePlacement(*source->nesting.placements[n],options,[](uintptr_t)->std::optional<std::shared_ptr<recovered::ProcessPlacement>>{return {};},board->lifecycle.validateOnCommit,[&](const auto &p){require(board->lifecycle.commit(p),PN2S_INTERNAL,"Original multi instance clone ownership");});}catch(...){swapShared(*scene,board->lifecycle);throw;}swapShared(*scene,board->lifecycle);
            scene->catalog->setPlaced(instance,true);board->output.placements.push_back(value);++trace.clonedOriginal;
        }
        scene->boards.push_back(std::move(board));++trace.originalBoards;
    }
    const auto fill=[&](OrdinaryMultiPredefinedBoard &board){budget.check();Input material=scene->input;material.width=board.source.width;material.height=board.source.height;material.pitch=double(scene->pitchX)/1e6;const auto originalOutput=board.output;const auto runner=factory(material);require(bool(runner),PN2S_INVALID,"Missing concrete multi continuation runner");++trace.boardAttempts;
        BoardContinuation current(*scene,board);runner(material,budget,current.state);
        if(!validatePredefinedContinuation(material,originalOutput,current.state.output).valid){++trace.rejectedGeometry;ordinaryRestore(current.state,originalOutput.placements.size());}
        trace.added+=current.state.output.placements.size()-originalOutput.placements.size();
    };
    refreshMissing(*scene);for(auto &board:scene->boards){if(scene->unplaced.empty())break;fill(*board);refreshMissing(*scene);}
    for(const auto &stock:stocks){if(scene->unplaced.empty())break;auto board=std::make_unique<OrdinaryMultiPredefinedBoard>(stock);board->output.pitchX=scene->pitchX;fill(*board);if(!board->output.placements.empty()){scene->boards.push_back(std::move(board));++trace.newBoards;}refreshMissing(*scene);}
    refreshMissing(*scene);return scene;
}
}
