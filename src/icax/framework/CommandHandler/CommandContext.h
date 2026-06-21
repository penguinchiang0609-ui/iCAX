#pragma once

#include "ICommandContext.h"

#include <map>
#include <mutex>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 默认命令上下文实现。
        * @details
        *   使用 type_index 到 shared_ptr<void> 的映射保存依赖。
        *   SetDependency(nullptr) 等价于移除依赖。
        */
        class _COMMAND_HANDLER_EXP CCommandContext final : public ICommandContext
        {
        public:
            CCommandContext() = default;
            ~CCommandContext() override = default;

            CCommandContext(IN const CCommandContext&) = delete;
            CCommandContext& operator=(IN const CCommandContext&) = delete;

        public:
            /*
            * @brief 设置或移除无类型依赖。
            */
            void SetDependencyUntyped(IN const std::type_index& Type_, IN std::shared_ptr<void> pValue_) override;

            /*
            * @brief 获取无类型依赖。
            */
            std::shared_ptr<void> GetDependencyUntyped(IN const std::type_index& Type_) const override;

            /*
            * @brief 判断依赖是否存在。
            */
            bool HasDependency(IN const std::type_index& Type_) const;

            /*
            * @brief 移除依赖。
            */
            void RemoveDependency(IN const std::type_index& Type_);

            /*
            * @brief 清空全部依赖。
            */
            void Clear();

        private:
            mutable std::mutex m_Mutex;
            std::map<std::type_index, std::shared_ptr<void>> m_mapDependencies;
        };
    }
}
