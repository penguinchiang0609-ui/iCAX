#include "recovered_shaker_iteration.hpp"
#include <bit>
#include <immintrin.h>
namespace study::recovered {
namespace {
double add(double a,double b){return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double sub(double a,double b){return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double mul(double a,double b){return _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(a),_mm_set_sd(b)));}
double negative(double a){return std::bit_cast<double>(std::bit_cast<uint64_t>(a)^0x8000000000000000ULL);}
int32_t increment(int32_t a){return std::bit_cast<int32_t>(uint32_t(a)+1);}
}
ShakerModelState shakerCreateModel(const ShakerPolicy &policy,double window,double angularWindow,
    bool rotation,double broadRadius,const std::function<void()> &resetSolver){
    ShakerModelState state;state.policy=policy;auto &p=state.problem;
    p.policy0=policy.bytes[0];p.policy1=policy.bytes[1];p.slack49=policy.bytes[2];p.policy3=policy.bytes[3];p.policy20=policy.bytes[0x20];
    p.window=window;p.angularWindow=angularWindow;p.rotation=rotation;
    state.toleranceLimit=policy.get<double>(0x28);state.broadRadius=broadRadius;
    state.finalize.directionMultiplicities={-1,-1};
    const double nan=std::bit_cast<double>(uint64_t(0x7ff8000000000000));
    state.finalize.ranges.fill(nan);state.report.objective=nan;state.report.solverOption1B8=6000;
    resetSolver();return state;
}
int32_t shakerIterate(std::span<ShakerPart> parts,ShakerIterationState &state,
    const ShakerIterationOptions &options,const ShakerIterationRuntime &runtime){
    state.stopped=false;state.seconds={};state.success=false;state.objective=0;state.iterations=1;
    runtime.initializeEnvironment();
    const auto time=[&](){return double(runtime.clockNanoseconds()/1000000)/1000;};const double start=time();
    ShakerPolicy policy;policy.bytes[0]=state.policy0;policy.bytes[1]=state.policy1;
    policy.put<double>(0x28,1e-6);policy.put<int32_t>(0x30,1);
    const double initialTolerance=mul(runtime.sceneSpan(),1e-6);
    const double translationRadius=mul(options.translationWindow,1.001);
    double best=0;
    const auto solve=[&](bool rotation,std::optional<double> admission,double seconds){
        ShakerIterationRequest request;request.policy=policy;request.rotation=rotation;
        request.window=rotation?options.rotationWindow:options.translationWindow;request.angularWindow=options.angularWindow;
        const double offset=state.policy0?negative(state.objective):0;
        request.broadRadius=rotation?add(mul(options.rotationWindow,1.001),offset):add(offset,translationRadius);
        const auto result=runtime.solve(request,admission,seconds);
        // MINSD selects its second operand for equality and unordered inputs.
        if(state.policy0&&result.status==0)state.objective=result.objective<state.objective?result.objective:state.objective;
        state.seconds[0]=add(result.seconds[0],state.seconds[0]);
        state.seconds[2]=add(result.seconds[2],state.seconds[2]);
        state.seconds[1]=add(result.seconds[1],state.seconds[1]);
        runtime.finishStep();return result;
    };
    auto result=solve(false,initialTolerance,options.seconds);
    if(result.status==2&&options.retryStatus2){
        policy.put<double>(0x28,std::bit_cast<double>(uint64_t(0x3f2a36e2eb1c432d)));
        result=solve(false,initialTolerance,options.seconds);
    }
    // The retry changes policy+28, but NOT the 1e-6 multiplier in XMM7.
    const double firstCheckTolerance=mul(runtime.sceneSpan(),1e-6);
    int32_t status=result.status;
    const auto save=[&](){state.saved.resize(parts.size());for(size_t n=0;n<parts.size();++n)state.saved[n]={parts[n].position,parts[n].angle};};
    if(status==0){
        const int32_t accepted=options.seconds<0?1:runtime.checkPlacement(options.seconds,firstCheckTolerance);
        if(accepted!=1)status=accepted==0?2:1;
        else {
            state.success=true;best=result.objective;save();
            if(runtime.callback&&runtime.callback(best))state.stopped=true;
            double remaining=sub(options.seconds,sub(time(),start));
            const bool below=state.iterations<options.maximumIterations;
            state.iterations=increment(state.iterations);int32_t stalled=0;
            if(below&&!state.stopped)while(remaining>0){
                policy.put<int32_t>(0x30,state.iterations);
                if(options.rotate){
                    result=solve(true,std::nullopt,remaining);
                    if(result.status)break;
                    if(runtime.callback&&runtime.callback(best))break; // original does not set stopped here
                    state.iterations=increment(state.iterations);runtime.clearConvolutionCache();
                    remaining=sub(options.seconds,sub(time(),start));
                }
                policy.put<int32_t>(0x30,state.iterations);
                if(remaining<0)break;
                result=solve(false,std::nullopt,remaining);if(result.status)break;
                if(runtime.callback&&runtime.callback(best))state.stopped=true;
                bool significant=false;
                if(best>result.objective){save();significant=sub(best,options.improvementThreshold)>result.objective;best=result.objective;}
                if(significant)stalled=0;
                else {stalled=increment(stalled);if(stalled>=options.stallLimit)break;}
                remaining=sub(options.seconds,sub(time(),start));
                if(remaining<=0)break;
                const bool more=state.iterations<options.maximumIterations;
                state.iterations=increment(state.iterations);
                if(!more||state.stopped)break;
            }
        }
    }
    if(state.success){
        for(size_t n=0;n<parts.size();++n){auto &p=parts[n];p.geometryValid30=0;p.position=state.saved[n].position;p.angle=state.saved[n].angle;runtime.refreshPart(n);}
        if(runtime.checkPlacement(-1,initialTolerance)==0){status=2;state.success=false;}
    }
    state.objective=best;
    runtime.statistics(sub(time(),start),state.seconds,state.iterations);return status;
}
}
