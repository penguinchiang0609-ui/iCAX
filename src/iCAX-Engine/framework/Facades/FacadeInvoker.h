#pragma once

#include "FacadeRegistry.h"
#include "Task/Task.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace iCAX::Interaction
{
    class CFacadeEndpoint;
    struct CFacadeFrame;

    /*
    * @brief 一个运行端的 Facade 调用器。
    * @details 既调用本端已注册 Facade，也可以异步调用对端 Facade，并处理双向调用帧。
    */
    class _FACADES_EXP CFacadeInvoker final
    {
    public:
        using RemoteTask = iCAX::Tasks::Task<CInvocationResult>;
        using RemoteReportHandler = std::function<void(const CInvocationResult&)>;
        using EventHandler = std::function<void(const CFacadeFrame&)>;

    private:
        struct CPendingRemoteCall final
        {
            CFacadeMethod Method;
            iCAX::Tasks::TaskCompletionSource<CInvocationResult> CompletionSource;
            RemoteReportHandler ReportHandler;
        };

    public:
        explicit CFacadeInvoker(IN std::shared_ptr<CFacadeRegistry> pRegistry_);
        ~CFacadeInvoker() = default;

        CFacadeInvoker(IN const CFacadeInvoker&) = delete;
        CFacadeInvoker& operator=(IN const CFacadeInvoker&) = delete;

        CInvocationResult Invoke(
            IN const CInvocation& Call_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) const;

        /*
        * @brief 异步调用对端提供的 Facade。
        * @return 表示远端结果的 Task。
        * @details
        *   本方法只发送请求，不阻塞等待。Response 只负责完成 Task，Facades 不创建线程、
        *   不替调用方决定 continuation 的执行线程。CallRemote() 会把 Scheduler_ 绑定到
        *   初始 TaskCompletionSource，后续 ContinueWith() 默认继承它。Scene 范围调用应
        *   传入所属 Universe 的 Engine Task scheduler。Report handler
        *   在调用 DispatchAvailableFrames() 的线程同步执行。
        */
        RemoteTask CallRemote(
            IN const CFacadeEndpoint& Endpoint_,
            IN const CFacadeMethod& Method_,
            IN const std::vector<uint8_t>& Payload_,
            IN RemoteReportHandler ReportHandler_ = {},
            IN iCAX::Tasks::TaskSchedulerPtr Scheduler_ = iCAX::Tasks::DefaultScheduler()) const;

        /*
        * @brief 处理本端已经到达的全部 Facade 帧。
        * @details Request 调用本端 Facade；Report/Response 完成此前发往对端的异步调用。
        */
        size_t DispatchAvailableFrames(
            IN const CFacadeEndpoint& Endpoint_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) const;

        void SetEventHandler(IN EventHandler Handler_);

    private:
        static CInvocation ToInvocation(IN const CFacadeFrame& Frame_);
        static CInvocationResult ToInvocationResult(IN const CFacadeFrame& Frame_, IN const CFacadeMethod& Method_);
        static void SendFacadeReport(
            IN const CFacadeEndpoint& Endpoint_,
            IN const CFacadeFrame& RequestFrame_,
            IN const std::vector<uint8_t>& Payload_);
        static void SendInvocationResult(
            IN const CFacadeEndpoint& Endpoint_,
            IN const CFacadeFrame& RequestFrame_,
            IN const CInvocationResult& Result_);

        void DispatchRemoteReport(IN const CFacadeFrame& Frame_) const;
        void DispatchRemoteResult(IN const CFacadeFrame& Frame_) const;
        void DispatchEvent(IN const CFacadeFrame& Frame_) const;
        uint64_t AllocateCallID() const noexcept;

    private:
        std::shared_ptr<CFacadeRegistry> m_pRegistry;
        mutable std::mutex m_RemoteCallMutex;
        mutable std::unordered_map<uint64_t, CPendingRemoteCall> m_PendingRemoteCalls;
        mutable std::atomic_uint64_t m_nNextCallID = 1;
        mutable std::mutex m_EventHandlerMutex;
        EventHandler m_EventHandler;
    };
}
