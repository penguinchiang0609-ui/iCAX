#pragma once
#include "Data.h"
#include "Variant.h"

namespace iCAX
{
    namespace Data
    {
        /*
        * @brief 属性包
        * @details
        *   PropertySet的包装
        */
        struct _DATA_EXP PropertyBag final
        {
        public:
            /*
            * @brief 构造函数
            */
            PropertyBag();

            /*
            * @brief 构造函数
            * @param [in] Properties_
            */
            PropertyBag(IN const PropertySet& Properties_);

            /*
            * @brief 析构函数
            */
            ~PropertyBag();

        public:
            /*
            * @brief 设置
            */
            void Set(const std::string& strkey_, const iCAX::Data::Variant& Varient_);

            /*
            * @brief 获取设置项/值
            * @param [in] strkey_
            * @param [in] Default_
            */
            iCAX::Data::Variant Get(const std::string& strkey_, const iCAX::Data::Variant& Default_) const;

            /*
            * @brief 获取设置项/值
            * @param [in] strkey_
            * @param [in] strPath_
            * @param [in] Default_
            */
            iCAX::Data::Variant Get(const std::string& strkey_, const std::string& strPath_, const  iCAX::Data::Variant& Default_) const;

            /*
            * @brief 设置
            * @param [in] strkey_
            * @param [in] strPath_
            * @param [in] Varient_
            */
            void Set(const std::string& strkey_, const std::string& strPath_, const iCAX::Data::Variant& Varient_);

            /*
            * @brief 获取属性集
            * @return const PropertySet&
            */
            const PropertySet& GetProperties() const;

        private:
            PropertySet m_Properties;
        };
    }
}