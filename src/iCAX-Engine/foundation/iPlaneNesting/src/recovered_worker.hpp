#pragma once
#include "recovered_scheduler.hpp"
namespace study::recovered {
struct FastEndInput {
    int64_t maximumRight=0;
    double trim=0,referenceLength=0;
    int32_t heads48=1;
    bool context240=false,config3c=false;
    double placedArea=0,target14b8=0,queue20=0;
};
// 444010: this is the supplier's early-ending predicate, not an optimality proof.
bool fastEnding(const FastEndInput &);
double workerRemaining(int64_t clockTicks,double startSeconds,double budgetSeconds);
class NesterWorkerHost {
public:
    virtual ~NesterWorkerHost()=default;
    virtual bool stopped()=0;
    virtual bool fastEnded()=0;
    virtual std::optional<int> nextManager()=0;
    virtual void construct(int manager)=0;
    virtual double remaining()=0;
    virtual bool inspectFastEnd()=0;
    virtual std::optional<FastEndInput> bestCandidate()=0;
    virtual void markFastEnd()=0;
};
enum class WorkerEnd { StopCallback, FastEnd, NoStrategy, TimeExpired };
struct WorkerRun {size_t attempts=0;WorkerEnd reason=WorkerEnd::NoStrategy;};
WorkerRun runNesterWorker(NesterWorkerHost &); // 444010; construction is 442be0 dependency
struct ShakerTask {std::optional<size_t> index;double seconds=0;};
// 454fc0: caller serializes access and owns the ordered candidate snapshot.
ShakerTask nextShakerTask(size_t &cursor,size_t count,int64_t ticks,double start,double total,double cap,const std::function<bool()> &stop);
}
