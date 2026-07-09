#pragma once

#include "ProjectExport.h"
#include "SceneRuntimeScheduler.h"

#include "ApplicationContext/IApplicationContext.h"
#include "Behaviour/IUniverse.h"
#include "Data/PropertyBag.h"
#include "Data/uuid.h"
#include "Database/IRepository.h"
#include "Database/IRepositoryEvent.h"
#include "Mailbox/MailChannelRegistry.h"
#include "Mailbox/MailPostOffice.h"
#include "PDO/IPDOHub.h"
#include "ProductContext/IProductContext.h"
#include "ProjectContext/ISceneContext.h"
#include "ProjectContext/SceneObjectRegistry.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourceLoaderRegistry.h"
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
    namespace Database
    {
        class IMetaRegistry;
    }

    namespace Behaviour
    {
        class IBehaviourRegistry;
    }

    namespace Project
    {
        class CProject;
        class CProjectScene;

        /*
        * @brief Scene 状态。
        */
        enum class ESceneState : uint8_t
        {
            Created = 0,  //!< 已创建但未启动。
            Starting = 1, //!< 工作线程正在启动。
            Running = 2,  //!< 工作线程运行中。
            Stopping = 3, //!< 正在请求停止。
            Stopped = 4,  //!< 已停止。
            Faulted = 5,  //!< 工作线程异常退出。
        };

        /*
        * @brief Scene 角色。
        */
        enum class ESceneRole : uint8_t
        {
            Main = 0,      //!< 主 Scene，外部默认连接到它。
            Transient = 1, //!< 子 Scene，用于导入预览、几何编辑、刀路编辑、仿真等隔离现场。
        };

        /*
        * @brief Scene 异常信息。
        */
        struct _PROJECT_EXP CSceneFault final
        {
            std::string Message; //!< 异常文本。
            std::exception_ptr Exception = nullptr; //!< 原始异常。
        };

        /*
        * @brief Scene PDO 共享内存描述。
        */
        struct _PROJECT_EXP CScenePDODescriptor final
        {
            bool bEnabled = false; //!< 当前 Scene 是否启用了 PDO。
            std::wstring SharedArenaName; //!< OS shared memory Arena 名称。
            uint64_t nSharedArenaSize = 0; //!< Arena 字节大小。
            std::vector<iCAX::PDO::PDODecl> Declarations; //!< PDO 声明快照。
        };

        /*
        * @brief Scene 每帧回调。
        */
        using SceneFrameHandler = std::function<void(CProjectScene&, const iCAX::Mail::CMailPostOffice&)>;

        /*
        * @brief Scene 创建参数。
        */
        struct _PROJECT_EXP CProjectSceneCreateInfo final
        {
            iCAX::Data::uuid SceneID; //!< Scene ID，nil 时自动生成。
            iCAX::Data::uuid SceneChannelID; //!< Scene 通信通道 ID，nil 时自动生成。
            iCAX::Data::uuid ParentSceneID; //!< 父 Scene ID；主 Scene 为空。
            ESceneRole Role = ESceneRole::Transient; //!< Scene 角色。
            std::string SceneName; //!< Scene 名称。
            iCAX::Data::PropertyBag Settings; //!< Scene 运行期设置，默认不持久化。
            std::string StartupComponent; //!< 启动组件类型名。
            std::shared_ptr<iCAX::Application::IApplicationContext> pApplicationContext; //!< 应用上下文。
            std::shared_ptr<iCAX::Product::IProductContext> pProductContext; //!< 产品上下文。
            std::shared_ptr<iCAX::Services::CServiceProvider> pServiceProvider; //!< 服务容器。
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry; //!< 产品级元数据注册表。
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry; //!< 产品级行为注册表。
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry; //!< Scene 资源加载器注册表。
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> pMailChannelRegistry; //!< 邮件通道注册表。
            bool bEnablePDOHub = false; //!< true 表示创建 Scene 级动态 PDOHub。
            iCAX::PDO::CPDOHubCreateInfo PDOHubCreateInfo; //!< 动态 PDOHub 创建参数。
            uint32_t nFrameIntervalMilliseconds = 16; //!< Scene 工作线程帧间隔。
            SceneFrameHandler FrameHandler; //!< 每帧回调。
        };

        /*
        * @brief Project 内部的独立运行/编辑现场。
        * @details
        *   Scene 是完整运行现场，拥有自己的 Repository、Undo/Redo、Transaction、Universe、
        *   ResourceLibrary、PDOHub、MailChannel 和工作线程。Project 只作为管理容器存在。
        */
        class _PROJECT_EXP CProjectScene final
            : public ISceneContext
            , public std::enable_shared_from_this<CProjectScene>
        {
        private:
            class CRepositoryEventForwarder;

        public:
            /*
            * @brief 构造 Scene。
            */
            CProjectScene(IN CProject& Project_, IN const CProjectSceneCreateInfo& CreateInfo_);
            ~CProjectScene() override;

            CProjectScene(IN const CProjectScene&) = delete;
            CProjectScene& operator=(IN const CProjectScene&) = delete;

        public:
            /*
            * @brief 获取所属 Project。
            */
            CProject& Project();
            const CProject& Project() const;

            /*
            * @brief 获取 Scene ID。
            */
            const iCAX::Data::uuid& GetSceneID() const override;

            /*
            * @brief 获取 Scene 通信通道 ID。
            */
            const iCAX::Data::uuid& GetSceneChannelID() const override;

            /*
            * @brief 获取父 Scene ID。
            */
            const iCAX::Data::uuid& GetParentSceneID() const;

            /*
            * @brief 获取 Scene 名称。
            */
            const std::string& GetSceneName() const override;

            /*
            * @brief 设置 Scene 名称。
            */
            void SetSceneName(IN const std::string& strName_);

            /*
            * @brief 获取 Scene 运行期设置。
            * @details SceneSetting 默认不持久化；跟图纸走的参数必须放在 ProjectSetting。
            */
            iCAX::Data::PropertyBag& SceneSettings();
            const iCAX::Data::PropertyBag& SceneSettings() const;

            /*
            * @brief 获取 Scene 角色。
            */
            ESceneRole GetRole() const;

            /*
            * @brief 是否为主 Scene。
            */
            bool IsMainScene() const override;

            /*
            * @brief 是否为临时 Scene。
            */
            bool IsTransientScene() const override;

            /*
            * @brief 获取启动组件类型名。
            */
            const std::string& GetStartupComponent() const;

            /*
            * @brief Scene 是否仍然打开。
            */
            bool IsOpen() const;

            /*
            * @brief Scene 工作线程是否运行中。
            */
            bool IsRunning() const;

            /*
            * @brief 获取 Scene 状态。
            */
            ESceneState GetState() const;

            /*
            * @brief 获取最近一次异常信息。
            */
            std::optional<CSceneFault> GetLastFault() const;

            /*
            * @brief 设置每帧回调。
            */
            void SetFrameHandler(IN SceneFrameHandler Handler_);

            /*
            * @brief 获取 Scene Repository。
            */
            iCAX::Database::IRepository& Database() override;
            const iCAX::Database::IRepository& Database() const override;

            /*
            * @brief 获取 Scene Universe。
            */
            iCAX::Behaviour::IUniverse& Universe();
            const iCAX::Behaviour::IUniverse& Universe() const;

            /*
            * @brief 获取 Scene 资源库。
            */
            iCAX::Resource::CResourceLibrary& Resources() override;
            const iCAX::Resource::CResourceLibrary& Resources() const override;

            /*
            * @brief 获取 Scene 运行时对象注册表。
            */
            CSceneObjectRegistry& Objects() override;
            const CSceneObjectRegistry& Objects() const override;

            /*
            * @brief 当前 Scene 是否配置 PDO Hub。
            */
            bool HasPDOHub() const override;

            /*
            * @brief 获取 PDO 共享内存描述快照。
            */
            CScenePDODescriptor GetPDODescriptor() const;

            /*
            * @brief 获取 Scene PDO Hub。
            */
            iCAX::PDO::IPDOHub& PDOHub() override;
            const iCAX::PDO::IPDOHub& PDOHub() const override;

            /*
            * @brief 获取 Scene 可用服务容器。
            */
            iCAX::Services::CServiceProvider& Services() const override;

            /*
            * @brief 获取后端视角 Scene 邮局。
            */
            iCAX::Mail::CMailPostOffice GetBackendPostOffice() const override;

            /*
            * @brief 获取前端视角 Scene 邮局。
            */
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const override;

            /*
            * @brief 向 Scene 前端邮箱主动发送事件邮件。
            */
            void SendFrontendEvent(IN uint64_t nTypeCode_, IN const std::string& strPayloadText_);

            /*
            * @brief 由当前 Scene 打开子 Scene。
            */
            std::shared_ptr<CProjectScene> OpenChildScene(IN CProjectSceneCreateInfo CreateInfo_);

            /*
            * @brief 启动 Scene 工作线程。
            */
            void Start();

            /*
            * @brief 停止 Scene 工作线程。
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
            * @brief 关闭 Scene 并释放 Repository/Universe/Resource/mail channel。
            */
            void Close();

        private:
            void WorkerMain();
            void OnRepositoryChanging(IN const iCAX::Database::RepositoryEventArgs& Args_);
            void OnRepositoryChanged(IN const iCAX::Database::RepositoryEventArgs& Args_);
            void SetState(IN ESceneState State_);
            void RecordFault(IN const std::string& strMessage_, IN std::exception_ptr Exception_);
            SceneFrameHandler GetFrameHandler() const;
            void EnsureOpen() const;
            void EnsureSceneThreadAccess(IN const char* strApiName_) const;

        private:
            CProject& m_Project;
            mutable std::mutex m_LifecycleMutex;
            std::condition_variable m_StopCondition;
            std::thread m_WorkThread;
            std::thread::id m_WorkThreadID;
            std::atomic_bool m_bStopRequested = false;
            std::atomic_bool m_bHasEnteredRunning = false;
            ESceneState m_State = ESceneState::Created;
            std::optional<CSceneFault> m_LastFault;
            iCAX::Data::uuid m_SceneID;
            iCAX::Data::uuid m_SceneChannelID;
            iCAX::Data::uuid m_ParentSceneID;
            ESceneRole m_Role = ESceneRole::Transient;
            std::string m_SceneName;
            iCAX::Data::PropertyBag m_SceneSettings;
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
            CSceneObjectRegistry m_Objects;
            iCAX::Resource::CResourceLibrary m_Resources;
            uint32_t m_nFrameIntervalMilliseconds = 16;
            CSceneRuntimeScheduler m_RuntimeScheduler;
            SceneFrameHandler m_FrameHandler;
            std::atomic_uint64_t m_nNextBackendMailID = 1;
            bool m_bStartupBound = false;
        };
    }
}
