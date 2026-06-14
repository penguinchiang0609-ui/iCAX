#pragma once
#include "Database.h"
#include "IEntity.h"
#include "ComponentBase.h"
#include "DerivedProperty.h"
#include <unordered_set>
#include "IAttribute.h"
#include "IChecker.h"

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 元数据注册表
        */
        class _DATABASE_EXP IMetaRegistry
        {
        public:
            /*
            * @brief 构造函数
            */
            IMetaRegistry() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IMetaRegistry() = default;
        
        public:
            /*
            * @brief 注册类型
            * @param [in] strComponentClass_
            * @param [in] strParentComponentClass_
            */
            virtual void RegistType(IN const std::string& strComponentClass_, IN const std::string& strParentComponentClass_) = 0;
        
            /*
            * @brief 是否属于继承关系
            * @param [in] strComponentClass_
            * @param [in] strParentComponentClass_
            * @return bool
            */
            virtual bool IsInheritance(IN const std::string& strComponentClass_, IN const std::string& strParentComponentClass_) = 0;

            /*
            * @brief 是否包含组件类型
            * @param [in] strComponentClass_
            * @return bool
            */
            virtual bool HasTypeByName(IN const std::string& strComponentClass_) const = 0;

        public://! 构造器
            /*
            * @brief 注册构造函数
            * @param [in] strComponentClass_
            * @param [in] Creator_
            */
            virtual void RegistCreatorByName(IN const std::string& strComponentClass_, IN const std::function<std::shared_ptr<CComponentBase>(IN std::shared_ptr<IEntity>)>& Creator_) = 0;

            /*
            * @brief 注册构造函数
            * @param [in] strComponentClass_
            * @param [in] Creator_
            */
            template<typename T>
            void RegistCreator(IN const std::function<std::shared_ptr<CComponentBase>(IN std::shared_ptr<IEntity>)>& Creator_)
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                RegistCreatorByName(T::S_ClassName, Creator_);
            }

            /*
            * @brief 旧拼写兼容，后续新代码使用 RegistCreator
            */
            template<typename T>
            void RegistCeator(IN const std::function<std::shared_ptr<CComponentBase>(IN std::shared_ptr<IEntity>)>& Creator_)
            {
                RegistCreator<T>(Creator_);
            }

            /*
            * @brief 构造
            * @param [in] strComponentClass_
            * @param [in] pEntity_
            */
            virtual std::shared_ptr<CComponentBase> CreateByName(IN const std::string& strComponentClass_, IN const std::shared_ptr<IEntity>& pEntity_) const = 0;

            /*
            * @brief 构造
            * @param [in] strComponentClass_
            * @param [in] pEntity_
            */
            template<typename T>
            std::shared_ptr<CComponentBase> Create(IN std::shared_ptr<IEntity>& pEntity_) const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return CreateByName(T::S_ClassName, pEntity_);
            }

            /*
            * @brief 是否包含指定组件的构造
            * @param [in] strComponentClass_
            */
            virtual bool HasCreatorByName(IN const std::string& strComponentClass_) const = 0;

            /*
            * @brief 是否包含指定组件的构造
            * @param [in] strComponentClass_
            */
            template<typename T>
            bool HasCreator(IN const std::string& strComponentClass_) const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return HasCreatorByName(T::S_ClassName);
            }

            /*
            * @brief 是否包含指定组件的构造
            */
            template<typename T>
            bool HasCreator() const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return HasCreatorByName(T::S_ClassName);
            }


        public://! 属性元数据
            /*
            * @brief 注册属性
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            * @param [in] Getter_ 获取器
            * @param [in] Setter_ 设置器
            * @param [in] Persistence_ 持久化语义
            * @param [in] ChangePolicy_ 修改传播策略
            */
            virtual void RegistPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const std::function<PropertyValue(const void*)>& Getter_, IN const std::function<void(void*, const PropertyValue&)>& Setter_, IN EPropertyPersistence Persistence_ = EPropertyPersistence::Persistent, IN EPropertyChangePolicy ChangePolicy_ = EPropertyChangePolicy::Transactional) = 0;

            /*
            * @brief 注册派生属性
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            * @param [in] Evaluator_ 计算器
            * @param [in] Persistence_ 持久化语义
            * @param [in] ChangePolicy_ 修改传播策略
            */
            virtual void RegistDerivedPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const DerivedPropertyEvaluator& Evaluator_, IN EPropertyPersistence Persistence_ = EPropertyPersistence::NonPersistent, IN EPropertyChangePolicy ChangePolicy_ = EPropertyChangePolicy::Silent) = 0;

            /*
            * @brief 是否包含属性
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            */
            virtual bool HasPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const = 0;

            /*
            * @brief 获取属性类型
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            */
            virtual EPropertyKind GetPropertyKindByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const = 0;

            /*
            * @brief 获取属性持久化语义
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            */
            virtual EPropertyPersistence GetPropertyPersistenceByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const = 0;

            /*
            * @brief 获取属性修改传播策略
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            */
            virtual EPropertyChangePolicy GetPropertyChangePolicyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const = 0;

            /*
            * @brief 是否为派生属性
            * @param [in] strComponentClass_ 组件类型名称
            * @param [in] strPropertyName_ 属性名称
            */
            virtual bool IsDerivedPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const = 0;

            /*
            * @brief 获取属性名称列表
            * @param [in] strComponentClass_
            * @return std::vector<std::string>
            */
            virtual std::vector<std::string> GetPropertyNames(IN const std::string& strComponentClass_) const = 0;

            /*
            * @brief 调用属性获取
            * @param [in] Component_
            * @param [in] strComponentClass_
            * @param [in] strPropertyName_
            */
            virtual PropertyValue InvokeGetter(IN const CComponentBase& Component_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) const = 0;

            /*
            * @brief 调用属性设置
            * @param [in] Component_
            * @param [in] strComponentClass_
            * @param [in] strPropertyName_
            * @param [in] NewValue_
            */
            virtual void InvokeSetter(IN CComponentBase& Component_, IN const std::string& strComponentClass_, IN const std::string& strPropertyName_, IN const PropertyValue& NewValue_) const = 0;

        public://! 特性
            /*
            * @brief 绑定特性
            * @param [in] 组件类型
            * @param [in] pAttribute_
            */
            template<typename T>
            void RegistAttribute(IN std::shared_ptr<IAttribute> pAttribute_)
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                RegistAttributeByName(pAttribute_, T::S_ClassName);
            }

            /*
            * @brief 绑定特性
            * @param [in] pAttribute_
            * @param [in] strComponentClass_ 组件类型，默认为空，表示从所有组件类型增加该特性
            */
            virtual void RegistAttributeByName(IN std::shared_ptr<IAttribute> pAttribute_, IN const std::string& strComponentClass_ = std::string()) = 0;

            /*
            * @brief 获取特性列表
            * @param [in] strComponentClass_ 组件类型
            */
            template<typename T>
            std::unordered_set<std::shared_ptr<IAttribute>> GetAttributes() const
            {
                return GetAttributesByName(T::S_ClassName);
            }

            /*
            * @brief 获取特性列表
            * @param [in] strComponentClass_ 组件类型
            */
            virtual std::unordered_set<std::shared_ptr<IAttribute>> GetAttributesByName(IN const std::string& strComponentClass_) const = 0;

        public://! 约束器
            /*
            * @brief 注册约束器
            * @param [in] pChecker_
            */
            template<typename T>
            void RegistChecker(IN std::shared_ptr<IChecker> pChecker_)
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                RegistCheckerByName(pChecker_, T::S_ClassName);
            }

            /*
            * @brief 注册
            * @param [in] pChecker_
            * @param [in] strComponentClass_，默认为空，表示从所有组件类型增加该约束器
            */
            virtual void RegistCheckerByName(IN const std::shared_ptr<IChecker>& pChecker_, IN const std::string& strComponentClass_ = std::string()) = 0;

            /*
            * @brief 获取约束列表
            * @param [in] strComponentClass_ 组件类型
            */
            template<typename T>
            std::unordered_set<std::shared_ptr<IChecker>> GetCheckers() const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return GetCheckersByName(T::S_ClassName);
            }

            /*
            * @brief 获取约束列表
            * @param [in] strComponentClass_ 组件类型
            */
            virtual std::unordered_set<std::shared_ptr<IChecker>> GetCheckersByName(IN const std::string& strComponentClass_) const = 0;

            /*
            * @brief 是否允许添加
            * @param [in] Entity_ 目标实体
            * @param [out] strError_ 错误信息
            * @return bool
            */
            template<typename T>
            bool AllowAttach(IN const IEntity& Entity_, OUT std::string& strError_) const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return AllowAttachByName(Entity_, T::S_ClassName, strError_);
            }

            /*
            * @brief 是否允许添加
            * @param [in] Entity_ 目标实体
            * @param [in] strComponent_ 组件类型名称
            * @param [out] strError_ 错误信息
            * @return bool
            */
            inline bool AllowAttachByName(IN const IEntity& Entity_, IN const std::string& strComponent_, OUT std::string& strError_) const
            {
                auto _vecCheckers = GetCheckersByName(strComponent_);
                for (const auto& _pChecker : _vecCheckers)
                {
                    if (!_pChecker->AllowAttachByName(Entity_, strComponent_, strError_))
                    {
                        return false;
                    }
                }
                return true;
            }

            /*
            * @brief 是否允许添加
            * @param [in] Entity_ 目标实体
            * @param [out] strError_ 错误信息
            * @return bool
            */
            template<typename T>
            bool AllowRemove(IN const IEntity& Entity_, OUT std::string& strError_) const
            {
                static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
                return AllowRemoveByName(Entity_, T::S_ClassName, strError_);
            }

            /*
            * @brief 是否允许移除组件
            * @param [in] Entity_ 目标实体
            * @param [in] strComponent_ 组件类型名称
            * @param [out] strError_ 错误信息
            * @return bool
            */
            inline bool AllowRemoveByName(IN const IEntity& Entity_, IN const std::string& strComponent_, OUT std::string& strError_) const
            {
                auto _vecCheckers = GetCheckersByName(strComponent_);
                for (const auto& _pChecker : _vecCheckers)
                {
                    if (!_pChecker->AllowRemoveByName(Entity_, strComponent_, strError_))
                    {
                        return false;
                    }
                }
                return true;
            }

            /*
            * @brief 是否允许修改目标组件
            * @param [in] Component_
            * @param [in] Properties_ 待修改的属性名称与新值列表
            * @param [out] strError_ 错误信息
            * @return bool
            */
            inline bool AllowModify(IN const CComponentBase& Component_, IN const PropertySet& Properties_, OUT std::string& strError_) const
            {
                auto _vecCheckers = GetCheckersByName(Component_.GetComponentClass());
                for (const auto& _pChecker : _vecCheckers)
                {
                    if (!_pChecker->AllowModify(Component_, Properties_, strError_))
                    {
                        return false;
                    }
                }
                return true;
            }

            //!-------------------------以下注释-----------------------------------
            //! 不需要支持Component类型的热卸载
            ///*
            //* @brief 注销构造函数
            //* @param [in] strComponentClass_
            //*/
            //virtual void UnregistCreatorByName(IN const std::string& strComponentClass_) = 0;

            ///*
            //* @brief 注销构造函数
            //* @param [in] strComponentClass_
            //*/
            //template<typename T>
            //void UnregistCreator()
            //{
            //    static_assert(std::is_base_of<CComponentBase, T>::value, "T must be derived from CComponentBase");
            //    UnregistCreatorByName(T::S_ClassName);
            //}

            ///*
            //* @brief 注销属性
            //* @param [in] strComponentClass_ 组件类型名称
            //* @param [in] strPropertyName_ 属性名称
            //*/
            //virtual void UnregistPropertyByName(IN const std::string& strComponentClass_, IN const std::string& strPropertyName_) = 0;
        };

        /*
        * @brief 获取全局元数据注册表
        * @return std::shared_ptr<IMetaRegistry>
        */
        _DATABASE_EXP std::shared_ptr<IMetaRegistry> GetGlobalMetaRegistry();
    }
}
