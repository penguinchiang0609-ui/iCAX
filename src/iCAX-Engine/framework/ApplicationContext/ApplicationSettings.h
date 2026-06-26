#pragma once

#include "ApplicationContextExport.h"

#include <Data/PropertyBag.h>

namespace iCAX
{
    namespace Application
    {
        /*
        * @brief 应用设置。
        * @details
        *   以 PropertyBag 保存键值和分层路径。它是应用级配置数据，不应存放项目级或产品级运行数据。
        */
        class _APPLICATION_CONTEXT_EXP CApplicationSettings final
        {
        public:
            /*
            * @brief 构造空设置。
            */
            CApplicationSettings();

            /*
            * @brief 从属性集合构造设置。
            * @param [in] Properties_ 初始属性集合。
            */
            explicit CApplicationSettings(IN const iCAX::Data::PropertySet& Properties_);
            ~CApplicationSettings();

        public:
            /*
            * @brief 设置顶层键值。
            * @param [in] strKey_ 键。
            * @param [in] Value_ 值。
            */
            void Set(IN const std::string& strKey_, IN const iCAX::Data::Variant& Value_);

            /*
            * @brief 设置分层路径下的键值。
            * @param [in] strKey_ 键。
            * @param [in] strPath_ 路径。
            * @param [in] Value_ 值。
            */
            void Set(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Value_);

            /*
            * @brief 获取顶层键值。
            * @return 找到时返回配置值，否则返回 Default_。
            */
            iCAX::Data::Variant Get(IN const std::string& strKey_, IN const iCAX::Data::Variant& Default_ = iCAX::Data::Variant()) const;

            /*
            * @brief 获取分层路径下的键值。
            * @return 找到时返回配置值，否则返回 Default_。
            */
            iCAX::Data::Variant Get(IN const std::string& strKey_, IN const std::string& strPath_, IN const iCAX::Data::Variant& Default_ = iCAX::Data::Variant()) const;

            /*
            * @brief 获取底层属性集合。
            * @return 只读属性集合引用。
            */
            const iCAX::Data::PropertySet& GetProperties() const;

        private:
            iCAX::Data::PropertyBag m_Bag;
        };
    }
}
