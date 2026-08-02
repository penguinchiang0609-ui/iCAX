#pragma once

#include "ProjectFileCodec.h"
#include "ProjectMigration.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::Database
{
    class IRepository;
}

namespace iCAX::Resource
{
    class CResourceLibrary;
}

namespace iCAX::ProjectFile
{
    /*
    * @brief 一个产品的项目文件固定定义。
    * @details 业务只在创建 CProjectFile 时配置一次，后续保存和打开只传
    *   Database 与 ResourceLibrary。
    */
    struct _PROJECT_FILE_EXP CProjectFileDefinition final
    {
        std::string Magic;
        std::string ProductID;
        std::string CurrentFormatVersion;
        uint32_t nCurrentFormatRevision = 0;
        EProjectFileEncoding DefaultEncoding =
            EProjectFileEncoding::Binary;

        // 为空时回放当前进程中的全部迁移；多产品宿主可限制为本产品 DLL。
        std::vector<std::string> MigrationModulePaths;
    };

    struct _PROJECT_FILE_EXP CProjectOpenResult final
    {
        CProjectDocumentInfo Info;
        EProjectFileEncoding SourceEncoding =
            EProjectFileEncoding::Binary;
        std::vector<std::string> MigrationDiagnostics;
    };

    /*
    * @brief 已完成读取、产品校验和版本升级的项目打开会话。
    * @details
    *   上层可以先读取 Info，以文件中的稳定 ID 创建 Project/MainScene，随后再把同一份
    *   已升级文档恢复到 Database 与 ResourceLibrary，避免重复读取及 TOCTOU 问题。
    */
    class _PROJECT_FILE_EXP CPreparedProjectOpen final
    {
    public:
        CPreparedProjectOpen();
        ~CPreparedProjectOpen();

        CPreparedProjectOpen(IN CPreparedProjectOpen&&) noexcept;
        CPreparedProjectOpen& operator=(IN CPreparedProjectOpen&&) noexcept;

        CPreparedProjectOpen(IN const CPreparedProjectOpen&) = delete;
        CPreparedProjectOpen& operator=(IN const CPreparedProjectOpen&) = delete;

        const CProjectOpenResult& Result() const noexcept;

    private:
        friend class CProjectFile;
        CProjectOpenResult m_Result;
        std::unique_ptr<CProjectDocument> m_pDocument;
    };

    /*
    * @brief 面向上层 target 的项目文件入口。
    * @details
    *   Save 自动从 Database 和 ResourceLibrary 采集持久化数据并原子落盘；
    *   Open 自动读取、校验、升级，再完整填充两个空的运行时容器。
    */
    class _PROJECT_FILE_EXP CProjectFile final
    {
    public:
        explicit CProjectFile(IN CProjectFileDefinition Definition_);
        ~CProjectFile();

        CProjectFile(IN const CProjectFile&) = delete;
        CProjectFile& operator=(IN const CProjectFile&) = delete;

        CProjectMigrationRegistry& Migrations() noexcept;

        CPreparedProjectOpen PrepareOpen(
            IN const std::filesystem::path& Path_) const;

        void Restore(
            IN const CPreparedProjectOpen& Prepared_,
            IN OUT iCAX::Database::IRepository& Database_,
            IN OUT iCAX::Resource::CResourceLibrary& Resources_) const;

        void Save(
            IN const std::filesystem::path& Path_,
            IN CProjectDocumentInfo Info_,
            IN const iCAX::Database::IRepository& Database_,
            IN const iCAX::Resource::CResourceLibrary& Resources_) const;

        void Save(
            IN const std::filesystem::path& Path_,
            IN CProjectDocumentInfo Info_,
            IN const iCAX::Database::IRepository& Database_,
            IN const iCAX::Resource::CResourceLibrary& Resources_,
            IN EProjectFileEncoding Encoding_) const;

        CProjectOpenResult Open(
            IN const std::filesystem::path& Path_,
            IN OUT iCAX::Database::IRepository& Database_,
            IN OUT iCAX::Resource::CResourceLibrary& Resources_) const;

    private:
        CProjectFileDefinition m_Definition;
        std::unique_ptr<CProjectMigrationRegistry> m_pMigrations;
    };
}
