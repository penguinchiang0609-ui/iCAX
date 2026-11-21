#include "recovered_preset.hpp"
#include "recovered_worker.hpp"
#include "recovered_pool.hpp"
#include "recovered_history.hpp"
#include "recovered_warm.hpp"
#include "recovered_extremal.hpp"
#include "recovered_placement.hpp"
#include "ordinary_runtime.hpp"
#include <iostream>
#include <bit>
using namespace study;using namespace study::recovered;
namespace {
size_t checks=0;
void check(bool v,const char *why){++checks;if(!v)throw std::runtime_error(why);}
struct Worker final:NesterWorkerHost {
    Random random{17};ScheduleState schedule{0,3,1,1.};
    CandidatePool<std::shared_ptr<CandidateMetrics>> pool{1,[](const auto &a,const auto &b){return candidateLess(*a,*b,[]{return 100;});}};
    std::vector<std::shared_ptr<CandidateMetrics>> candidates;
    std::vector<double> seconds;size_t calls=0;bool fast=false,inspect=false;
    bool stopped()override{return false;}
    bool fastEnded()override{return fast;}
    std::optional<int> nextManager()override {
        const std::array<double,1> weights{1.};auto index=nextStrategy(random,schedule,weights);
        if(index && pool.limit()!=uint32_t(schedule.poolLimit))pool.rebuild(uint32_t(schedule.poolLimit));
        return index?std::optional<int>(8):std::nullopt;
    }
    void construct(int id)override{check(id==8,"worker dispatched wrong manager");pool.insert(candidates.at(calls++));}
    double remaining()override{return seconds.at(calls-1);}
    bool inspectFastEnd()override{return inspect;}
    std::optional<FastEndInput> bestCandidate()override {
        if(!pool.best())return {};
        FastEndInput input;input.maximumRight=(*pool.best())->correctedLength;input.referenceLength=10.;input.placedArea=100.;return input;
    }
    void markFastEnd()override{fast=true;}
};
struct TrialLayout:TrialNesting {int64_t length=0;};
struct ReplayIntegration final:WarmStartHost {
    WarmNesting nesting;int starts=0,ends=0;
    const std::array<size_t,3> types{0,0,0};
    PlacementLifecycle lifecycle{nesting,types};
    std::map<uintptr_t,WarmPlacementPtr> mapping;
    WarmNesting &current()override{return nesting;}
    bool isColumnNesting()override{return false;}
    void resetCurrent()override{lifecycle.removeTrailing();mapping.clear();nesting.quantity28=0;}
    void removeTrailingPlacements()override{lifecycle.removeTrailing();mapping.clear();}
    void notifyStartPredefined()override{++starts;}
    void notifyEndPredefined()override{++ends;}
    void resetSelectionQueue()override{}
    bool instanceAvailable(int32_t id)override{return lifecycle.catalog.states.at(size_t(id))==0;}
    bool acceptsPlacement(const WarmPlacement &)override{return true;} // Explicit geometry constraint fixture.
    std::optional<std::shared_ptr<ProcessPlacement>> lookup(uintptr_t id) {
        auto it=mapping.find(id);if(it==mapping.end())return {};return it->second;
    }
    WarmPlacementPtr cloneAndInsert(const WarmPlacement &source)override {
        PlacementCloneInput v;v.pitch=1000000;v.instance18=source.instance18;v.instance40=source.instance40+1000;
        auto p=clonePlacement(source,v,[&](uintptr_t id){return lookup(id);},lifecycle.validateOnCommit,[&](const auto &copy){
            check(!lifecycle.validateOnCommit,"replay clone did not disable validation at commit");copy->group08=2;check(lifecycle.commit(copy),"replay lifecycle commit rejected a free instance");
        });
        mapping[source.instance40]=p;return p;
    }
    double copyProcesses(const WarmPlacement &source)override {
        return copyPlacementProcesses(source,mapping.at(source.instance40),[&](uintptr_t id){return lookup(id);});
    }
    void pruneAuxiliaryGroups()override{}
    double debugColorConfig750()override{return 0;}
    void colorPlacements(const char*)override{}
};
struct ControlledConstruction final:ConstructionHost {
    TrialManagerState managerState{0,30.,0,0};
    HistoryControl history;
    Random random{5};
    std::shared_ptr<TrialNesting> layout=std::make_shared<TrialLayout>();
    CandidatePool<TrialRecordPtr> local{3,[](const auto &a,const auto &b){return a->rawLength<b->rawLength;}};
    CandidatePool<TrialRecordPtr> global{3,[](const auto &a,const auto &b){return a->rawLength<b->rawLength;}};
    int managerLock=0,supervisorLock=0,callbacks=0,runs=0,updates=0;
    int64_t ticks=0;
    ControlledConstruction() {
        history.targetManager29d8=1;history.budget1b8=3.;history.statistics.estimates={2.,1.};history.statistics.attempts={1,1};history.statistics.samples.resize(2);history.statistics.elite=1;history.statistics.best=1e9;
        history.elements={std::make_shared<StructuringElement>(),std::make_shared<StructuringElement>()};history.fixedElement=history.elements[0];
    }
    TrialManagerState &manager()override{return managerState;}
    std::shared_ptr<TrialNesting> &current()override{return layout;}
    void lockManager()override{check(managerLock++==0,"recursive construction manager lock");}
    void unlockManager()override{check(--managerLock==0,"unbalanced construction manager lock");}
    void lockSupervisor()override{check(supervisorLock++==0,"recursive construction supervisor lock");}
    void unlockSupervisor()override{check(--supervisorLock==0,"unbalanced construction supervisor lock");}
    int32_t managerId()override{return 1;}
    double config4e0()override{return 0.;}
    void preprocess()override{throw std::runtime_error("unexpected preprocessing");}
    void finalizeGeometry()override{}
    int64_t rawLength(const TrialNesting &n)override{return dynamic_cast<const TrialLayout&>(n).length;}
    void evaluate(const TrialRecordPtr &r)override{r->corrected.valid=true;r->corrected.value=r->rawLength;}
    CandidatePool<TrialRecordPtr>::Inserted notifyLocal(const TrialRecordPtr &r)override{return local.insert(r);}
    CandidatePool<TrialRecordPtr>::Inserted notifyGlobal(const TrialRecordPtr &r)override{return global.insert(r);}
    void publishBest(const TrialRecordPtr &)override{++callbacks;}
    bool hasBestCallback()override{return true;}
    int64_t clockTicks()override{return ticks+=1000000000;}
    double preciseSeconds()override{return double(ticks)/1e9;}
    void resetCurrent()override{dynamic_cast<TrialLayout&>(*layout).length=0;}
    std::optional<Best> bestGoal()override{if(global.best())return Best{1.,10.,(*global.best())->rawLength};return {};}
    void resetRandom(int32_t salt)override{resetTrialRandom(random,5.,uint32_t(salt));layout->seed13b8=uint32_t(salt);}
    int phase()override{return 0;}
    void prepareWarmStart(double,bool)override{throw std::runtime_error("phase zero used warm start");}
    Worst worstCandidate()override{return {};}
    double evaluatorFactor()override{return 0.;}
    void runPreNester()override{}
    void prepareModules()override{}
    bool useHistory()override{return true;}
    uint32_t prepareHistory()override{const auto p=recovered::prepareHistory(history,*layout,2.,managerState.count60);return p.choice?uint32_t(p.choice->index):0;}
    void createStrategy()override{}
    void attachStop()override{}
    void runStrategy()override{++runs;dynamic_cast<TrialLayout&>(*layout).length=(100-runs)*1000000;}
    void finalizeStrategy()override{}
    void completeHistory(uint32_t index)override{HistoryCompletionInput in;in.target14b8=in.queue20=in.placedArea=100.;in.maximumRight=rawLength(*layout);in.managerId=1;in.index=int32_t(index);updates+=recovered::completeHistory(history,*layout,in).updated?1:0;}
    bool mayShake()override{return false;}
    bool shake()override{throw std::runtime_error("unexpected shake");}
    void logTrial(const TrialRecordPtr &,double)override{}
    void releaseCurrentResources()override{}
    std::shared_ptr<TrialNesting> acquireNesting()override{return std::make_shared<TrialLayout>();}
};
}
int main(){try {
    Preset preset;std::vector<int> visited;
    decimalPreset(preset,20301,[&](int id)->std::optional<StrategyEntry>{visited.push_back(id);if(id==3)return {};return StrategyEntry{id,strategyCalibration(id,11,100.),0,0};});
    check(visited==std::vector<int>({1,3,5}) && preset.entries.size()==2 && preset.totalWeight==3.,"preset zero digit/filter behavior");
    addPreset(preset,{1,11,4,7});check(preset.entries.at(1).weight==4 && preset.totalWeight==7.,"preset stored total must survive replacement");
    MapEligibilityState cache{3,1};int estimates=0;std::array<EligibilityPart,1> parts{};
    MapEligibilityInput eligibility{parts,10,0,1000000,1,[&]{++estimates;return .5;}};
    check(mapEligible(cache,eligibility)&&cache.status==2&&estimates==1,"map cache initial evaluation");eligibility.estimateCost=[&]{++estimates;return 999.;};
    check(mapEligible(cache,eligibility)&&estimates==1,"map cache must not recompute");
    std::array<int,3> ranks{10,20,15};CandidatePool<int> pool(4,[&](int a,int b){return ranks[a]<ranks[b];});pool.insert(0);pool.insert(1);
    ranks={30,10,20};const auto rejected=pool.notify(2,[](int,int){return true;});
    check(!rejected.inserted && pool.values()==std::vector<int>({0,1}),"related rejection must preserve all old records");
    pool.rebuild(1);check(pool.values()==std::vector<int>({1}),"rebuild must reevaluate ordering before trimming");
    bool badCapacity=false;try{pool.rebuild(0);}catch(const std::invalid_argument&){badCapacity=true;}
    check(badCapacity&&pool.size()==1,"invalid capacity must not destroy pool");
    std::weak_ptr<int> lifetime;
    {CandidatePool<std::shared_ptr<int>> owned(1,[](const auto &a,const auto &b){return *a<*b;});auto p=std::make_shared<int>(5);lifetime=p;owned.insert(p);p.reset();auto queue=owned.values();owned.insert(std::make_shared<int>(1));check(!lifetime.expired(),"shaker snapshot must retain evicted result");queue.clear();check(lifetime.expired(),"evicted result leaked after snapshot release");}
    auto a=std::make_shared<ProcessPlacement>(),b=std::make_shared<ProcessPlacement>();a->group08=b->group08=1;a->geometry60=10.;b->geometry60=20.;
    a->edges.push_back({a,b,2.});b->edges.push_back({a,b,2.});b->groupFirst=a;
    check(candidateProcessQuantities({a,b})==std::array<double,3>{8.,20.,2.},"shared process must be charged only to its owner");
    auto edge=a->edges.front();b.reset();check(!processEdgeActive(edge)&&candidateAuxiliary(*a)==0.,"expired process endpoint must not contribute");
    LayoutSignature la;la.sheetIdentity=1;la.height10=1000.;la.maximumRight=1000000;la.typeCount=1;la.typeOrder={0};la.placementCount=2;la.placements={{0,0,0},{0,1000000,0}};
    auto lb=la;std::reverse(lb.placements.begin(),lb.placements.end());check(layoutDifference(la,lb)==0,"placement input order must not create a new layout");
    lb.sheetIdentity=2;check(layoutDifference(la,lb)==100,"different stock identity must not collapse");
    size_t cursor=0;int stopCalls=0;auto stop=[&]{++stopCalls;return false;};const double nan=std::numeric_limits<double>::quiet_NaN();
    auto task=nextShakerTask(cursor,2,0,0,nan,1,stop);check(task.index==0&&task.seconds==1.&&stopCalls==1,"minsd NaN first operand behavior");
    task=nextShakerTask(cursor,2,0,0,1,nan,stop);check(!task.index&&std::isnan(task.seconds)&&cursor==1&&stopCalls==1,"NaN cap must not consume task/call stop");
    task=nextShakerTask(cursor,2,0,0,0,-0.,stop);check(!task.index&&std::signbit(task.seconds),"minsd equal operands preserve second signed zero");
    Worker worker;for(int k=0;k<3;++k){auto c=std::make_shared<CandidateMetrics>();c->metric=k+1;c->correctedLength=1000000;c->sourceId=8;c->trial=k;worker.candidates.push_back(c);}worker.seconds={0.,-1.,100.};
    auto result=runNesterWorker(worker);check(result.reason==WorkerEnd::TimeExpired&&result.attempts==2&&worker.calls==2&&(*worker.pool.best())->metric==2.,"worker must construct at zero time and preserve best before time expiry");
    Worker early;early.candidates=worker.candidates;early.seconds={1.,1.,1.};early.inspect=true;result=runNesterWorker(early);
    check(result.reason==WorkerEnd::FastEnd&&result.attempts==1&&early.fast,"worker fast-end must stop before next strategy");
    ControlledConstruction chain;std::vector<TrialRecordPtr> snapshots;
    std::weak_ptr<TrialNesting> oldLayout;std::weak_ptr<TrialRecord> oldRecord;
    for(int i=0;i<16;++i) {
        const auto previous=chain.current();const auto constructed=runConstruction(chain);snapshots.push_back(constructed.record);
        check(constructed.replacedNesting&&constructed.localInserted&&constructed.globalInserted&&constructed.newBest,"construction chain did not retain/replace accepted snapshot");
        check(constructed.record->nesting==previous&&chain.current()!=previous,"construction mutated the accepted snapshot");
        check(constructed.record->timings.size()==1&&constructed.record->timings.at({1,0})==0.,"construction absent shake timing differs");
        if(i==0){oldLayout=previous;oldRecord=constructed.record;}
    }
    check(chain.callbacks==16&&chain.runs==16&&chain.updates==6&&chain.history.statistics.updates==6,"construction/history activation or result feedback differs");
    check(chain.managerState.count60==16&&chain.history.dynamic29dc&&!chain.managerLock&&!chain.supervisorLock,"construction chain state/lock balance differs");
    for(size_t i=0;i<snapshots.size();++i)check(snapshots[i]->rawLength==int64_t(99-i)*1000000&&snapshots[i]->nesting->element14c0,"later trials changed an earlier layout or released its history element");
    check(!oldLayout.expired()&&!oldRecord.expired(),"evicted construction snapshot did not survive consumer retention");
    snapshots.clear();check(oldLayout.expired()&&oldRecord.expired(),"evicted construction snapshot leaked after consumer release");
    TrialRecord timing;timing.timings[{9,3}]=4.;recordTiming(timing,{0,2},5.);
    check(timing.timings.size()==2&&timing.timings.at({1,0})==5.&&timing.timings.at({0,2})==5.,"timing replacement retained stale stages");
    const double negativeNan=std::bit_cast<double>(uint64_t{0xfff8000000000011});
    auto cdf=historyDistribution(std::array<double,3>{negativeNan,nan,0.});
    check(std::bit_cast<uint64_t>(cdf.cumulative.back())==0xfff8000000000011ull,"CDF did not retain the first NaN payload/sign");
    check(historyOrder(std::array<double,4>{1.,nan,3.,2.})==std::vector<int32_t>{3,1,0,2},"history insertion front shortcut differs for unordered scores");
    Rows mapRows(130,Row{{0,100}});mapRows[125]={{0,10},{20,100}};
    ExtremalInput scan;scan.first=62;scan.end=130;scan.maxSteps=3;scan.supplementEnd=129;scan.count=2;
    for(bool lower:{false,true})for(bool fromLow:{false,true}) {
        scan.lower=lower;scan.fromLow=fromLow;auto points=extremalPoints(mapRows,scan);
        check(points.back()==StripPoint{125,lower?0:100},"supplement must scan forward across chunk boundary in every direction");
    }
    mapRows[63]={{0,10},{20,100}};scan.fromLow=true;
    auto points=extremalPoints(mapRows,scan);
    check(std::none_of(points.begin(),points.end(),[](auto p){return p[0]==125;}),"early multi-interval row must terminate supplement search");
    scan.first=0;scan.end=3;scan.maxSteps=3;scan.supplementEnd=2;scan.lower=true;scan.count=1;
    check(extremalPoints(Rows{Row{{0,0}},Row{{INT32_MAX,INT32_MAX}},Row{{0,0}}},scan)==std::vector<StripPoint>{{2,0}},"rank curvature must wrap at 32 bits");
    FirstPlacementInput first;first.width=10;first.height=10;first.pitch=3000000;first.originX=1000000;first.partBounds={0,0,8,10};
    check(firstPlacement(first)==std::optional<StripPoint>{{0,0}},"first placement must round negative relative origin toward next grid point");
    first.partBounds.x1=10;check(!firstPlacement(first),"fallback must reduce usable width by grid alignment loss");
    // Actual geometry host: fallback is per module and only before any piece
    // has been placed. Disabling scans must never fabricate later placements.
    Input exact;exact.width=10;exact.height=10;exact.pitch=1;exact.recoveredOffset=true;
    PartInput exactPart;exactPart.polygon={{0,0},{10,0},{10,10},{0,10}};exactPart.quantity=2;exactPart.poses={{0,false}};exact.parts={exactPart};
    OrdinaryControl noScan;noScan.values={100};noScan.directions={false,false,false,false};OrdinaryState exactState;Budget exactBudget(10);
    check(ordinaryRun(exact,exactBudget,exactState,&noScan)==1 && exactState.output.placements.size()==1,"geometry host did not apply first-piece fallback exactly once");
    check(exactState.output.placements[0].x==0 && exactState.output.placements[0].y==0,"first-piece fallback lost physical coordinates");
    ReplayIntegration replay;replay.nesting.totalValue78=30;
    auto source=std::make_shared<WarmNesting>();source->quantity18=30;source->shake14e8=1;
    auto sourceGroup=std::make_shared<PlacementGroup>();
    for(int i=0;i<3;++i){auto p=std::make_shared<WarmPlacement>();p->instance18=i;p->instance40=uintptr_t(i+1);p->group08=1;p->area58=10;p->geometry60=14;p->x48=i*6000000;p->rightF0=5000000;p->topF8=2000;p->attributes68["name"]=std::to_string(i);p->geometryOwner=std::make_shared<const Polygon>(Polygon{{0,0},{5,0},{5,2},{0,2}});p->group58=sourceGroup;sourceGroup->members.push_back(p);source->placements.push_back(p);}
    linkCommonCut(source->placements[0],source->placements[1],2.5);linkCommonCut(source->placements[1],source->placements[2],4.5);
    auto replayRecord=std::make_shared<TrialRecord>();replayRecord->nesting=source;
    auto applied=applyWarmStart(replay,replayRecord,1.,false);
    check(applied.copied==3 && replay.nesting.placedCount13d8==3 && replay.starts==1 && replay.ends==1 && replay.lifecycle.validateOnCommit,"replay clone/process integration did not complete");
    check(replay.nesting.quantity28==7. && candidateAuxiliary(*replay.nesting.placements[0])==2.5 && candidateAuxiliary(*replay.nesting.placements[1])==4.5,"replay reconstructed incorrect common-cut ownership");
    const auto copiedGroup=replay.nesting.placements[0]->group58;
    check(copiedGroup && copiedGroup!=sourceGroup && copiedGroup->members.size()==3,"replay group aliases source group");
    for(size_t i=0;i<3;++i)check(copiedGroup->members[i].lock()==replay.nesting.placements[i] && replay.nesting.placements[i]->geometryOwner==source->placements[i]->geometryOwner,"replay lost group position or immutable geometry ownership");
    auto retained=replay.nesting.placements[1];std::weak_ptr<ProcessPlacement> released=replay.nesting.placements[0];replay.resetCurrent();
    check(released.expired() && candidateAuxiliary(*retained)==0.,"replay edges/groups retained removed placements or charged expired common cuts");
    check(source->placements[0]->group58==sourceGroup && candidateAuxiliary(*source->placements[0])==2.5,"replay rollback changed source snapshot");
    bool cloneThrew=false;try{PlacementCloneInput v;clonePlacement(*source->placements[0],v,[](uintptr_t)->std::optional<std::shared_ptr<ProcessPlacement>>{return {};},replay.lifecycle.validateOnCommit,[](const auto&){throw std::runtime_error("commit failed");});}catch(const std::runtime_error&){cloneThrew=true;}
    check(cloneThrew && replay.lifecycle.validateOnCommit,"clone exception left validation disabled");
    Rows constrainedRows{Row{{0,20},{30,40}},Row{{5,10},{35,45}}};Row allowed{{7,9},{38,50}};
    EvaluatedStripMask fixedMask;fixedMask.first=0;fixedMask.end=2;fixedMask.accepted={3};fixedMask.keepRow={0};
    EvaluatedMapConstraints limits{&fixedMask,&allowed};
    check(constrainedEndpoint(constrainedRows,0,true,limits)==7 && constrainedEndpoint(constrainedRows,0,false,limits)==40,"constraint masks lost neighbor-row or vertical intersection");
    allowed={{100,101}};check(constrainedEndpoint(constrainedRows,0,true,limits)==-1000,"filtered endpoint lost its distinct no-intersection sentinel");
    StripConstraintCache lazy;lazy.pitch=3;lazy.first18=0;lazy.limit1c=10;lazy.mask.evaluatedUntil=0;lazy.batch24=1;
    auto interval=buildStripIntervals(lazy);buildStripMask(lazy.mask,interval.intervals,3,0,0);
    check(acceptsStrip(lazy,5) && lazy.mask.evaluatedUntil==6 && lazy.batch24==24,"lazy strip admission did not grow from the prior frontier");
    check((lazy.mask.keepRow[0]&1)==0,"strip mask interval start incorrectly kept the original row");
    VerticalConstraintCache moving;moving.baseIntervals={{0,10}};moving.yOffset=5;moving.pitch=3;
    auto movingFilter=std::make_shared<MovingVerticalConstraint>();movingFilter->filter=[](uintptr_t,Row &r,uintptr_t,const std::array<int64_t,2> &x){r={{x[0],x[1]}};};moving.constraints={movingFilter};
    EvaluatedMapConstraints movingLimits;movingLimits.movingVertical=&moving;
    check(constrainedEndpoint(Rows{Row{{0,20}},Row{{0,20}},Row{{0,20}}},2,true,movingLimits)==1,"moving vertical filter did not undo the Y translation");
    check(composeOrientation({0,0,0},0,true).angle==-36000000,"reflected zero orientation lost its sentinel");
    const std::array<PlacementOrientation,1> relative{{{10000,0,0}}};
    check(!relativeOrientationAllowed({},{},relative,true) && relativeOrientationAllowed({},{},relative,false),"relative orientation tolerance or detached-instance rule differs");
    std::cout<<"{\"pass\":true,\"checks\":"<<checks<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
