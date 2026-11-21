#include "ordinary_runtime.hpp"
#include <bit>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <random>
using namespace study;
using namespace study::recovered;
namespace {
size_t checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
bool same(const Output &a, const Output &b, bool identities = true) {
    if (a.pitchX != b.pitchX || a.placements.size() != b.placements.size()) return false;
    for (size_t i = 0; i < a.placements.size(); ++i) {
        const auto &x = a.placements[i], &y = b.placements[i];
        if (std::tie(x.group,x.poseIndex,x.x,x.y) != std::tie(y.group,y.poseIndex,y.x,y.y)) return false;
        if (identities && x.ordinal != y.ordinal) return false;
    }
    return true;
}
class ControllerFixture final : public BtHost {
public:
    std::vector<Placement> current, committed;
    std::vector<size_t> prediction;
    std::array<double,3> projections{5,4,4}, occupied{6,8,7};
    std::string calls;
    size_t round = 0, checkpoint = 0, selected = 0, stops = 0, stopAt = 0;
    int throwStage = -1, throwCommit = -1;
    bool emptyA = false;
    bool stopped() override { return stopAt && ++stops >= stopAt; }
    std::vector<BtCandidate> alternatives(std::span<const Placement> p) override {
        calls += 'G';
        for (auto item : p) prediction.push_back(item.ordinal);
        if (round) return {};
        return {{0,1,0},{2,3,1},{4,5,2}};
    }
    void beginRound() override { calls += 'S'; checkpoint = current.size(); ++round; }
    int runStage(size_t stage) override {
        calls += stage%2 ? 'B' : 'A'; selected = stage/2;
        if (emptyA && stage%2 == 0) return 0;
        Placement p; p.ordinal = stage+10; current.push_back(p);
        if (static_cast<int>(stage) == throwStage) throw Failure(PN2S_TIMEOUT,"injected timeout");
        return 1;
    }
    std::span<const Placement> placements() const override { return current; }
    BtScoreInput scoreInput(std::span<const Placement>,std::span<const Placement>) override {
        calls += 'M'; BtScoreInput v;
        v.targetField14b8 = projections[selected]; v.totalValue = 1; v.valueA = 1;
        v.baselineFreeArea = 1; v.sheetHeight = 1; v.maxPlacedX = rounded(occupied[selected]*1e6);
        return v;
    }
    void restore() override { calls += 'R'; current.resize(checkpoint); }
    void commit(const Placement &p) override {
        calls += 'C'; current.push_back(p);
        if (static_cast<int>(committed.size()) == throwCommit) throw Failure(PN2S_INTERNAL,"injected commit failure");
        committed.push_back(p);
    }
    void endRound() override { calls += 'E'; }
};
void controllerTests() {
    for (bool lengthMode : {false,true}) {
        ControllerFixture f;
        check(runBt(f,lengthMode) == 1,"Bt must commit one A, not A+B");
        check(f.current.size() == 1 && f.committed.size() == 1,"trial placements leaked");
        check(f.committed[0].ordinal == (lengthMode ? 10U : 12U),"score mode or first tie changed");
        check(f.prediction == std::vector<size_t>{lengthMode ? 11U : 13U},"B not forwarded to next candidate generation");
        check(f.calls == "GSABMRABMRABMRCEGSE","Bt outer operation order changed");
    }
    ControllerFixture strict;
    strict.projections[2] = 4 - 1e-6;
    runBt(strict,false);
    check(strict.committed[0].ordinal == 14,"Bt incorrectly used Offset's near-tie tolerance");
    ControllerFixture nan;
    nan.projections[0] = std::numeric_limits<double>::quiet_NaN();
    runBt(nan,false);
    check(nan.committed[0].ordinal == 10,"unordered comparison must preserve first trial");
    ControllerFixture empty;
    empty.emptyA = true;
    check(!evaluateBtTrial(empty,{0,1,0}) && empty.calls == "A","empty A must not run B/map/restore");
    for (int stage : {0,1}) {
        ControllerFixture f; f.throwStage = stage;
        bool caught = false;
        try { evaluateBtTrial(f,{0,1,0}); } catch(const Failure &e) { caught = e.code == PN2S_TIMEOUT; }
        check(caught && f.current.empty() && f.calls.back() == 'R',"trial timeout leaked placements");
        f.calls.clear(); f.stops = 0;
        caught = false;
        try { runBt(f,false); } catch(const Failure &e) { caught = e.code == PN2S_TIMEOUT; }
        check(caught && f.current.empty() && f.calls.back() == 'E',"outer timeout failed to release round");
    }
    ControllerFixture commitFailure; commitFailure.throwCommit = 0;
    bool caught = false;
    try { runBt(commitFailure,false); } catch(const Failure&) { caught = true; }
    check(caught && commitFailure.current.empty() && commitFailure.calls.ends_with("CRE"),"failed commit leaked current-round placements");
    ControllerFixture stopped; stopped.stopAt = 3;
    check(runBt(stopped,false) == 1 && stopped.committed[0].ordinal == 10,"stop after valid trial must commit available best A");
    BtScoreInput v; v.targetField14b8=20; v.totalValue=1; v.valueA=1;
    v.baselineFreeArea=1; v.sheetHeight=1; v.maxPlacedX=3'000'000; v.maximumLengthX=10'000'000;
    check(btScore(v).length == 3,"maximum-length overflow must fall back to occupied, not clamp");
    std::vector<Placement> p(4);
    for(size_t i=0;i<p.size();++i)p[i].ordinal=i;
    check(btCapture(p,2)[0].ordinal==2 && btCapture(p,0).empty(),"tail capture changed");
    check(btCapture(p,-1).size()==4 && btCapture(p,8).size()==4,"full-list capture changed");
}
void directionTests() {
    std::mt19937 random(0x3dad90);
    for(int trial=0;trial<4000;++trial) {
        Rows rows(1+random()%30);
        for(auto &row:rows)if(random()%3) {
            int64_t y=random()%4000;row.push_back({y,y+random()%5000});
        }
        const Point direction{(static_cast<int>(random()%11)-5)/3.,(static_cast<int>(random()%11)-5)/7.};
        const int64_t pitch=1+random()%1000000;
        auto points=directionPoints(rows,pitch,std::array{direction,direction},0,9000);
        // Exhaustive support search, without the original early-stop bound.
        const double xStep=static_cast<double>(pitch)/1e6*direction.x;
        std::optional<std::array<int64_t,2>> best;double value=-std::numeric_limits<double>::max();
        for(size_t j=0;j<rows.size();++j) {
            const size_t k=xStep<0?j:rows.size()-1-j;if(rows[k].empty())continue;
            const int64_t y=direction.y<0?rows[k].front()[0]:rows[k].back()[1];
            const double v=(y/1000.)*direction.y+static_cast<double>(k)*xStep;
            if(v>value){value=v;best=std::array<int64_t,2>{static_cast<int64_t>(k),y};}
        }
        check(points.size()==(best?1:0) && (!best || points[0]==*best),"direction support differs from exhaustive maximum or duplicate filtering");
    }
    const Rows tied{{{0,1000}},{{0,1000}},{{0,1000}}};
    check(directionPoints(tied,1000000,std::array{Point{-1,0}},0,1000)[0]==std::array<int64_t,2>{0,1000},"zero dy must select upper boundary");
    check(directionPoints(tied,1000000,std::array{Point{0,0}},0,1000)[0]==std::array<int64_t,2>{2,1000},"zero dx must retain reverse scan tie");
    const std::array<DirectionGroup,4> groups{{{3,3,0},{7,8,4},{9,99,0,true},{11,5,0}}};
    for(int limit:{-9,0,1,10,11,80}) {
        Random rng(42),expectedRng(42);
        auto pairs=directionGenerator18(rng,2,3,2,limit,groups);
        const size_t expected=limit<0?1:std::min<size_t>(22,static_cast<size_t>(limit)+1);
        check(pairs.size()==expected,"Generator18 must check limit after append");
        for(size_t j=0;j<pairs.size();++j)
            check(pairs[j].keys==std::vector<size_t>{j<11?7U:11U} && pairs[j].batch==2 && pairs[j].tag==3 && pairs[j].directions.size()==1,"Generator18 threshold/optional/type order changed");
        for(int k=0;k<20;++k)expectedRng.unit();
        check(rng.words==expectedRng.words,"Generator18 truncated calls must consume all 20 U53 values");
        if(pairs.size()>11)check(std::bit_cast<uint64_t>(pairs[0].directions[0].x)==std::bit_cast<uint64_t>(pairs[11].directions[0].x) &&
            std::bit_cast<uint64_t>(pairs[0].directions[0].y)==std::bit_cast<uint64_t>(pairs[11].directions[0].y),"Generator18 must reuse directions across types");
    }
    Random emptyRng(42),expectedEmpty(42);
    check(directionGenerator18(emptyRng,2,3,2,0,{}).empty(),"Empty Generator18 catalog fabricated candidates");
    for(int k=0;k<20;++k)expectedEmpty.unit();
    check(emptyRng.words==expectedEmpty.words,"Empty Generator18 catalog must still draw directions");
    for(int count:{-3,0,1,3}) {
        Random rng(123),expected(123);std::vector<ValuePart> parts(1);
        parts[0].area=6;parts[0].bboxArea=6;parts[0].quantity=4;parts[0].maxWidth=3;parts[0].maxHeight=2;parts[0].perimeterField=10;
        auto pairs=directionGenerator10(rng,8,20,2,count,.15,.25,0,.05,parts,24);
        check(pairs.size()==static_cast<size_t>(std::max(0,count)),"Generator10 signed iteration count differs");
        for(int k=0;k<std::max(0,count)*8;++k)expected.unit();
        check(rng.words==expected.words,"Generator10 no-perimeter path must use eight U53 per pair");
        for(const auto &pair:pairs)check(pair.candidate.tag==1 && pair.candidate.directions.size()==3 && pair.valuesA.size()==1 && std::abs(pair.valuesA[0]-6)<1e-12,"Generator10 direction set or weight normalization differs");
    }
}
Polygon rect(double w,double h) { return {{0,0},{w,0},{w,h},{0,h}}; }
PartInput part(Polygon polygon,int count,std::vector<Pose> poses) {
    PartInput p; p.polygon=std::move(polygon); p.quantity=count; p.poses=std::move(poses); return p;
}
Input makeInput(size_t profile,uint32_t seed) {
    Input i; i.width=30; i.height=20; i.pitch=.5; i.randomSeed=seed;
    const Polygon L{{0,0},{4,0},{4,1},{1,1},{1,4},{0,4}};
    if(profile==0) i.parts={part(rect(4,2),8,{{0},{90},{180},{270}})};
    else if(profile==1) i.parts={part(L,3,{{0},{90}}),part(rect(3,2),4,{{0},{90}}),part({{0,0},{3,0},{0,2}},3,{{0},{90}})};
    else {
        i.parts={part(L,4,{{0},{90,true},{180}}),part(rect(5,1),4,{{0},{90}})};
        // Original point coordinates and a non-origin pivot; still on the exact grid.
        for(auto &p:i.parts){for(auto &q:p.polygon){q.x+=2;q.y-=3;}p.pivot={2,-3};}
    }
    return i;
}
BtProgram program(const Input &i,bool lengthMode) {
    BtProgram p; p.lengthMode=lengthMode;
    for(const auto &part:i.parts){const double a=area(part.polygon);p.evaluationValues.push_back(a);p.targetField14b8+=a*part.quantity;}
    for(size_t direction=0;direction<4;++direction) {
        OrdinaryControl a; a.values=p.evaluationValues; a.placementLimit=2;
        a.directions.fill(false); a.directions[direction]=true;
        OrdinaryControl b=a; b.placementLimit=3; b.directions.fill(true);
        p.stages.push_back(a);p.stages.push_back(b);
        p.candidates.push_back({2*direction,2*direction+1,static_cast<int>(direction)});
    }
    return p;
}
Output publicDirection(const Input &input,const BtProgram &config,const MultiProgram *multi=nullptr) {
    struct Owner { PN2_Session session=PN2_CreateSession();~Owner(){PN2_DeleteSession(session);} } owner;
    const auto s=owner.session;
    check(!PN2_IsObjectError(s),"public Bt session failed");
    check(PN2S_SetPitch(s,input.pitch)==0 && PN2S_SetRandomSeed(s,input.randomSeed)==0,"public Bt configuration failed");
    std::vector<PN2_Part> all;std::vector<double> values;
    std::map<PN2_Part,std::pair<size_t,size_t>> identities;
    for(size_t g=0;g<input.parts.size();++g) {
        const auto &p=input.parts[g];
        auto shape=PN2_AddShape(s,p.polygon.data(),static_cast<int>(p.polygon.size()),p.pivot);
        check(!PN2_IsObjectError(shape),"public Bt shape failed");
        std::vector<PN2_Orientation> poses;for(auto pose:p.poses)poses.push_back({pose.flip,pose.angle,pose.angle});
        for(int n=0;n<p.quantity;++n) {
            auto part=PN2_AddPart(s,shape,poses.data(),static_cast<int>(poses.size()));
            check(!PN2_IsObjectError(part),"public Bt part failed");identities[part]={g,n};all.push_back(part);values.push_back(config.evaluationValues[g]);
        }
    }
    auto sheet=PN2_AddSheet(s,input.height,input.width);PN2_Result result;
    if(multi) {
        PN2S_MultiStrategyOptions o{};o.struct_size=sizeof(o);o.context_batch_238=multi->contextBatch238;
        o.current_reference_kind=static_cast<int>(multi->currentReference);o.length_mode=multi->lengthMode;
        o.direction_scale_218=multi->contextField218;o.direction_scale_220=multi->contextField220;
        o.target_14b8=multi->targetField14b8;o.queue_20=multi->queueAggregate20;o.maximum_length=multi->maximumLengthX/1e6;
        o.perimeter_probability_1d8=multi->config.perimeterProbability;o.perimeter_scale_1e0=multi->config.perimeterScale;
        o.alpha_limit_260=multi->config.alphaLimit;o.gamma_limit_268=multi->config.gammaLimit;
        o.fixed_front=multi->fixedFront/1e6;o.force_process_inspection=multi->forceProcessInspection;
        o.replay_previous=multi->replayPrediction;o.filter_replay_front=multi->filterReplayAtFront;
        result=PN2S_NestMultiStrategy(s,sheet,all.data(),static_cast<int>(all.size()),&o,30);
    } else if(config.combineGenerators || config.extremal1c) {
        PN2S_BtOptions o{};o.struct_size=sizeof(o);o.batch_size=config.extremal1c->batch;
        o.reference_kind=static_cast<int>(config.reference);o.length_mode=config.lengthMode;
        o.direction_scale_218=input.height;o.direction_scale_220=input.width;
        o.target_14b8=config.targetField14b8;o.queue_20=config.queueAggregate20;o.maximum_length=config.maximumLengthX/1e6;
        o.flag_14=config.direction14?7:0;o.limit_18=config.direction18?config.direction18->limit18:0;
        o.count_1c=config.extremal1c->count1c;o.fixed_front=config.extremal1c->front/1e6;
        o.force_process_inspection=config.extremal1c->forceProcessInspection;
        o.replay_previous=config.replayPrediction;o.filter_replay_front=config.filterReplayAtFront;
        if(config.direction10) {
            const auto &d=*config.direction10;o.count_10=d.count10;o.alpha_50=d.alpha50;o.gamma_58=d.gamma58;
            o.perimeter_probability_1d8=d.perimeterProbability;o.perimeter_scale_1e0=d.perimeterScale;
        }
        result=PN2S_NestBt(s,sheet,all.data(),static_cast<int>(all.size()),values.data(),&o,30);
    } else if(config.direction10) {
        const auto &d=*config.direction10;
        PN2S_Direction10Options options{sizeof(PN2S_Direction10Options),d.batch,static_cast<int>(config.reference),config.lengthMode,
            d.contextField218,d.contextField220,config.targetField14b8,config.queueAggregate20,config.maximumLengthX/1e6,
            d.count10,d.alpha50,d.gamma58,d.perimeterProbability,d.perimeterScale};
        result=PN2S_NestDirection10(s,sheet,all.data(),static_cast<int>(all.size()),values.data(),&options,30);
    } else if(config.direction18) {
        const auto &d=*config.direction18;
        PN2S_Direction18Options options{sizeof(PN2S_Direction18Options),d.batch,static_cast<int>(config.reference),config.lengthMode,
            d.contextField218,d.contextField220,config.targetField14b8,config.queueAggregate20,config.maximumLengthX/1e6,d.limit18};
        result=PN2S_NestDirection18(s,sheet,all.data(),static_cast<int>(all.size()),values.data(),&options,30);
    } else {
        const auto &d=*config.direction14;
        PN2S_Direction14Options options{sizeof(PN2S_Direction14Options),d.batch,static_cast<int>(config.reference),config.lengthMode,
            d.contextField218,d.contextField220,config.targetField14b8,config.queueAggregate20,config.maximumLengthX/1e6};
        result=PN2S_NestDirection14(s,sheet,all.data(),static_cast<int>(all.size()),values.data(),&options,30);
    }
    if(PN2_IsObjectError(result))std::cerr<<PN2S_LastErrorDetail()<<'\n';
    check(!PN2_IsObjectError(result),"public Bt nesting failed");
    int n;double length;check(PN2_GetResultInfos(s,result,&length,&n)==0,"public Bt result failed");
    Output out;out.pitchX=rounded(input.pitch*1e6);
    for(int k=0;k<n;++k) {
        PN2_Part part;PN2_Point pos;PN2_Orientation pose;
        check(PN2_GetKthNestedPart(s,result,k,&part,&pos,&pose)==0,"public Bt placement failed");
        auto [g,ordinal]=identities.at(part);size_t index=0;
        while(index<input.parts[g].poses.size() && (input.parts[g].poses[index].angle!=pose.min_angle || input.parts[g].poses[index].flip!=!!pose.horizontal_flip))++index;
        check(index<input.parts[g].poses.size(),"public Bt returned unauthorized pose");
        out.placements.push_back({g,ordinal,index,rounded(pos.x*1e6),rounded(pos.y*1000)});
    }
    size_t queried=0;
    for(const auto &[part,identity]:identities) {
        PN2_Point pos;PN2_Orientation pose;const int found=PN2_GetNestedPart(s,result,part,&pos,&pose);
        const bool expected=std::any_of(out.placements.begin(),out.placements.end(),[&](const auto &p){return std::pair{p.group,p.ordinal}==identity;});
        check(found==(expected?1:0),"Public result must report unplaced tail instances accurately");queried+=found==1;
    }
    check(queried==out.placements.size(),"Public instance-query count differs");
    return out;
}
void rollbackTests() {
    Input i=makeInput(0,1); OrdinaryState state; Budget budget(30);
    OrdinaryControl c; c.values={8}; c.placementLimit=0;
    ReferenceProbe initial, after, restored;
    ordinaryRun(i,budget,state,&c,&initial);
    c.placementLimit=2;
    check(ordinaryRun(i,budget,state,&c)==2,"ordinary continuation setup failed");
    const auto verifyLifecycle=[&]{
        const auto &host=*state.placementHost;
        check(host.nesting.placements.size()==state.output.placements.size() && host.nesting.placedCount13d8==int32_t(state.output.placements.size()),"ordinary lifecycle count differs from published geometry");
        for(size_t id=0;id<state.catalog->instances.size();++id){
            const auto current=host.lifecycle.instances[id].current00.lock();
            check(bool(current)==state.catalog->instances[id].placed && (host.lifecycle.catalog.states[id]==1)==bool(current),"ordinary lifecycle and candidate catalog disagree");
            check((host.lifecycle.instances[id].owner08==&host.nesting)==bool(current),"ordinary lifecycle retained a stale nesting owner");
        }
        for(size_t j=0;j<state.output.placements.size();++j){
            const auto &value=state.output.placements[j];const auto &live=*host.nesting.placements[j];
            const auto &instance=state.catalog->instances[size_t(live.instance18)];
            check(instance.group==value.group && instance.ordinal==value.ordinal && live.x48==value.x && live.y50==value.y,"ordinary lifecycle lost placement identity or coordinates");
            check(live.geometryOwner==host.modules[value.group][value.poseIndex].geometryOwner,"ordinary lifecycle did not preserve immutable module geometry");
        }
    };
    verifyLifecycle();
    std::weak_ptr<recovered::WarmPlacement> removedOwner=state.placementHost->nesting.placements.front();
    const auto expected=state.output;
    std::vector<size_t> cursors;
    for(const auto &r:state.catalog->records)cursors.push_back(r.cursor);
    c.placementLimit=0; ordinaryRun(i,budget,state,&c,&after);
    check(after.currentCount<initial.currentCount,"trial did not change reference free area");
    ordinaryRestore(state,0);
    verifyLifecycle();check(removedOwner.expired(),"ordinary rollback kept a removed placement alive");
    for(size_t n=0;n<cursors.size();++n) {
        check(state.catalog->records[n].cursor==cursors[n],"rollback rewound catalog cursor");
        check(state.catalog->records[n].nestedCount==0,"rollback leaked observer count");
    }
    for(auto instance:state.catalog->instances)check(!instance.placed,"rollback leaked instance flag");
    ordinaryRun(i,budget,state,&c,&restored);
    check(initial.currentCount==restored.currentCount && initial.initialCount==restored.initialCount,"free map did not return to checkpoint");
    c.placementLimit=2; ordinaryRun(i,budget,state,&c);
    check(same(expected,state.output,false),"rollback changed repeated trial geometry");
    ordinaryRestore(state,0);
    for(auto p:expected.placements)ordinaryCommit(state,p);
    verifyLifecycle();
    c.placementLimit=0; ordinaryRun(i,budget,state,&c,&restored);
    check(same(expected,state.output) && restored.currentCount==after.currentCount,"commit did not recreate winning A geometry");
    const auto baseline=state.output;
    c.placementLimit=3; ordinaryRun(i,budget,state,&c); ordinaryRestore(state,baseline.placements.size());
    check(same(baseline,state.output),"rollback altered prior committed placements");
    verifyLifecycle();
}
void writeCase(std::ostream &out,const Input &i,const Output &r,const BtTrace &trace,size_t profile,bool mode,int route) {
    out<<"{\"route\":\""<<(route==100?"public-multistrategy":route==99?"public-combined":route==28?"public-extremal1c":route==10?"public-direction10":route==18?"public-direction18":route==14?"public-direction14":"explicit")<<"\",\"profile\":"<<profile<<",\"seed\":"<<i.randomSeed<<",\"lengthMode\":"<<(mode?"true":"false")
       <<",\"trace\":{\"rounds\":"<<trace.rounds<<",\"trials\":"<<trace.trials<<",\"restored\":"<<trace.restored
       <<",\"committed\":"<<trace.committed<<",\"predicted\":"<<trace.predicted<<",\"generatedPairs\":"<<trace.generatedPairs<<",\"perturbedStages\":"<<trace.perturbedStages
       <<",\"dynamicEvaluations\":"<<trace.dynamicEvaluations<<",\"replayPairs\":"<<trace.replayPairs<<",\"replayed\":"<<trace.replayed<<"},\"width\":"<<i.width<<",\"height\":"<<i.height<<",\"parts\":[";
    for(size_t g=0;g<i.parts.size();++g) {
        if(g)out<<',';const auto &p=i.parts[g];
        out<<"{\"quantity\":"<<p.quantity<<",\"polygon\":[";
        for(size_t j=0;j<p.polygon.size();++j){if(j)out<<',';out<<'['<<p.polygon[j].x<<','<<p.polygon[j].y<<']';}
        out<<"],\"holes\":[],\"ranges\":[";
        for(size_t j=0;j<p.poses.size();++j){if(j)out<<',';const auto &pose=p.poses[j];out<<'['<<pose.angle<<','<<pose.angle<<','<<pose.flip<<']';}
        out<<"]}";
    }
    out<<"],\"placements\":[";
    for(size_t n=0;n<r.placements.size();++n) {
        if(n)out<<',';const auto &p=r.placements[n];const auto &g=i.parts[p.group];const auto pose=g.poses[p.poseIndex];
        // Audit transforms about zero, so report the equivalent zero-pivot translation.
        const auto transformedOrigin=transform({{0,0}},g.pivot,pose)[0];
        out<<"{\"group\":"<<p.group<<",\"ordinal\":"<<p.ordinal<<",\"x\":"<<p.x/1e6+transformedOrigin.x
           <<",\"y\":"<<p.y/1000.+transformedOrigin.y<<",\"angle\":"<<pose.angle<<",\"flip\":"<<pose.flip<<'}';
    }
    out<<"],\"unplaced\":[";bool first=true;
    for(size_t g=0;g<i.parts.size();++g)for(size_t ordinal=0;ordinal<static_cast<size_t>(i.parts[g].quantity);++ordinal) {
        if(std::any_of(r.placements.begin(),r.placements.end(),[&](const auto &p){return p.group==g && p.ordinal==ordinal;}))continue;
        if(!first)out<<',';first=false;out<<"{\"group\":"<<g<<",\"ordinal\":"<<ordinal<<'}';
    }
    out<<"]}";
}
}
int main() {
    try {
        controllerTests();rollbackTests();directionTests();
        for(const auto &weights:std::vector<std::vector<double>>{{0,0},{1,-1},{1,std::numeric_limits<double>::infinity()},{std::numeric_limits<double>::quiet_NaN()},{1e308,1e308},{std::numeric_limits<double>::denorm_min(),0}}) {
            Random rng(1);auto before=rng.words;bool rejected=false;
            try{aliasDraw(rng,weights);}catch(const Failure &e){rejected=e.code==PN2S_INVALID;}
            check(rejected && rng.words==before,"Invalid alias weights must fail before drawing");
        }
        std::ostringstream cases;cases<<std::setprecision(17);size_t count=0,placements=0;
        for(int route:{0,14,18,10,28,99,100})for(size_t profile=0;profile<3;++profile)for(uint32_t seed=0;seed<4;++seed)for(bool mode:{false,true}) {
            auto input=makeInput(profile,seed);auto config=program(input,mode);Budget budget(30);BtTrace trace;
            MultiProgram multi;
            if(route==100) {
                multi.contextBatch238=1+seed%2;multi.contextField218=input.height;multi.contextField220=input.width;
                multi.currentReference=static_cast<ReferenceKind>(profile);multi.targetField14b8=config.targetField14b8;multi.lengthMode=mode;
                multi.config={std::array{0.,.5,1.,.5}[seed],.05,.3,.3};
                MultiFactoryResult d;config=multiProgram(input,multi,&d);
                check(config.initialRandom.has_value() && config.initialRandom->words!=Random(seed).words,"Multi factory RNG was reseeded");
                check(!config.direction18 && config.direction10->count10==5 && config.extremal1c->count1c==5,"Multi default routes differ");
            } else if(route) {
                config.stages.clear();config.candidates.clear();
                if(route==28 || route==99) {
                    config.extremal1c=BtProgram::Extremal1c{2,2+static_cast<int>(seed),route==99 && seed%2?rounded(input.width*.5e6):0,seed%2!=0};
                    if(route==99) {
                        config.combineGenerators=true;config.replayPrediction=true;config.filterReplayAtFront=seed%2!=0;
                        config.direction10=BtProgram::Direction10{input.height,input.width,2,2,.15,.25,.5,.05};
                        config.direction14=BtProgram::Direction14{input.height,input.width,2};
                        config.direction18=BtProgram::Direction18{input.height,input.width,2,10};
                    }
                } else if(route==10) {
                    config.direction10=BtProgram::Direction10{input.height,input.width,static_cast<int>(seed%3)+1,static_cast<int>(seed)+2,.15,.25,std::array{0.,.5,1.,.5}[seed],.05};
                    for(size_t k=0;k<config.evaluationValues.size();++k)config.evaluationValues[k]*=k+2.;
                } else if(route==14)config.direction14=BtProgram::Direction14{input.height,input.width,2};
                else config.direction18=BtProgram::Direction18{input.height,input.width,static_cast<int>(seed%3)+1,std::array{-1,0,10,11}[seed]};
                config.reference=static_cast<ReferenceKind>(profile);
            }
            auto output=route==100?ordinaryMulti(input,multi,budget,&trace):ordinaryBt(input,config,budget,&trace);
            size_t required=0;std::set<std::pair<size_t,size_t>> seen;
            for(const auto &p:input.parts)required+=p.quantity;
            size_t expectedPlaced=required;
            if(route==18) {
                expectedPlaced=0;const int batch=config.direction18->batch;
                for(size_t g=0;g<input.parts.size();++g) {
                    const size_t expected=batch*std::max(0,input.parts[g].quantity/batch-1);expectedPlaced+=expected;
                    check(static_cast<size_t>(std::count_if(output.placements.begin(),output.placements.end(),[&](const auto &p){return p.group==g;}))==expected,"Generator18 stage crossed type restriction or consumed below-threshold tail");
                }
            }
            check(output.placements.size()==expectedPlaced,"Bt placement count differs from route eligibility");
            for(const auto &p:output.placements) {
                check(p.group<input.parts.size() && p.ordinal<static_cast<size_t>(input.parts[p.group].quantity),"invalid instance");
                check(seen.emplace(p.group,p.ordinal).second,"trial and prediction committed a duplicate");
            }
            check(trace.trials==trace.restored && trace.committed==expectedPlaced,"Bt leaked trials or committed predictions");
            if(route!=18)check(trace.trials>trace.rounds && (route==100 || trace.predicted>0),"Bt geometry did not exercise multiple restored alternatives");
            else check(trace.trials==trace.generatedPairs && trace.rounds==expectedPlaced/config.direction18->batch+1,"Generator18 count/termination differs from configured sub-strategy");
            if(seed==0){Budget replayBudget(30);check(same(output,ordinaryBt(input,config,replayBudget)),"Bt deterministic replay differs");}
            if(route) {
                if(route==14)check(trace.generatedPairs==6*trace.rounds,"Generator14 must regenerate six pairs each round");
                if(route==10)check(trace.generatedPairs==static_cast<size_t>(config.direction10->count10)*trace.rounds && trace.perturbedStages==trace.generatedPairs,"Generator10 must construct new A weights for each pair and round");
                if(route==28 || route==99)check(trace.dynamicEvaluations>0,"Extremal1c failed to evaluate runtime random weights");
                if(route==99)check(trace.replayPairs>0 && trace.replayed>0,"Combined route did not exercise prior prediction replay");
                if(route==100)check(trace.generatedPairs==16*trace.rounds && trace.dynamicEvaluations>0,"Multi default pair count/runtime weights differ");
                check(same(output,publicDirection(input,config,route==100?&multi:nullptr)),"Public DLL direction generator differs from core execution");
            }
            if(count++)cases<<',';writeCase(cases,input,output,trace,profile,mode,route);placements+=output.placements.size();
        }
        // No feasible A: clean termination, and unsupported domains never fall back.
        auto input=makeInput(0,1);input.width=1;auto config=program(input,false);Budget budget(30);BtTrace trace;
        check(ordinaryBt(input,config,budget,&trace).placements.empty() && trace.trials==0 && trace.committed==0,"unplaceable A must not fabricate results");
        input=makeInput(0,1);input.gap=1;bool rejected=false;
        try{ordinaryBt(input,config,budget);}catch(const Failure &e){rejected=e.code==PN2S_UNSUPPORTED;}
        check(rejected,"Bt silently accepted unsupported spacing");
        std::cout<<"{\"pass\":true,\"checks\":"<<checks<<",\"caseCount\":"<<count<<",\"placementCount\":"<<placements<<",\"cases\":["<<cases.str()<<"]}\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
