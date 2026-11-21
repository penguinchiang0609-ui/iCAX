#pragma once
#include "recovered_pool.hpp"
#include "recovered_offset.hpp"
#include <map>

namespace study::recovered {
struct StructuringElement;
// 455d70. Zero returns zero after consuming two words; 4542c0 handles its
// full-range case separately, without calling this helper.
uint64_t boundedDraw(Random &,uint64_t exclusiveLimit);
void resetTrialRandom(Random &,double config178,uint32_t salt); // 3c5fb0, retains inactive bank/mask

struct TrialManagerState {
    int32_t count60=0;
    double budget58=0,totalC8=0,meanD0=0;
};
int32_t beginConstruction(TrialManagerState &); // 442be0 locked section
void accountConstruction(TrialManagerState &,double elapsed); // 44e500 locked section
double warmStartFraction(const TrialManagerState &); // phase 1 in 442be0
int64_t constructionLengthBound(int64_t worstCorrected,double worstShake,double currentShake,double factor);
double constructionSeconds(int64_t ticks);

struct TrialNesting {
    virtual ~TrialNesting()=default;
    uint32_t seed13b8=0;
    int32_t trial13bc=0;
    int64_t bound14a8=0;
    double goal14d0=0,goal14d8=0,shake14e8=0;
    bool goal14e0=false;
    std::shared_ptr<const StructuringElement> element14c0;
};
struct TimingKey {
    int32_t stage=0;uint8_t variant=0;
    auto operator<=>(const TimingKey &)const=default;
};
struct TrialRecord {
    std::shared_ptr<TrialNesting> nesting;
    int64_t rawLength=0;
    std::map<TimingKey,double> timings;
    int64_t field28=0;
    int32_t sourceId=0,field34=-1;
    CorrectedLengthCache corrected;
};
using TrialRecordPtr=std::shared_ptr<TrialRecord>;
void recordTiming(TrialRecord &,TimingKey,double); // 4428f0: replaces the map
bool warmStartEligible(const TrialRecord &,bool idOver10); // 455ae0
bool shakeHistoryEligible(TrialRecord &); // 455b30, inserts missing timing key
std::optional<size_t> selectWarmStart(std::span<const TrialRecordPtr>,double fraction,bool idOver10,uint32_t salt); // 4542c0 selection
bool shouldShake(std::span<const TrialRecordPtr>,const std::shared_ptr<TrialNesting> &,
    const std::function<TrialRecordPtr(const std::shared_ptr<TrialNesting> &)> &evaluate,
    const std::function<bool(const TrialRecordPtr &,const TrialRecordPtr &)> &better); // 454020

// Geometry, strategy creation, history and shake are explicit providers until
// their concrete types are recovered. This contract never silently substitutes
// a different nesting strategy. Callers retain snapshots through shared owners.
class ConstructionHost {
public:
    virtual ~ConstructionHost()=default;
    virtual TrialManagerState &manager()=0;
    virtual std::shared_ptr<TrialNesting> &current()=0;
    virtual void lockManager()=0;
    virtual void unlockManager()=0;
    virtual void lockSupervisor()=0;
    virtual void unlockSupervisor()=0;
    virtual int32_t managerId()=0;
    virtual double config4e0()=0;
    virtual void preprocess()=0; // 3cd6e0
    virtual void finalizeGeometry()=0; // 3ccef0
    virtual int64_t rawLength(const TrialNesting &)=0;
    virtual void evaluate(const TrialRecordPtr &)=0;
    virtual CandidatePool<TrialRecordPtr>::Inserted notifyLocal(const TrialRecordPtr &)=0;
    virtual CandidatePool<TrialRecordPtr>::Inserted notifyGlobal(const TrialRecordPtr &)=0;
    virtual void publishBest(const TrialRecordPtr &)=0;
    virtual bool hasBestCallback()=0;
    virtual int64_t clockTicks()=0;
    virtual double preciseSeconds()=0;
    virtual void resetCurrent()=0;
    struct Best {double metric=0,height=0;int64_t rawLength=0;};
    virtual std::optional<Best> bestGoal()=0;
    virtual void resetRandom(int32_t salt)=0;
    virtual int phase()=0; // Supervisor base+1c8 (wrapper+1d0), distinct from its assigned-trial count.
    virtual void prepareWarmStart(double fraction,bool idOver10)=0; // 4542c0
    struct Worst {bool hasGroups=false;int64_t corrected=0;double shake=0;};
    virtual Worst worstCandidate()=0;
    virtual double evaluatorFactor()=0; // absent evaluator => zero
    virtual void runPreNester()=0;
    virtual void prepareModules()=0;
    virtual bool useHistory()=0;
    virtual uint32_t prepareHistory()=0;
    virtual void createStrategy()=0;
    virtual void attachStop()=0;
    virtual void runStrategy()=0;
    virtual void finalizeStrategy()=0;
    virtual void completeHistory(uint32_t)=0;
    virtual bool mayShake()=0; // 454020
    virtual bool shake()=0; // 64ff50
    virtual void logTrial(const TrialRecordPtr &,double elapsed)=0;
    virtual void releaseCurrentResources()=0;
    virtual std::shared_ptr<TrialNesting> acquireNesting()=0;
};
struct PreparedRecord {TrialRecordPtr record;bool localInserted=false;};
PreparedRecord prepareRecord(ConstructionHost &,double elapsed); // 44e500 -> 446510 -> 446340
struct ConstructionRun {
    TrialRecordPtr record;
    bool localInserted=false,globalInserted=false,newBest=false,replacedNesting=false;
    bool shakeAttempted=false,shakeSucceeded=false;
};
ConstructionRun runConstruction(ConstructionHost &); // 442be0, providers above are required
}
