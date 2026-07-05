#pragma once

#include "ResourceInfo.h"
#include "ResourcesExport.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        class CResourceLibrary;

        /*
        * @brief 资源格式描述。
        * @details
        *   格式由插件声明，框架只按 FormatID、扩展名和 CanImport/CanExport 做调度。
        */
        struct _RESOURCES_EXP CResourceFormatDescriptor final
        {
            std::string FormatID; //!< 稳定格式 ID，例如 cad.step、cad.iges、image.png。
            std::string DisplayName; //!< UI 展示名。
            std::vector<std::string> Extensions; //!< 支持扩展名，包含点号，例如 .step。
            std::vector<std::string> MimeTypes; //!< 可选 MIME 类型。
            bool bCanImport = false; //!< 是否支持导入。
            bool bCanExport = false; //!< 是否支持导出。
        };

        /*
        * @brief 资源导入请求。
        * @details
        *   导入器负责把外部来源转换为一个或多个 Scene.Resources 资源，并返回资源角色。
        */
        struct _RESOURCES_EXP CResourceImportRequest final
        {
            std::string SourcePath; //!< 外部来源文件路径或 URI。
            std::string FormatID; //!< 可选格式 ID；为空时由导入器按来源自行判断。
            std::string TargetResourceID; //!< 可选主资源 ID；为空时导入器按来源生成。
            EResourcePersistenceMode Persistence = EResourcePersistenceMode::Embedded; //!< 导入后持久化语义。
            std::map<std::string, std::string> Options; //!< 导入器扩展参数。
        };

        /*
        * @brief 单个导入资源项。
        */
        struct _RESOURCES_EXP CResourceImportItem final
        {
            std::string Role; //!< 资源角色，例如 source、geometry.brep、preview.mesh。
            std::string ResourceID; //!< Scene.Resources 中的资源 key。
            CResourceInfo Info; //!< 资源信息快照。
        };

        /*
        * @brief 资源导入结果状态。
        */
        enum class EResourceImportExportStatus : uint8_t
        {
            Ok = 0,
            NoHandler = 1,
            Unsupported = 2,
            InvalidRequest = 3,
            Failed = 4
        };

        /*
        * @brief 资源导入结果。
        */
        struct _RESOURCES_EXP CResourceImportResult final
        {
            EResourceImportExportStatus Status = EResourceImportExportStatus::Failed; //!< 结果状态。
            std::string PrimaryResourceID; //!< 主资源 key。
            std::vector<CResourceImportItem> Items; //!< 本次导入产生的资源项。
            std::string Error; //!< 失败原因。
            std::map<std::string, std::string> Metadata; //!< 导入器扩展元数据。

            bool IsOK() const noexcept
            {
                return Status == EResourceImportExportStatus::Ok;
            }

            static CResourceImportResult Succeeded(IN const std::string& strPrimaryResourceID_, IN std::vector<CResourceImportItem> Items_);
            static CResourceImportResult NoHandler(IN const CResourceImportRequest& Request_);
            static CResourceImportResult Unsupported(IN const CResourceImportRequest& Request_, IN const std::string& strMessage_);
            static CResourceImportResult Invalid(IN const CResourceImportRequest& Request_, IN const std::string& strMessage_);
            static CResourceImportResult Failed(IN const CResourceImportRequest& Request_, IN const std::string& strMessage_);
        };

        /*
        * @brief 资源导出请求。
        */
        struct _RESOURCES_EXP CResourceExportRequest final
        {
            std::string ResourceID; //!< 要导出的资源 key。
            std::string TargetPath; //!< 目标文件路径。
            std::string FormatID; //!< 可选格式 ID。
            std::map<std::string, std::string> Options; //!< 导出器扩展参数。
        };

        /*
        * @brief 资源导出结果。
        */
        struct _RESOURCES_EXP CResourceExportResult final
        {
            EResourceImportExportStatus Status = EResourceImportExportStatus::Failed; //!< 结果状态。
            std::string TargetPath; //!< 实际输出路径。
            std::string FormatID; //!< 实际导出格式。
            std::vector<std::string> ResourceIDs; //!< 参与导出的资源 key。
            std::string Error; //!< 失败原因。
            std::map<std::string, std::string> Metadata; //!< 导出器扩展元数据。

            bool IsOK() const noexcept
            {
                return Status == EResourceImportExportStatus::Ok;
            }

            static CResourceExportResult Succeeded(IN const std::string& strTargetPath_, IN const std::string& strFormatID_, IN std::vector<std::string> ResourceIDs_);
            static CResourceExportResult NoHandler(IN const CResourceExportRequest& Request_);
            static CResourceExportResult Unsupported(IN const CResourceExportRequest& Request_, IN const std::string& strMessage_);
            static CResourceExportResult Invalid(IN const CResourceExportRequest& Request_, IN const std::string& strMessage_);
            static CResourceExportResult Failed(IN const CResourceExportRequest& Request_, IN const std::string& strMessage_);
        };

        /*
        * @brief 资源导入器接口。
        * @details
        *   插件实现该接口并通过宏注册，即可在不修改框架的情况下新增格式导入能力。
        */
        class _RESOURCES_EXP IResourceImporter
        {
        public:
            IResourceImporter() = default;
            virtual ~IResourceImporter() = default;

            IResourceImporter(IN const IResourceImporter&) = delete;
            IResourceImporter& operator=(IN const IResourceImporter&) = delete;

        public:
            virtual std::vector<CResourceFormatDescriptor> GetImportFormats() const = 0;
            virtual bool CanImport(IN const CResourceImportRequest& Request_) const = 0;
            virtual CResourceImportResult Import(IN CResourceLibrary& Library_, IN const CResourceImportRequest& Request_) = 0;
        };

        /*
        * @brief 资源导出器接口。
        */
        class _RESOURCES_EXP IResourceExporter
        {
        public:
            IResourceExporter() = default;
            virtual ~IResourceExporter() = default;

            IResourceExporter(IN const IResourceExporter&) = delete;
            IResourceExporter& operator=(IN const IResourceExporter&) = delete;

        public:
            virtual std::vector<CResourceFormatDescriptor> GetExportFormats() const = 0;
            virtual bool CanExport(IN const CResourceLibrary& Library_, IN const CResourceExportRequest& Request_) const = 0;
            virtual CResourceExportResult Export(IN const CResourceLibrary& Library_, IN const CResourceExportRequest& Request_) = 0;
        };
    }
}
