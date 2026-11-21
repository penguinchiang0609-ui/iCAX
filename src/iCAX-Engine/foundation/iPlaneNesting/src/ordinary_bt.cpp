#include "ordinary_runtime.hpp"
#include "ordinary_pair_map.hpp"
namespace study {
namespace {
class OrdinaryBtHost final : public recovered::BtHost {
    const Input &input;
    const BtProgram &program;
    Budget &budget;
    BtTrace &trace;
    OrdinaryState &state;
    OrdinaryControl measurement;
    recovered::Random directionRng;
    std::vector<OrdinaryControl> stages;
    std::vector<recovered::ValuePart> valueParts;
    size_t checkpoint = 0, required = 0;
    double totalArea = 0, totalValue = 0, baselineFree = 0, baselineLost = 0, baselineValue = 0;
    double value(std::span<const Placement> list) const {
        double sum = 0;
        for (const auto &p : list) sum += program.evaluationValues.at(p.group);
        return sum;
    }
public:
    OrdinaryBtHost(const Input &i, const BtProgram &p, Budget &budgetInput, BtTrace &t, OrdinaryState &existing)
        : input(i), program(p), budget(budgetInput), trace(t), state(existing), directionRng(p.initialRandom.value_or(recovered::Random(i.randomSeed))), stages(p.stages) {
        require(program.evaluationValues.size() == input.parts.size(), PN2S_INVALID, "Bt evaluation values missing");
        require(ordinaryPreparedDomain(input), PN2S_UNSUPPORTED, "Bt requires prepared finite-grid rings and zero spacing");
        measurement.values = program.evaluationValues;
        measurement.reference = program.reference;
        measurement.referenceGeometry = program.referenceGeometry;
        measurement.placementLimit = 0;
        for (size_t k = 0; k < input.parts.size(); ++k) {
            required += input.parts[k].quantity;
            totalArea += input.parts[k].quantity * material_area(input.parts[k]);
            totalValue += input.parts[k].quantity * program.evaluationValues[k];
            if(program.direction10 || program.extremal1c) {
                const auto &part=input.parts[k];recovered::ValuePart v;
                const auto box=bounds(part.polygon);v.area=material_area(part);v.bboxArea=(box.x1-box.x0)*(box.y1-box.y0);v.quantity=part.quantity;
                for(size_t j=0;j<part.polygon.size();++j) {
                    auto a=part.polygon[j],b=part.polygon[(j+1)%part.polygon.size()];v.perimeterField+=std::hypot(b.x-a.x,b.y-a.y);
                }
                for(const auto &hole:part.holes)for(size_t j=0;j<hole.size();++j){const auto a=hole[j],b=hole[(j+1)%hole.size()];v.perimeterField+=std::hypot(b.x-a.x,b.y-a.y);}
                for(auto pose:part.poses) {
                    const auto b=bounds(transform(part.polygon,part.pivot,pose));
                    v.maxWidth=std::max(v.maxWidth,b.x1-b.x0);v.maxHeight=std::max(v.maxHeight,b.y1-b.y0);
                }
                valueParts.push_back(v);
            }
        }
        for (const auto &a : program.candidates)
            require(a.stageA < program.stages.size() && a.stageB < program.stages.size(), PN2S_INVALID, "Bt stage index outside program");
        require(int(program.direction10.has_value())+int(program.direction14.has_value())+int(program.direction18.has_value())+int(program.extremal1c.has_value())<=1 || program.combineGenerators,PN2S_INVALID,"Choose one generator or the combined factory");
        auto validateDirection=[&](const auto &d) {
            require(d.batch>=0 && std::isfinite(d.contextField218) && std::isfinite(d.contextField220),PN2S_INVALID,"Invalid direction generator fields");
            require(program.candidates.empty() && program.stages.empty(),PN2S_INVALID,"Choose explicit stages or a direction generator");
        };
        if(program.direction14)validateDirection(*program.direction14);
        if(program.direction18)validateDirection(*program.direction18);
        if(program.direction10) {
            const auto &d=*program.direction10;validateDirection(d);
            require(d.count10<=10000 && std::isfinite(d.alpha50) && std::isfinite(d.gamma58) &&
                std::isfinite(d.perimeterProbability) && d.perimeterProbability>=0 && d.perimeterProbability<=1 &&
                std::isfinite(d.perimeterScale) && d.perimeterScale>=0,PN2S_INVALID,"Invalid Direction10 weight fields");
        }
        if(program.extremal1c) {
            const auto &d=*program.extremal1c;
            require(d.batch>=0 && d.count1c<=10000 && d.front>=0,PN2S_INVALID,"Invalid Extremal1c fields");
            require(program.candidates.empty() && program.stages.empty(),PN2S_INVALID,"Choose explicit stages or a generator");
        }
        ordinaryRun(input, budget, state, &measurement);
    }
    bool stopped() override {
        budget.check(true);
        return state.output.placements.size() >= required;
    }
    std::vector<recovered::BtCandidate> alternatives(std::span<const Placement> previous) override {
        if(!program.direction10 && !program.direction14 && !program.direction18 && !program.extremal1c)return program.candidates;
        stages.clear();std::vector<recovered::BtCandidate> out;
        auto append=[&](OrdinaryControl a,OrdinaryControl b,int tag) {
            out.push_back({stages.size(),stages.size()+1,tag});
            stages.push_back(std::move(a));stages.push_back(std::move(b));++trace.generatedPairs;
        };
        auto normal=[&](int batch) {
            OrdinaryControl c;c.reference=program.reference;c.referenceGeometry=program.referenceGeometry;
            c.values=program.evaluationValues;c.placementLimit=batch;return c;
        };
        auto directions=[&](const auto &specs) {
            for(const auto &spec:specs) {
                budget.check(true);auto c=normal(spec.batch);c.directionVectors=spec.directions;c.keys=spec.keys;append(c,c,spec.tag);
            }
        };
        // 441ec0: field14 enables one invocation, not that many repetitions.
        if(program.direction14) {
            const auto &d=*program.direction14;
            directions(recovered::directionGenerator14(directionRng,d.contextField218,d.contextField220,d.batch));
        }
        if(program.direction18 && (!program.combineGenerators || program.direction18->limit18>0)) {
            const auto &d=*program.direction18;std::vector<recovered::DirectionGroup> groups;
            for(size_t k=0;k<input.parts.size();++k)groups.push_back({k,input.parts[k].quantity,0});
            for(const auto &p:state.output.placements)++groups.at(p.group).placed;
            directions(recovered::directionGenerator18(directionRng,d.contextField218,d.contextField220,d.batch,d.limit18,groups));
        }
        if(program.extremal1c) {
            const auto &d=*program.extremal1c;
            for(const auto &spec:recovered::extremalGenerator1c(directionRng,d.batch,d.count1c,d.front,d.forceProcessInspection,valueParts,program.evaluationValues,totalArea)) {
                budget.check(true);auto a=normal(spec.batch),b=a;
                a.directions=b.directions={true,true,false,false};
                a.extremaCount=spec.extremaA;b.extremaCount=spec.extremaB;a.fixedFront=spec.front;
                if(spec.uniformA) {
                    a.values=spec.noise;
                    a.dynamicValue=[this](double base){++trace.dynamicEvaluations;return recovered::uniformValue(directionRng,base);};
                    ++trace.perturbedStages;
                }
                for(double v:a.values)require(std::isfinite(v)&&v>0,PN2S_INVALID,"Extremal1c generated invalid weights");
                append(std::move(a),std::move(b),4);
            }
        }
        if(program.direction10) {
            const auto &d=*program.direction10;
            for(const auto &spec:recovered::directionGenerator10(directionRng,d.contextField218,d.contextField220,d.batch,d.count10,
                d.alpha50,d.gamma58,d.perimeterProbability,d.perimeterScale,valueParts,totalArea)) {
                budget.check(true);
                for(double v:spec.valuesA)require(std::isfinite(v)&&v>0,PN2S_INVALID,"Direction10 generated invalid weights");
                auto b=normal(spec.candidate.batch);b.directionVectors=spec.candidate.directions;
                auto a=b;a.values=spec.valuesA;append(std::move(a),std::move(b),1);++trace.perturbedStages;
            }
        }
        if(program.replayPrediction && !previous.empty() && !out.empty()) {
            // 3e4a06..3e4a1b: B is the last newly generated pair's B.
            const size_t continuation=out.back().stageB;auto a=normal(0);a.replay.emplace();
            const int64_t front=program.extremal1c?program.extremal1c->front:0;
            std::vector<int64_t> rightEdges;
            for(const auto &p:previous) {
                const auto &part=input.parts[p.group];
                const int64_t right=p.x+rounded(bounds(transform(part.polygon,part.pivot,part.poses[p.poseIndex])).x1*1e6);
                rightEdges.push_back(right);
            }
            if(program.filterReplayAtFront && front>0) {
                for(auto k:recovered::replayFrontIndices(rightEdges,front))a.replay->push_back(previous[k]);
            } else a.replay->assign(previous.begin(),previous.end());
            out.push_back({stages.size(),continuation,6});stages.push_back(std::move(a));++trace.replayPairs;
        }
        return out;
    }
    void beginRound() override {
        checkpoint = state.output.placements.size();
        ReferenceProbe probe;
        ordinaryRun(input, budget, state, &measurement, &probe);
        const double step = static_cast<double>(state.output.pitchX) / 1e6;
        baselineFree = step * (static_cast<double>(probe.currentCount) / 1000.);
        baselineLost = (static_cast<double>(probe.initialCount - probe.currentCount) / 1000.) * step;
        baselineValue = value(state.output.placements);
        ++trace.rounds;
    }
    int runStage(size_t stage) override {
        const auto &c=stages.at(stage);
        if(!c.replay)return ordinaryRun(input,budget,state,&c);
        for(const auto &p:*c.replay){budget.check();ordinaryCommit(state,p);++trace.replayed;}
        return static_cast<int>(c.replay->size());
    }
    std::span<const Placement> placements() const override { return state.output.placements; }
    recovered::BtScoreInput scoreInput(std::span<const Placement> first, std::span<const Placement> prediction) override {
        ReferenceProbe probe;
        ordinaryRun(input, budget, state, &measurement, &probe);
        recovered::BtScoreInput v;
        v.pitchX = state.output.pitchX; v.currentGridCount = probe.currentCount;
        v.targetField14b8 = program.targetField14b8;
        v.queueAggregate20 = program.dynamicQueue ? state.placementHost->queueAggregate20 : program.queueAggregate20;
        v.totalArea = totalArea; v.totalValue = totalValue;
        v.baselineFreeArea = baselineFree; v.baselineLostArea = baselineLost; v.baselineValue = baselineValue;
        v.valueA = value(first); v.valueB = value(prediction);
        v.sheetHeight = input.height; v.referenceHeight = probe.height; v.maximumLengthX = program.maximumLengthX;
        for (const auto &p : state.output.placements) {
            const auto &part = input.parts[p.group];
            v.placedArea += material_area(part);
            const auto box = bounds(transform(part.polygon, part.pivot, part.poses[p.poseIndex]));
            v.maxPlacedX = std::max(v.maxPlacedX, p.x + rounded(box.x1 * 1e6));
        }
        ++trace.trials;trace.predicted += prediction.size();
        return v;
    }
    void restore() override { ordinaryRestore(state, checkpoint); ++trace.restored; }
    void commit(const Placement &p) override { ordinaryCommit(state,p); ++trace.committed; }
    void endRound() override {}
    ~OrdinaryBtHost() override { if (state.placementHost) state.placementHost->nesting.random = directionRng; }
};
}
Output ordinaryBt(const Input &input, const BtProgram &program, Budget &budget, BtTrace *trace) {
    if (input.parts.empty()) return {};
    OrdinaryState state;
    ordinaryBtContinue(input, program, budget, state, trace);
    return std::move(state.output);
}
int ordinaryBtContinue(const Input &input, const BtProgram &program, Budget &budget, OrdinaryState &state, BtTrace *trace) {
    if (input.parts.empty()) return 0;
    BtTrace local;
    OrdinaryBtHost host(input, program, budget, trace ? *trace : local, state);
    return int(recovered::runBt(host, program.lengthMode));
}
BtProgram multiProgram(const Input &input,const MultiProgram &in,recovered::MultiFactoryResult *descriptor) {
    require(in.contextBatch238>=0,PN2S_INVALID,"Multi context batch must be nonnegative");
    std::vector<recovered::ValuePart> parts;double totalArea=0;
    for(const auto &p:input.parts) {
        recovered::ValuePart v;auto b=bounds(p.polygon);
        v.area=material_area(p);v.bboxArea=(b.x1-b.x0)*(b.y1-b.y0);v.quantity=p.quantity;
        totalArea+=v.area*v.quantity;
        for(size_t j=0;j<p.polygon.size();++j){auto a=p.polygon[j],z=p.polygon[(j+1)%p.polygon.size()];v.perimeterField+=std::hypot(z.x-a.x,z.y-a.y);}
        for(const auto &hole:p.holes)for(size_t j=0;j<hole.size();++j){const auto a=hole[j],b=hole[(j+1)%hole.size()];v.perimeterField+=std::hypot(b.x-a.x,b.y-a.y);}
        for(auto pose:p.poses){const auto box=bounds(transform(p.polygon,p.pivot,pose));
            const Point size{box.x1-box.x0,box.y1-box.y0};v.variantSizes.push_back(size);
            v.maxWidth=std::max(v.maxWidth,size.x);v.maxHeight=std::max(v.maxHeight,size.y);
        }
        parts.push_back(std::move(v));
    }
    const auto mean=recovered::effectiveAverage(parts,totalArea,{in.contextField218,in.contextField220});
    auto height=[&](recovered::ReferenceKind kind){auto b=bounds(recovered::referenceShape(mean,input.height,kind));return b.y1-b.y0;};
    recovered::MultiFactoryInput f;f.contextBatch238=in.contextBatch238;f.config=in.config;f.currentReference=in.currentReference;
    f.normalHeight=height(recovered::ReferenceKind::Normal);
    f.currentHeight=in.referenceGeometry ? bounds(*in.referenceGeometry).y1-bounds(*in.referenceGeometry).y0 : height(in.currentReference);
    recovered::Random rng=in.initialRandom?*in.initialRandom:recovered::Random(input.randomSeed);auto d=recovered::multiFactory(rng,f,parts,totalArea);
    for(double v:d.evaluationValues)require(std::isfinite(v)&&v>0,PN2S_INVALID,"Multi factory generated invalid weights");
    BtProgram p;p.initialRandom=rng;p.combineGenerators=true;p.evaluationValues=d.evaluationValues;p.reference=d.reference;
    if(in.referenceGeometry && f.currentHeight!=f.normalHeight)p.referenceGeometry=in.referenceGeometry;
    p.dynamicQueue=in.dynamicQueue;
    p.direction10=BtProgram::Direction10{in.contextField218,in.contextField220,d.batch,d.count10,d.complexity.alpha,d.complexity.gamma,in.config.perimeterProbability,in.config.perimeterScale};
    p.direction14=BtProgram::Direction14{in.contextField218,in.contextField220,d.batch};
    p.extremal1c=BtProgram::Extremal1c{d.batch,d.count1c,in.fixedFront,in.forceProcessInspection};
    p.targetField14b8=in.targetField14b8;p.queueAggregate20=in.queueAggregate20;p.maximumLengthX=in.maximumLengthX;p.lengthMode=in.lengthMode;
    p.replayPrediction=in.replayPrediction;p.filterReplayAtFront=in.filterReplayAtFront;
    if(descriptor)*descriptor=std::move(d);
    return p;
}
Output ordinaryMulti(const Input &input,const MultiProgram &program,Budget &budget,BtTrace *trace) {
    require(ordinaryPreparedDomain(input),PN2S_UNSUPPORTED,"MultiStrategy requires prepared finite-grid rings and zero spacing");
    return ordinaryBt(input,multiProgram(input,program),budget,trace);
}
int ordinaryMultiContinue(const Input &input,const MultiProgram &program,Budget &budget,OrdinaryState &state,BtTrace *trace) {
    require(ordinaryPreparedDomain(input),PN2S_UNSUPPORTED,"MultiStrategy requires prepared finite-grid rings and zero spacing");
    return ordinaryBtContinue(input,multiProgram(input,program),budget,state,trace);
}
}
