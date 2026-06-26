#pragma once
#include "System.h"
#include "BehaviourBase.h"
#include "BehaviourRegistrationCatalog.h"
#include "../Database/IEntity.h"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace iCAX
{
    namespace Behaviour
    {
        /*
        * @brief Behaviour 工厂函数。
        * @details
        *   注册表只保存可创建 Behaviour 的工厂，不保存可执行实例。
        *   Universe/Dispatcher 绑定行为时调用工厂创建本 Project 独享的 Behaviour 实例。
        */
        using BehaviourFactory = std::function<std::shared_ptr<CBehaviourBase>()>;

        /*
        * @brief Behaviour 注册描述。
        */
        struct _SYSTEM_EXP CBehaviourDescriptor final
        {
            CBehaviourDescriptor();

            std::type_index Type;              //!< Behaviour C++ 类型。
            std::string BehaviourClass;        //!< Behaviour 逻辑名称。
            std::string ComponentClass;        //!< 该 Behaviour 关注的组件类型。
            BehaviourFactory Factory;          //!< 创建新 Behaviour 实例的工厂。
        };

        /*
        * @brief Behaviour注册表
        * @remark
        *   注册表保存 Behaviour 描述和工厂，不保存运行实例。
        *   Application/Product 可以创建自己的注册表并按模块回放自动注册，避免不同产品行为互相污染。
        *   注册表的生命周期由所属 Application/Product 管理；注销插件 DLL 不是本接口职责。
        *   一个 ComponentClass 只能注册一个 Behaviour，冲突会在注册阶段直接抛出。
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
            * @brief 注册 Behaviour 工厂。
            * @param [in] Descriptor_ 行为描述，Factory 不能为空。
            */
            virtual void RegisterBehaviourFactory(IN const CBehaviourDescriptor& Descriptor_) = 0;

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
            * @brief 创建行为
            * @param [in] nType_
            * @return 新创建的 Behaviour；不存在时返回 nullptr。
            */
            virtual std::shared_ptr<CBehaviourBase> CreateBehaviourByType(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 创建行为
            * @param [in] strType_
            * @return 新创建的 Behaviour；不存在时返回 nullptr。
            */
            virtual std::shared_ptr<CBehaviourBase> CreateBehaviourByType(IN const std::string& strType_) const = 0;

            /*
            * @brief 获取 Behaviour 描述。
            * @param [in] nType_ Behaviour C++ 类型。
            * @return 描述指针；不存在时返回 nullptr。
            */
            virtual const CBehaviourDescriptor* GetDescriptorByType(IN const std::type_index& nType_) const = 0;

            /*
            * @brief 获取 Behaviour 描述。
            * @param [in] strType_ Behaviour 类名。
            * @return 描述指针；不存在时返回 nullptr。
            */
            virtual const CBehaviourDescriptor* GetDescriptorByType(IN const std::string& strType_) const = 0;

            /*
            * @brief 获取行为
            * @param [in] strComponent_ 组件名称
            * @return std::vector<std::type_index>
            */
            virtual std::vector<std::type_index> GetTypeIndexByComponentType(IN const std::string& strComponent_) const = 0;

            /*
            * @brief 注册Behaviour
            * @param_template [in] T Behaviour 类型。
            * @param [in] args 构造参数。
            */
            template<typename T, typename... Args>
            void RegisterBehaviour(IN Args&&... args)
            {
                static_assert(std::is_base_of<CBehaviourBase, T>::value, "T must derive from IBehaviour");
                if (HasBehaviourByType(std::type_index(typeid(T))))
                {
                    return;
                }

                auto _Factory = [ArgsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> std::shared_ptr<CBehaviourBase>
                {
                    return std::apply(
                        [](auto&&... UnpackedArgs_) -> std::shared_ptr<CBehaviourBase>
                        {
                            return std::make_shared<T>(std::forward<decltype(UnpackedArgs_)>(UnpackedArgs_)...);
                        },
                        ArgsTuple);
                };

                auto _pPrototype = std::dynamic_pointer_cast<T>(_Factory());
                if (!_pPrototype)
                {
                    throw std::runtime_error("Behaviour factory returned invalid instance");
                }

                CBehaviourDescriptor _Descriptor;
                _Descriptor.Type = std::type_index(typeid(T));
                _Descriptor.BehaviourClass = _pPrototype->GetBehaviourClass();
                _Descriptor.ComponentClass = _pPrototype->GetComponentClass();
                _Descriptor.Factory = std::move(_Factory);
                RegisterBehaviourFactory(_Descriptor);
            }

            /*
            * @brief 是否已包含
            * @return true 表示 T 已注册。
            */
            template<typename T>
            bool HasBehaviour() const
            {
                return HasBehaviourByType(std::type_index(typeid(T)));
            }

            /*
            * @brief 创建 Behaviour
            * @return 新创建的行为；不存在时返回 nullptr。
            */
            template<typename T>
            std::shared_ptr<T> CreateBehaviour() const
            {
                auto _pBehaviour = CreateBehaviourByType(std::type_index(typeid(T)));
                if (_pBehaviour != nullptr)
                {
                    return std::dynamic_pointer_cast<T>(_pBehaviour);
                }
                return nullptr;
            }
        };

        /*
        * @brief 创建独立行为注册表
        * @return 新的行为注册表实例。
        * @details Behaviour 不提供全局默认注册表；调用方必须显式持有并传递自己的注册表。
        */
        _SYSTEM_EXP std::shared_ptr<IBehaviourRegistry> CreateBehaviourRegistry();

    }
}

//! 自动注册行为。
//! 该宏只把注册回放函数追加到进程级 BehaviourRegistrationCatalog。
//! 它不提供注销能力，也不支持插件 DLL 热卸载；产品隔离由 ProductRuntime
//! 按模块路径回放到产品自己的 IBehaviourRegistry 完成。
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

