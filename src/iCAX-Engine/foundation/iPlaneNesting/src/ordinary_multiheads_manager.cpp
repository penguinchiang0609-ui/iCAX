#include "ordinary_multiheads_manager.hpp"
#include "ordinary_runtime.hpp"
namespace study {
namespace {
class ManagerGeometry final:public recovered::FinalizeHost {
    const Input &input;Budget &budget;const MultiheadsManagerProgram &program;MultiheadsManagerTrace &trace;
    std::vector<recovered::MultiheadsChild> children;MultiheadsGeometryResult current,best,output;
    std::optional<recovered::FinalizeKey> bestKey;size_t childIndex=0;
public:
    ManagerGeometry(const Input &source,Budget &time,const MultiheadsManagerProgram &settings,MultiheadsManagerTrace &record)
        :input(source),budget(time),program(settings),trace(record),children(recovered::multiheadsChildren(settings.hardware,settings.admission,settings.trial,settings.selection458,settings.reporting488)){}
    size_t childCount()const override{return children.size();}
    double remainingTime()override{return std::chrono::duration<double>(budget.deadline-std::chrono::steady_clock::now()).count();}
    bool stopRequested()override{return false;}
    void checkpoint()override{}
    int runChild(size_t n)override{budget.check(true);childIndex=n;const auto &child=children[n];trace.children.push_back(child.kind);
        if(child.kind==recovered::MultiheadsChildKind::Division){auto settings=program.division;settings.child=child;current=ordinaryMultiheadsDivisionResult(input,budget,settings,&trace.division);}
        else{auto settings=program.clustering;settings.child=child;settings.trial=program.trial;current=ordinaryMultiheadsClusteringResult(input,budget,settings,&trace.clustering);}
        trace.placed.push_back(current.output.placements.size());if(children.size()==1)trace.selected=n;return int(current.output.placements.size());}
    recovered::FinalizeKey evaluate(int32_t source)override{require(source==-1234,PN2S_INTERNAL,"Unexpected Multiheads Finalize source");recovered::FinalizeKey key;size_t required=0;for(const auto &part:input.parts)required+=size_t(part.quantity);key.complete=current.output.placements.size()==required;
        for(const auto &p:current.output.placements){const auto &part=input.parts[p.group];const auto box=bounds(transform(part.polygon,part.pivot,part.poses[p.poseIndex]));key.correctedLength=std::max(key.correctedLength,p.x+rounded(box.x1*1e6));key.value+=area(part.polygon);}
        trace.keys.push_back(key);if(!bestKey||recovered::finalizeLess(key,*bestKey)){bestKey=key;best=current;trace.selected=childIndex;}return key;}
    std::span<const Placement> placements()const override{return current.output.placements;}
    void restore()override{current={};}
    void commit(const Placement &p)override{const auto n=output.output.placements.size();const auto &original=best.output.placements.at(n);require(p.group==original.group&&p.ordinal==original.ordinal&&p.poseIndex==original.poseIndex&&p.x==original.x&&p.y==original.y,PN2S_INTERNAL,"Multiheads Finalize owner order differs from its geometry snapshot");output.output.pitchX=best.output.pitchX;output.output.placements.push_back(p);output.placements.push_back(best.placements.at(n));}
    MultiheadsGeometryResult result(){return children.size()==1?std::move(current):std::move(output);}
};
}
MultiheadsGeometryResult ordinaryMultiheadsManagerResult(const Input &input,Budget &budget,const MultiheadsManagerProgram &program,MultiheadsManagerTrace *trace){
    require(program.hardware.heads48>=2,PN2S_UNSUPPORTED,"Multiheads manager requires configured hardware with at least two heads");
    require((!program.admission.hasPlacements&&!program.admission.hasPreNestPlacements)||bool(program.clustering.warm),PN2S_UNSUPPORTED,"Multiheads warm admission requires complete placement owners");
    MultiheadsManagerTrace local;auto &record=trace?*trace:local;record={};ManagerGeometry host(input,budget,program,record);recovered::runFinalize(host);auto result=host.result();
    require(valid_layout(input,result.output),PN2S_INTERNAL,"Multiheads manager result failed strict geometry validation");multiheadsShakerConstraints(result.placements);return result;
}
MultiheadsManagerProgram multiheadsProgram(const Input &input,const recovered::MultiheadsHardware &hardware,int32_t trial,
    const recovered::Random *initialRandom,MultiheadsGeometryChild divisionFactories){
    MultiheadsManagerProgram program;program.hardware=hardware;program.trial=trial;const auto random=initialRandom?*initialRandom:recovered::Random(input.randomSeed);
    program.division.random=random;program.clustering.random=random;
    if(divisionFactories)program.division.geometry=std::move(divisionFactories);
    else program.division.geometry=[](const Input &source,const Output &warm,int32_t method,Budget &time){require(method==1,PN2S_UNSUPPORTED,"Multiheads host has no Rectangle/Pattern division factory");
        OrdinaryState state;OrdinaryControl initialize;initialize.placementLimit=0;for(const auto &part:source.parts)initialize.values.push_back(area(part.polygon));ordinaryRun(source,time,state,&initialize);for(const auto &p:warm.placements)ordinaryCommit(state,p);
        recovered::OffsetFactoryInput settings;settings.count=1;ordinaryOffsetContinue(source,offsetProgram(source,settings),time,state);return std::move(state.output);};
    return program;
}
int ordinaryMultiheadsContinue(const Input &input,const MultiheadsManagerProgram &program,Budget &budget,OrdinaryState &state,MultiheadsManagerTrace *trace){
    if(!state.placementHost){OrdinaryControl initialize;initialize.placementLimit=0;for(const auto &part:input.parts)initialize.values.push_back(area(part.polygon));ordinaryRun(input,budget,state,&initialize);}
    const auto baseline=state.output.placements.size();require(state.placementHost&&state.placementHost->nesting.placements.size()==baseline,PN2S_INTERNAL,"Multiheads ordinary output and owner catalog differ");
    auto settings=program;Input effective=input;if(state.output.pitchX>0)effective.pitch=double(state.output.pitchX)/1e6;
    if(baseline){auto warm=std::make_shared<MultiheadsGeometryResult>();warm->output=state.output;warm->placements=state.placementHost->nesting.placements;multiheadsShakerConstraints(warm->placements);settings.clustering.warm=std::move(warm);settings.admission.hasPlacements=true;}
    const auto result=ordinaryMultiheadsManagerResult(effective,budget,settings,trace);require(result.output.placements.size()>=baseline,PN2S_INTERNAL,"Multiheads removed a warm placement");
    for(size_t n=0;n<baseline;++n){const auto &a=state.output.placements[n],&b=result.output.placements[n];require(a.group==b.group&&a.ordinal==b.ordinal&&a.poseIndex==b.poseIndex&&a.x==b.x&&a.y==b.y,PN2S_INTERNAL,"Multiheads moved an existing warm placement");}
    try{for(size_t n=baseline;n<result.output.placements.size();++n)ordinaryCommit(state,result.output.placements[n]);
        std::map<uintptr_t,std::shared_ptr<recovered::ProcessPlacement>> owners;for(const auto &owner:state.placementHost->nesting.placements)owners[owner->instance40]=owner;
        const recovered::PlacementCopyLookup lookup=[&](uintptr_t id)->std::optional<std::shared_ptr<recovered::ProcessPlacement>>{if(const auto found=owners.find(id);found!=owners.end())return found->second;return {};};
        for(size_t n=baseline;n<result.placements.size();++n){const auto &source=result.placements[n];const auto &destination=state.placementHost->nesting.placements[n];recovered::copyPlacementRelations(*source,destination,lookup);recovered::copyPlacementProcesses(*source,destination,lookup);}
        multiheadsShakerConstraints(state.placementHost->nesting.placements);
    }catch(...){ordinaryRestore(state,baseline);throw;}
    return int(result.output.placements.size()-baseline);
}
} // namespace study
