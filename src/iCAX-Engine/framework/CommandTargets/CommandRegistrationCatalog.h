#pragma once

#include "CommandTargetsExport.h"
#include "CommandRegistry.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace iCAX
{
    namespace Command
    {
        /*
        * @brief 命令自动注册目录。
        * @details
        *   宏注册时只保存回放函数，真正注册到哪个 CCommandRegistry 由 ApplicationHost/ProductRuntime 决定。
        *   这样能保留 DLL 静态自动注册体验，同时支持按产品模块隔离命令。
        */
        class _COMMAND_TARGETS_EXP CCommandRegistrationCatalog final
        {
        public:
            /*
            * @brief 命令注册回放函数类型。
            */
            using ReplayFunc = std::function<void(CCommandRegistry&)>;

            /*
            * @brief 一条命令注册记录。
            */
            struct RegistrationRecord final
            {
                std::string ModulePath; //!< 注册对象所在模块路径。
                ReplayFunc Replay;      //!< 对目标 Registry 执行真实注册的函数。
            };

        private:
            CCommandRegistrationCatalog() = delete;
            ~CCommandRegistrationCatalog() = delete;

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
            * @brief 回放全部命令注册。
            */
            static void ReplayAll(IN CCommandRegistry& Registry_);

            /*
            * @brief 从指定下标开始增量回放。
            * @return 下一次回放应使用的起始下标。
            */
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN CCommandRegistry& Registry_);

            /*
            * @brief 只回放指定模块路径的命令注册。
            */
            static void ReplayByModulePaths(IN CCommandRegistry& Registry_, IN const std::vector<std::string>& ModulePaths_);

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

        /*
        * @brief 判断命令目标类型是否是不增加实例状态的 CCommandTarget 子类。
        * @details
        *   CommandTarget 子类只应该作为命令组织单元，不应该携带业务状态。
        *   该检查依赖 sizeof，能在宏注册阶段拦截绝大多数新增非静态成员变量的写法。
        *   如果业务代码绕过 ICAX_REGISTER_COMMAND_TARGET 手动注册，编译期检查不会自动生效。
        */
        template <typename TCommandTarget>
        inline constexpr bool IsStatelessCommandTargetType =
            std::is_base_of_v<CCommandTarget, TCommandTarget> &&
            sizeof(TCommandTarget) == sizeof(CCommandTarget);
    }
}

#define ICAX_COMMAND_REGISTRATION_CONCAT_IMPL(a, b) a##b
#define ICAX_COMMAND_REGISTRATION_CONCAT(a, b) ICAX_COMMAND_REGISTRATION_CONCAT_IMPL(a, b)

#define ICAX_REGISTER_COMMAND_TARGET(commandTargetType) \
    ICAX_REGISTER_COMMAND_TARGET_IMPL(commandTargetType, __COUNTER__)

#define ICAX_REGISTER_COMMAND_TARGET_IMPL(commandTargetType, uniqueId) \
    namespace \
    { \
        static_assert( \
            ::iCAX::Command::IsStatelessCommandTargetType<commandTargetType>, \
            "CommandTarget subclass registered by ICAX_REGISTER_COMMAND_TARGET must derive from CCommandTarget and must not add non-static data members"); \
        struct ICAX_COMMAND_REGISTRATION_CONCAT(AutoRegisterCommandTarget_, uniqueId) \
        { \
            ICAX_COMMAND_REGISTRATION_CONCAT(AutoRegisterCommandTarget_, uniqueId)() \
            { \
                iCAX::Command::CCommandRegistrationCatalog::Register( \
                    [](iCAX::Command::CCommandRegistry& registry) \
                    { \
                        auto commandTarget = std::make_shared<commandTargetType>(); \
                        if (!registry.Register(commandTarget)) \
                        { \
                            throw std::runtime_error("Command target is already registered: " + commandTarget->GetMainName()); \
                        } \
                    }, \
                    this); \
            } \
        }; \
        inline ICAX_COMMAND_REGISTRATION_CONCAT(AutoRegisterCommandTarget_, uniqueId) \
            ICAX_COMMAND_REGISTRATION_CONCAT(s_autoRegisterCommandTarget_, uniqueId){}; \
    }
