#pragma once

#include "ProjectDocument.h"

#include <memory>
#include <string>
#include <vector>

namespace iCAX::ProjectFile
{
    struct _PROJECT_FILE_EXP CProjectMigrationContext final
    {
        std::vector<std::string> Diagnostics;

        void Note(IN std::string Message_);
    };

    class _PROJECT_FILE_EXP IProjectDocumentMigration
    {
    public:
        virtual ~IProjectDocumentMigration() = default;

        virtual std::string ProductID() const = 0;
        virtual uint32_t FromRevision() const = 0;
        virtual uint32_t ToRevision() const = 0;
        virtual std::string ToFormatVersion() const = 0;
        virtual void Upgrade(
            IN OUT CProjectDocument& Document_,
            IN OUT CProjectMigrationContext& Context_) const = 0;
    };

    class _PROJECT_FILE_EXP IProjectResourceMigration
    {
    public:
        virtual ~IProjectResourceMigration() = default;

        virtual std::string ResourceTypeID() const = 0;
        virtual uint32_t FromSchemaVersion() const = 0;
        virtual uint32_t ToSchemaVersion() const = 0;
        virtual void Upgrade(
            IN OUT CProjectResourceRecord& Resource_,
            IN OUT CProjectMigrationContext& Context_) const = 0;
    };

    class _PROJECT_FILE_EXP CProjectMigrationRegistry final
    {
    public:
        CProjectMigrationRegistry();
        ~CProjectMigrationRegistry();

        CProjectMigrationRegistry(const CProjectMigrationRegistry&) = delete;
        CProjectMigrationRegistry& operator=(
            const CProjectMigrationRegistry&) = delete;

        void RegisterDocumentMigration(
            IN std::shared_ptr<IProjectDocumentMigration> pMigration_);
        void RegisterResourceMigration(
            IN std::shared_ptr<IProjectResourceMigration> pMigration_);
        void SetCurrentResourceSchema(
            IN const std::string& ResourceTypeID_,
            IN uint32_t nSchemaVersion_);

        CProjectDocument UpgradeToCurrent(
            IN const CProjectDocument& Source_,
            IN uint32_t nTargetRevision_,
            IN const std::string& strTargetFormatVersion_,
            OUT CProjectMigrationContext* pContext_ = nullptr) const;

    private:
        struct SImpl;
        std::unique_ptr<SImpl> m_pImpl;
    };
}

