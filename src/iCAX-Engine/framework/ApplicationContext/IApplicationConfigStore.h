#pragma once

#include "ApplicationContextExport.h"
#include "Data/PropertyBag.h"

#include <string>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用配置存储接口。
        * @details 负责把应用级 PropertyBag 和外部存储互相转换；当前默认实现是文件。
        */
        class _APPLICATION_CONTEXT_EXP IApplicationConfigStore
        {
        public:
            IApplicationConfigStore() = default;
            virtual ~IApplicationConfigStore() = default;

            IApplicationConfigStore(IN const IApplicationConfigStore&) = delete;
            IApplicationConfigStore& operator=(IN const IApplicationConfigStore&) = delete;

        public:
            /*
            * @brief 加载应用设置。
            * @param [in] strPath_ 配置路径。
            * @return 加载后的设置；文件不存在时可返回空设置。
            */
            virtual iCAX::Data::PropertyBag Load(IN const std::string& strPath_) const = 0;

            /*
            * @brief 保存应用设置。
            * @param [in] strPath_ 配置路径。
            * @param [in] Settings_ 待保存设置。
            */
            virtual void Save(IN const std::string& strPath_, IN const iCAX::Data::PropertyBag& Settings_) const = 0;
        };
    }
}
