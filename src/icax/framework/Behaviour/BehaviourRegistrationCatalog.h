#pragma once

#include "System.h"

#include <functional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Behaviour
    {
        class IBehaviourRegistry;

        /*
        * @brief Behaviour 自动注册目录。
        * @details
        *   AUTO_REGIST_BEHAVIOUR 宏把回放函数登记到 Catalog。
        *   真正写入哪个 IBehaviourRegistry，由 Application/Product 启动时决定。
        */
        class _SYSTEM_EXP CBehaviourRegistrationCatalog final
        {
        public:
            /*
            * @brief 行为注册回放函数类型。
            */
            using ReplayFunc = std::function<void(IBehaviourRegistry&)>;

            /*
            * @brief 一条行为注册记录。
            */
            struct RegistrationRecord final
            {
                std::string ModulePath; //!< 注册对象所在模块路径。
                ReplayFunc Replay;      //!< 对目标注册表执行真实注册的函数。
            };

        private:
            CBehaviourRegistrationCatalog() = delete;
            ~CBehaviourRegistrationCatalog() = delete;

        public:
            /*
            * @brief 注册不绑定模块路径的回放函数。
            */
            static void Register(IN ReplayFunc Func_);

            /*
            * @brief 注册可按模块路径筛选的回放函数。
            */
            static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);

            /*
            * @brief 回放全部行为注册。
            */
            static void ReplayAll(IN IBehaviourRegistry& Registry_);

            /*
            * @brief 从指定下标开始增量回放。
            * @return 下一次回放应使用的起始下标。
            */
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN IBehaviourRegistry& Registry_);

            /*
            * @brief 只回放指定模块路径的行为注册。
            */
            static void ReplayByModulePaths(IN IBehaviourRegistry& Registry_, IN const std::vector<std::string>& ModulePaths_);

            /*
            * @brief 获取注册记录数量。
            */
            static size_t Count();

        private:
            /*
            * @brief 获取进程内静态注册记录数组。
            */
            static std::vector<RegistrationRecord>& GetRegistrations();
        };
    }
}
