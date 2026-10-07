#pragma once

#include "OpenCascadeResourceImportExport.h"
#include <functional>

class BRepBuilderAPI_MakeShape;

namespace iCAX::OpenCascade
{
    // A synchronous operation inherits its caller's cancellation scope. Jobs
    // moved to another thread must explicitly capture and bind the check there.
    // Checks are also called by OCC workers and must be thread-safe and nonblocking.
    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP COpenCascadeCancellationScope final
    {
    public:
        using CancellationCheck = std::function<bool()>;
        explicit COpenCascadeCancellationScope(CancellationCheck Check_ = {});
        ~COpenCascadeCancellationScope();
        COpenCascadeCancellationScope(const COpenCascadeCancellationScope&) = delete;
        COpenCascadeCancellationScope& operator=(const COpenCascadeCancellationScope&) = delete;

        static CancellationCheck Capture();
        static void ThrowIfCancellationRequested();
        // Build once, with an OCC progress indicator whose UserBreak observes
        // shutdown. Never use an eagerly executing two-shape boolean constructor.
        static void Build(BRepBuilderAPI_MakeShape& Builder_);

    private:
        CancellationCheck m_Check;
        const CancellationCheck* m_Previous = nullptr;
    };
}
