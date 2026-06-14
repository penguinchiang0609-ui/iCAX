#pragma once
#include "Threading.h"

namespace iCAX
{
    namespace Threading
    {
        enum class TaskStatus
        {
            kTaskPending,
            kTaskCompleted,
            kTaskFaulted,
            kTaskCanceled
        };
    }
}