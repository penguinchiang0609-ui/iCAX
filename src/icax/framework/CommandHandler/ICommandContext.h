#pragma once

#include "CommandHandlerExport.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <typeinfo>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令上下文。
        * @details
        *   上下文是一次命令执行时的依赖集合。
        *   ApplicationHost/ProductRuntime 会根据命令来源填入不同依赖，handler 通过类型获取自己需要的对象。
        */
        class _COMMAND_HANDLER_EXP ICommandContext
        {
        public:
            ICommandContext() = default;
            virtual ~ICommandContext() = default;

            ICommandContext(IN const ICommandContext&) = delete;
            ICommandContext& operator=(IN const ICommandContext&) = delete;

        public:
            /*
            * @brief 设置无类型依赖。
            * @param [in] Type_ 依赖类型。
            * @param [in] pValue_ 依赖对象；为空表示移除该依赖。
            */
            virtual void SetDependencyUntyped(IN const std::type_index& Type_, IN std::shared_ptr<void> pValue_) = 0;

            /*
            * @brief 获取无类型依赖。
            * @param [in] Type_ 依赖类型。
            * @return 依赖对象；不存在时返回 nullptr。
            */
            virtual std::shared_ptr<void> GetDependencyUntyped(IN const std::type_index& Type_) const = 0;

            template <typename T>
            /*
            * @brief 设置类型化依赖。
            * @param [in] pValue_ 依赖对象；为空表示移除。
            */
            void SetDependency(IN std::shared_ptr<T> pValue_)
            {
                SetDependencyUntyped(std::type_index(typeid(T)), std::static_pointer_cast<void>(pValue_));
            }

            template <typename T>
            /*
            * @brief 获取类型化依赖。
            * @return 依赖对象；不存在时返回 nullptr。
            */
            std::shared_ptr<T> GetDependency() const
            {
                auto _pValue = GetDependencyUntyped(std::type_index(typeid(T)));
                if (!_pValue)
                {
                    return nullptr;
                }
                return std::static_pointer_cast<T>(_pValue);
            }

            template <typename T>
            /*
            * @brief 获取必须存在的类型化依赖。
            * @return 依赖对象。
            * @throws std::logic_error 依赖不存在时抛出。
            */
            std::shared_ptr<T> RequireDependency() const
            {
                auto _pValue = GetDependency<T>();
                if (!_pValue)
                {
                    throw std::logic_error("command context dependency is missing: " + std::string(typeid(T).name()));
                }
                return _pValue;
            }
        };
    }
}
