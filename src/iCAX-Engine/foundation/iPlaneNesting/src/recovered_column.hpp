#pragma once
#include "recovered_factory.hpp"
namespace study::recovered {
struct ColumnScoreInput {
    int64_t pitchX=1,currentGridCount=0,maximumRight=0;
    int32_t reportedCount=0;
    double sheetHeight=0,referenceHeight=0,trim70=0;
    double initialFreeB0=0,baselineFree100=0,totalValueA8=0,baselineValue108=0,trialValue=0;
    bool boostEnabled=false,enforceRemainingFront=false;
    double boostLow=0,boostHigh=0,boost=0,monoPenalty98=0,remainingWidth1b8=0;
};
double columnTrialScore(const ColumnScoreInput &); // arithmetic in 3efde0
struct ColumnTrialState {
    bool hasBest11a=false;
    double score120=0;
    std::vector<Placement> placements128;
};
class ColumnTrialHost {
public:
    virtual ~ColumnTrialHost()=default;
    virtual int run()=0; // 3ee340, including stop/prepare dispatch
    virtual ColumnScoreInput measure()=0; // 3cb990 and initial trial fields
    virtual std::span<const Placement> placements() const=0;
    virtual double value(std::span<const Placement>)=0;
    virtual void remainingFront(ColumnScoreInput &)=0; // only when field118 is true
    virtual void restore()=0; // 3d43b0
};
// A zero report restores before returning. Every nonzero report measures,
// captures, evaluates and restores before the strict best-score comparison.
bool evaluateColumnTrial(ColumnTrialHost &,ColumnTrialState &); // 3efde0
struct ColumnRandomEvaluation {double amplitude=0,inverse=1,normalization=1,minimum=1,maximum=1;};
ColumnRandomEvaluation columnRandomEvaluation(double amplitude); // 366900
double columnRandomValue(const ColumnRandomEvaluation &,Random &,const std::function<double()> &); // 3669d0
struct ColumnConfig {
    ColumnTrialConfig trial;
    int32_t tries=50,finalTries=20,maxMono=3,minimumBigParts=4,maximumBigParts=16,regularParts=4;
    double generationRandom=.07,minimumStop=.3,maximumStop=1.5,tooSmallRatio=.5;
    double firstRatio=.5,firstBoost=.5,monoPenaltyRatio=.5,monoPenalty=.6,biRatio=.7,biCount=4;
    int32_t multiplicity23c=1;
};
}
