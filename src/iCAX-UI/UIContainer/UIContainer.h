#pragma once

#include "UIContainerExport.h"
#include "FrontendBridgeContract.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Frontend
    {
        /*
        * @brief UI 容器启动配置。
        * @details
        *   Application.exe 从配置文件读取这些字段，再交给 CUIContainerFactory 创建真实容器。
        *   ContainerType 为 headless 时使用内置无窗口容器；其他类型通常由 ModulePath 指向的 DLL
        *   静态注册到 CUIContainerFactory。
        */
        struct _UI_CONTAINER_EXP CUIContainerConfig final
        {
            IFrontendBridge* pFrontendBridge = nullptr; //!< Application 提供的通用前端桥，非拥有。
            std::string ContainerType = "headless";     //!< 容器类型，如 headless、cef、wpf、qt。
            std::string ModulePath;                     //!< 动态 UI 容器 DLL 路径；加载后通过静态注册表构造。
            std::string WebPageRoot;                    //!< H5 AppShell 根目录。
            std::string StartURL;                       //!< 可选启动 URL；为空时由具体容器决定。
            uint32_t nStartupHandshakeTimeoutMS = 5000; //!< 启动时等待 ApplicationProxy 握手的超时。
            std::vector<std::pair<std::string, std::string>> Properties; //!< 容器实现自定义参数。
        };

        /*
        * @brief UI 容器接口。
        * @details
        *   UIContainer 是 Application.exe 看到的唯一 UI 抽象。CEF、WPF、QT、Headless
        *   都可以实现该接口；它们负责启动前端运行环境，并通过 IFrontendBridge 连接 Engine。
        */
        class _UI_CONTAINER_EXP IUIContainer
        {
        public:
            virtual ~IUIContainer() = default;

            /*
            * @brief 设置容器配置。
            * @param [in] Config_ 容器配置。
            * @throws std::logic_error 容器已启动时抛出。
            */
            virtual void SetConfig(const CUIContainerConfig& Config_) = 0;

            /*
            * @brief 启动 UI 容器。
            * @details 后端 ApplicationHost 必须已经启动，FrontendBridge 必须已经 attach。
            */
            virtual void Start() = 0;

            /*
            * @brief 停止 UI 容器。
            * @details 本方法不停止 Engine。
            */
            virtual void Stop() = 0;

            /*
            * @brief 等待 UI 容器退出。
            * @details Headless 容器在启动握手完成后立即返回；窗口型容器应等待主窗口关闭或宿主退出。
            */
            virtual void WaitForExit() = 0;

            /*
            * @brief 当前容器是否运行。
            */
            virtual bool IsRunning() const = 0;
        };

        using UIContainerCreateFunction = IUIContainer* (__cdecl*)();
        using UIContainerDestroyFunction = void (__cdecl*)(IUIContainer*);
        using UIContainerExecuteSubProcessFunction = int (__cdecl*)(void*);

        /*
        * @brief UI 容器静态注册项。
        */
        struct _UI_CONTAINER_EXP CUIContainerRegistration final
        {
            std::string ContainerType;                         //!< 注册的容器类型。
            UIContainerCreateFunction pCreateFunction = nullptr; //!< 创建函数。
            UIContainerDestroyFunction pDestroyFunction = nullptr; //!< 销毁函数。
            UIContainerExecuteSubProcessFunction pExecuteSubProcessFunction = nullptr; //!< 可选子进程入口。
        };

        /*
        * @brief UI 容器实例句柄。
        * @details
        *   句柄负责保存动态 DLL 生命周期，并在析构时调用配置的 DestroyFunction。
        *   Application 启停流程仍应显式调用 Start/Stop，句柄只负责所有权释放。
        */
        class _UI_CONTAINER_EXP CUIContainerInstance final
        {
        public:
            CUIContainerInstance();
            ~CUIContainerInstance();

            CUIContainerInstance(const CUIContainerInstance&) = delete;
            CUIContainerInstance& operator=(const CUIContainerInstance&) = delete;

            CUIContainerInstance(CUIContainerInstance&& Right_) noexcept;
            CUIContainerInstance& operator=(CUIContainerInstance&& Right_) noexcept;

        public:
            IUIContainer& Get() const;
            IUIContainer* operator->() const;
            bool IsValid() const noexcept;

        private:
            friend class CUIContainerFactory;

            CUIContainerInstance(IUIContainer* pContainer_, UIContainerDestroyFunction pDestroyFunction_);
            void Reset() noexcept;

        private:
            class Impl;
            std::unique_ptr<Impl> m_pImpl;
        };

        /*
        * @brief UI 容器工厂。
        * @details
        *   工厂只负责把配置转换为 IUIContainer 实例。headless 是内置实现；
        *   CEF、WPF、QT 等真实 UI 容器应通过 ModulePath 动态加载。
        */
        class _UI_CONTAINER_EXP CUIContainerFactory final
        {
        public:
            CUIContainerFactory() = delete;

        public:
            /*
            * @brief 注册 UI 容器类型。
            * @param [in] Registration_ 注册项。
            * @return 注册成功返回 true；重复注册同一个类型返回 false。
            * @details 不提供注销。DLL 载入后的注册在进程生命周期内保持有效。
            */
            static bool Register(const CUIContainerRegistration& Registration_);

            /*
            * @brief 查询当前已经注册的 UI 容器类型。
            */
            static std::vector<std::string> ListRegisteredTypes();

            /*
            * @brief 执行 UI 容器子进程入口。
            * @param [in] Config_ UI 容器配置。
            * @param [in] pNativeInstanceHandle_ 原生进程实例句柄，Windows 下为 HINSTANCE。
            * @return 子进程返回码；当前进程不是 UI 子进程时返回 -1。
            * @details CEF 要求在主程序逻辑之前调用 CefExecuteProcess，因此 Application.exe
            *   必须先调用本方法，再启动 Engine。
            */
            static int ExecuteSubProcessIfNeeded(
                const CUIContainerConfig& Config_,
                void* pNativeInstanceHandle_);

            /*
            * @brief 根据配置创建 UI 容器实例。
            * @param [in] Config_ UI 容器配置。
            * @return UI 容器实例句柄。
            * @details 如果类型尚未注册且配置指定了 ModulePath，工厂会先加载该 DLL，触发其静态注册。
            */
            static CUIContainerInstance Create(const CUIContainerConfig& Config_);
        };

        /*
        * @brief 静态注册辅助对象。
        */
        class _UI_CONTAINER_EXP CUIContainerRegistrar final
        {
        public:
            explicit CUIContainerRegistrar(const CUIContainerRegistration& Registration_);
        };
    }
}

#define ICAX_UI_CONTAINER_DETAIL_CONCAT_INNER(a, b) a##b
#define ICAX_UI_CONTAINER_DETAIL_CONCAT(a, b) ICAX_UI_CONTAINER_DETAIL_CONCAT_INNER(a, b)

#define ICAX_REGISTER_UI_CONTAINER(containerTypeLiteral, containerClass) \
    ICAX_REGISTER_UI_CONTAINER_IMPL(containerTypeLiteral, containerClass, __COUNTER__)

#define ICAX_REGISTER_UI_CONTAINER_WITH_SUBPROCESS(containerTypeLiteral, containerClass, subProcessFunction) \
    ICAX_REGISTER_UI_CONTAINER_WITH_SUBPROCESS_IMPL(containerTypeLiteral, containerClass, subProcessFunction, __COUNTER__)

#define ICAX_REGISTER_UI_CONTAINER_IMPL(containerTypeLiteral, containerClass, uniqueID) \
    ICAX_REGISTER_UI_CONTAINER_WITH_SUBPROCESS_IMPL(containerTypeLiteral, containerClass, nullptr, uniqueID)

#define ICAX_REGISTER_UI_CONTAINER_WITH_SUBPROCESS_IMPL(containerTypeLiteral, containerClass, subProcessFunction, uniqueID) \
    namespace \
    { \
        iCAX::Frontend::IUIContainer* __cdecl ICAX_UI_CONTAINER_DETAIL_CONCAT(_CreateICAXUIContainer_, uniqueID)() \
        { \
            return new containerClass(); \
        } \
        void __cdecl ICAX_UI_CONTAINER_DETAIL_CONCAT(_DestroyICAXUIContainer_, uniqueID)(iCAX::Frontend::IUIContainer* pContainer_) \
        { \
            delete static_cast<containerClass*>(pContainer_); \
        } \
        const iCAX::Frontend::CUIContainerRegistrar ICAX_UI_CONTAINER_DETAIL_CONCAT(_ICAXUIContainerRegistrar_, uniqueID)( \
            iCAX::Frontend::CUIContainerRegistration{ \
                containerTypeLiteral, \
                &ICAX_UI_CONTAINER_DETAIL_CONCAT(_CreateICAXUIContainer_, uniqueID), \
                &ICAX_UI_CONTAINER_DETAIL_CONCAT(_DestroyICAXUIContainer_, uniqueID), \
                subProcessFunction }); \
    }

