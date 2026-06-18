#pragma once

#include "ProductDefinition.h"
#include "ProjectExport.h"

#include "Behaviour/IUniverse.h"
#include "Behaviour/IUniverseContext.h"
#include "Data/PropertyBag.h"
#include "Data/uuid.h"
#include "Database/IRepository.h"
#include "Database/IRepositoryEvent.h"
#include "Mailbox/MailChannel.h"
#include "Mailbox/MailPostOffice.h"
#include "Resources/ResourceLibrary.h"

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
        class CProjectSession;

        enum class EProjectSessionState : uint8_t
        {
            Created = 0,
            Starting = 1,
            Running = 2,
            Stopping = 3,
            Stopped = 4,
            Faulted = 5,
        };

        struct _PROJECT_EXP CProjectSessionFault final
        {
            std::string Message;
            std::exception_ptr Exception = nullptr;
        };

        using ProjectFrameHandler = std::function<void(CProjectSession&, const iCAX::Mail::CMailPostOffice&)>;

        struct _PROJECT_EXP CProjectSessionCreateInfo final
        {
            iCAX::Data::uuid ProjectID;
            CProductDefinition Product;
            std::string ProjectName;
            std::string ProjectPath;
            std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings;
            uint32_t nFrameIntervalMilliseconds = 16;
            ProjectFrameHandler FrameHandler;
        };

        class _PROJECT_EXP CProjectSession final
        {
        private:
            class CRepositoryEventForwarder;

        public:
            explicit CProjectSession(IN const CProjectSessionCreateInfo& CreateInfo_);
            ~CProjectSession();

            CProjectSession(IN const CProjectSession&) = delete;
            CProjectSession& operator=(IN const CProjectSession&) = delete;

        public:
            const iCAX::Data::uuid& GetProjectID() const;
            const std::string& GetProjectName() const;
            void SetProjectName(IN const std::string& strName_);
            const std::string& GetProjectPath() const;
            void SetProjectPath(IN const std::string& strPath_);
            const std::string& GetProductID() const;
            const CProductDefinition& GetProductDefinition() const;
            bool IsOpen() const;
            bool IsRunning() const;
            EProjectSessionState GetState() const;
            std::optional<CProjectSessionFault> GetLastFault() const;
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
            void SetState(IN EProjectSessionState State_);
            void RecordFault(IN const std::string& strMessage_, IN std::exception_ptr Exception_);
            ProjectFrameHandler GetFrameHandler() const;
            void EnsureOpen() const;

        private:
            mutable std::mutex m_LifecycleMutex;
            std::condition_variable m_StopCondition;
            std::thread m_WorkThread;
            std::atomic_bool m_bStopRequested = false;
            EProjectSessionState m_State = EProjectSessionState::Created;
            std::optional<CProjectSessionFault> m_LastFault;
            iCAX::Data::uuid m_ProjectID;
            CProductDefinition m_Product;
            std::string m_ProjectName;
            std::string m_ProjectPath;
            std::shared_ptr<iCAX::Data::PropertyBag> m_pApplicationSettings;
            std::shared_ptr<iCAX::Database::IRepository> m_pRepository;
            std::shared_ptr<iCAX::Behaviour::IUniverseContext> m_pUniverseContext;
            std::shared_ptr<iCAX::Behaviour::IUniverse> m_pUniverse;
            std::shared_ptr<CRepositoryEventForwarder> m_pRepositoryEventForwarder;
            iCAX::Resource::CResourceLibrary m_Resources;
            iCAX::Mail::CMailChannel m_MailChannel;
            uint32_t m_nFrameIntervalMilliseconds = 16;
            ProjectFrameHandler m_FrameHandler;
            bool m_bStartupBound = false;
        };
    }
}
