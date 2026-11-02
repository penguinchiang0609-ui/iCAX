#pragma once

#include <Task/Task.h>
#include "OpenCascadeCancellation.h"
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace iCAX::OpenCascade::detail
{
    inline std::size_t GeometryConcurrency(std::size_t Requested_)
    {
        const auto _Hardware = std::thread::hardware_concurrency();
        return Requested_ == 0
            ? std::min(8u, std::max(1u, _Hardware > 1 ? _Hardware - 1 : 1u))
            : std::clamp<std::size_t>(Requested_, 1, 8);
    }

    inline iCAX::Tasks::TaskSchedulerPtr GeometryTaskScheduler()
    {
        // Shared by evaluation and BRep translation, separate from the default
        // pool because a synchronous CAD call may itself run on a default worker.
        static auto _Scheduler = std::make_shared<iCAX::Tasks::ThreadPoolTaskScheduler>(8);
        return _Scheduler;
    }

    struct SGeometryTaskBatch final
    {
        std::vector<iCAX::Tasks::Task<void>> Tasks;
        ~SGeometryTaskBatch()
        {
            // Drain even on submission/allocation failure before captured inputs
            // leave scope. Wait does not rethrow a task's exception.
            for (const auto& _Task : Tasks) _Task.Wait();
        }
        void Complete() const
        {
            for (const auto& _Task : Tasks) _Task.Wait();
            for (const auto& _Task : Tasks) _Task.Result();
        }
    };

    // Independent geometry jobs use the existing bounded CAD Task pool.
    // Nested synchronous CAD calls on that pool stay inline: Task has no
    // work stealing, so filling its workers with child waits would deadlock.
    template<class TWork>
    std::size_t ForGeometryIndices(std::size_t Count_,std::size_t MaximumConcurrency_,TWork&& Work_)
    {
        if(!Count_)return 0;
        const auto scheduler=GeometryTaskScheduler();
        const auto workers=iCAX::Tasks::CurrentScheduler()==scheduler?std::size_t{1}
            :std::min(Count_,GeometryConcurrency(MaximumConcurrency_));
        std::atomic<std::size_t> next{0};
        const auto cancellation=COpenCascadeCancellationScope::Capture();
        const auto consume=[&]{
            COpenCascadeCancellationScope scope(cancellation);
            for(auto i=next.fetch_add(1);i<Count_;i=next.fetch_add(1)){
                scope.ThrowIfCancellationRequested();Work_(i);
            }
        };
        SGeometryTaskBatch batch;batch.Tasks.reserve(workers-1);
        for(std::size_t i=1;i<workers;++i)
            batch.Tasks.push_back(iCAX::Tasks::Run(consume,scheduler));
        consume();batch.Complete();return workers;
    }
}
