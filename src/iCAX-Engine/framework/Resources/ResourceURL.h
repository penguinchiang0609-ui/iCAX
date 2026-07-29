#pragma once

#include "Data/uuid.h"
#include "ResourcesExport.h"

#include <cstdint>
#include <optional>
#include <string>

namespace iCAX::Resource
{
    /*
    * @brief 资源所属的运行时层级。
    */
    enum class EResourceScope : uint8_t
    {
        Application = 0,
        Product = 1,
        Project = 2,
        Scene = 3
    };

    /*
    * @brief 一个资源池的稳定层级身份。
    * @details
    *   ApplicationID/ProductID 必须是持久化 ID，不能使用运行期 ChannelID。
    *   ProjectID/SceneID 使用持久化 GUID。
    */
    struct _RESOURCES_EXP CResourceScope final
    {
        EResourceScope Scope = EResourceScope::Application;
        std::string ApplicationID;
        std::string ProductID;
        iCAX::Data::uuid ProjectID;
        iCAX::Data::uuid SceneID;

        bool IsValid() const noexcept;
    };

    /*
    * @brief 解析后的规范资源 URL。
    * @details ResourceID 为 nil 时表示对应作用域的 resources 集合 URL。
    */
    struct _RESOURCES_EXP CResourceURL final
    {
        CResourceScope Owner;
        iCAX::Data::uuid ResourceID;

        bool IsCollection() const noexcept;
        bool IsResource() const noexcept;
    };

    _RESOURCES_EXP CResourceScope MakeApplicationResourceScope(
        const std::string& strApplicationID_);
    _RESOURCES_EXP CResourceScope MakeProductResourceScope(
        const std::string& strApplicationID_,
        const std::string& strProductID_);
    _RESOURCES_EXP CResourceScope MakeProjectResourceScope(
        const std::string& strApplicationID_,
        const std::string& strProductID_,
        const iCAX::Data::uuid& ProjectID_);
    _RESOURCES_EXP CResourceScope MakeSceneResourceScope(
        const std::string& strApplicationID_,
        const std::string& strProductID_,
        const iCAX::Data::uuid& ProjectID_,
        const iCAX::Data::uuid& SceneID_);

    /*
    * @brief 生成新的调用方可预分配 ResourceID。
    */
    _RESOURCES_EXP iCAX::Data::uuid GenerateResourceID();

    /*
    * @brief 构造规范集合 URL 或资源 URL。
    */
    _RESOURCES_EXP std::string MakeResourceCollectionURL(
        const CResourceScope& Scope_);
    _RESOURCES_EXP std::string MakeResourceURL(
        const CResourceScope& Scope_,
        const iCAX::Data::uuid& ResourceID_);

    /*
    * @brief 解析规范 resource:// URL。
    * @throws std::invalid_argument URL 非规范、层级无效或 GUID 无效时抛出。
    */
    _RESOURCES_EXP CResourceURL ParseResourceURL(
        const std::string& strURL_);
    _RESOURCES_EXP std::optional<CResourceURL> TryParseResourceURL(
        const std::string& strURL_) noexcept;

    _RESOURCES_EXP bool ResourceScopeEquals(
        const CResourceScope& Left_,
        const CResourceScope& Right_) noexcept;
}
