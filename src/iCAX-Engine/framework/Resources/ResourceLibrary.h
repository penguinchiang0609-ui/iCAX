#pragma once

#include "ResourceImportExport.h"
#include "ResourceInfo.h"
#include "ResourceTypeName.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        class CResourceLoaderRegistry;
        class CResourcePool;

        /*
        * @brief 资源库。
        * @details
        *   ResourceLibrary 是 Scene 对外暴露的资源入口，内部拥有一个 ResourcePool，并使用注入的 LoaderRegistry 执行加载。
        *   上层推荐通过 scene.Resources().Load<T>(path) 使用资源，而不是直接操作全局 loader 或静态资源池。
        */
        class _RESOURCES_EXP CResourceLibrary final
        {
        public:
            /*
            * @brief 构造独立资源库。
            * @details 默认创建自己的 ResourcePool 和 LoaderRegistry。
            */
            CResourceLibrary();

            /*
            * @brief 构造使用指定加载器注册表的资源库。
            * @param [in] pLoaderRegistry_ 加载器注册表；为空时加载会抛出逻辑错误。
            */
            explicit CResourceLibrary(IN std::shared_ptr<CResourceLoaderRegistry> pLoaderRegistry_);
            ~CResourceLibrary();

            CResourceLibrary(IN const CResourceLibrary&) = delete;
            CResourceLibrary& operator=(IN const CResourceLibrary&) = delete;
            CResourceLibrary(IN CResourceLibrary&& Other_) noexcept;
            CResourceLibrary& operator=(IN CResourceLibrary&& Other_) noexcept;

        public:
            template <typename T>
            /*
            * @brief 加载或获取资源。
            * @param [in] strSource_ 资源来源，同时作为 key。
            * @param [in] Info_ 附加资源信息。
            * @param [in] Options_ 加载选项。
            * @return 类型匹配的资源对象；来源为空、类型冲突或加载失败时返回 nullptr。
            * @details
            *   先查 ResourcePool 缓存；缓存命中则直接返回。
            *   缓存未命中时按 typeid(T) 查找 loader，加载成功后写回 ResourcePool。
            */
            std::shared_ptr<T> Load(
                IN const std::string& strSource_,
                IN const CResourceInfo& Info_ = CResourceInfo(),
                IN const std::map<std::string, std::string>& Options_ = {})
            {
                if (strSource_.empty())
                {
                    return nullptr;
                }

                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                auto _pResource = LoadUntyped(strSource_, typeid(TResource), Info_, Options_);
                if (!_pResource)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<TResource>(_pResource);
            }

            template <typename T>
            /*
            * @brief 按目标资源类型导入外部文件。
            * @param [in] strSourcePath_ 外部文件路径或 URI。
            * @param [in] Persistence_ 导入后资源持久化语义。
            * @param [in] Options_ 导入选项。
            * @param [in] strFormatID_ 可选格式 ID；为空时导入器按后缀、magic 或自身规则判断。
            * @return 导入后保存到资源库中的第一个 T 类型资源；找不到导入器、导入失败或结果没有 T 类型资源时返回 nullptr。
            * @details
            *   这是上层推荐使用的导入入口。调用方只需要表达“我要什么类型”和“从哪里来”，
            *   ResourceLibrary 会把目标 C++ 类型写入请求，并由 ResourceLoaderRegistry 选择导入器。
            */
            std::shared_ptr<T> Import(
                IN const std::string& strSourcePath_,
                IN EResourcePersistenceMode Persistence_ = EResourcePersistenceMode::Embedded,
                IN const std::map<std::string, std::string>& Options_ = {},
                IN const std::string& strFormatID_ = {})
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                CResourceImportRequest _Request;
                _Request.SourcePath = strSourcePath_;
                _Request.Persistence = Persistence_;
                _Request.Options = Options_;
                _Request.FormatID = strFormatID_;
                _Request.TargetResourceTypeName = GetResourceTypeName<TResource>();
                return Import<T>(std::move(_Request));
            }

            template <typename T>
            /*
            * @brief 按目标资源类型导入外部资源，并可取回完整导入结果。
            * @param [in] Request_ 导入请求；TargetResourceType 会被模板类型覆盖。
            * @param [out] pResult_ 可选完整导入结果，用于需要读取附带资源角色的场景。
            */
            std::shared_ptr<T> Import(IN CResourceImportRequest Request_, CResourceImportResult* pResult_ = nullptr)
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                Request_.TargetResourceType = std::type_index(typeid(TResource));
                if (Request_.TargetResourceTypeName.empty())
                {
                    Request_.TargetResourceTypeName = GetResourceTypeName<TResource>();
                }

                auto _Result = Import(Request_);
                if (pResult_)
                {
                    *pResult_ = _Result;
                }
                if (!_Result.IsOK())
                {
                    return nullptr;
                }

                if (!_Result.PrimaryResourceID.empty())
                {
                    auto _pPrimary = Get<TResource>(_Result.PrimaryResourceID);
                    if (_pPrimary)
                    {
                        return _pPrimary;
                    }
                }

                for (const auto& _Item : _Result.Items)
                {
                    auto _pResource = Get<TResource>(_Item.ResourceID);
                    if (_pResource)
                    {
                        return _pResource;
                    }
                }

                return nullptr;
            }

            template <typename T>
            /*
            * @brief 手动设置资源对象。
            * @param [in] strSource_ 资源来源，同时作为 key。
            * @param [in] pResource_ 资源对象。
            * @param [in] Info_ 资源信息。
            */
            void Set(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                SetUntyped(strSource_, std::static_pointer_cast<void>(std::move(pResource_)), typeid(TResource), Info_);
            }

            template <typename T>
            /*
            * @brief 尝试手动新增资源对象。
            * @return true 表示新增成功；false 表示同来源资源已存在。
            */
            bool TryAdd(IN const std::string& strSource_, IN std::shared_ptr<T> pResource_, IN const CResourceInfo& Info_ = CResourceInfo())
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                return TryAddUntyped(strSource_, std::static_pointer_cast<void>(std::move(pResource_)), typeid(TResource), Info_);
            }

            template <typename T>
            /*
            * @brief 获取已加载资源对象。
            * @return 类型匹配的资源对象；未找到或类型不匹配时返回 nullptr。
            */
            std::shared_ptr<T> Get(IN const std::string& strSource_) const
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;
                auto _pResource = GetUntyped(strSource_, typeid(TResource));
                if (!_pResource)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<TResource>(_pResource);
            }

            /*
            * @brief 登记资源信息。
            * @param [in] strSource_ 资源来源，同时作为 key。
            * @param [in] Info_ 资源信息。
            */
            void Register(IN const std::string& strSource_, IN const CResourceInfo& Info_ = CResourceInfo());

            /*
            * @brief 尝试登记资源信息。
            * @return true 表示新增成功。
            */
            bool TryRegister(IN const std::string& strSource_, IN const CResourceInfo& Info_ = CResourceInfo());

            /*
            * @brief 判断资源记录是否存在。
            */
            bool Contains(IN const std::string& strSource_) const;

            /*
            * @brief 判断资源对象是否已加载。
            */
            bool HasObject(IN const std::string& strSource_) const;

            /*
            * @brief 卸载资源对象但保留资源信息。
            */
            bool Unload(IN const std::string& strSource_);

            /*
            * @brief 移除资源记录和对象。
            */
            bool Remove(IN const std::string& strSource_);

            /*
            * @brief 清空资源库。
            */
            void Clear();

            /*
            * @brief 获取资源记录数量。
            */
            size_t Count() const;

            /*
            * @brief 获取资源信息。
            */
            std::optional<CResourceInfo> GetInfo(IN const std::string& strSource_) const;

            /*
            * @brief 获取资源内容版本。
            * @return 资源存在时返回资源版本，否则返回 0。
            */
            uint64_t GetVersion(IN const std::string& strSource_) const;

            /*
            * @brief 标记资源内容发生变化并递增版本。
            * @return 递增后的资源版本；资源不存在时返回 0。
            */
            uint64_t Touch(IN const std::string& strSource_);

            /*
            * @brief 更新资源信息。
            */
            bool UpdateInfo(IN const std::string& strSource_, IN const CResourceInfo& Info_);

            /*
            * @brief 获取全部资源键。
            */
            std::vector<CResourceKey> GetKeys() const;

            /*
            * @brief 获取全部资源信息。
            */
            std::vector<CResourceInfo> GetInfos() const;

            /*
            * @brief 获取资源清单。
            * @param [in] bIncludeRuntimeOnly_ true 表示包含运行期资源。
            */
            std::vector<CResourceInfo> GetManifest(IN bool bIncludeRuntimeOnly_ = false) const;

            /*
            * @brief 获取当前资源库可用的导入格式。
            * @details
            *   格式来自注入的 ResourceLoaderRegistry。产品或插件通过导入器注册扩展格式，
            *   ResourceLibrary 只负责暴露当前 Scene 可见的能力。
            */
            std::vector<CResourceFormatDescriptor> GetImportFormats() const;

            /*
            * @brief 获取当前资源库可用的导出格式。
            */
            std::vector<CResourceFormatDescriptor> GetExportFormats() const;

            /*
            * @brief 导入外部资源。
            * @param [in] Request_ 导入请求，通常包含外部文件路径、可选格式 ID 和导入选项。
            * @return 导入结果；成功时 Items 中包含本次生成的一组资源及其角色。
            * @details
            *   该方法是资源导入的统一入口。调用方不关心具体格式由哪个插件处理，
            *   Registry 会按 CanImport 选择合适的 IResourceImporter。
            */
            CResourceImportResult Import(IN const CResourceImportRequest& Request_);

            /*
            * @brief 导出资源到外部目标。
            * @param [in] Request_ 导出请求，包含资源 ID、目标路径、可选格式 ID 和导出选项。
            * @return 导出结果。
            */
            CResourceExportResult Export(IN const CResourceExportRequest& Request_) const;

            template <typename T>
            /*
            * @brief 按源资源类型导出资源到外部文件。
            * @param [in] strResourceID_ 资源库中的资源 ID。
            * @param [in] strTargetPath_ 目标文件路径。
            * @param [in] Options_ 导出选项。
            * @param [in] strFormatID_ 可选格式 ID；为空时导出器按目标后缀或自身规则判断。
            */
            CResourceExportResult Export(
                IN const std::string& strResourceID_,
                IN const std::string& strTargetPath_,
                IN const std::map<std::string, std::string>& Options_ = {},
                IN const std::string& strFormatID_ = {}) const
            {
                using TResource = std::remove_cv_t<std::remove_reference_t<T>>;

                CResourceExportRequest _Request;
                _Request.ResourceID = strResourceID_;
                _Request.TargetPath = strTargetPath_;
                _Request.FormatID = strFormatID_;
                _Request.Options = Options_;
                _Request.SourceResourceType = std::type_index(typeid(TResource));
                _Request.SourceResourceTypeName = GetResourceTypeName<TResource>();
                return Export(_Request);
            }

        private:
            /*
            * @brief 获取资源池。
            * @return 内部资源池引用；如果移动后资源池为空，会重新创建。
            */
            CResourcePool& GetPool();

            /*
            * @brief 获取只读资源池。
            * @return 内部资源池引用。
            * @throws std::logic_error 当资源库已被移动且资源池为空时抛出。
            */
            const CResourcePool& GetPool() const;

            /*
            * @brief 无类型加载入口。
            * @return 加载或缓存命中的资源对象；失败时返回 nullptr。
            */
            std::shared_ptr<void> LoadUntyped(
                IN const std::string& strSource_,
                IN const std::type_info& RuntimeType_,
                IN const CResourceInfo& Info_,
                IN const std::map<std::string, std::string>& Options_);
            /*
            * @brief 无类型设置资源对象。
            */
            void SetUntyped(
                IN const std::string& strSource_,
                IN std::shared_ptr<void> pResource_,
                IN const std::type_info& RuntimeType_,
                IN const CResourceInfo& Info_);
            /*
            * @brief 无类型尝试新增资源对象。
            */
            bool TryAddUntyped(
                IN const std::string& strSource_,
                IN std::shared_ptr<void> pResource_,
                IN const std::type_info& RuntimeType_,
                IN const CResourceInfo& Info_);
            /*
            * @brief 无类型获取资源对象。
            */
            std::shared_ptr<void> GetUntyped(
                IN const std::string& strSource_,
                IN const std::type_info& RuntimeType_) const;

        private:
            std::unique_ptr<CResourcePool> m_pPool;
            std::shared_ptr<CResourceLoaderRegistry> m_pLoaderRegistry;
        };
    }
}
