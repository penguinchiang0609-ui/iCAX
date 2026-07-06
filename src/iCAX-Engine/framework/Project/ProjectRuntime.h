#pragma once

#include "Project.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace iCAX
{
    namespace Project
    {
        class IProjectRuntime;

        /*
        * @brief 项目运行时每帧回调。
        */
        using SceneRuntimeFrameHandler = std::function<void(
            IProjectRuntime&,
            iCAX::Project::ISceneContext&,
            const iCAX::Mail::CMailPostOffice&)>;

        /*
        * @brief 项目运行时接口。
        * @details
        *   当前实现是本地 CProject，接口保留未来进程外/远程项目运行时的替换空间。
        */
        class _PROJECT_EXP IProjectRuntime
        {
        public:
            virtual ~IProjectRuntime() = default;

        public:
            /*
            * @brief 获取项目 ID。
            */
            virtual const iCAX::Data::uuid& GetProjectID() const = 0;
            /*
            * @brief 获取项目主 Scene 通信通道 ID。
            */
            virtual const iCAX::Data::uuid& GetMainSceneChannelID() const = 0;
            /*
            * @brief 获取项目名称。
            */
            virtual std::string GetProjectName() const = 0;
            /*
            * @brief 获取项目路径。
            */
            virtual std::string GetProjectPath() const = 0;
            /*
            * @brief 获取启动组件类型名。
            */
            virtual std::string GetStartupComponent() const = 0;
            /*
            * @brief 项目是否仍然打开。
            */
            virtual bool IsOpen() const = 0;
            /*
            * @brief 项目主 Scene 工作线程是否运行中。
            */
            virtual bool IsRunning() const = 0;
            /*
            * @brief 获取项目状态。
            */
            virtual EProjectState GetState() const = 0;
            /*
            * @brief 获取最近一次异常信息。
            */
            virtual std::optional<CProjectFault> GetLastFault() const = 0;
            /*
            * @brief 获取项目主 Scene PDO 共享内存描述快照。
            */
            virtual CMainScenePDODescriptor GetMainScenePDODescriptor() const = 0;
            /*
            * @brief 获取前端视角项目主 Scene 邮局。
            */
            virtual iCAX::Mail::CMailPostOffice GetMainSceneFrontendPostOffice() = 0;
            /*
            * @brief 设置 runtime 每帧回调。
            */
            virtual void SetSceneFrameHandler(IN SceneRuntimeFrameHandler Handler_) = 0;
            /*
            * @brief 启动项目 runtime。
            */
            virtual void Start() = 0;
            /*
            * @brief 停止项目 runtime。
            */
            virtual void Stop() = 0;
            /*
            * @brief 关闭项目 runtime 并释放本地项目引用。
            */
            virtual void Close() = 0;

            /*
            * @brief 获取本地项目对象。
            * @return 本地项目；远程 runtime 可以返回 nullptr。
            */
            virtual std::shared_ptr<CProject> GetLocalProject() const = 0;
        };

        /*
        * @brief 本地项目运行时适配器。
        * @details
        *   包装 CProject 并转发生命周期调用。FrameHandler 会被转接到每个 Scene 的每帧回调中。
        */
        class _PROJECT_EXP CLocalProjectRuntime final : public IProjectRuntime
        {
        public:
            /*
            * @brief 构造本地项目运行时。
            * @param [in] pProject_ 本地项目，不能为空。
            */
            explicit CLocalProjectRuntime(IN std::shared_ptr<CProject> pProject_);
            ~CLocalProjectRuntime() override;

            CLocalProjectRuntime(IN const CLocalProjectRuntime&) = delete;
            CLocalProjectRuntime& operator=(IN const CLocalProjectRuntime&) = delete;

        public:
            /*
            * @brief 获取项目 ID。
            */
            const iCAX::Data::uuid& GetProjectID() const override;
            /*
            * @brief 获取项目主 Scene 通信通道 ID。
            */
            const iCAX::Data::uuid& GetMainSceneChannelID() const override;
            /*
            * @brief 获取项目名称。
            */
            std::string GetProjectName() const override;
            /*
            * @brief 获取项目路径。
            */
            std::string GetProjectPath() const override;
            /*
            * @brief 获取启动组件类型名。
            */
            std::string GetStartupComponent() const override;
            /*
            * @brief 本地项目是否仍然打开。
            */
            bool IsOpen() const override;
            /*
            * @brief 本地项目主 Scene 是否运行中。
            */
            bool IsRunning() const override;
            /*
            * @brief 获取项目状态。
            */
            EProjectState GetState() const override;
            /*
            * @brief 获取最近异常。
            */
            std::optional<CProjectFault> GetLastFault() const override;
            /*
            * @brief 获取项目主 Scene PDO 共享内存描述快照。
            */
            CMainScenePDODescriptor GetMainScenePDODescriptor() const override;
            /*
            * @brief 获取前端视角项目主 Scene 邮局。
            */
            iCAX::Mail::CMailPostOffice GetMainSceneFrontendPostOffice() override;
            /*
            * @brief 设置每帧回调。
            */
            void SetSceneFrameHandler(IN SceneRuntimeFrameHandler Handler_) override;
            /*
            * @brief 启动本地项目。
            */
            void Start() override;
            /*
            * @brief 停止本地项目。
            */
            void Stop() override;
            /*
            * @brief 关闭本地项目并清理回调。
            */
            void Close() override;
            /*
            * @brief 获取本地项目。
            */
            std::shared_ptr<CProject> GetLocalProject() const override;

        private:
            /*
            * @brief 获取每帧回调快照。
            */
            SceneRuntimeFrameHandler GetSceneFrameHandler() const;

            /*
            * @brief 获取本地项目引用。
            * @throws std::logic_error runtime 已关闭时抛出。
            */
            CProject& RequireProject() const;

        private:
            std::shared_ptr<CProject> m_pProject;
            mutable std::mutex m_FrameHandlerMutex;
            SceneRuntimeFrameHandler m_SceneFrameHandler;
        };

        /*
        * @brief 创建本地项目运行时。
        * @param [in] pProject_ 本地项目。
        * @return IProjectRuntime 接口。
        */
        _PROJECT_EXP std::shared_ptr<IProjectRuntime> CreateLocalProjectRuntime(
            IN std::shared_ptr<CProject> pProject_);
    }
}
