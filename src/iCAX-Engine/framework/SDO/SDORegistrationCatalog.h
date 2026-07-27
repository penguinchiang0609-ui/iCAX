#pragma once

#include "SDOExport.h"
#include "SDORegistry.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace iCAX::Interaction
{
    /*
    * @brief SDO 静态注册目录。
    * @details 注册时保存回放函数，ApplicationRuntime 或 ProductRuntime 决定回放到哪个 Registry。
    */
    class _SDO_EXP CSDORegistrationCatalog final
    {
    public:
        using ReplayFunc = std::function<void(CSDORegistry&)>;

        struct RegistrationRecord final
        {
            std::string ModulePath;
            ReplayFunc Replay;
        };

    private:
        CSDORegistrationCatalog() = delete;
        ~CSDORegistrationCatalog() = delete;

    public:
        static void Register(IN ReplayFunc Func_);
        static void Register(IN ReplayFunc Func_, IN const void* pModuleAddress_);
        static void ReplayAll(IN CSDORegistry& Registry_);
        static size_t ReplayFrom(IN size_t nFirstIndex_, IN CSDORegistry& Registry_);
        static void ReplayByModulePaths(
            IN CSDORegistry& Registry_,
            IN const std::vector<std::string>& ModulePaths_);
        static size_t Count();

    private:
        static std::vector<RegistrationRecord>& GetRegistrations();
    };

    template <typename TSDO>
    inline constexpr bool IsStatelessSDOType =
        std::is_base_of_v<CSDO, TSDO> &&
        sizeof(TSDO) == sizeof(CSDO);
}

#define ICAX_SDO_REGISTRATION_CONCAT_IMPL(a, b) a##b
#define ICAX_SDO_REGISTRATION_CONCAT(a, b) ICAX_SDO_REGISTRATION_CONCAT_IMPL(a, b)

#define ICAX_REGISTER_SDO(sdoType) \
    ICAX_REGISTER_SDO_IMPL(sdoType, __COUNTER__)

#define ICAX_REGISTER_SDO_IMPL(sdoType, uniqueId) \
    namespace \
    { \
        static_assert( \
            ::iCAX::Interaction::IsStatelessSDOType<sdoType>, \
            "SDO registered by ICAX_REGISTER_SDO must derive from CSDO and must not add non-static data members"); \
        struct ICAX_SDO_REGISTRATION_CONCAT(AutoRegisterSDO_, uniqueId) \
        { \
            ICAX_SDO_REGISTRATION_CONCAT(AutoRegisterSDO_, uniqueId)() \
            { \
                ::iCAX::Interaction::CSDORegistrationCatalog::Register( \
                    [](::iCAX::Interaction::CSDORegistry& registry) \
                    { \
                        auto sdo = std::make_shared<sdoType>(); \
                        if (!registry.Register(sdo)) \
                        { \
                            throw std::runtime_error("SDO is already registered: " + sdo->GetName()); \
                        } \
                    }, \
                    this); \
            } \
        }; \
        inline ICAX_SDO_REGISTRATION_CONCAT(AutoRegisterSDO_, uniqueId) \
            ICAX_SDO_REGISTRATION_CONCAT(s_autoRegisterSDO_, uniqueId){}; \
    }
