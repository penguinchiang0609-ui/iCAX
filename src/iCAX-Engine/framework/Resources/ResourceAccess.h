#pragma once

#include "FlatBufferResource.h"
#include "ResourcesExport.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace iCAX::Resource
{
    class CResourceLibrary;

    /*
    * @brief Resource API 使用的 HTTP/REST 风格方法。
    * @details 这些方法描述资源语义，不要求底层真正运行 HTTP Server。
    */
    enum class EResourceMethod : uint8_t
    {
        Head = 0,
        Get = 1,
        Put = 2,
        Delete = 3,
        Options = 4
    };

    using CResourceHeaders = std::map<std::string, std::string>;

    /*
    * @brief 一次直接资源访问请求。
    * @details URL 是资源身份；Headers 和 Body 采用 HTTP 对应语义。
    */
    struct _RESOURCES_EXP CResourceRequest final
    {
        EResourceMethod Method = EResourceMethod::Get;
        std::string URL;
        CResourceHeaders Headers;
        CFlatBufferResource Body;
    };

    /*
    * @brief 一次直接资源访问响应。
    */
    struct _RESOURCES_EXP CResourceResponse final
    {
        uint16_t nStatus = 500;
        CResourceHeaders Headers;
        CFlatBufferResource Body;

        bool IsSuccessful() const noexcept
        {
            return nStatus >= 200 && nStatus < 300;
        }

        bool HasBody() const noexcept
        {
            return !Body.Empty();
        }
    };

    /*
    * @brief 解析 REST 方法名。
    * @throws std::invalid_argument 方法不受支持时抛出。
    */
    _RESOURCES_EXP EResourceMethod ParseResourceMethod(const std::string& strMethod_);

    /*
    * @brief 获取 REST 方法名。
    */
    _RESOURCES_EXP const char* ToString(EResourceMethod Method_) noexcept;

    /*
    * @brief 按 HTTP 规则进行不区分大小写的 Header 查询。
    */
    _RESOURCES_EXP std::optional<std::string> GetResourceHeader(
        const CResourceHeaders& Headers_,
        const std::string& strName_);

    /*
    * @brief 设置 Header；会覆盖大小写不同但名称相同的旧值。
    */
    _RESOURCES_EXP void SetResourceHeader(
        CResourceHeaders& Headers_,
        const std::string& strName_,
        std::string strValue_);

    /*
    * @brief Scene ResourceLibrary 的直接资源访问服务。
    * @details
    *   服务不经过 SDO 邮件；调用方直接提交请求并取得响应。
    *   当前规范资源表示为不可变 Google FlatBuffer。
    */
    class _RESOURCES_EXP CResourceAccessService final
    {
    public:
        explicit CResourceAccessService(CResourceLibrary& Library_) noexcept;

        CResourceResponse Request(const CResourceRequest& Request_);

    private:
        CResourceResponse HeadOrGet(const CResourceRequest& Request_, bool bIncludeBody_);
        CResourceResponse Put(const CResourceRequest& Request_);
        CResourceResponse Delete(const CResourceRequest& Request_);
        CResourceResponse Options(const CResourceRequest& Request_) const;

    private:
        CResourceLibrary& m_Library;
    };
}
