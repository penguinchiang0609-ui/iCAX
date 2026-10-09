#pragma once

#include "OpenCascadeResourceImportExport.h"
#include "Services/IService.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace iCAX::CAD
{
    struct SCadGroup final { int Code = 0; std::string Value; };
    struct SCadRecord final { std::string Type; std::vector<SCadGroup> Values; };
    struct SCadDocument final
    {
        std::map<std::string, std::vector<SCadGroup>> Header;
        std::map<std::string, std::vector<SCadRecord>> Sections;
        std::string SourceFormat; // Input exchange encoding: ascii or binary.
    };

    // Decoded neutral document records, before any product units, positioning,
    // fill semantics or editable-entity mapping. Text is Unicode UTF-8.
    struct SCadExchangeDocument final
    {
        std::filesystem::path SourcePath;
        std::string SourceFormat;
        std::string SourceVersion;
        SCadDocument Document;
        std::string ConverterVersion;
    };

    class _OPEN_CASCADE_RESOURCE_IMPORT_EXP ICadDocumentImportService
        : public iCAX::Services::IService
    {
    public:
        ~ICadDocumentImportService() override = default;
        // Reads one absolute local file. DWG conversion is offline and isolated;
        // the service neither receives nor retains business/runtime contexts.
        virtual SCadExchangeDocument Load(const std::filesystem::path& SourcePath_) const = 0;
    };
}
