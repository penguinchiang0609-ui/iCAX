#pragma once

#include "ProjectExport.h"

#include "ApplicationContext/IApplicationContext.h"
#include "Behaviour/IUniverse.h"
#include "Data/uuid.h"
#include "Database/IRepository.h"
#include "Database/IRepositoryEvent.h"
#include "Mailbox/MailPostOffice.h"
#include "PDO/IPDOHub.h"
#include "ProjectRuntimeScheduler.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourceLoaderRegistry.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/IProjectContext.h"
#include "Mailbox/MailChannelRegistry.h"
#include "Services/ServiceProvider.h"

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
#include <vector>

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
        * @brief 项目 PDO 共享内存描述。
        * @details
        *   该结构用于跨线程/跨前端返回只读快照。调用方只能拿到 Arena 名称、大小和声明，
        *   不能绕过 Project 线程直接取得可写 PDOHub。
        */
        struct _PROJECT_EXP CProjectPDODescriptor final
        {
            bool bEnabled = false; //!< 当前项目是否启用了 PDO。
            std::wstring SharedArenaName; //!< OS shared memory Arena 名称。
            uint64_t nSharedArenaSize = 0; //!< Arena 字节大小。
            std::vector<iCAX::PDO::PDODecl> Declarations; //!< PDO 声明快照。
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
            iCAX::Data::uuid ProjectChannelID; //!< 项目通信通道 ID，nil 时自动生成。
            EProjectRole Role = EProjectRole::Main; //!< 项目角色。
            std::string ProjectName; //!< 项目名称。
            std::string ProjectPath; //!< 项目路径。
            std::string QuickSaveLogPath; //!< 快速保存日志路径；为空时根据 ProjectPath 推导。
            std::string StartupComponent; //!< 启动组件类型名。
            std::shared_ptr<iCAX::Application::IApplicationContext> pApplicationContext; //!< 应用上下文。
            std::shared_ptr<iCAX::Product::IProductContext> pProductContext; //!< 产品上下文。
            std::shared_ptr<iCAX::Services::CServiceProvider> pServiceProvider; //!< 服务容器。
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry; //!< 元数据注册表。
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry; //!< 行为注册表。
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry; //!< 项目资源加载器注册表。
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> pMailChannelRegistry; //!< 邮件通道注册表。
            bool bEnablePDOHub = false; //!< true 表示创建项目级动态 PDOHub。
            iCAX::PDO::CPDOHubCreateInfo PDOHubCreateInfo; //!< 动态 PDOHub 创建参数。
            uint32_t nFrameIntervalMilliseconds = 16; //!< 项目工作线程帧间隔。
            ProjectFrameHandler FrameHandler; //!< 每帧回调。
        };

        /*
        * @brief 项目运行实体。
        * @details
        *   一个 Project 拥有自己的 Repository、Universe、ResourceLibrary、mail channel 和工作线程。
        *   ProductRuntime 管理 Project 的打开/关闭，但项目数据和帧循环由 Project 自身维护。
        */
        class _PROJECT_EXP CProject final : public IProjectContext
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
            const iCAX::Data::uuid& GetProjectID() const override;

            /*
            * @brief 获取项目通信通道 ID。
            */
            const iCAX::Data::uuid& GetProjectChannelID() const override;

            /*
            * @brief 获取项目名称。
            */
            const std::string& GetProjectName() const override;

            /*
            * @brief 设置项目名称。
            */
            void SetProjectName(IN const std::string& strName_);

            /*
            * @brief 获取项目路径。
            */
            const std::string& GetProjectPath() const override;

            /*
            * @brief 设置项目路径。
            */
            void SetProjectPath(IN const std::string& strPath_);

            /*
            * @brief 获取快速保存日志路径。
            * @return 显式日志路径；未显式设置时返回 ProjectPath + ".log"。
            */
            std::string GetQuickSaveLogPath() const;

            /*
            * @brief 设置快速保存日志路径。
            * @details 该方法只修改路径，不会自动打开或迁移日志。
            */
            void SetQuickSaveLogPath(IN const std::string& strPath_);

            /*
            * @brief 回放快速保存日志。
            * @param [in] strMagic_ 期望日志 magic，由产品或文件模块传入。
            * @param [in] nVersion_ 期望日志格式版本；不匹配时拒绝回放。
            * @details
            *   用于项目文件基线加载完成之后，将崩溃前追加日志恢复到 Repository。
            *   若日志不存在或为空，则无操作。回放不会打开追加写入。
            */
            void ReplayQuickSaveLog(IN const std::string& strMagic_, IN uint32_t nVersion_);

            /*
            * @brief 打开快速保存日志。
            * @param [in] bTruncate_ true 表示清空旧日志，false 表示追加写入。
            * @param [in] strMagic_ 日志 magic，由产品或文件模块传入。
            * @param [in] nVersion_ 日志格式版本；追加旧日志时必须匹配。
            * @details 后续 Repository 修改会自动追加到日志。
            */
            void OpenQuickSaveLog(IN bool bTruncate_, IN const std::string& strMagic_, IN uint32_t nVersion_);

            /*
            * @brief 保存项目文件成功后重置快速保存日志。
            * @param [in] strProjectPath_ 新项目文件路径；为空表示沿用当前 ProjectPath。
            * @param [in] strMagic_ 日志 magic，由产品或文件模块传入。
            * @param [in] nVersion_ 日志格式版本。
            * @details
            *   调用方应先完成主项目文件写入，再调用本方法。它会关闭旧日志、
            *   更新项目路径、截断日志并重新以追加模式打开。
            */
            void MarkProjectFileSaved(
                IN const std::string& strProjectPath_,
                IN const std::string& strMagic_,
                IN uint32_t nVersion_);

            /*
            * @brief 关闭快速保存日志。
            */
            void CloseQuickSaveLog();

            /*
            * @brief 当前是否已打开快速保存日志。
            */
            bool IsQuickSaveLogOpen() const;

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
            iCAX::Database::IRepository& Database() override;
            const iCAX::Database::IRepository& Database() const override;

            /*
            * @brief 获取项目 Universe。
            */
            iCAX::Behaviour::IUniverse& Universe();
            const iCAX::Behaviour::IUniverse& Universe() const;

            /*
            * @brief 获取项目资源库。
            */
            iCAX::Resource::CResourceLibrary& Resources() override;
            const iCAX::Resource::CResourceLibrary& Resources() const override;

            /*
            * @brief 当前项目是否配置 PDO Hub。
            */
            bool HasPDOHub() const override;

            /*
            * @brief 获取 PDO 共享内存描述快照。
            * @return 未启用 PDO 时 bEnabled 为 false。
            */
            CProjectPDODescriptor GetPDODescriptor() const;

            /*
            * @brief 获取项目 PDO Hub。
            * @throws std::logic_error 未配置 PDO 时抛出。
            */
            iCAX::PDO::IPDOHub& PDOHub() override;
            const iCAX::PDO::IPDOHub& PDOHub() const override;

            /*
            * @brief 获取项目可用服务容器。
            */
            iCAX::Services::CServiceProvider& Services() const override;

            /*
            * @brief 获取后端视角项目邮局。
            */
            iCAX::Mail::CMailPostOffice GetBackendPostOffice() const override;

            /*
            * @brief 向项目级前端邮箱主动发送事件邮件。
            * @param [in] nTypeCode_ 事件类型码，使用 CommandRoute 的 64 位主/子编码。
            * @param [in] strPayloadText_ UTF-8 文本负载，通常是 VariantSerializer 文本。
            * @details
            *   主动事件邮件的 nOriginId 固定为 0，前端通过 FrontendMailbox.subscribe/subscribeAll 接收。
            */
            void SendFrontendEvent(IN uint64_t nTypeCode_, IN const std::string& strPayloadText_);

            /*
            * @brief 获取前端视角项目邮局。
            */
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const override;

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
            * @details Project 先推进自己的运行时调度器，再把 DeltaTime/TotalTime 传入 Universe。
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

            /*
            * @brief 确保运行中的项目数据只能在项目线程访问。
            * @param [in] strApiName_ 调用 API 名称，用于异常文本。
            * @throws std::logic_error 项目运行中且当前线程不是项目工作线程时抛出。
            * @details
            *   Project 未启动或已停止时允许直接访问，便于文件加载、白盒测试和关闭前检查。
            *   一旦进入 Running，Repository/Universe/Resource/PDO 的可变入口只能由项目线程使用。
            */
            void EnsureProjectThreadAccess(IN const char* strApiName_) const;

        private:
            mutable std::mutex m_LifecycleMutex;
            std::condition_variable m_StopCondition;
            std::thread m_WorkThread;
            std::thread::id m_WorkThreadID;
            std::atomic_bool m_bStopRequested = false;
            std::atomic_bool m_bHasEnteredRunning = false;
            EProjectState m_State = EProjectState::Created;
            std::optional<CProjectFault> m_LastFault;
            iCAX::Data::uuid m_ProjectID;
            iCAX::Data::uuid m_ProjectChannelID;
            EProjectRole m_Role = EProjectRole::Main;
            std::string m_ProjectName;
            std::string m_ProjectPath;
            std::string m_QuickSaveLogPath;
            std::string m_StartupComponent;
            std::shared_ptr<iCAX::Application::IApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Product::IProductContext> m_pProductContext;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pServiceProvider;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pResourceLoaderRegistry;
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> m_pMailChannelRegistry;
            std::shared_ptr<iCAX::Database::IRepository> m_pRepository;
            std::shared_ptr<iCAX::Behaviour::IUniverse> m_pUniverse;
            std::shared_ptr<iCAX::PDO::IPDOHub> m_pPDOHub;
            std::shared_ptr<CRepositoryEventForwarder> m_pRepositoryEventForwarder;
            iCAX::Resource::CResourceLibrary m_Resources;
            uint32_t m_nFrameIntervalMilliseconds = 16;
            CProjectRuntimeScheduler m_RuntimeScheduler;
            ProjectFrameHandler m_FrameHandler;
            std::atomic_uint64_t m_nNextBackendMailID = 1;
            bool m_bStartupBound = false;
        };
    }
}

