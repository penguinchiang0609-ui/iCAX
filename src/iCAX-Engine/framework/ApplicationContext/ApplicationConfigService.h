#pragma once

#include "ApplicationContext.h"
#include <memory>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用配置服务。
        * @details
        *   ApplicationRuntime 的应用级 Facade 使用该写入器修改 Context 管理的配置。
        */
        class _APPLICATION_CONTEXT_EXP CApplicationConfigService final
        {
        public:
            /*
            * @brief 构造应用配置服务。
            * @param [in] pContext_ 应用上下文，不能为空。
            */
            explicit CApplicationConfigService(IN std::shared_ptr<CApplicationContext> pContext_);
            ~CApplicationConfigService() = default;

            CApplicationConfigService(IN const CApplicationConfigService&) = delete;
            CApplicationConfigService& operator=(IN const CApplicationConfigService&) = delete;

        public:
            /*
            * @brief 获取当前应用设置。
            * @return 上下文中的当前设置快照。
            */
            iCAX::Data::PropertyBag GetSettings() const;

            /*
            * @brief 设置顶层配置值。
            */
            void SetValue(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_);

            /*
            * @brief 设置分层路径下的配置值。
            */
            void SetValue(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_);

            /*
            * @brief 保存当前设置到配置存储。
            */
            void Save() const;

            /*
            * @brief 从配置存储重新加载设置并替换上下文中的 Settings。
            */
            void Reload();

        private:
            std::shared_ptr<CApplicationContext> m_pContext;
        };
    }
}
