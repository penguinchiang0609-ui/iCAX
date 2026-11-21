#include "recovered_column.hpp"
#include "recovered_bt.hpp"
namespace study::recovered {
double columnTrialScore(const ColumnScoreInput &input) {
    const double freeArea=(double(input.pitchX)/1e6)*(double(input.currentGridCount)/1000.);
    const double loss=input.baselineFree100-freeArea;
    const double height=input.sheetHeight-input.referenceHeight;
    const double length=loss/height;
    double bonus=0;
    // COMISD/JAE regards an unordered comparison as passing both boundaries.
    if(input.boostEnabled && !(input.boostLow>=length) && !(length>=input.boostHigh))bonus=input.boost;
    if(input.reportedCount==1)bonus-=input.monoPenalty98;
    double result=loss/((bonus+1.)*input.trialValue);
    result*=input.totalValueA8-input.baselineValue108;
    result+=input.initialFreeB0-input.baselineFree100;
    result/=height;
    if(input.enforceRemainingFront) {
        const double occupied=double(rawCandidateLength(input.maximumRight,input.trim70))/1e6;
        const double lower=input.remainingWidth1b8*.9+occupied;
        result=result>lower?result:lower; // MAXSD takes the second operand on NaN/equality.
    }
    return result;
}
bool evaluateColumnTrial(ColumnTrialHost &host,ColumnTrialState &state) {
    try {
        const int reported=host.run();
        if(reported==0){host.restore();return false;}
        auto input=host.measure();input.reportedCount=reported;
        auto trial=btCapture(host.placements(),reported);
        input.trialValue=host.value(trial);
        if(input.enforceRemainingFront)host.remainingFront(input);
        const double score=columnTrialScore(input);
        host.restore();
        if(state.hasBest11a && !(score<state.score120))return false;
        state.hasBest11a=true;state.score120=score;state.placements128=std::move(trial);return true;
    }catch(...){host.restore();throw;}
}
ColumnRandomEvaluation columnRandomEvaluation(double amplitude) {
    ColumnRandomEvaluation result;result.amplitude=amplitude;result.minimum=1.-amplitude;
    result.inverse=1./result.minimum;result.normalization=std::pow(result.inverse,result.inverse);
    result.maximum=result.inverse;return result;
}
double columnRandomValue(const ColumnRandomEvaluation &state,Random &random,const std::function<double()> &base) {
    const double span=state.maximum-state.minimum;
    const double factor=std::pow(random.unit()*span+state.minimum,state.inverse);
    const double value=base();return value*(factor/state.normalization);
}
}
