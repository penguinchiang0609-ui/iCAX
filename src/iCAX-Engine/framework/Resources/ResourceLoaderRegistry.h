#pragma once

#include "IResourceLoader.h"
#include "ResourceImportExport.h"
#include "ResourceLoaderRegistrationCatalog.h"

#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 资源加载器注册表。
        * @details
        *   Registry 按资源 C++ 类型保存 loader 列表。Load<T> 会用 typeid(T) 找到候选 loader，
        *   再由 loader 的 CanLoad 判断来源、选项是否适配。
        *   具体 Scene 可以持有自己的 Registry，从而隔离产品或 Scene 级加载能力。
        */
        class _RESOURCES_EXP CResourceLoaderRegistry final
        {
        public:
            /*
            * @brief 资源处理器选择规则。
            * @details
            *   规则来自产品配置或运行期配置。注册表选择 importer/exporter/loader 时会先匹配规则，
            *   再退回默认注册顺序。数值越大的 Priority 越优先。
            */
            struct CHandlerSelectionRule final
            {
                std::string Kind; //!< loader、importer 或 exporter。
                std::string ResourceTypeName; //!< 可选稳定资源类型名，例如 geometry.brep；为空表示不限制。
                std::string FormatID; //!< 可选格式 ID。
                std::vector<std::string> Extensions; //!< 可选扩展名列表。
                std::string ProviderID; //!< 可选 provider ID。
                std::string ModulePath; //!< 可选模块路径。
                int Priority = 0;
            };

        public:
            CResourceLoaderRegistry() = default;
            ~CResourceLoaderRegistry() = default;

            CResourceLoaderRegistry(IN const CResourceLoaderRegistry&) = delete;
            CResourceLoaderRegistry& operator=(IN const CResourceLoaderRegistry&) = delete;

        public:
            /*
            * @brief 注册加载器。
            * @param [in] ResourceType_ 资源 C++ 类型。
            * @param [in] pLoader_ 加载器实例，不能为空。
            * @return true 表示注册成功；false 表示相同实例或相同 loader 类型已存在。
            */
            bool RegisterLoader(IN const std::type_info& ResourceType_, IN const std::shared_ptr<IResourceLoader>& pLoader_);

            /*
            * @brief 注册加载器并附带运行期可选择的 provider/module 信息。
            */
            bool RegisterLoader(
                IN const std::type_info& ResourceType_,
                IN const std::shared_ptr<IResourceLoader>& pLoader_,
                IN const std::string& strProviderID_,
                IN const std::string& strModulePath_);

            template <typename TResource>
            /*
            * @brief 按模板资源类型注册加载器。
            * @param [in] pLoader_ 加载器实例。
            * @return true 表示注册成功。
            */
            bool RegisterLoader(IN const std::shared_ptr<IResourceLoader>& pLoader_)
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return RegisterLoader(typeid(TActualResource), pLoader_);
            }

            /*
            * @brief 获取指定资源类型的全部加载器。
            * @return 候选加载器列表；不存在时返回空数组。
            */
            std::vector<std::shared_ptr<IResourceLoader>> GetLoadersFor(IN const std::type_info& ResourceType_) const;

            template <typename TResource>
            /*
            * @brief 获取模板资源类型的全部加载器。
            */
            std::vector<std::shared_ptr<IResourceLoader>> GetLoadersFor() const
            {
                using TActualResource = std::remove_cv_t<std::remove_reference_t<TResource>>;
                return GetLoadersFor(typeid(TActualResource));
            }

            /*
            * @brief 为当前上下文查找第一个可用加载器。
            * @param [in] Context_ 加载上下文。
            * @return 可处理该上下文的加载器；找不到时返回 nullptr。
            */
            std::shared_ptr<IResourceLoader> FindLoaderFor(IN const CResourceLoadContext& Context_) const;

            /*
            * @brief 执行资源加载。
            * @param [in] Context_ 加载上下文。
            * @return 加载结果；找不到 loader 时返回 NoLoader。
            */
            CResourceLoadResult LoadResource(IN const CResourceLoadContext& Context_);

            /*
            * @brief 注册资源导入器。
            * @return true 表示注册成功；false 表示相同实例或相同 importer 类型已存在。
            */
            bool RegisterImporter(IN const std::shared_ptr<IResourceImporter>& pImporter_);

            /*
            * @brief 注册导入器并附带运行期可选择的 provider/module 信息。
            */
            bool RegisterImporter(
                IN const std::shared_ptr<IResourceImporter>& pImporter_,
                IN const std::string& strProviderID_,
                IN const std::string& strModulePath_);

            /*
            * @brief 注册资源导出器。
            * @return true 表示注册成功；false 表示相同实例或相同 exporter 类型已存在。
            */
            bool RegisterExporter(IN const std::shared_ptr<IResourceExporter>& pExporter_);

            /*
            * @brief 注册导出器并附带运行期可选择的 provider/module 信息。
            */
            bool RegisterExporter(
                IN const std::shared_ptr<IResourceExporter>& pExporter_,
                IN const std::string& strProviderID_,
                IN const std::string& strModulePath_);

            /*
            * @brief 设置资源处理器选择规则。
            */
            void SetSelectionRules(IN std::vector<CHandlerSelectionRule> Rules_);

            /*
            * @brief 追加一条资源处理器选择规则。
            */
            void AddSelectionRule(IN CHandlerSelectionRule Rule_);

            struct CLoaderEntry final
            {
                std::shared_ptr<IResourceLoader> Loader;
                std::string ProviderID;
                std::string ModulePath;
                std::type_index ResourceType = std::type_index(typeid(void));
                size_t Order = 0;
            };

            struct CImporterEntry final
            {
                std::shared_ptr<IResourceImporter> Importer;
                std::string ProviderID;
                std::string ModulePath;
                size_t Order = 0;
            };

            struct CExporterEntry final
            {
                std::shared_ptr<IResourceExporter> Exporter;
                std::string ProviderID;
                std::string ModulePath;
                size_t Order = 0;
            };

            /*
            * @brief 获取全部导入格式。
            */
            std::vector<CResourceFormatDescriptor> GetImportFormats() const;

            /*
            * @brief 获取全部导出格式。
            */
            std::vector<CResourceFormatDescriptor> GetExportFormats() const;

            /*
            * @brief 查找第一个可处理请求的导入器。
            */
            std::shared_ptr<IResourceImporter> FindImporterFor(IN const CResourceImportRequest& Request_) const;

            /*
            * @brief 查找第一个可处理请求的导出器。
            */
            std::shared_ptr<IResourceExporter> FindExporterFor(IN const CResourceLibrary& Library_, IN const CResourceExportRequest& Request_) const;

            /*
            * @brief 执行资源导入。
            */
            CResourceImportResult ImportResource(IN CResourceLibrary& Library_, IN const CResourceImportRequest& Request_);

            /*
            * @brief 执行资源导出。
            */
            CResourceExportResult ExportResource(IN const CResourceLibrary& Library_, IN const CResourceExportRequest& Request_);

        private:
            mutable std::shared_mutex m_Mutex;
            size_t m_nNextRegistrationOrder = 0;
            std::map<std::type_index, std::vector<CLoaderEntry>> m_Loaders;
            std::vector<CImporterEntry> m_Importers;
            std::vector<CExporterEntry> m_Exporters;
            std::vector<CHandlerSelectionRule> m_SelectionRules;
        };
    }
}

#define ICAX_RESOURCE_DETAIL_JOIN_IMPL(x, y) x##y
#define ICAX_RESOURCE_DETAIL_JOIN(x, y) ICAX_RESOURCE_DETAIL_JOIN_IMPL(x, y)

#define ICAX_REGISTER_RESOURCE_LOADER(ResourceClass, LoaderType) ICAX_REGISTER_RESOURCE_LOADER_PROVIDER(ResourceClass, #LoaderType, LoaderType)
#define ICAX_REGISTER_RESOURCE_LOADER_PROVIDER(ResourceClass, ProviderID, LoaderType) ICAX_REGISTER_RESOURCE_LOADER_IMPL(ResourceClass, ProviderID, LoaderType, __COUNTER__)
#define ICAX_REGISTER_RESOURCE_LOADER_IMPL(ResourceClass, ProviderID, LoaderType, UniqueID) \
    namespace \
    { \
        struct ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceLoaderRegistration_, UniqueID) final \
        { \
            ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceLoaderRegistration_, UniqueID)() \
            { \
                ::iCAX::Resource::CResourceLoaderRegistrationCatalog::Register([](::iCAX::Resource::CResourceLoaderRegistry& Registry_, const std::string& ModulePath_) \
                { \
                    Registry_.RegisterLoader(typeid(ResourceClass), std::make_shared<LoaderType>(), ProviderID, ModulePath_); \
                }, this); \
            } \
        }; \
        const ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceLoaderRegistration_, UniqueID) ICAX_RESOURCE_DETAIL_JOIN(g_AutoResourceLoaderRegistration_, UniqueID); \
    }

#define ICAX_REGISTER_RESOURCE_IMPORTER(ImporterType) ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER(#ImporterType, ImporterType)
#define ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER(ProviderID, ImporterType) ICAX_REGISTER_RESOURCE_IMPORTER_IMPL(ProviderID, ImporterType, __COUNTER__)
#define ICAX_REGISTER_RESOURCE_IMPORTER_IMPL(ProviderID, ImporterType, UniqueID) \
    namespace \
    { \
        struct ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceImporterRegistration_, UniqueID) final \
        { \
            ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceImporterRegistration_, UniqueID)() \
            { \
                ::iCAX::Resource::CResourceLoaderRegistrationCatalog::Register([](::iCAX::Resource::CResourceLoaderRegistry& Registry_, const std::string& ModulePath_) \
                { \
                    Registry_.RegisterImporter(std::make_shared<ImporterType>(), ProviderID, ModulePath_); \
                }, this); \
            } \
        }; \
        const ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceImporterRegistration_, UniqueID) ICAX_RESOURCE_DETAIL_JOIN(g_AutoResourceImporterRegistration_, UniqueID); \
    }

#define ICAX_REGISTER_RESOURCE_EXPORTER(ExporterType) ICAX_REGISTER_RESOURCE_EXPORTER_PROVIDER(#ExporterType, ExporterType)
#define ICAX_REGISTER_RESOURCE_EXPORTER_PROVIDER(ProviderID, ExporterType) ICAX_REGISTER_RESOURCE_EXPORTER_IMPL(ProviderID, ExporterType, __COUNTER__)
#define ICAX_REGISTER_RESOURCE_EXPORTER_IMPL(ProviderID, ExporterType, UniqueID) \
    namespace \
    { \
        struct ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceExporterRegistration_, UniqueID) final \
        { \
            ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceExporterRegistration_, UniqueID)() \
            { \
                ::iCAX::Resource::CResourceLoaderRegistrationCatalog::Register([](::iCAX::Resource::CResourceLoaderRegistry& Registry_, const std::string& ModulePath_) \
                { \
                    Registry_.RegisterExporter(std::make_shared<ExporterType>(), ProviderID, ModulePath_); \
                }, this); \
            } \
        }; \
        const ICAX_RESOURCE_DETAIL_JOIN(CAutoResourceExporterRegistration_, UniqueID) ICAX_RESOURCE_DETAIL_JOIN(g_AutoResourceExporterRegistration_, UniqueID); \
    }
