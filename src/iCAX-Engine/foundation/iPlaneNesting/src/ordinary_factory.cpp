#include "ordinary_runtime.hpp"
#include "ordinary_pair_map.hpp"
namespace study {
std::vector<recovered::ValuePart> ordinaryValueParts(const Input &input, double &totalArea, int32_t &population) {
    std::vector<recovered::ValuePart> parts;
    int64_t count=0;
    for(const auto &part:input.parts) {
        recovered::ValuePart value;
        const auto box=bounds(part.polygon);
        value.area=material_area(part); value.bboxArea=(box.x1-box.x0)*(box.y1-box.y0);
        value.quantity=part.quantity;
        totalArea+=value.area*part.quantity; count+=part.quantity;
        for(size_t edge=0;edge<part.polygon.size();++edge) {
            const auto a=part.polygon[edge],b=part.polygon[(edge+1)%part.polygon.size()];
            value.perimeterField+=std::hypot(b.x-a.x,b.y-a.y);
        }
        for(const auto &hole:part.holes)for(size_t edge=0;edge<hole.size();++edge){
            const auto a=hole[edge],b=hole[(edge+1)%hole.size()];value.perimeterField+=std::hypot(b.x-a.x,b.y-a.y);}
        for(const auto pose:part.poses) {
            const auto transformed=bounds(transform(part.polygon,part.pivot,pose));
            const Point size{transformed.x1-transformed.x0,transformed.y1-transformed.y0};
            value.variantSizes.push_back(size);
            value.maxWidth=std::max(value.maxWidth,size.x);value.maxHeight=std::max(value.maxHeight,size.y);
        }
        parts.push_back(std::move(value));
    }
    require(count>=0 && count<=INT32_MAX,PN2S_RESOURCE_LIMIT,"Offset population exceeds original 32-bit range");
    population=int32_t(count);
    return parts;
}
namespace {
class OrdinaryFinalizeHost final:public recovered::FinalizeHost {
    const Input &input;
    const OffsetProgram &program;
    Budget &budget;
    OffsetTrace &trace;
    recovered::Random random;
    OrdinaryState &state;
    size_t baseline=0,required=0;
public:
    OrdinaryFinalizeHost(const Input &i,const OffsetProgram &p,Budget &b,OffsetTrace &t,OrdinaryState &s)
        :input(i),program(p),budget(b),trace(t),random(p.random),state(s) {
        require(ordinaryPreparedDomain(input),PN2S_UNSUPPORTED,
                "Offset factory requires finite grid-aligned single rings and zero spacing");
        OrdinaryControl initialize;initialize.placementLimit=0;
        for(const auto &part:input.parts){initialize.values.push_back(material_area(part));required+=part.quantity;}
        ordinaryRun(input,budget,state,&initialize);
    }
    ~OrdinaryFinalizeHost() override { if (state.placementHost) state.placementHost->nesting.random = random; }
    size_t childCount()const override{return program.children.size();}
    double remainingTime()override{return std::chrono::duration<double>(budget.deadline-std::chrono::steady_clock::now()).count();}
    bool stopRequested()override{return false;}
    void checkpoint()override{baseline=state.output.placements.size();}
    int runChild(size_t index)override {
        const auto &child=program.children.at(index);
        OrdinaryControl control;control.values=child.evaluation.values;control.reference=child.evaluation.reference;
        control.referenceScale=child.evaluationReferenceScale;
        control.referenceGeometry=program.referenceGeometry;control.extendReferenceSheet=program.extendReferenceSheet;
        control.auxiliaryReference=program.auxiliaryReference;
        control.auxiliaryReferenceScale=child.candidater.rightToLeft?program.reverseReferenceScale:1;
        control.extremaCount=child.candidater.extremaCount;
        control.extremaMaxSteps=child.candidater.maximumForwardStrips;
        control.directions={child.candidater.field90,child.candidater.field92,
                            child.candidater.rightToLeft,child.candidater.field93};
        auto decreasing=child.evaluation.decreasing;
        if(decreasing) {
            control.prepareEvaluation=[&](int32_t placed){recovered::decreasingRandomSetPlaced(*decreasing,placed);};
            control.dynamicValue=[&](double value){
                ++trace.dynamicEvaluations;
                return recovered::decreasingRandomValue(*decreasing,random,[&]{return value;});
            };
        }
        ++trace.trials;
        return ordinaryRun(input,budget,state,&control);
    }
    recovered::FinalizeKey evaluate(int32_t source)override {
        require(source==-1234,PN2S_INTERNAL,"Unexpected Finalize source ID");
        recovered::FinalizeKey key;key.complete=state.output.placements.size()==required;
        int64_t maximumRight=0;
        for(const auto &placement:state.output.placements) {
            const auto &part=input.parts.at(placement.group);
            const auto box=bounds(transform(part.polygon,part.pivot,part.poses.at(placement.poseIndex)));
            maximumRight=std::max(maximumRight,placement.x+rounded(box.x1*1e6));
            key.value+=material_area(part);
        }
        // This adapter has no trim, process edges or material-cost overrides;
        // the recovered corrected-length formula reduces to the raw length.
        key.correctedLength=recovered::rawCandidateLength(maximumRight,0);
        return key;
    }
    std::span<const Placement> placements()const override{return state.output.placements;}
    void restore()override{ordinaryRestore(state,baseline);++trace.restored;}
    void commit(const Placement &placement)override{ordinaryCommit(state,placement);++trace.committed;}
};
}
OffsetProgram offsetProgram(const Input &input,recovered::OffsetFactoryInput factory,const recovered::Random *initialRandom) {
    double totalArea=0;int32_t population=0;
    const auto parts=ordinaryValueParts(input,totalArea,population);
    factory.population88=population;
    std::vector<recovered::OffsetSparsePart> sparseParts;
    for(const auto &part:parts)sparseParts.push_back({part.quantity,0,part.optional,part.area});
    recovered::OffsetSparseInput sparse;
    sparse.parts=sparseParts;sparse.sheetWidth=input.width;sparse.sheetHeight=input.height;
    sparse.specialSheet=factory.candidater.specialSheet;
    factory.candidater.sparseLayout=recovered::offsetSparseLayout(sparse);
    OffsetProgram result;result.random=initialRandom?*initialRandom:recovered::Random(input.randomSeed);
    result.reverseReferenceScale=factory.reverseReferenceScale290;
    result.children=recovered::offsetFactory(result.random,factory,parts,totalArea);
    return result;
}
Output ordinaryOffset(const Input &input,const OffsetProgram &program,Budget &budget,OffsetTrace *trace) {
    if(input.parts.empty())return {};
    OrdinaryState state;ordinaryOffsetContinue(input,program,budget,state,trace);return std::move(state.output);
}
int ordinaryOffsetContinue(const Input &input,const OffsetProgram &program,Budget &budget,OrdinaryState &state,OffsetTrace *trace) {
    if(input.parts.empty())return 0;
    OffsetTrace local;OrdinaryFinalizeHost host(input,program,budget,trace?*trace:local,state);
    return recovered::runFinalize(host);
}
}
