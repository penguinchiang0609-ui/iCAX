#pragma once

#include <Task/Task.h>
#include <algorithm>
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
}
