#pragma once

#include "FacadesExport.h"
#include "FacadeRegistry.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace iCAX::Interaction
{
    /*
    * @brief Facade 静态注册目录。
    * @details 注册时保存回放函数，ApplicationHost 或 ProductRuntime 决定回放到哪个 Registry。
    */
    class _FACADES_EXP CFacadeRegistrationCatalog final
    {
    public:
        using ReplayFunc = std::function<void(CFacadeRegistry&)>;

        struct RegistrationRecord final
        {
            std::string ModulePath;
            ReplayFunc Replay;
        };

    private:
        CFacadeRegistrationCatalog() = delete;
        ~CFacadeRegistrationCatalog() = delete;

    public:
        static void Register(IN ReplayFunc Func_);
        static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);
        static void ReplayAll(IN CFacadeRegistry& Registry_);
        static size_t ReplayFrom(IN size_t nFirstIndex_, IN CFacadeRegistry& Registry_);
        static void ReplayByModulePaths(
            IN CFacadeRegistry& Registry_,
            IN const std::vector<std::string>& ModulePaths_);
        static size_t Count();

    private:
        static std::vector<RegistrationRecord>& GetRegistrations();
    };

    template <typename TFacade>
    inline constexpr bool IsStatelessFacadeType =
        std::is_base_of_v<CFacade, TFacade> &&
        sizeof(TFacade) == sizeof(CFacade);
}

#define ICAX_FACADE_REGISTRATION_CONCAT_IMPL(a, b) a##b
#define ICAX_FACADE_REGISTRATION_CONCAT(a, b) ICAX_FACADE_REGISTRATION_CONCAT_IMPL(a, b)

#define ICAX_REGISTER_FACADE(facadeType) \
    ICAX_REGISTER_FACADE_IMPL(facadeType, __COUNTER__)

#define ICAX_REGISTER_FACADE_IMPL(facadeType, uniqueId) \
    namespace \
    { \
        static_assert( \
            ::iCAX::Interaction::IsStatelessFacadeType<facadeType>, \
            "Facade registered by ICAX_REGISTER_FACADE must derive from CFacade and must not add non-static data members"); \
        struct ICAX_FACADE_REGISTRATION_CONCAT(AutoRegisterFacade_, uniqueId) \
        { \
            ICAX_FACADE_REGISTRATION_CONCAT(AutoRegisterFacade_, uniqueId)() \
            { \
                ::iCAX::Interaction::CFacadeRegistrationCatalog::Register( \
                    [](::iCAX::Interaction::CFacadeRegistry& registry) \
                    { \
                        auto facade = std::make_shared<facadeType>(); \
                        if (!registry.Register(facade)) \
                        { \
                            throw std::runtime_error("Facade is already registered: " + facade->GetName()); \
                        } \
                    }, \
                    this); \
            } \
        }; \
        inline ICAX_FACADE_REGISTRATION_CONCAT(AutoRegisterFacade_, uniqueId) \
            ICAX_FACADE_REGISTRATION_CONCAT(s_autoRegisterFacade_, uniqueId){}; \
    }
