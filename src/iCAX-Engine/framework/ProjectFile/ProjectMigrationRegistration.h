#pragma once

#include "ProjectMigration.h"

#include <functional>
#include <string>
#include <vector>

namespace iCAX::ProjectFile
{
    /*
    * @brief 产品模块贡献的项目文件迁移注册目录。
    * @details 静态注册只记录回放函数；ProductRuntime 按本产品实际加载的
    *   DLL 路径回放到独立 Registry，避免多产品之间共享迁移状态。
    */
    class _PROJECT_FILE_EXP CProjectMigrationRegistrationCatalog final
    {
    public:
        using ReplayFunc =
            std::function<void(CProjectMigrationRegistry&)>;

        struct CRegistrationRecord final
        {
            std::string ModulePath;
            ReplayFunc Replay;
        };

        static void Register(
            IN ReplayFunc Func_,
            IN const void* pModuleAddress_ = nullptr);
        static void ReplayAll(
            IN CProjectMigrationRegistry& Registry_);
        static void ReplayByModulePaths(
            IN CProjectMigrationRegistry& Registry_,
            IN const std::vector<std::string>& ModulePaths_);
        static size_t Count();

    private:
        static std::vector<CRegistrationRecord>&
            GetRegistrations();
    };
}

#define ICAX_PROJECT_MIGRATION_JOIN_IMPL(x, y) x##y
#define ICAX_PROJECT_MIGRATION_JOIN(x, y) \
    ICAX_PROJECT_MIGRATION_JOIN_IMPL(x, y)

#define ICAX_REGISTER_PROJECT_DOCUMENT_MIGRATION(MigrationType) \
    ICAX_REGISTER_PROJECT_DOCUMENT_MIGRATION_IMPL( \
        MigrationType, __COUNTER__)

#define ICAX_REGISTER_PROJECT_DOCUMENT_MIGRATION_IMPL( \
    MigrationType, UniqueID) \
    namespace \
    { \
        struct ICAX_PROJECT_MIGRATION_JOIN( \
            CAutoProjectDocumentMigration_, UniqueID) final \
        { \
            ICAX_PROJECT_MIGRATION_JOIN( \
                CAutoProjectDocumentMigration_, UniqueID)() \
            { \
                ::iCAX::ProjectFile:: \
                    CProjectMigrationRegistrationCatalog::Register( \
                    [](::iCAX::ProjectFile:: \
                        CProjectMigrationRegistry& Registry_) \
                    { \
                        Registry_.RegisterDocumentMigration( \
                            std::make_shared<MigrationType>()); \
                    }, \
                    this); \
            } \
        }; \
        const ICAX_PROJECT_MIGRATION_JOIN( \
            CAutoProjectDocumentMigration_, UniqueID) \
            ICAX_PROJECT_MIGRATION_JOIN( \
                g_AutoProjectDocumentMigration_, UniqueID); \
    }

#define ICAX_REGISTER_PROJECT_RESOURCE_MIGRATION(MigrationType) \
    ICAX_REGISTER_PROJECT_RESOURCE_MIGRATION_IMPL( \
        MigrationType, __COUNTER__)

#define ICAX_REGISTER_PROJECT_RESOURCE_MIGRATION_IMPL( \
    MigrationType, UniqueID) \
    namespace \
    { \
        struct ICAX_PROJECT_MIGRATION_JOIN( \
            CAutoProjectResourceMigration_, UniqueID) final \
        { \
            ICAX_PROJECT_MIGRATION_JOIN( \
                CAutoProjectResourceMigration_, UniqueID)() \
            { \
                ::iCAX::ProjectFile:: \
                    CProjectMigrationRegistrationCatalog::Register( \
                    [](::iCAX::ProjectFile:: \
                        CProjectMigrationRegistry& Registry_) \
                    { \
                        Registry_.RegisterResourceMigration( \
                            std::make_shared<MigrationType>()); \
                    }, \
                    this); \
            } \
        }; \
        const ICAX_PROJECT_MIGRATION_JOIN( \
            CAutoProjectResourceMigration_, UniqueID) \
            ICAX_PROJECT_MIGRATION_JOIN( \
                g_AutoProjectResourceMigration_, UniqueID); \
    }

#define ICAX_REGISTER_CURRENT_PROJECT_RESOURCE_SCHEMA( \
    ResourceTypeID, SchemaVersion) \
    ICAX_REGISTER_CURRENT_PROJECT_RESOURCE_SCHEMA_IMPL( \
        ResourceTypeID, SchemaVersion, __COUNTER__)

#define ICAX_REGISTER_CURRENT_PROJECT_RESOURCE_SCHEMA_IMPL( \
    ResourceTypeID, SchemaVersion, UniqueID) \
    namespace \
    { \
        struct ICAX_PROJECT_MIGRATION_JOIN( \
            CAutoCurrentProjectResourceSchema_, UniqueID) final \
        { \
            ICAX_PROJECT_MIGRATION_JOIN( \
                CAutoCurrentProjectResourceSchema_, UniqueID)() \
            { \
                ::iCAX::ProjectFile:: \
                    CProjectMigrationRegistrationCatalog::Register( \
                    [](::iCAX::ProjectFile:: \
                        CProjectMigrationRegistry& Registry_) \
                    { \
                        Registry_.SetCurrentResourceSchema( \
                            ResourceTypeID, SchemaVersion); \
                    }, \
                    this); \
            } \
        }; \
        const ICAX_PROJECT_MIGRATION_JOIN( \
            CAutoCurrentProjectResourceSchema_, UniqueID) \
            ICAX_PROJECT_MIGRATION_JOIN( \
                g_AutoCurrentProjectResourceSchema_, UniqueID); \
    }
