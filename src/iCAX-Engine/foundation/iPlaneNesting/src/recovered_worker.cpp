#include "recovered_worker.hpp"
#include <bit>
namespace study::recovered {
namespace {
int64_t originalTruncate64(double value) {
    if(!std::isfinite(value) || value>=9223372036854775808. || value<-9223372036854775808.)return INT64_MIN;
    return static_cast<int64_t>(value);
}
}
bool fastEnding(const FastEndInput &input) {
    double trim=input.trim*1000000.;
    trim=trim<0.?trim-.5:trim+.5;
    const int64_t length=std::bit_cast<int64_t>(static_cast<uint64_t>(input.maximumRight)-static_cast<uint64_t>(originalTruncate64(trim)));
    if(!(static_cast<double>(length)/1000000.<input.referenceLength*1.00001))return false;
    if(input.heads48>=2 || input.context240 || input.config3c)return false;
    return ((input.target14b8-input.queue20)+input.placedArea)*.9999999<input.placedArea;
}
double workerRemaining(int64_t ticks,double start,double budget) {
    return budget-(static_cast<double>(ticks/1000000)/1000.-start);
}
WorkerRun runNesterWorker(NesterWorkerHost &host) {
    WorkerRun out;
    for(;;) {
        if(host.stopped()){out.reason=WorkerEnd::StopCallback;return out;}
        if(host.fastEnded()){out.reason=WorkerEnd::FastEnd;return out;}
        const auto manager=host.nextManager();
        if(!manager){out.reason=WorkerEnd::NoStrategy;return out;}
        host.construct(*manager);++out.attempts;
        if(host.remaining()<0.){out.reason=WorkerEnd::TimeExpired;return out;}
        if(host.inspectFastEnd())if(auto candidate=host.bestCandidate())if(fastEnding(*candidate))host.markFastEnd();
    }
}
ShakerTask nextShakerTask(size_t &cursor,size_t count,int64_t ticks,double start,double total,double cap,const std::function<bool()> &stop) {
    const double left=workerRemaining(ticks,start,total);
    // SSE minsd chooses its second operand on unordered comparisons and ties.
    ShakerTask task{{},left<cap?left:cap};
    if(cursor==count || !(task.seconds>0.))return task;
    if(cursor>count)throw std::invalid_argument("shaker queue cursor out of range");
    if(stop && stop())return task;
    task.index=cursor++;return task;
}
}
