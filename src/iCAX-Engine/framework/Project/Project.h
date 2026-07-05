#pragma once

#include "ProjectExport.h"
#include "ProjectScene.h"

#include "ApplicationContext/IApplicationContext.h"
#include "Data/PropertyBag.h"
#include "Data/uuid.h"
#include "ProjectContext/IProjectContext.h"
#include "ProductContext/IProductContext.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Project
    {
        class CProject;

        enum class EProjectState : uint8_t
        {
            Created = 0,
            Starting = 1,
            Running = 2,
            Stopping = 3,
            Stopped = 4,
            Faulted = 5,
        };

        enum class EProjectRole : uint8_t
        {
            Main = 0,
            Transient = 1,
        };

        struct _PROJECT_EXP CProjectFault final
        {
            std::string Message;
            std::exception_ptr Exception = nullptr;
        };

        using CProjectPDODescriptor = CScenePDODescriptor;

        /*
        * @brief 项目每帧回调。
        * @details 回调由主 Scene 的工作线程触发；CProject 只作为项目级上下文传入。
        */
        using ProjectFrameHandler = std::function<void(CProject&, const iCAX::Mail::CMailPostOffice&)>;

        /*
        * @brief 项目创建参数。
        * @details
        *   Project 自身只承载项目身份、路径和 ProjectSetting。
        *   MainSceneChannelID、PDOHubCreateInfo、StartupComponent 等用于初始化主 Scene。
        */
        struct _PROJECT_EXP CProjectCreateInfo final
        {
            iCAX::Data::uuid ProjectID;
            iCAX::Data::uuid ProjectChannelID; //!< 兼容旧字段名；实际作为主 Scene 通信通道 ID 使用。
            EProjectRole Role = EProjectRole::Main;
            std::string ProjectName;
            std::string ProjectPath;
            iCAX::Data::PropertyBag Settings;
            std::string QuickSaveLogPath;
            std::string StartupComponent;
            std::shared_ptr<iCAX::Application::IApplicationContext> pApplicationContext;
            std::shared_ptr<iCAX::Product::IProductContext> pProductContext;
            std::shared_ptr<iCAX::Services::CServiceProvider> pServiceProvider;
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry;
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> pMailChannelRegistry;
            bool bEnablePDOHub = false;
            iCAX::PDO::CPDOHubCreateInfo PDOHubCreateInfo;
            uint32_t nFrameIntervalMilliseconds = 16;
            ProjectFrameHandler FrameHandler;
        };

        /*
        * @brief Project 是项目级管理容器。
        * @details
        *   Project 不拥有 Repository、Universe、ResourceLibrary、PDOHub、MailChannel 或工作线程。
        *   这些运行期对象归属 Scene。Project 启动时会创建一个 MainScene；
        *   需要临时编辑、导入预览或刀路局部编辑时，由已有 Scene 打开子 Scene。
        */
        class _PROJECT_EXP CProject final
            : public IProjectContext
            , public std::enable_shared_from_this<CProject>
        {
        public:
            explicit CProject(IN const CProjectCreateInfo& CreateInfo_);
            ~CProject() override;

            CProject(IN const CProject&) = delete;
            CProject& operator=(IN const CProject&) = delete;

        public:
            const iCAX::Data::uuid& GetProjectID() const override;
            const std::string& GetProjectName() const override;
            void SetProjectName(IN const std::string& strName_);
            const std::string& GetProjectPath() const override;
            void SetProjectPath(IN const std::string& strPath_);
            iCAX::Data::PropertyBag& Settings() override;
            const iCAX::Data::PropertyBag& Settings() const override;

            std::string GetQuickSaveLogPath() const;
            void SetQuickSaveLogPath(IN const std::string& strPath_);
            void ReplayQuickSaveLog(IN const std::string& strMagic_, IN uint32_t nVersion_);
            void OpenQuickSaveLog(IN bool bTruncate_, IN const std::string& strMagic_, IN uint32_t nVersion_);
            void MarkProjectFileSaved(
                IN const std::string& strProjectPath_,
                IN const std::string& strMagic_,
                IN uint32_t nVersion_);
            void CloseQuickSaveLog();
            bool IsQuickSaveLogOpen() const;

            EProjectRole GetRole() const;
            bool IsMainProject() const;
            bool IsTransientProject() const;
            const std::string& GetStartupComponent() const;

            const iCAX::Data::uuid& GetMainSceneID() const;
            CProjectScene& GetMainScene();
            const CProjectScene& GetMainScene() const;
            std::shared_ptr<CProjectScene> GetScene(IN const iCAX::Data::uuid& SceneID_) const;
            std::vector<std::shared_ptr<CProjectScene>> GetScenes() const;
            std::shared_ptr<CProjectScene> OpenChildScene(
                IN const iCAX::Data::uuid& ParentSceneID_,
                IN CProjectSceneCreateInfo CreateInfo_);
            bool CloseScene(IN const iCAX::Data::uuid& SceneID_);

            /*
            * @brief 主 Scene 通信通道 ID。
            * @details 保留旧接口名，返回 MainScene.GetSceneChannelID()。
            */
            const iCAX::Data::uuid& GetProjectChannelID() const;

            bool IsOpen() const;
            bool IsRunning() const;
            EProjectState GetState() const;
            std::optional<CProjectFault> GetLastFault() const;
            void SetFrameHandler(IN ProjectFrameHandler Handler_);

            iCAX::Database::IRepository& Database();
            const iCAX::Database::IRepository& Database() const;
            iCAX::Behaviour::IUniverse& Universe();
            const iCAX::Behaviour::IUniverse& Universe() const;
            iCAX::Resource::CResourceLibrary& Resources();
            const iCAX::Resource::CResourceLibrary& Resources() const;
            bool HasPDOHub() const;
            CProjectPDODescriptor GetPDODescriptor() const;
            iCAX::PDO::IPDOHub& PDOHub();
            const iCAX::PDO::IPDOHub& PDOHub() const;
            iCAX::Services::CServiceProvider& Services() const;
            iCAX::Mail::CMailPostOffice GetBackendPostOffice() const;
            void SendFrontendEvent(IN uint64_t nTypeCode_, IN const std::string& strPayloadText_);
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const;

            void Start();
            void Stop();
            void BindStartup();
            void PreSwapPDO();
            void Tick();
            void PostSwapPDO();
            void Close();

        private:
            CProjectSceneCreateInfo MakeMainSceneCreateInfo(IN const CProjectCreateInfo& CreateInfo_) const;
            std::shared_ptr<CProjectScene> RequireMainScene() const;
            static EProjectState ConvertSceneState(IN ESceneState State_);
            static CProjectFault ConvertSceneFault(IN const CSceneFault& Fault_);

        private:
            mutable std::recursive_mutex m_Mutex;
            iCAX::Data::uuid m_ProjectID;
            EProjectRole m_Role = EProjectRole::Main;
            std::string m_ProjectName;
            std::string m_ProjectPath;
            iCAX::Data::PropertyBag m_Settings;
            std::string m_QuickSaveLogPath;
            bool m_bQuickSaveLogOpen = false;
            std::string m_StartupComponent;
            std::shared_ptr<iCAX::Application::IApplicationContext> m_pApplicationContext;
            std::shared_ptr<iCAX::Product::IProductContext> m_pProductContext;
            std::shared_ptr<iCAX::Services::CServiceProvider> m_pServiceProvider;
            std::shared_ptr<iCAX::Database::IMetaRegistry> m_pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> m_pBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> m_pResourceLoaderRegistry;
            std::shared_ptr<iCAX::Mail::CMailChannelRegistry> m_pMailChannelRegistry;
            iCAX::Data::uuid m_MainSceneID;
            std::map<iCAX::Data::uuid, std::shared_ptr<CProjectScene>> m_Scenes;
            ProjectFrameHandler m_FrameHandler;
        };
    }
}
