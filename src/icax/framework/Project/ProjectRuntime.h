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

        using ProjectRuntimeFrameHandler = std::function<void(
            IProjectRuntime&,
            const iCAX::Mail::CMailPostOffice&)>;

        class _PROJECT_EXP IProjectRuntime
        {
        public:
            virtual ~IProjectRuntime() = default;

        public:
            virtual const iCAX::Data::uuid& GetProjectID() const = 0;
            virtual const iCAX::Data::uuid& GetProjectMailID() const = 0;
            virtual std::string GetProjectName() const = 0;
            virtual std::string GetProjectPath() const = 0;
            virtual std::string GetStartupComponent() const = 0;
            virtual EProjectRole GetRole() const = 0;
            virtual bool IsMainProject() const = 0;
            virtual bool IsTransientProject() const = 0;
            virtual bool IsOpen() const = 0;
            virtual bool IsRunning() const = 0;
            virtual EProjectState GetState() const = 0;
            virtual std::optional<CProjectFault> GetLastFault() const = 0;
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice() = 0;
            virtual void SetFrameHandler(IN ProjectRuntimeFrameHandler Handler_) = 0;
            virtual void Start() = 0;
            virtual void Stop() = 0;
            virtual void Close() = 0;

            virtual std::shared_ptr<CProject> GetLocalProject() const = 0;
        };

        class _PROJECT_EXP CLocalProjectRuntime final : public IProjectRuntime
        {
        public:
            explicit CLocalProjectRuntime(IN std::shared_ptr<CProject> pProject_);
            ~CLocalProjectRuntime() override;

            CLocalProjectRuntime(IN const CLocalProjectRuntime&) = delete;
            CLocalProjectRuntime& operator=(IN const CLocalProjectRuntime&) = delete;

        public:
            const iCAX::Data::uuid& GetProjectID() const override;
            const iCAX::Data::uuid& GetProjectMailID() const override;
            std::string GetProjectName() const override;
            std::string GetProjectPath() const override;
            std::string GetStartupComponent() const override;
            EProjectRole GetRole() const override;
            bool IsMainProject() const override;
            bool IsTransientProject() const override;
            bool IsOpen() const override;
            bool IsRunning() const override;
            EProjectState GetState() const override;
            std::optional<CProjectFault> GetLastFault() const override;
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice() override;
            void SetFrameHandler(IN ProjectRuntimeFrameHandler Handler_) override;
            void Start() override;
            void Stop() override;
            void Close() override;
            std::shared_ptr<CProject> GetLocalProject() const override;

        private:
            ProjectRuntimeFrameHandler GetFrameHandler() const;
            CProject& RequireProject() const;

        private:
            std::shared_ptr<CProject> m_pProject;
            mutable std::mutex m_FrameHandlerMutex;
            ProjectRuntimeFrameHandler m_FrameHandler;
        };

        _PROJECT_EXP std::shared_ptr<IProjectRuntime> CreateLocalProjectRuntime(
            IN std::shared_ptr<CProject> pProject_);
    }
}
