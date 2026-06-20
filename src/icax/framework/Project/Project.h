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

        using ProjectFrameHandler = std::function<void(CProject&, const iCAX::Mail::CMailPostOffice&)>;

        struct _PROJECT_EXP CProjectCreateInfo final
        {
            iCAX::Data::uuid ProjectID;
            iCAX::Data::uuid ProjectMailID;
            EProjectRole Role = EProjectRole::Main;
            std::string ProjectName;
            std::string ProjectPath;
            std::string StartupComponent;
            std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings;
            std::shared_ptr<iCAX::Database::IMetaRegistry> pMetaRegistry;
            std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pBehaviourRegistry;
            std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pResourceLoaderRegistry;
            std::shared_ptr<iCAX::Services::IMailChannelService> pMailChannelService;
            uint32_t nFrameIntervalMilliseconds = 16;
            ProjectFrameHandler FrameHandler;
        };

        class _PROJECT_EXP CProject final
        {
        private:
            class CRepositoryEventForwarder;

        public:
            explicit CProject(IN const CProjectCreateInfo& CreateInfo_);
            ~CProject();

            CProject(IN const CProject&) = delete;
            CProject& operator=(IN const CProject&) = delete;

        public:
            const iCAX::Data::uuid& GetProjectID() const;
            const iCAX::Data::uuid& GetProjectMailID() const;
            const std::string& GetProjectName() const;
            void SetProjectName(IN const std::string& strName_);
            const std::string& GetProjectPath() const;
            void SetProjectPath(IN const std::string& strPath_);
            EProjectRole GetRole() const;
            bool IsMainProject() const;
            bool IsTransientProject() const;
            const std::string& GetStartupComponent() const;
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
            iCAX::Mail::CMailPostOffice GetBackendPostOffice();
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice();

            void Start();
            void Stop();
            void BindStartup();
            void PreSwapPDO();
            void Tick();
            void PostSwapPDO();
            void Close();

        private:
            void WorkerMain();
            void OnRepositoryChanging(IN const iCAX::Database::RepositoryEventArgs& Args_);
            void OnRepositoryChanged(IN const iCAX::Database::RepositoryEventArgs& Args_);
            void SetState(IN EProjectState State_);
            void RecordFault(IN const std::string& strMessage_, IN std::exception_ptr Exception_);
            ProjectFrameHandler GetFrameHandler() const;
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
