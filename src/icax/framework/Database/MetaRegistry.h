#pragma once
#include "Database.h"
#include "IMetaRegistry.h"
#include <vector>

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 元数据注册表
        */
        class CMetaRegistry : public IMetaRegistry
        {
        private:
            /*
            * @brief 属性元数据
            */
            struct PropertyMeta
            {
                std::string Name;//!< 属性名称
                EPropertyKind Kind = EPropertyKind::Value;
                EPropertyPersistence Persistence = EPropertyPersistence::Persistent;
                EPropertyChangePolicy ChangePolicy = EPropertyChangePolicy::Transactional;
                std::function<PropertyValue(const void*)> Getter;//!< 属性的获取方法
                std::function<void(void*, const PropertyValue&)> Setter;//!< 属性的设置方法
                DerivedPropertyEvaluator Evaluator;//!< 派生属性计算器
            };

            /*
            * @brief 组件元数据
            */
            struct ComponentMeta
            {
                std::string strComponentClass;          //!< 组件类型名称
                std::function<std::shared_ptr<CComponentBase>(IN std::shared_ptr<IEntity>)> Creator;//!< 组件的构建方法
                std::unordered_set<std::shared_ptr<IAttribute>> vecAttributes;        //!< 所有特性
                std::unordered_set<std::shared_ptr<IChecker>> vecCheckers;            //!< 约束器
                std::vector<uint32_t> vecPropertyOrder;                               //!< 属性注册顺序
                std::unordered_map<uint32_t, PropertyMeta> mapProperties;                       //!< 属性元数据
            };

        public:
            /*
            * @brief 构造函数
            */
            CMetaRegistry() = default;

            /*
            * @brief 析构函数
            */
            virtual ~CMetaRegistry() = default;

        public:
            /*
            * @brief 注册类型
            * @param [in] strComponentClass_
            * @param [in] strParentComponentClass_
            */
            virtual void RegistType(IN const std::string& strComponentClass_, IN const std::string& strParentComponentClass_) override;

            /*
            * @brief 是否属于继承关系
            * @param [in] strComponentClass_
            * @param [in] strParentComponentClass_
            * @return bool
            */
            virtual bool IsInheritance(IN const std::string& strComponentClass_, IN const std::string& strParentComponentClass_) override;

            /*
            * @brief 是否包含组件类型
            */
            virtual bool HasTypeByName(IN const std::string& strComponentClass_) const override;

        public://! 构造器
            /*
            * @brief 注册构造函数
            * @param [in] strComponentClass_
            * @param [in] Creator_
            */
            virtual void RegistCreatorByName(IN const std::string& strComponentClass_, IN const std::function<std::shared_ptr<CComponentBase>(IN std::shared_ptr<IEntity>)>& Creator_) override;

            /*
            * @brief 构造
            * @param [in] strComponentClass_
            * @param [in] pEntity_
            */
            virtual std::shared_ptr<CComponentBase> CreateByName(IN const std::string& strComponentClass_, IN const std::shared_ptr<IEntity>& pEntity_) const override;

            /*
            * @brief 是否包含指定组件的构造
            * @param [in] strComponentClass_
            */
            virtual bool HasCreatorByName(IN const std::string& strComponentClass_) const override;

        public://! 属性元数据
            /*
            * @brief 注册属性
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            * @param [in] Getter_ 获取器
            * @param [in] Setter_ 设置器
            */
            virtual void RegistPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const std::function<PropertyValue(const void*)>& Getter_, IN const std::function<void(void*, const PropertyValue&)>& Setter_, IN EPropertyPersistence Persistence_, IN EPropertyChangePolicy ChangePolicy_) override;

            /*
            * @brief 注册派生属性
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            * @param [in] Evaluator_ 计算器
            */
            virtual void RegistDerivedPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_, IN EPropertyPersistence Persistence_, IN EPropertyChangePolicy ChangePolicy_) override;

            /*
            * @brief 是否包含属性
            */
            virtual bool HasPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const override;

            /*
            * @brief 获取属性类型
            */
            virtual EPropertyKind GetPropertyKindByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const override;

            /*
            * @brief 获取属性持久化语义
            */
            virtual EPropertyPersistence GetPropertyPersistenceByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const override;

            /*
            * @brief 获取属性修改传播策略
            */
            virtual EPropertyChangePolicy GetPropertyChangePolicyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const override;

            /*
            * @brief 是否为派生属性
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            */
            virtual bool IsDerivedPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const override;

            /*
            * @brief 获取属性名称列表
            * @param [in] strComponentClass_
            * @return std::unordered_set<std::string>
            */
            virtual std::vector<std::string> GetPropertyNames(IN const std::string& strComponentClass_) const override;

            /*
            * @brief 调用属性获取
            * @param [in] pComponent_
            * @param [in] strComponentClass_ 此处额外传输该字段，用于对子类实例可指定父类进行查找属性，避免被子类覆盖
            * @param [in] strPropertyName_
            */
            virtual PropertyValue InvokeGetter(IN const CComponentBase& Component_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const override;

            /*
            * @brief 调用属性设置
            * @param [in] pComponent_
            * @param [in] strComponentClass_  此处额外传输该字段，用于对子类实例可指定父类进行查找属性，避免被子类覆盖
            * @param [in] strPropertyName_
            * @param [in] NewValue_
            */
            virtual void InvokeSetter(IN CComponentBase& Component_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_) const override;

        public://! 特性
            /*
            * @brief 绑定特性
            * @param [in] pAttribute_
            * @param [in] strComponentClass_ 组件类型，默认为空，表示从所有组件类型增加该特性
            */
            virtual void RegistAttributeByName(IN std::shared_ptr<IAttribute> pAttribute_, IN const std::string& strComponentClass_) override;

            /*
            * @brief 获取特性列表
            * @param [in] strComponentClass_ 组件类型
            */
            virtual std::unordered_set<std::shared_ptr<IAttribute>> GetAttributesByName(IN const std::string& strComponentClass_) const override;

        public://! 约束器
            /*
            * @brief 注册约束器
            * @param [in] pChecker_
            * @param [in] strComponentClass_，默认为空，表示从所有组件类型增加该约束器
            */
            virtual void RegistCheckerByName(IN const std::shared_ptr<IChecker>& pChecker_, IN const std::string& strComponentClass_) override;

            /*
            * @brief 获取约束列表
            * @param [in] strComponentClass_ 组件类型
            */
            virtual std::unordered_set<std::shared_ptr<IChecker>> GetCheckersByName(IN const std::string& strComponentClass_) const override;

        private:
            /*
            * @brief 是否属于继承关系
            * @param [in] strComponentClass_
            * @param [in] strParentComponentClass_
            * @return bool
            */
            bool IsInheritance(IN const uint32_t& nComponentClass_, IN const uint32_t& nParentComponentClass_);

            /*
            * @brief 获取属性名称列表
            * @param [in] strComponentClass_
            * @return std::unordered_set<std::string>
            */
            virtual std::vector<std::string> GetPropertyNames(IN const uint32_t& nComponentClass_) const;

            /*
            * @brief 获取值
            */
            PropertyValue InvokeGetter(IN const CComponentBase& Component_, IN const uint32_t& nComponentClass_, IN const uint32_t& strPropertyName_) const;

            /*
            * @brief 调用属性设置
            * @param [in] pComponent_
            * @param [in] strComponentClass_  此处额外传输该字段，用于对子类实例可指定父类进行查找属性，避免被子类覆盖
            * @param [in] strPropertyName_
            * @param [in] NewValue_
            */
            void InvokeSetter(IN CComponentBase& Component_, IN const uint32_t& nComponentClass_, IN const uint32_t& nPropertyName_, IN const PropertyValue& NewValue_) const;

            /*
            * @brief 是否为派生属性
            */
            bool IsDerivedProperty(IN const uint32_t& nComponentClass_, IN const uint32_t& nPropertyName_) const;

            /*
            * @brief 查找属性元数据
            */
            const PropertyMeta* FindPropertyMeta(IN const uint32_t& nComponentClass_, IN const uint32_t& nPropertyName_) const;

            /*
            * @brief 获取特性列表
            * @param [in] strComponentClass_ 组件类型
            */
            std::unordered_set<std::shared_ptr<IAttribute>> GetAttributesByName(IN const uint32_t& nComponentClass_) const;

            /*
            * @brief 获取约束列表
            * @param [in] strComponentClass_ 组件类型
            */
            std::unordered_set<std::shared_ptr<IChecker>> GetCheckersByName(IN const uint32_t& nComponentClass_) const;

            /*
            * @brief 获取元数据
            * @param [in] strComponentClass_
            * @return std::shared_ptr<ComponentMeta>
            */
            std::shared_ptr<ComponentMeta> GetMeta(IN const uint32_t& nComponentClass_);

            /*
            * @brief 获取元数据
            * @param [in] strComponentClass_
            * @return std::shared_ptr<ComponentMeta>
            */
            std::shared_ptr<ComponentMeta> GetMeta(IN const uint32_t& nComponentClass_) const;

        private:
            std::unordered_map<uint32_t, uint32_t> m_mapType;//! 类型映射表，key为类型，value为父类型（此处设定不允许多重继承）
            std::unordered_map<uint32_t, std::shared_ptr<ComponentMeta>> m_mapMeta;
        };
    }
}
