#include "ordinary_runtime.hpp"
#include <iostream>
using namespace study;
using namespace study::recovered;
namespace {
size_t checks=0;
void check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
Polygon rect(double w,double h){return {{0,0},{w,0},{w,h},{0,h}};}
PartInput part(Polygon polygon,int count,std::vector<Pose> poses) {
    PartInput p;p.polygon=std::move(polygon);p.quantity=count;p.poses=std::move(poses);return p;
}
Input makeInput(size_t profile,uint32_t seed) {
    Input i;i.width=25;i.height=15;i.pitch=.5;i.randomSeed=seed;i.recoveredOffset=true;
    const Polygon L{{0,0},{4,0},{4,1},{1,1},{1,4},{0,4}};
    if(profile==0)i.parts={part(rect(4,2),8,{{0},{90},{180},{270}})};
    else if(profile==1)i.parts={part(L,4,{{0},{90}}),part(rect(3,2),4,{{0},{90}}),part({{0,0},{3,0},{0,2}},3,{{0},{90}})};
    else {
        i.width=11;i.height=7;
        i.parts={part(L,8,{{0},{90,true},{180}}),part(rect(5,1),8,{{0},{90}})};
        for(auto &p:i.parts){for(auto &q:p.polygon){q.x+=2;q.y-=3;}p.pivot={2,-3};}
    }
    return i;
}
FinalizeKey key(const Input &input,const Output &out) {
    FinalizeKey k;size_t total=0;for(const auto &p:input.parts)total+=p.quantity;
    k.complete=out.placements.size()==total;
    for(const auto &record:out.placements) {
        const auto &p=input.parts.at(record.group);
        k.correctedLength=std::max(k.correctedLength,record.x+rounded(bounds(transform(p.polygon,p.pivot,p.poses.at(record.poseIndex))).x1*1e6));
        k.value+=area(p.polygon);
    }
    return k;
}
bool same(const Output &a,const Output &b) {
    if(a.pitchX!=b.pitchX || a.placements.size()!=b.placements.size())return false;
    for(size_t j=0;j<a.placements.size();++j) {
        const auto &p=a.placements[j],&q=b.placements[j];
        if(std::tie(p.group,p.ordinal,p.poseIndex,p.x,p.y)!=std::tie(q.group,q.ordinal,q.poseIndex,q.x,q.y))return false;
    }
    return true;
}
Output firstChild(const Input &input,const OffsetProgram &program) {
    const auto &child=program.children.front();OrdinaryControl c;
    c.values=child.evaluation.values;c.reference=child.evaluation.reference;c.referenceScale=child.evaluationReferenceScale;
    c.referenceGeometry=program.referenceGeometry;c.extendReferenceSheet=program.extendReferenceSheet;
    c.auxiliaryReference=program.auxiliaryReference;c.auxiliaryReferenceScale=child.candidater.rightToLeft?program.reverseReferenceScale:1;
    c.extremaCount=child.candidater.extremaCount;c.extremaMaxSteps=child.candidater.maximumForwardStrips;
    c.directions={child.candidater.field90,child.candidater.field92,child.candidater.rightToLeft,child.candidater.field93};
    auto dynamic=child.evaluation.decreasing;auto random=program.random;
    if(dynamic) {
        c.prepareEvaluation=[&](int32_t placed){decreasingRandomSetPlaced(*dynamic,placed);};
        c.dynamicValue=[&](double v){return decreasingRandomValue(*dynamic,random,[&]{return v;});};
    }
    Budget budget(30);OrdinaryState state;ordinaryRun(input,budget,state,&c);return std::move(state.output);
}
}
int main() {
    try {
        size_t cases=0,placements=0,dynamicValues=0;
        for(size_t profile=0;profile<3;++profile)for(uint32_t seed:{1U,19U})
            for(bool dynamic:{false,true})for(int32_t tries:{1,10}) {
                const auto input=makeInput(profile,seed);OffsetFactoryInput factory;factory.count=tries;factory.dynamicEvaluation=dynamic;
                const auto program=offsetProgram(input,factory);check(program.children.size()==size_t(tries),"Offset/Oreol concrete child count");
                Budget budget(30);OffsetTrace trace;const auto output=ordinaryOffset(input,program,budget,&trace);
                check(trace.trials==size_t(tries),"Offset must execute every constructed child");
                check(trace.restored==size_t(tries==1?0:tries),"Offset multi-child trials must restore before final commit");
                check(trace.committed==(tries==1?0:output.placements.size()),"Offset commits only winning child");
                check(valid_layout(input,output),"Offset/Oreol geometric output overlaps or exceeds stock");
                std::set<std::pair<size_t,size_t>> identities;
                for(const auto &p:output.placements)check(p.ordinal<size_t(input.parts.at(p.group).quantity)&&identities.emplace(p.group,p.ordinal).second,"Offset trial leaked a consumed instance");
                if(profile<2) {
                    size_t total=0;for(const auto &p:input.parts)total+=p.quantity;
                    check(output.placements.size()==total,"Offset/Oreol missed feasible required instances");
                }
                check(dynamic?trace.dynamicEvaluations>0:trace.dynamicEvaluations==0,"Offset decreasing-random evaluator routing");
                if(tries>1)check(!finalizeLess(key(input,firstChild(input,program)),key(input,output)),"Finalize winner is worse than its first real child");
                Budget repeat(30);check(same(output,ordinaryOffset(input,program,repeat)),"Offset/Oreol seeded replay differs");
                if(!dynamic && tries==10){Budget ordinaryBudget(30);check(same(output,ordinary(input,ordinaryBudget)),"ordinary recoveredOffset bypasses its default factory");}
                ++cases;placements+=output.placements.size();dynamicValues+=trace.dynamicEvaluations;
            }
        auto impossible=makeInput(1,1);impossible.width=.5;impossible.height=.5;
        Budget budget(30);OffsetTrace trace;
        check(ordinaryOffset(impossible,offsetProgram(impossible),budget,&trace).placements.empty()&&trace.trials==10,"Unplaceable Offset trials must terminate without fabricated placements");
        for(size_t profile=0;profile<3;++profile)for(uint32_t seed:{3U,17U}) {
            const auto input=makeInput(profile,seed);ColumnConfig config;config.tries=6;config.finalTries=2;config.biCount=2;
            const auto program=columnProgram(input,config);Budget columnBudget(30);ColumnTrace columnTrace;
            const auto output=ordinaryColumn(input,program,columnBudget,&columnTrace);
            check(valid_layout(input,output),"Column/Offset continuation overlaps or exceeds stock");
            check(columnTrace.trials>0 && columnTrace.restored==columnTrace.trials,"Column real trial placements must always restore before commit");
            check(columnTrace.columns>0 && columnTrace.committed>0 && columnTrace.dynamicEvaluations>0,"Column bypassed actual strip generation and trial scoring");
            check(columnTrace.committed+columnTrace.fallback.committed==output.placements.size(),"Column and continuation duplicated or lost a committed instance");
            std::set<std::pair<size_t,size_t>> seen;
            for(const auto &p:output.placements)check(p.ordinal<size_t(input.parts[p.group].quantity)&&seen.emplace(p.group,p.ordinal).second,"Column trial or continuation reused a consumed instance");
            if(profile<2) {
                size_t required=0;for(const auto &p:input.parts)required+=p.quantity;
                check(output.placements.size()==required,"Column continuation missed feasible required instances");
            }
            Budget replayBudget(30);check(same(output,ordinaryColumn(input,program,replayBudget)),"Column/Offset seeded replay differs");
            ++cases;placements+=output.placements.size();dynamicValues+=columnTrace.dynamicEvaluations;
        }
        {
            const auto input=makeInput(0,29);ColumnConfig config;config.maxMono=-1;config.finalTries=2;
            const auto program=columnProgram(input,config);Budget columnBudget(30);ColumnTrace columnTrace;
            const auto output=ordinaryColumn(input,program,columnBudget,&columnTrace);
            check(columnTrace.columns==0 && columnTrace.trials==0 && columnTrace.fallback.trials==3,"Column disabled loop must run its recovered Offset continuation");
            check(output.placements.size()==8 && valid_layout(input,output),"Column continuation-only result invalid");++cases;
        }
        {
            const auto input=makeInput(0,7);OrdinaryState state;OrdinaryControl partial;partial.values={area(input.parts[0].polygon)};partial.placementLimit=2;
            Budget partialBudget(30);ordinaryRun(input,partialBudget,state,&partial);
            const auto prefix=state.output.placements;OffsetTrace continuation;
            ordinaryOffsetContinue(input,offsetProgram(input),partialBudget,state,&continuation);
            check(state.output.placements.size()==8 && valid_layout(input,state.output),"Offset continuation failed to preserve and fill an existing layout");
            check(continuation.committed==6 && continuation.trials==10 && continuation.restored==10,"Offset continuation baseline counts differ");
            for(size_t index=0;index<prefix.size();++index)check(std::tie(prefix[index].x,prefix[index].y,prefix[index].ordinal)==std::tie(state.output.placements[index].x,state.output.placements[index].y,state.output.placements[index].ordinal),"Offset continuation replaced its existing prefix");
            ++cases;
        }
        {
            auto input=makeInput(0,1);input.parts[0].poses={{0}};OrdinaryControl c;c.values={8};c.placementLimit=0;
            ReferenceProbe probe;probe.wantFirstPositions=true;OrdinaryState state;Budget b(30);ordinaryRun(input,b,state,&c,&probe);
            check(probe.firstRightStrip.size()==1 && probe.firstRightStrip[0]==8,"Column first-position provider must include the module width");
            c.placementLimit=-1;c.keysExplicit=true;c.keys.clear();check(ordinaryRun(input,b,state,&c)==0,"Column explicit empty group list silently selected all groups");
            c.keys={0};c.columnBounds=std::array<int32_t,2>{0,1000};c.directionVectors={{-1,1}};
            check(ordinaryRun(input,b,state,&c)==0,"Column exclusive X bound admitted strip zero");++cases;
        }
        {
            Input input;input.width=20;input.height=6;input.pitch=1;
            input.parts={part(rect(2,1),3,{{0}})};
            OrdinaryControl c;c.values={2};c.placementLimit=1;c.auxiliaryReference=true;
            OrdinaryState state;ReferenceProbe probe;Budget b(30);ordinaryRun(input,b,state,&c,&probe);
            check(probe.front && probe.initialization && probe.scoreState,"Offset initialization diagnostics were not produced");
            check(probe.front->initialCount==26*5876 && probe.front->currentCount==probe.front->initialCount,
                  "Unscaled c8 auxiliary free map did not use its special X extension");
            check(probe.front->targetField14b8==6 && probe.front->queueAggregate20==0 && probe.front->placedArea==0,
                  "Initial auxiliary producer fields differ from required physical area");
            check(probe.initialization->targetEstimate==1 && probe.initialization->lastMinWidth==2 &&
                  probe.initialization->lastMaxWidth==2 && probe.scoreState->frontEnabled && probe.scoreState->limitX==2200000,
                  "Empty-layout auxiliary estimate did not reach actual Offset front initialization");
            check(state.placementHost->informerArea==2 && state.placementHost->queueAggregate20==2,
                  "Committed placement did not update both original area observers");
            ordinaryRun(input,b,state,&c,&probe);
            check(probe.front->placedArea==2 && probe.front->queueAggregate20==2 &&
                  probe.front->currentCount<probe.front->initialCount && probe.initialization->maxPlacedX==2000000,
                  "Occupied auxiliary map and prior front did not reach the next greedy iteration");
            check(probe.initialization->targetEstimate==currentFront(*probe.front),"Occupied CurrentFront result was discarded");
            ordinaryRestore(state,0);check(state.placementHost->queueAggregate20==0 && state.placementHost->informerArea==0,
                  "Auxiliary area metrics survived a complete trial rollback");
            c.auxiliaryReferenceScale=.55;c.referenceScale=.55;c.placementLimit=0;
            ReferenceProbe scaled;ordinaryRun(input,b,state,&c,&scaled);
            // Height .06875 leaves closed legal Y ticks 0..5931. The old
            // ceil-based sheet cap counted one tick outside the stock.
            check(scaled.initialCount==20*5932,"Scaled 88 primary reference incorrectly retained special X extension");
            c.placementLimit=1;ordinaryRun(input,b,state,&c,&scaled);
            check(scaled.front && !scaled.front->auxIsSpecial && scaled.front->initialCount==19*5932,
                  "Scaled d8 auxiliary map used the unscaled c8 special-reference behavior");
            check(valid_layout(input,state.output),"Auxiliary front scoring produced invalid placement geometry");++cases;
        }
        {
            Input input;input.width=10;input.height=5;input.pitch=.5;
            input.parts={part(rect(1,.3),3,{{0},{90}})};
            OrdinaryState state;OrdinaryControl c;c.values={.3};c.placementLimit=0;Budget b(30);ordinaryRun(input,b,state,&c);
            double expected=0;const double originalArea=area(input.parts[0].polygon);
            for(int instance=0;instance<3;++instance)expected+=originalArea;
            check(state.placementHost->requiredArea==expected,"Required area producer changed physical-instance summation order");
            for(const auto &module:state.placementHost->modules[0])check(module.area58==originalArea,"Area informer depends on transformed-pose arithmetic");
            for(size_t instance=0;instance<3;++instance)ordinaryCommit(state,{0,instance,instance%2,int64_t(instance)*2000000,0});
            check(state.placementHost->informerArea==expected && state.placementHost->queueAggregate20==expected,"Lifecycle area additions differ");
            for(size_t remaining=2;;--remaining) {
                expected-=originalArea;ordinaryRestore(state,remaining);
                check(state.placementHost->informerArea==expected,"Informer removal changed original floating subtraction order");
                check(state.placementHost->queueAggregate20==(remaining?expected:0),"Queue last-removal reset differs from informer arithmetic");
                if(!remaining)break;
            }
            ++cases;
        }
        {
            Input input;input.width=20;input.height=6;input.pitch=1;
            input.parts={part(rect(2,1),1,{{0},{90}}),part(rect(1,4),1,{{0}})};
            const auto rotated=bounds(transform(input.parts[0].polygon,{},input.parts[0].poses[1]));
            const double tiedWidth=rotated.x1-rotated.x0;input.parts[1].polygon=rect(tiedWidth,4);
            OrdinaryControl c;c.values={2,4};c.placementLimit=1;c.keysExplicit=true;c.keys={0};c.targetX=0;
            ReferenceProbe probe;OrdinaryState state;Budget b(30);ordinaryRun(input,b,state,&c,&probe);
            check(probe.initialization && probe.initialization->lastMaxWidth==tiedWidth && probe.initialization->lastMinWidth==tiedWidth &&
                  probe.initialization->firstPositions.size()==1,
                  "Global remaining-lastPart equal-width order did not select the larger part index");
            check(probe.initialization->groupWidths==std::vector<double>{2} && probe.scoreState->frontEnabled,
                  "Explicit candidate-group widths or targetX were omitted from front initialization");++cases;
        }
        std::cout<<"{\"pass\":true,\"caseCount\":"<<cases+1<<",\"placements\":"<<placements<<",\"dynamicValues\":"<<dynamicValues<<",\"checks\":"<<checks<<"}\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
