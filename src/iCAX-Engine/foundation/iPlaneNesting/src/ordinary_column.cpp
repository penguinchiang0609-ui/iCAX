#include "ordinary_runtime.hpp"
#include "ordinary_pair_map.hpp"
#include <bit>
namespace study {
namespace {
Polygon referenceRectangle(double width,double height){return {{0,-height*.5},{width,-height*.5},{width,height*.5},{0,height*.5}};}
int32_t add32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t subtract32(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
class ColumnHost final:public recovered::CombinedHost {
    const Input &input;
    const ColumnProgram &program;
    Budget &budget;
    ColumnTrace &trace;
    OrdinaryState &state;
    recovered::Random random;
    OrdinaryControl measurement;
    size_t checkpoint=0;
    double initialFree=0,baselineFree=0,baselineValue=0,totalValue=0;
    double boostLow=0,boostHigh=0,monoPenalty=0;
    bool boostEnabled=false,enforceFront=false;
    recovered::ColumnTrialState best;
    double placedValue(std::span<const Placement> placements)const {
        double result=0;for(const auto &p:placements)result+=program.evaluation.values.at(p.group);return result;
    }
    std::vector<recovered::ColumnPart> remainingParts()const {
        auto parts=program.parts;
        for(const auto &p:state.output.placements)++parts.at(p.group).placed;
        parts.erase(std::remove_if(parts.begin(),parts.end(),[](const auto &p){return p.placed>=p.required254;}),parts.end());
        return parts;
    }
    std::optional<size_t> lastPart()const {
        std::optional<size_t> result;
        for(const auto &p:remainingParts())if(!result||program.minimumWidth[p.key]>program.minimumWidth[*result]||
            (program.minimumWidth[p.key]==program.minimumWidth[*result]&&p.key>*result))result=p.key;
        return result;
    }
    ReferenceProbe measure(bool positions=false) {
        ReferenceProbe probe;probe.wantFirstPositions=positions;ordinaryRun(input,budget,state,&measurement,&probe);return probe;
    }
    double freeArea(const ReferenceProbe &probe)const{return (double(state.output.pitchX)/1e6)*(double(probe.currentCount)/1000.);}
    OrdinaryControl control()const {
        OrdinaryControl result;result.values=program.evaluation.values;result.referenceGeometry=program.valueReference;
        result.extendReferenceSheet=false;return result;
    }
    class Trial final:public recovered::ColumnTrialHost {
        ColumnHost &host;
        const OrdinaryControl &control;
    public:
        Trial(ColumnHost &h,const OrdinaryControl &c):host(h),control(c){}
        int run()override{++host.trace.trials;return ordinaryRun(host.input,host.budget,host.state,&control);}
        recovered::ColumnScoreInput measure()override {
            const auto probe=host.measure();recovered::ColumnScoreInput result;
            result.pitchX=host.state.output.pitchX;result.currentGridCount=probe.currentCount;
            result.sheetHeight=host.input.height;result.referenceHeight=probe.height;
            result.initialFreeB0=host.initialFree;result.baselineFree100=host.baselineFree;
            result.totalValueA8=host.totalValue;result.baselineValue108=host.baselineValue;
            result.boostEnabled=host.boostEnabled;result.boostLow=host.boostLow;result.boostHigh=host.boostHigh;
            result.boost=host.program.config.firstBoost;result.monoPenalty98=host.monoPenalty;
            result.enforceRemainingFront=host.enforceFront;return result;
        }
        std::span<const Placement> placements()const override{return host.state.output.placements;}
        double value(std::span<const Placement> placements)override{return host.placedValue(placements);}
        void remainingFront(recovered::ColumnScoreInput &result)override {
            for(const auto &placement:host.state.output.placements) {
                const auto &part=host.input.parts.at(placement.group);
                const auto box=bounds(transform(part.polygon,part.pivot,part.poses.at(placement.poseIndex)));
                result.maximumRight=std::max(result.maximumRight,placement.x+rounded(box.x1*1e6));
            }
            if(const auto last=host.lastPart())result.remainingWidth1b8=host.program.minimumWidth[*last];
        }
        void restore()override{ordinaryRestore(host.state,host.checkpoint);++host.trace.restored;}
    };
    void evaluate(const OrdinaryControl &control) {Trial trial(*this,control);recovered::evaluateColumnTrial(trial,best);}
    double selectedArea(std::span<const size_t> keys,std::span<const recovered::ColumnPart> parts)const {
        double result=0;
        for(size_t key:keys)for(const auto &part:parts)if(part.key==key)result+=double(part.required254-part.placed)*part.area58;
        return result;
    }
    void construct(int32_t attempts,int32_t primaryLimit,uint32_t secondaryLimit,bool initial,
                   int32_t front,double width,double columnArea,const recovered::ColumnPartition &partition,
                   std::span<const recovered::ColumnPart> parts,bool overrideLong) {
        const auto &config=program.config;
        const double step=double(state.output.pitchX)/1e6;
        for(int32_t attempt=0;attempt<attempts;++attempt) {
            if(attempt%10==0 && remainingTime()<=0.)return;
            const auto plan=recovered::columnTrialPlan(random,program.context218,program.context220,overrideLong,config.trial);
            const int32_t end=add32(front,recovered::truncateInt32((width*plan.lengthFraction)/step));
            const int32_t first=initial?subtract32(front,recovered::truncateInt32((width*.2)/step)):end;
            const uint32_t limit=uint32_t(primaryLimit<0?random.integer(config.minimumBigParts,config.maximumBigParts):primaryLimit);
            auto keys=recovered::columnSelectGroups(random,parts,partition.primary,limit,true);
            const auto secondary=recovered::columnSelectGroups(random,parts,partition.secondary,secondaryLimit,false);
            keys.insert(keys.end(),secondary.begin(),secondary.end());
            const double amplitude=random.unit()*(config.generationRandom-0.)+0.;
            if(selectedArea(keys,parts)<columnArea*.5)continue;
            auto trialControl=control();trialControl.keys=std::move(keys);trialControl.keysExplicit=true;
            trialControl.columnBounds=std::array{first,end};trialControl.directionVectors=plan.directions;
            const auto noise=recovered::columnRandomEvaluation(amplitude);
            trialControl.dynamicValue=[&](double base){++trace.dynamicEvaluations;return recovered::columnRandomValue(noise,random,[&]{return base;});};
            evaluate(trialControl);
        }
    }
    int runColumn() {
        const auto &config=program.config;const auto firstPart=lastPart();if(!firstPart)return 0;
        const bool overrideLong=random.unit()<.5;
        const double stop=random.unit()*(config.maximumStop-config.minimumStop)+config.minimumStop;
        if(config.multiplicity23c<4 && random.unit()<config.firstRatio) {
            const double width=program.minimumWidth[*firstPart];
            const double low=config.trial.longLengthRatio2b8*.5+1.,high=config.trial.longLengthRatio2b8+1.;
            const double center=(random.unit()*(high-low)+low)*width;
            boostLow=center-width*.1;boostHigh=width*.1+center;boostEnabled=true;
        }
        if(random.unit()<config.monoPenaltyRatio)monoPenalty=random.unit()*(config.monoPenalty-0.)+0.;
        const int32_t bi=random.unit()<config.biRatio?recovered::truncateInt32(config.biCount):0;
        const double widthThreshold=(random.unit()*.5+.3)*program.context218;
        const double areaThreshold=(random.unit()*.3+.2)*program.averageArea228;
        initialFree=freeArea(measure());
        size_t added=0;int32_t mono=0;
        for(int32_t column=0;config.maxMono>=0 && lastPart();++column) {
            if(remainingTime()<=0.)break;
            if(column==1){if(random.unit()<.5)boostEnabled=false;}else if(column>1)boostEnabled=false;
            const auto parts=remainingParts();const auto partition=recovered::columnPartition(parts,widthThreshold,areaThreshold);
            const auto last=lastPart();if(!last||partition.remainingArea==0.)break;
            const double width=program.minimumWidth[*last],columnArea=width*input.height;
            if(width<program.context218*config.tooSmallRatio || partition.remainingArea<columnArea*stop)break;
            const auto probe=measure(true);if(!probe.firstRightStrip[*last])break;
            checkpoint=state.output.placements.size();baselineFree=freeArea(probe);baselineValue=placedValue(state.output.placements);
            enforceFront=selectedArea(partition.primary,parts)<columnArea;best={};
            auto one=[&](size_t group) {
                auto c=control();c.keys={group};c.keysExplicit=true;c.placementLimit=1;
                c.directionVectors={{-program.context220,program.context218*.25},{-program.context220,program.context218*-.25}};
                evaluate(c);
            };
            for(size_t group:partition.primary)one(group);
            for(size_t group:partition.secondary)one(group);
            construct(config.tries,-1,uint32_t(config.regularParts),true,*probe.firstRightStrip[*last],width,columnArea,partition,parts,overrideLong);
            const int32_t biAttempts=std::bit_cast<int32_t>(uint32_t(bi)*uint32_t(config.tries));
            construct(biAttempts,2,0,false,*probe.firstRightStrip[*last],width,columnArea,partition,parts,overrideLong);
            if(!best.hasBest11a||best.placements128.empty())break;
            for(const auto &placement:best.placements128){ordinaryCommit(state,placement);++trace.committed;}
            ++trace.columns;added+=best.placements128.size();mono=best.placements128.size()==1?add32(mono,1):0;
            if(config.maxMono<mono)break;
        }
        return int(added);
    }
public:
    ColumnHost(const Input &i,const ColumnProgram &p,Budget &b,ColumnTrace &t,OrdinaryState &s):input(i),program(p),budget(b),trace(t),state(s),random(p.random) {
        require(ordinaryPreparedDomain(input),PN2S_UNSUPPORTED,"Column requires prepared finite-grid rings and zero spacing");
        measurement=control();measurement.referenceGeometry=program.measurementReference;measurement.placementLimit=0;
        ordinaryRun(input,budget,state,&measurement);
        for(size_t group=0;group<input.parts.size();++group)totalValue+=input.parts[group].quantity*program.evaluation.values[group];
    }
    ~ColumnHost() override { if (state.placementHost) state.placementHost->nesting.random = random; }
    uint64_t totalInstanceCount()override{uint64_t total=0;for(const auto &part:input.parts)total+=part.quantity;return total;}
    int32_t runChild(size_t child)override {
        if(child==0)return runColumn();
        auto fallback=program.fallback;fallback.random=random;
        const auto added=ordinaryOffsetContinue(input,fallback,budget,state,&trace.fallback);
        random=state.placementHost->nesting.random;
        return added;
    }
    double remainingTime()override{return std::chrono::duration<double>(budget.deadline-std::chrono::steady_clock::now()).count();}
    bool stopRequested()override{return false;}
};
}
ColumnProgram columnProgram(const Input &input,const recovered::ColumnConfig &config,const recovered::Random *initialRandom) {
    require(config.tries>=0 && config.tries<=1000 && config.finalTries>=0 && config.finalTries<=1000,PN2S_RESOURCE_LIMIT,"Column trial count exceeds native study allocation range");
    double totalArea=0;int32_t population=0;const auto parts=ordinaryValueParts(input,totalArea,population);
    ColumnProgram result;result.config=config;
    const auto mean=recovered::effectiveAverage(parts,totalArea,{1,1});const auto context=ordinaryContext(input);
    result.context218=context.width218;result.context220=context.height220;result.averageArea228=context.area228;
    result.config.multiplicity23c=context.multiplicity23c;
    result.valueReference=referenceRectangle((mean.x*.25)*.5,(mean.y*.25)*3.);
    result.measurementReference=referenceRectangle((mean.x*.25)*2.,(mean.y*.25)*2.);
    for(size_t group=0;group<parts.size();++group) {
        const auto &part=input.parts[group];const auto box=bounds(part.polygon);double minWidth=std::numeric_limits<double>::max();
        for(const auto size:parts[group].variantSizes)minWidth=std::min(minWidth,size.x);
        if(parts[group].variantSizes.empty())minWidth=box.x1-box.x0;
        result.parts.push_back({group,box.x1-box.x0,parts[group].area,part.quantity,0,false});result.minimumWidth.push_back(minWidth);
    }
    recovered::Random random=initialRandom?*initialRandom:recovered::Random(input.randomSeed);recovered::OffsetFactoryInput factory;factory.population88=population;
    result.evaluation=recovered::offsetValue(random,factory,parts,totalArea);result.evaluation.reference=recovered::ReferenceKind::Normal;
    std::vector<recovered::OffsetSparsePart> sparseParts;for(const auto &part:parts)sparseParts.push_back({part.quantity,0,false,part.area});
    recovered::OffsetSparseInput sparse;sparse.parts=sparseParts;sparse.sheetWidth=input.width;sparse.sheetHeight=input.height;
    factory.candidater.sparseLayout=recovered::offsetSparseLayout(sparse);
    std::vector<recovered::OffsetValuePlan> evaluations(size_t(config.finalTries)+1,result.evaluation);
    result.fallback.children=recovered::offsetChildren(random,factory,evaluations);
    for(auto &child:result.fallback.children)child.evaluationReferenceScale=1;
    result.fallback.referenceGeometry=result.valueReference;result.fallback.extendReferenceSheet=false;
    result.random=random;result.fallback.random=random;return result;
}
Output ordinaryColumn(const Input &input,const ColumnProgram &program,Budget &budget,ColumnTrace *trace) {
    if(input.parts.empty())return {};
    OrdinaryState state;ordinaryColumnContinue(input,program,budget,state,trace);return std::move(state.output);
}
int ordinaryColumnContinue(const Input &input,const ColumnProgram &program,Budget &budget,OrdinaryState &state,ColumnTrace *trace) {
    if(input.parts.empty())return 0;
    ColumnTrace local;ColumnHost host(input,program,budget,trace?*trace:local,state);return recovered::runCombined(host);
}
}
