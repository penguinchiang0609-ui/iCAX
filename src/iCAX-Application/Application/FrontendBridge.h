#pragma once

#include "UIContainer/FrontendBridgeContract.h"

#include <memory>
#include <string>

namespace iCAX
{
    namespace Application
    {
        class CApplicationRuntime;
    }

    namespace Application
    {
        using CFrontendFacadeFrame = iCAX::Frontend::CFrontendFacadeFrame;
        using FrontendFacadeFrameHandler = iCAX::Frontend::FrontendFacadeFrameHandler;

        /*
        * @brief 前端桥接器。
        * @details
        *   CFrontendBridge 是 Application 级别的 UI 桥，不属于任何具体 UI 技术。
        *   它在 UI 与 Engine 之间双向传递 Facade request/report/response/event。
        *   H5/CEF、WPF、QT 等 UI 宿主都应复用该桥接入后台。
        */
        class CFrontendBridge final : public iCAX::Frontend::IFrontendBridge
        {
        public:
            CFrontendBridge();
            ~CFrontendBridge();

            CFrontendBridge(const CFrontendBridge&) = delete;
            CFrontendBridge& operator=(const CFrontendBridge&) = delete;

        public:
            /*
            * @brief 绑定已经启动的 ApplicationRuntime。
            * @param [in] Runtime_ 已启动的 ApplicationRuntime。
            * @throws std::logic_error Engine 未运行时抛出。
            * @details Attach 后会登记 application channel 的前端 Facade endpoint。
            */
            void Attach(iCAX::Application::CApplicationRuntime& Runtime_);

            /*
            * @brief 解除 ApplicationRuntime 绑定，并清空所有已登记 endpoint。
            */
            void Detach();

            /*
            * @brief 当前是否已经绑定 ApplicationRuntime。
            */
            bool IsAttached() const override;

            /*
            * @brief 获取应用级 channel id 文本。
            */
            std::string GetApplicationChannelIDText() const override;

            /*
            * @brief 登记产品通信通道。
            * @param [in] strProductID_ 产品 ID。
            * @return 产品 channel id 文本。
            * @details 该方法只登记 endpoint，不负责启动产品；产品启动应走 App.StartProduct 命令。
            */
            std::string RegisterProductChannel(const std::string& strProductID_) override;

            /*
            * @brief 登记 Scene 通信通道。
            * @param [in] strProjectID_ Project ID 文本。
            * @param [in] strSceneID_ Scene ID 文本。
            * @return Scene channel id 文本。
            */
            std::string RegisterSceneChannel(
                const std::string& strProjectID_,
                const std::string& strSceneID_) override;

            /*
            * @brief 从 UI 向指定 Facade channel 投递调用帧。
            */
            void PostFacadeFrame(const CFrontendFacadeFrame& Frame_) override;

            /*
            * @brief 轮询所有已登记 endpoint，取出 ApplicationRuntime 发给 UI 的 Facade 帧。
            */
            std::vector<CFrontendFacadeFrame> PollFacadeFrames() override;

            /*
            * @brief 设置收到 ApplicationRuntime Facade 帧时的回调。
            * @details 如果设置了回调，PollFacadeFrames 仍返回帧，同时逐帧调用回调。
            */
            void SetFacadeFrameHandler(FrontendFacadeFrameHandler Handler_) override;

            /*
            * @brief 获取只由前端 event loop 消费的 Front Task 调度器。
            */
            iCAX::Tasks::TaskSchedulerPtr GetFrontTaskScheduler() const override;

            /*
            * @brief 在当前前端线程消费 Front Task 队列。
            */
            std::size_t RunFrontTasks() override;

            void PauseFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase& handle_) override;
            void ResumeFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase& handle_) override;
            void CancelFrontCoroutine(const iCAX::Coroutines::CCoroutineHandleBase& handle_) override;
            bool IsFrontCoroutineRunning(const iCAX::Coroutines::CCoroutineHandleBase& handle_) const override;
            std::size_t RunFrontCoroutines() override;

        protected:
            iCAX::Coroutines::CCoroutineRuntime& FrontCoroutineRuntime() override;

        private:
            class Impl;
            std::unique_ptr<Impl> m_pImpl;
        };
    }
}
