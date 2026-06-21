#pragma once

#include "Services.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Services
    {
        class CServiceProvider;

        /*
        * @brief 服务自动注册目录。
        * @details
        *   宏注册时不会直接写入某个 ServiceProvider，而是把“如何注册”的回放函数保存到 Catalog。
        *   Application/Product 可以按需要把这些回放函数应用到自己的 Provider，从而保留自动注册写法，
        *   同时避免所有运行态共享同一个服务容器。
        */
        class _SERVICE_EXP CServiceRegistrationCatalog final
        {
        public:
            /*
            * @brief 注册回放函数类型。
            * @param [in,out] CServiceProvider& 目标服务容器。
            */
            using ReplayFunc = std::function<void(CServiceProvider&)>;

            /*
            * @brief 一条自动注册记录。
            */
            struct RegistrationRecord final
            {
                std::string ModulePath; //!< 注册代码所在模块路径；用于按产品模块选择性回放。
                ReplayFunc Replay;      //!< 对目标 ServiceProvider 执行真实注册的函数。
            };

        private:
            CServiceRegistrationCatalog() = delete;
            ~CServiceRegistrationCatalog() = delete;

        public:
            /*
            * @brief 注册一个不绑定模块路径的回放函数。
            * @param [in] Func_ 回放函数；不能为空。
            */
            static void Register(IN ReplayFunc Func_);

            /*
            * @brief 注册一个可追踪模块路径的回放函数。
            * @param [in] Func_ 回放函数；不能为空。
            * @param [in] pModuleAddress_ 静态注册对象地址，用于反查所在 DLL/EXE 路径。
            */
            static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);

            /*
            * @brief 回放全部注册记录。
            * @param [in,out] Provider_ 目标服务容器。
            */
            static void ReplayAll(IN CServiceProvider& Provider_);

            /*
            * @brief 从指定记录下标开始回放。
            * @param [in] nFirstIndex_ 起始记录下标。
            * @param [in,out] Provider_ 目标服务容器。
            * @return 回放完成后的下一次起始下标。
            */
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN CServiceProvider& Provider_);

            /*
            * @brief 按模块路径回放注册记录。
            * @param [in,out] Provider_ 目标服务容器。
            * @param [in] ModulePaths_ 允许回放的模块路径集合。
            */
            static void ReplayByModulePaths(IN CServiceProvider& Provider_, IN const std::vector<std::string>& ModulePaths_);

            /*
            * @brief 获取当前 catalog 中的注册记录数量。
            * @return 注册记录数量。
            */
            static size_t Count();

        private:
            /*
            * @brief 获取进程内注册记录存储。
            * @return 静态注册记录数组。
            */
            static std::vector<RegistrationRecord>& GetRegistrations();
        };
    }
}
