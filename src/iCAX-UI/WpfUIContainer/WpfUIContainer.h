#pragma once

#include "UIContainer/UIContainer.h"

#include <memory>

namespace iCAX
{
    namespace Frontend
    {
        /*
        * @brief WPF UI 容器。
        * @details
        *   该类型是 native IUIContainer 的实现，内部通过 C++/CLI 启动 WPF 窗口。
        *   它让 Application.exe 保持 native C++ 主体，同时把前端技术切换为 WPF。
        */
        class CWpfUIContainer final : public IUIContainer
        {
        public:
            CWpfUIContainer();
            ~CWpfUIContainer() override;

            CWpfUIContainer(const CWpfUIContainer&) = delete;
            CWpfUIContainer& operator=(const CWpfUIContainer&) = delete;

        public:
            void SetConfig(const CUIContainerConfig& Config_) override;
            void Start() override;
            void Stop() override;
            void WaitForExit() override;
            bool IsRunning() const override;

        private:
            class Impl;
            std::unique_ptr<Impl> m_pImpl;
        };
    }
}
