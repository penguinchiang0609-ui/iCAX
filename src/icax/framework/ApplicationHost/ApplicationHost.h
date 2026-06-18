#pragma once
#include "ApplicationHostExport.h"
#include "Behaviour/IUniverseContext.h"
#include "Behaviour/IUniverse.h"
#include "Data/Variant.h"
#include "Data/PropertyBag.h"
#include "ApplicationContext/ApplicationContext.h"
#include "CommandHandler/CommandContext.h"
#include "CommandHandler/CommandDispatcher.h"
#include "CommandHandler/CommandRegistry.h"
#include "CommandHandler/ICommandContext.h"
#include "Mailbox/MailPostOffice.h"
#include "Project/Project.h"
#include "Services/ServiceProvider.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "ApplicationHostConfig.h"
#include "Services/IMailPostOfficeService.h"


namespace iCAX
{
    namespace ApplicationHost
    {
        enum class EApplicationHostState : uint8_t
        {
            Stopped = 0,
            Starting = 1,
            Running = 2,
            Stopping = 3,
            Faulted = 4,
        };

        enum class EApplicationHostEventCode : uint8_t
        {
            Starting = 0,
            Started = 1,
            Stopping = 2,
            Stopped = 3,
            Faulted = 4,
        };

        enum class EApplicationHostStopReason : uint8_t
        {
            None = 0,
            Requested = 1,
            StartFailed = 2,
            Faulted = 3,
        };

        enum class EApplicationHostPhase : uint8_t
        {
            None = 0,
            Starting = 1,
            Loading = 2,
            Running = 3,
            Stopping = 4,
            Unloading = 5,
        };

        struct _APPLICATION_HOST_EXP ApplicationHostFault final
        {
            EApplicationHostPhase Phase = EApplicationHostPhase::None;
            std::string strMessage;
            std::exception_ptr pException = nullptr;
        };

        struct _APPLICATION_HOST_EXP ApplicationHostEvent final
        {
            EApplicationHostEventCode Code = EApplicationHostEventCode::Stopped;
            EApplicationHostState State = EApplicationHostState::Stopped;
            EApplicationHostStopReason StopReason = EApplicationHostStopReason::None;
            EApplicationHostPhase Phase = EApplicationHostPhase::None;
            std::string strMessage;
            std::exception_ptr pException = nullptr;
        };

        using ApplicationHostEventHandler = std::function<void(const ApplicationHostEvent&)>;

        /*
        * @brief 应用宿主
        */
        class _APPLICATION_HOST_EXP CApplicationHost
        {
        public:
            /*
            * @brief 构造函数
            */
            CApplicationHost();

            /*
            * @brief 析构函数
            */
            virtual ~CApplicationHost();

        public:
            /*
            * @brief 设置应用程序配置路径
            * @param [in] 配置信息
            */
            void SetConfig(IN const ApplicationHostConfig& Config_);

            /*
            * @brief 启动后台工作线程
            */
            void Start();

            /*
            * @brief 停止后台工作线程
            */
            void Stop();

            /*
            * @brief 是否正在运行
            */
            bool IsRunning() const;

            /*
            * @brief 获取运行状态
            */
            EApplicationHostState GetState() const;

            /*
            * @brief 获取当前运行阶段
            */
            EApplicationHostPhase GetPhase() const;

            /*
            * @brief 获取当前停止原因
            */
            EApplicationHostStopReason GetStopReason() const;

            /*
            * @brief 获取最后一次故障
            */
            std::optional<ApplicationHostFault> GetLastFault() const;

            /*
            * @brief 订阅宿主事件
            */
            uint64_t SubscribeEvent(IN ApplicationHostEventHandler Handler_);

            /*
            * @brief 取消订阅宿主事件
            */
            bool UnsubscribeEvent(IN uint64_t nSubscriptionID_);

        private:
            /*
            * @brief 工作线程入口
            */
            void WorkerMain();

            /*
            * @brief 加载
            */
            void Load();

            /*
            * @brief 主循环
            */
            void MainLoop();

            /*
            * @brief 关闭
            */
            void Unload();

            /*
            * @brief 发布宿主事件
            */
            void NotifyEvent(
                IN EApplicationHostEventCode Code_,
                IN const std::string& strMessage_ = {},
                IN std::exception_ptr pException_ = nullptr);

            /*
            * @brief 设置运行阶段
            */
            void SetPhase(IN EApplicationHostPhase Phase_);

            /*
            * @brief 记录故障
            */
            void RecordFault(
                IN EApplicationHostPhase Phase_,
                IN const std::string& strMessage_,
                IN std::exception_ptr pException_);

            /*
            * @brief 从异常中提取消息
            */
            static std::string GetExceptionMessage(IN std::exception_ptr pException_);

            /*
            * @brief 准备命令上下文依赖
            */
            void PrepareCommandContext();

            /*
            * @brief 填充一次命令执行需要的上下文
            */
            void PopulateCommandContext(
                IN OUT iCAX::Command::CCommandContext& Context_,
                IN const std::shared_ptr<iCAX::Project::CProjectSession>& pProject_) const;

            /*
            * @brief 处理后台收到的邮件命令
            */
            void DispatchBackendMails();

            /*
            * @brief 处理指定邮局收到的邮件命令
            */
            void DispatchBackendMails(
                IN const iCAX::Mail::CMailPostOffice& PostOffice_,
                IN const std::shared_ptr<iCAX::Project::CProjectSession>& pProject_);

            /*
            * @brief 分配后台发出的邮件ID
            */
            uint64_t AllocateBackendMailID();

            /*
            * @brief 加载应用程序配置
            * @return iCAX::Application::CApplicationSettings
            */
            iCAX::Application::CApplicationSettings LoadApplicationSettings() const;

            /*
            * @brief 创建应用上下文
            * @return std::shared_ptr<iCAX::Application::CApplicationContext>
            */
            std::shared_ptr<iCAX::Application::CApplicationContext> CreateApplicationContext() const;

            /*
            * @brief 加载产品定义
            */
            std::vector<iCAX::Project::CProductDefinition> LoadProductDefinitions() const;

            /*
            * @brief 加载产品或插件模块
            */
            void LoadProductModules(IN const iCAX::Project::CProductDefinition& Product_) const;

        public:
            /*
            * @brief 获取项目管理器
            */
            iCAX::Project::CProjectManager& GetProjectManager() const;

            /*
            * @brief 获取当前活动项目
            */
            std::shared_ptr<iCAX::Project::CProjectSession> GetActiveProject() const;

            /*
            * @brief 打开一个项目实例
            */
            std::shared_ptr<iCAX::Project::CProjectSession> OpenProject(
                IN const std::string& strProductID_,
                IN const std::string& strProjectName_ = std::string(),
                IN const std::string& strProjectPath_ = std::string());

            /*
            * @brief 关闭一个项目实例
            */
            bool CloseProject(IN const iCAX::Data::uuid& ProjectID_);

            /*
            * @brief 获取项目侧 frontend 邮局
            */
            iCAX::Mail::CMailPostOffice GetProjectFrontendPostOffice(IN const iCAX::Data::uuid& ProjectID_) const;

            /*
            * @brief 获取应用级 frontend 邮局
            */
            iCAX::Mail::CMailPostOffice GetApplicationFrontendPostOffice() const;

            /*
            * @brief 获取应用级通信ID
            */
            const iCAX::Data::uuid& GetApplicationMailID() const;

            /*
            * @brief 获取应用上下文
            * @return const iCAX::Application::IApplicationContext&
            */
            const iCAX::Application::IApplicationContext& GetApplicationContext() const
            {
                return *m_pApplicationContext;
            }

            /*
            * @brief 获取命令注册表
            * @return iCAX::Command::CCommandRegistry&
            */
            iCAX::Command::CCommandRegistry& GetCommandRegistry() const
            {
                return *m_pCommandRegistry;
            }

            /*
            * @brief 获取命令上下文
            * @return iCAX::Command::ICommandContext&
            */
            iCAX::Command::ICommandContext& GetCommandContext()
            {
                return m_CommandContext;
            }

            /*
            * @brief 获取命令上下文
            * @return const iCAX::Command::ICommandContext&
            */
            const iCAX::Command::ICommandContext& GetCommandContext() const
            {
                return m_CommandContext;
            }

        private:
            ApplicationHostConfig m_Config;
            mutable std::mutex m_LifecycleMutex;
            std::condition_variable m_StartupCondition;
            std::thread m_WorkThread;
            std::atomic_bool m_bStopRequested = false;
            bool m_bStartupCompleted = false;
            EApplicationHostState m_State = EApplicationHostState::Stopped;
            EApplicationHostStopReason m_StopReason = EApplicationHostStopReason::None;
            EApplicationHostPhase m_Phase = EApplicationHostPhase::None;
            std::optional<ApplicationHostFault> m_LastFault;
            std::exception_ptr m_pWorkerException = nullptr;
            mutable std::mutex m_EventMutex;
            std::map<uint64_t, ApplicationHostEventHandler> m_mapEventHandlers;
            uint64_t m_nNextEventSubscriptionID = 1;
            iCAX::Data::uuid m_ApplicationMailID;
            std::shared_ptr<iCAX::Command::CCommandRegistry> m_pCommandRegistry; //!< 命令注册表
            std::unique_ptr<iCAX::Command::CCommandDispatcher> m_pCommandDispatcher; //!< 命令分发器
            iCAX::Command::CCommandContext m_CommandContext; //!< 命令上下文
            std::atomic_uint64_t m_nNextBackendMailID = 1; //!< 后台发出的邮件ID
            std::shared_ptr<iCAX::Application::CApplicationContext> m_pApplicationContext; //!< 应用上下文
            std::shared_ptr<iCAX::Services::IMailPostOfficeService> m_pMailPostOfficeService; //!< 邮局服务
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSetting; //!< 应用设置快照
            std::shared_ptr<iCAX::Project::CProjectManager> m_pProjectManager; //!< 多产品多项目管理器
        };
    }
}
