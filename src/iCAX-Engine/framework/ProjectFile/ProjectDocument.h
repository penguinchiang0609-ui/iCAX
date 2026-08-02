#pragma once

#include "ProjectFileExport.h"

#include "Data/Variant.h"
#include "Data/uuid.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace iCAX::ProjectFile
{
    inline constexpr uint32_t kCurrentContainerVersion = 2;
    inline constexpr const char* kResourceReferenceValueTag = "resource_ref";

    enum class EProjectFileEncoding : uint8_t
    {
        ASCII = 1,
        Binary = 2,
    };

    enum class EProjectResourcePersistence : uint8_t
    {
        Embedded = 1,
        External = 2,
    };

    struct _PROJECT_FILE_EXP CProjectResourceReference final
    {
        std::string URL;
        uint64_t nVersion = 0;

        bool IsValid() const noexcept;
        bool operator==(const CProjectResourceReference&) const = default;
        bool operator<(const CProjectResourceReference& Right_) const noexcept;
    };

    struct _PROJECT_FILE_EXP CProjectDocumentInfo final
    {
        std::string Magic;
        std::string ProductID;
        std::string FormatVersion;
        uint32_t nFormatRevision = 0;
        iCAX::Data::uuid ProjectID;
        iCAX::Data::uuid MainSceneID;
        std::string ProjectName;
        iCAX::Data::ObjectMap ProjectSettings;
        iCAX::Data::ObjectMap MainSceneSettings;

        bool operator==(const CProjectDocumentInfo&) const = default;
    };

    struct _PROJECT_FILE_EXP CProjectEntityRecord final
    {
        iCAX::Data::uuid EntityID;

        bool operator==(const CProjectEntityRecord&) const = default;
    };

    struct _PROJECT_FILE_EXP CProjectComponentRecord final
    {
        iCAX::Data::uuid EntityID;
        std::string ComponentClass;
        bool bEnabled = true;
        iCAX::Data::ObjectMap Properties;

        bool operator==(const CProjectComponentRecord&) const = default;
    };

    struct _PROJECT_FILE_EXP CProjectResourceRecord final
    {
        CProjectResourceReference Reference;
        std::string ResourceTypeID;
        uint32_t nSchemaVersion = 0;
        EProjectResourcePersistence Persistence =
            EProjectResourcePersistence::Embedded;
        std::string Name;
        std::string MediaType;
        std::string FlatBufferIdentifier;
        std::string ContentHash;
        std::string Source;
        uint64_t nSize = 0;
        uint32_t nMinimumReaderVersion = 0;
        uint32_t nFlags = 0;
        std::map<std::string, std::string> Metadata;
        std::vector<CProjectResourceReference> Dependencies;
        std::vector<uint8_t> Body;

        bool operator==(const CProjectResourceRecord&) const = default;
    };

    struct _PROJECT_FILE_EXP CProjectDocument final
    {
        CProjectDocumentInfo Info;
        std::vector<CProjectEntityRecord> Entities;
        std::vector<CProjectComponentRecord> Components;
        std::vector<CProjectResourceRecord> Resources;

        void Canonicalize();
        CProjectResourceRecord* FindResource(
            IN const CProjectResourceReference& Reference_);
        const CProjectResourceRecord* FindResource(
            IN const CProjectResourceReference& Reference_) const;
        void RewriteResourceReference(
            IN const CProjectResourceReference& Previous_,
            IN const CProjectResourceReference& Current_);

        bool operator==(const CProjectDocument&) const = default;
    };

    _PROJECT_FILE_EXP iCAX::Data::Variant MakeResourceReferenceValue(
        IN const CProjectResourceReference& Reference_);
    _PROJECT_FILE_EXP std::optional<CProjectResourceReference>
        TryGetResourceReferenceValue(IN const iCAX::Data::Variant& Value_);

    _PROJECT_FILE_EXP std::vector<std::string>
        ValidateProjectDocument(IN const CProjectDocument& Document_);
    _PROJECT_FILE_EXP void RequireValidProjectDocument(
        IN const CProjectDocument& Document_);
}
