#pragma once

#include "TemplateRuntimeExport.h"

#include "Data/Variant.h"

#include <filesystem>
#include <memory>

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

        iCAX::Data::ObjectMap Invoke(const iCAX::Data::ObjectMap& Request_);
        bool IsRunning() const noexcept;
        void Stop() noexcept;

    private:
        class CImpl;
        std::unique_ptr<CImpl> m_pImpl;
    };
}
