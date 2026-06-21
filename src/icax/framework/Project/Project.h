#pragma once

#include "ProjectExport.h"

#include "Behaviour/IUniverse.h"
#include "Behaviour/IUniverseContext.h"
#include "Data/PropertyBag.h"
#include "Data/uuid.h"
#include "Database/IRepository.h"
#include "Database/IRepositoryEvent.h"
#include "Mailbox/MailPostOffice.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourceLoaderRegistry.h"
#include "Services/IMailChannelService.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace iCAX
{
    namespace Project
    {
        class CProject;

        enum class EProjectState : uint8_t
        {
            Created = 0,  //!< 已创建但未启动。
            Starting = 1, //!< 工作线程正在启动。
            Running = 2,  //!< 工作线程运行中。
            Stopping = 3, //!< 正在请求停止。
            Stopped = 4,  //!< 已停止。
            Faulted = 5,  //!< 工作线程异常退出。
        };

        enum class EProjectRole : uint8_t
        {
            Main = 0,      //!< 主项目。
            Transient = 1, //!< 临时/预览/导入项目。
        };

        /*
        * @brief 项目异常信息。
        */
        struct _PROJECT_EXP CProjectFault final
        {
            std::string Message; //!< 异常文本。
            std::exception_ptr Exception = nullptr; //!< 原始异常，用于调用方重新抛出或诊断。
        };

        /*
        * @brief 项目每帧回调。
        * @param [in,out] CProject& 当前项目。
        * @param [in] CMailPostOffice 后端视角项目邮局。
        */
        using ProjectFrameHandler = std::function<void(CProject&, const iCAX::Mail::CMailPostOffice&)>;

        /*
        * @brief 项目创建参数。
        */
        struct _PROJECT_EXP CProjectCreateInfo final
        {
            iCAX::Data::uuid ProjectID; //!< 项目 ID，nil 时自动生成。
            iCAX::Data::uuid ProjectMailID; //!< 项目邮箱通道 ID，nil 时自动生成。
            EProjectRole Role = EProjectRole::Main; //!< 项目角色。
            std::string ProjectName; //!< 项目名称。
            std::string ProjectPath; //!< 项目路径。
            std::string StartupComponent; //!< 启动组件类型名。
            std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings; //!< 应用设置。
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry; //!< 元数据注册表。
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry; //!< 行为注册表。
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry; //!< 项目资源加载器注册表。
            std::shared_ptr<iCAX::Services::IMailChannelService> pMailChannelService; //!< 邮件通道服务。
            uint32_t nFrameIntervalMilliseconds = 16; //!< 项目工作线程帧间隔。
            ProjectFrameHandler FrameHandler; //!< 每帧回调。
        };

        /*
        * @brief 项目运行实体。
        * @details
        *   一个 Project 拥有自己的 Repository、Universe、ResourceLibrary、mail channel 和工作线程。
        *   ProductRuntime 管理 Project 的打开/关闭，但项目数据和帧循环由 Project 自身维护。
        */
        class _PROJECT_EXP CProject final
        {
        private:
            class CRepositoryEventForwarder;

        public:
            /*
            * @brief 构造项目。
            * @param [in] CreateInfo_ 创建参数。
            * @throws std::invalid_argument 必需注册表或服务为空时抛出。
            */
            explicit CProject(IN const CProjectCreateInfo& CreateInfo_);
            ~CProject();

            CProject(IN const CProject&) = delete;
            CProject& operator=(IN const CProject&) = delete;

        public:
            /*
            * @brief 获取项目 ID。
            */
            const iCAX::Data::uuid& GetProjectID() const;

            /*
            * @brief 获取项目邮箱 ID。
            */
            const iCAX::Data::uuid& GetProjectMailID() const;

            /*
            * @brief 获取项目名称。
            */
            const std::string& GetProjectName() const;

            /*
            * @brief 设置项目名称。
            */
            void SetProjectName(IN const std::string& strName_);

            /*
            * @brief 获取项目路径。
            */
            const std::string& GetProjectPath() const;

            /*
            * @brief 设置项目路径。
            */
            void SetProjectPath(IN const std::string& strPath_);

            /*
            * @brief 获取项目角色。
            */
            EProjectRole GetRole() const;

            /*
            * @brief 是否为主项目。
            */
            bool IsMainProject() const;

            /*
            * @brief 是否为临时项目。
            */
            bool IsTransientProject() const;

            /*
            * @brief 获取启动组件类型名。
            */
            const std::string& GetStartupComponent() const;

            /*
            * @brief 判断项目是否仍然打开。
            */
            bool IsOpen() const;

            /*
            * @brief 判断项目工作线程是否运行中。
            */
            bool IsRunning() const;

            /*
            * @brief 获取项目状态。
            */
            EProjectState GetState() const;

            /*
            * @brief 获取最近一次异常信息。
            */
            std::optional<CProjectFault> GetLastFault() const;

            /*
            * @brief 设置每帧回调。
            * @param [in] Handler_ 回调；为空表示不处理帧回调。
            */
            void SetFrameHandler(IN ProjectFrameHandler Handler_);

            /*
            * @brief 获取项目 Repository。
            * @throws std::logic_error 项目已关闭时抛出。
            */
            iCAX::Database::IRepository& Database();
            const iCAX::Database::IRepository& Database() const;

            /*
            * @brief 获取项目 Universe。
            */
            iCAX::Behaviour::IUniverse& Universe();
            const iCAX::Behaviour::IUniverse& Universe() const;

            /*
            * @brief 获取项目资源库。
            */
            iCAX::Resource::CResourceLibrary& Resources();
            const iCAX::Resource::CResourceLibrary& Resources() const;

            /*
            * @brief 获取后端视角项目邮局。
            */
            iCAX::Mail::CMailPostOffice GetBackendPostOffice();

            /*
            * @brief 获取前端视角项目邮局。
            */
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice();

            /*
            * @brief 启动项目工作线程。
            */
            void Start();

            /*
            * @brief 停止项目工作线程。
            */
            void Stop();

            /*
            * @brief 绑定启动组件对应的行为。
            */
            void BindStartup();

            /*
            * @brief 帧前 PDO 交换。
            */
            void PreSwapPDO();

            /*
            * @brief 执行一帧 Universe Tick。
            */
            void Tick();

            /*
            * @brief 帧后 PDO 交换。
            */
            void PostSwapPDO();

            /*
            * @brief 关闭项目并释放 Repository/Universe/Resource/mail channel。
            */
            void Close();

        private:
            /*
            * @brief 项目工作线程入口。
            * @details
            *   启动后绑定启动行为，进入固定帧循环：
            *   PreSwapPDO -> FrameHandler -> Tick -> PostSwapPDO。
            *   任意异常会被记录为 Faulted，不向其他项目传播。
            */
            void WorkerMain();

            /*
            * @brief Repository 修改前事件转发给 Universe。
            */
            void OnRepositoryChanging(IN const iCAX::Database::RepositoryEventArgs& Args_);

            /*
            * @brief Repository 修改后事件转发给 Universe。
            */
            void OnRepositoryChanged(IN const iCAX::Database::RepositoryEventArgs& Args_);

            /*
            * @brief 设置项目状态并唤醒等待线程。
            */
            void SetState(IN EProjectState State_);

            /*
            * @brief 记录项目工作线程异常。
            */
            void RecordFault(IN const std::string& strMessage_, IN std::exception_ptr Exception_);

            /*
            * @brief 获取每帧回调快照。
            */
            ProjectFrameHandler GetFrameHandler() const;

            /*
            * @brief 确保项目仍然打开。
            * @throws std::logic_error 项目已关闭时抛出。
            */
            void EnsureOpen() const;

        private:
            mutable std::mutex m_LifecycleMutex;
            std::condition_variable m_StopCondition;
            std::thread m_WorkThread;
            std::atomic_bool m_bStopRequested = false;
            EProjectState m_State = EProjectState::Created;
            std::optional<CProjectFault> m_LastFault;
            iCAX::Data::uuid m_ProjectID;
            iCAX::Data::uuid m_ProjectMailID;
            EProjectRole m_Role = EProjectRole::Main;
            std::string m_ProjectName;
            std::string m_ProjectPath;
            std::string m_StartupComponent;
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSettings;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pResourceLoaderRegistry;
            std::shared_ptr<iCAX::Services::IMailChannelService> m_pMailChannelService;
            std::shared_ptr<iCAX::Database::IRepository> m_pRepository;
            std::shared_ptr<iCAX::Behaviour::IUniverseContext> m_pUniverseContext;
            std::shared_ptr<iCAX::Behaviour::IUniverse> m_pUniverse;
            std::shared_ptr<CRepositoryEventForwarder> m_pRepositoryEventForwarder;
            iCAX::Resource::CResourceLibrary m_Resources;
            uint32_t m_nFrameIntervalMilliseconds = 16;
            ProjectFrameHandler m_FrameHandler;
            bool m_bStartupBound = false;
        };
    }
}
