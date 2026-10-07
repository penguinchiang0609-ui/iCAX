#pragma once

#include "TemplateRuntimeExport.h"

#include "Data/Variant.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::TemplateRuntime
{
    struct _TEMPLATE_RUNTIME_EXP SPythonTemplateHostOptions final
    {
        std::filesystem::path PythonRuntimeLibrary;
        std::filesystem::path WorkerScript;
        std::filesystem::path WorkingDirectory;
    };

    class _TEMPLATE_RUNTIME_EXP CPythonTemplateHost final
    {
    public:
        explicit CPythonTemplateHost(SPythonTemplateHostOptions Options_);
        ~CPythonTemplateHost();

        CPythonTemplateHost(const CPythonTemplateHost&) = delete;
        CPythonTemplateHost& operator=(const CPythonTemplateHost&) = delete;

        // Product callers require the actual SDK lookup roles from the current
        // response envelope. Public model documents and parameters stay pure.
        iCAX::Data::ObjectMap Invoke(const iCAX::Data::ObjectMap& Request_,
            std::vector<std::string>* ProfileRolesConsumed_ = nullptr);
        iCAX::Data::ObjectMap Invoke(iCAX::Data::ObjectMap&& Request_,
            std::vector<std::string>* ProfileRolesConsumed_ = nullptr);
        bool IsRunning() const noexcept;
        void Stop() noexcept;

    private:
        class CImpl;
        std::unique_ptr<CImpl> m_pImpl;
    };
}
