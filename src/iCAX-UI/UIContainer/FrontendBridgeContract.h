#pragma once

#include "UIContainerExport.h"
#include "Task/Coroutine.h"
#include "Task/TaskScheduler.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace iCAX
{
    namespace Frontend
    {
        inline constexpr uint16_t kFrontendFacadeRequest = 0;
        inline constexpr uint16_t kFrontendFacadeReport = 1;
        inline constexpr uint16_t kFrontendFacadeResponse = 2;
        inline constexpr uint16_t kFrontendFacadeEvent = 3;

        /*
        * @brief UI 与 Engine 之间传递的 Facade 调用帧。
        * @details
        *   该结构只表达 Facade 调用边界，不绑定 H5/CEF/WPF/QT 等具体前端技术。
        *   PayloadText 是 UTF-8 文本，通常是 Data::VariantSerializer 产生的文本。
        */
        struct _UI_CONTAINER_EXP CFrontendFacadeFrame final
        {
            std::string ChannelID;    //!< 目标 channel id。
            uint64_t nCallID = 0;     //!< 一次调用的 ID；request/report/response 全程保持不变。
            uint64_t nMethodCode = 0; //!< FacadeName.MethodName 的 64 位紧凑编码。
            uint16_t nKind = kFrontendFacadeRequest;
            uint16_t nStatus = 0;
            std::string PayloadText;  //!< UTF-8 文本 payload。
        };

        using FrontendFacadeFrameHandler = std::function<void(const CFrontendFacadeFrame&)>;

        /*
        * @brief UI 容器依赖的前端桥接口。
        * @details
        *   UI 容器不能反向链接 Application.exe。Application.exe 持有具体实现，
        *   并通过该接口双向传递 Facade 调用帧。
        */
        class _UI_CONTAINER_EXP IFrontendBridge
        {
        public:
            virtual ~IFrontendBridge() = default;

        public:
            virtual bool IsAttached() const = 0;
            virtual std::string GetApplicationChannelIDText() const = 0;
            virtual std::string RegisterProductChannel(const std::string& strProductID_) = 0;
            virtual std::string RegisterSceneChannel(
                const std::string& strProjectID_,
                const std::string& strSceneID_) = 0;
            virtual void PostFacadeFrame(const CFrontendFacadeFrame& Frame_) = 0;
            virtual std::vector<CFrontendFacadeFrame> PollFacadeFrames() = 0;
            virtual void SetFacadeFrameHandler(FrontendFacadeFrameHandler Handler_) = 0;

            /*
            * @brief 获取 Front Task 调度器。
            * @details
            *   Scheduler 只负责入队；UI 容器在自己的前端线程调用 RunFrontTasks() 消费。
            *   前端原生 Task/TaskCompletionSource 构造时绑定本 scheduler，后续 ContinueWith()
            *   会默认继承前端线程调度语义。
            */
            virtual iCAX::Tasks::TaskSchedulerPtr GetFrontTaskScheduler() const = 0;

            /*
            * @brief 在当前前端线程执行所有已入队 Front Task。
            * @return 本次执行的任务数量。
            */
            virtual std::size_t RunFrontTasks() = 0;

            /*
            * @brief 启动和控制前端 Coroutine。
            * @details
            *   页面、窗口或原生 UI 模块自行保存返回的 handle，并在禁用、启用、销毁时分别
            *   调用 PauseFrontCoroutine、ResumeFrontCoroutine、CancelFrontCoroutine。
            *   下列 API 以及 RunFrontCoroutines 都只允许在前端线程调用。
            */
            template<typename TResult>
            iCAX::Coroutines::CCoroutineHandle<TResult> StartFrontCoroutine(
                iCAX::Coroutines::CCoroutine<TResult> coroutine_)
            {
                return FrontCoroutineRuntime().Start(std::move(coroutine_));
            }

            virtual void PauseFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase& handle_) = 0;
            virtual void ResumeFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase& handle_) = 0;
            virtual void CancelFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase& handle_) = 0;
            virtual bool IsFrontCoroutineRunning(const iCAX::Coroutines::CCoroutineHandleBase& handle_) const = 0;

            /*
            * @brief 在当前前端线程推进一帧 Front Coroutine。
            * @details UI loop 应先调用 RunFrontTasks()，再调用本方法。
            * @return 本帧恢复执行的协程数量。
            */
            virtual std::size_t RunFrontCoroutines() = 0;

        protected:
            virtual iCAX::Coroutines::CCoroutineRuntime& FrontCoroutineRuntime() = 0;
        };
    }
}
