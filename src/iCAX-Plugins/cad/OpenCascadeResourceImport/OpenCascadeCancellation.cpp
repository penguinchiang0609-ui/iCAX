#include "pch.h"
#include "OpenCascadeCancellation.h"
#include "Task/TaskStatus.h"
#include <BRepBuilderAPI_MakeShape.hxx>
#include <Message_ProgressIndicator.hxx>
#include <utility>

namespace iCAX::OpenCascade
{
    namespace
    {
        thread_local const COpenCascadeCancellationScope::CancellationCheck* s_Check = nullptr;

        class CShutdownProgress final : public Message_ProgressIndicator
        {
        public:
            explicit CShutdownProgress(COpenCascadeCancellationScope::CancellationCheck Check_)
                : m_Check(std::move(Check_)) {}
        protected:
            bool UserBreak() override { return m_Check && m_Check(); }
            void Show(const Message_ProgressScope&, bool) override {}
        private:
            const COpenCascadeCancellationScope::CancellationCheck m_Check;
        };
    }

    COpenCascadeCancellationScope::COpenCascadeCancellationScope(CancellationCheck Check_)
        : m_Check(Check_ ? std::move(Check_) : Capture()), m_Previous(s_Check)
    {
        s_Check = &m_Check;
    }

    COpenCascadeCancellationScope::~COpenCascadeCancellationScope() { s_Check = m_Previous; }

    COpenCascadeCancellationScope::CancellationCheck COpenCascadeCancellationScope::Capture()
    {
        return s_Check ? *s_Check : CancellationCheck{};
    }

    void COpenCascadeCancellationScope::ThrowIfCancellationRequested()
    {
        if (s_Check && *s_Check && (*s_Check)()) throw iCAX::Tasks::TaskCanceledException();
    }

    void COpenCascadeCancellationScope::Build(BRepBuilderAPI_MakeShape& Builder_)
    {
        ThrowIfCancellationRequested();
        const auto _Check = Capture();
        try
        {
            if (_Check)
            {
                // Keep the indicator alive until the algorithm and all of its
                // parallel workers have returned. Each build owns its root range.
                occ::handle<Message_ProgressIndicator> _Progress = new CShutdownProgress(_Check);
                Builder_.Build(_Progress->Start());
            }
            else Builder_.Build();
        }
        catch (...)
        {
            ThrowIfCancellationRequested();
            throw;
        }
        // Cancellation must not become an incomplete successful shape, nor a
        // recognition failure that allows later candidates/commits to proceed.
        ThrowIfCancellationRequested();
    }
}
