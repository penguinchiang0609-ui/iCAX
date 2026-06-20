#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include "BehaviourRegistrationCatalog.h"
#include "../Database/IEntity.h"

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief Behaviour注册表
        */
        class _SYSTEM_EXP IBehaviourRegistry
        {
        public:
            /*
            * @brief 构造函数
            */
            IBehaviourRegistry() = default;

            /*
            * @brief 构造函数
            */
            virtual ~IBehaviourRegistry() = default;

        public:
            /*
            * @brief 注册Behaviour
            * @param [in] pBehaviour_
            */
            virtual void RegisterBehaviourByInstance(IN const std::shared_ptr<CBehaviourBase>& pBehaviour_) = 0;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            virtual bool HasBehaviourByType(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 是否已包含
            * @param [in] nType_
            * @return bool
            */
            virtual bool HasBehaviourByType(IN const std::string& strType_) const = 0;

            /*
            * @brief 获取行为
            * @param [in] nType_
            * @return std::shared_ptr<CBehaviourBase>
            */
            virtual std::shared_ptr<CBehaviourBase> GetBehaviourByType(IN const std::type_index& nType_) const  = 0;

            /*
            * @brief 获取行为
            * @param [in] strType_
            * @return std::shared_ptr<CBehaviourBase>
            */
            virtual std::shared_ptr<CBehaviourBase> GetBehaviourByType(IN const std::string& strType_) const = 0;

            /*
            * @brief 获取行为
            * @param [in] strComponent_ 组件名称
            * @return std::vector<std::type_index>
            */
            virtual std::vector<std::type_index> GetTypeIndexByComponentType(IN const std::string& strComponent_) const = 0;

            /*
            * @brief 注册Behaviour
            */
            template<typename T, typename... Args>
            std::shared_ptr<T> RegisterBehaviour(IN Args&&... args)
            {
                static_assert(std::is_base_of<CBehaviourBase, T>::value, "T must derive from IBehaviour");
                if (HasBehaviourByType(std::type_index(typeid(T))))
                {
                    return GetBehaviour<T>();
                }
                std::shared_ptr<T> _pBehaviour = std::make_shared<T>(std::forward<Args>(args)...);
                RegisterBehaviourByInstance(_pBehaviour);
                return _pBehaviour;
            }

            /*
            * @brief 是否已包含
            */
            template<typename T>
            std::shared_ptr<T> HasBehaviour() const
            {
                return HasBehaviourByType(std::type_index(typeid(T)));
            }

            /*
            * @brief 获取Behaviour
            */
            template<typename T>
            std::shared_ptr<T> GetBehaviour() const
            {
                auto _pBehaviour = GetBehaviourByType(std::type_index(typeid(T)));
                if (_pBehaviour != nullptr)
                {
                    return std::dynamic_pointer_cast<T>(_pBehaviour);
                }
                return nullptr;
            }
        };

        /*
        * @brief 创建独立行为注册表
        */
        _SYSTEM_EXP std::shared_ptr<IBehaviourRegistry> CreateBehaviourRegistry();

        /*
        * @brief 获取全局行为注册表
        * @return std::shared_ptr<IBehaviourRegistry>
        */
        _SYSTEM_EXP std::shared_ptr<IBehaviourRegistry> GetGlobalBehaviourRegistry();
    }
}

//! 自动注册行为
#define AUTO_REGIST_BEHAVIOUR(TBehaviour) \
public:\
    virtual std::string GetBehaviourClass() const override { return #TBehaviour; }\
private:\
    struct AutoRegisterBehaviour\
    {\
        AutoRegisterBehaviour()\
        {\
            iCAX::Behaviour::CBehaviourRegistrationCatalog::Register([](iCAX::Behaviour::IBehaviourRegistry& Registry_)\
            {\
                Registry_.RegisterBehaviour<TBehaviour>();\
            }, this);\
        }\
    };\
    static inline AutoRegisterBehaviour s_auto_register_Instance{};

