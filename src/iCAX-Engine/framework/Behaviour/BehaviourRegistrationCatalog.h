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
        *   ProductRuntime 启动时按产品模块路径把注册动作回放到产品级 IBehaviourRegistry。
        *   Catalog 是进程级追加式目录，只记录注册回放函数；
        *   不提供注销能力，也不表示插件 DLL 可以热卸载或热替换。
        *   产品隔离通过 ReplayByModulePaths 回放到产品自己的注册表实现。
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
                std::string ModulePath; //!< 注册对象所在模块路径，用于产品按模块筛选回放。
                ReplayFunc Replay;      //!< 对目标注册表执行真实注册的函数；记录追加后不再移除。
            };

        private:
            CBehaviourRegistrationCatalog() = delete;
            ~CBehaviourRegistrationCatalog() = delete;

        public:
            /*
            * @brief 注册不绑定模块路径的回放函数。
            * @details 追加到进程级 Catalog，不支持注销。
            */
            static void Register(IN ReplayFunc Func_);

            /*
            * @brief 注册可按模块路径筛选的回放函数。
            * @details 追加到进程级 Catalog，不支持注销；pModuleAddress_ 仅用于解析模块路径。
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
            * @details 用于 ProductRuntime 建立产品级 BehaviourRegistry，实现产品间注册隔离。
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
