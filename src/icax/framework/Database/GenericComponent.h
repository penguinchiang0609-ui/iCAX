#pragma once
#include "Database.h"
#include "ComponentBase.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 通用组件
        * @remark 当高版本软件在低版本打开时会遇到不支持的组件数据，此时这些数据会以通用组件承载。
        */
        class _DATABASE_EXP CGenericComponent final : public CComponentBase
        {
        public:
            /*
            * @brief 构造函数
            * @param [in] pEntity_
            * @param [in] strClassName_
            */
            CGenericComponent(IN std::shared_ptr<class IEntity> pEntity_, IN const std::string& strClassName_);

            /*
            * @brief 析构函数
            */
            virtual ~CGenericComponent();

        public:
            /*
            * @brief 获取类型
            * @return ComponentType
            */
            virtual std::string GetComponentClass() const override;

            /*
            * @brief 获取属性名称列表
            * @return std::vector<std::string>
            */
            virtual std::vector<std::string> GetPropertyNameArray() const override;

            /*
            * @brief 获取属性值
            * @return strPropertyName_
            */
            virtual PropertyValue GetProperty(IN const std::string& strPropertyName_) const override;

            /*
            * @brief 设置属性
            * @param [in] strPropertyName_ 属性名称
            * @param [in] NewValue_ 值
            */
            virtual void OnSetProperty(IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_) override;

        private:
            std::string m_strClassName;
            PropertySet m_Properties;
        };
    }
}