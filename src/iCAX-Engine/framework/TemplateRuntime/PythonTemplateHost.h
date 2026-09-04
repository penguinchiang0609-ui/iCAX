#pragma once

#include "TemplateRuntimeExport.h"

#include "Data/Variant.h"

#include <chrono>
#include <filesystem>
#include <memory>

namespace iCAX::TemplateRuntime
{
    struct _TEMPLATE_RUNTIME_EXP SPythonTemplateHostOptions final
    {
        std::filesystem::path PythonExecutable;
        std::filesystem::path WorkerScript;
        std::filesystem::path WorkingDirectory;
        std::chrono::milliseconds RequestTimeout{ 30'000 };
        std::chrono::milliseconds ShutdownTimeout{ 2'000 };
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
