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
        class _COMMAND_HANDLER_EXP ICommandContext
        {
        public:
            ICommandContext() = default;
            virtual ~ICommandContext() = default;

            ICommandContext(IN const ICommandContext&) = delete;
            ICommandContext& operator=(IN const ICommandContext&) = delete;

        public:
            virtual void SetDependencyUntyped(IN const std::type_index& Type_, IN std::shared_ptr<void> pValue_) = 0;
            virtual std::shared_ptr<void> GetDependencyUntyped(IN const std::type_index& Type_) const = 0;

            template <typename T>
            void SetDependency(IN std::shared_ptr<T> pValue_)
            {
                SetDependencyUntyped(std::type_index(typeid(T)), std::static_pointer_cast<void>(pValue_));
            }

            template <typename T>
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
