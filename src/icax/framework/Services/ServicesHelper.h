#pragma once
#include <memory>
#include "IService.h"
#include "ServiceProvider.h"


//! 服务自动注册宏
#define AUTO_REGIST_SERVICE(ServiceType, ImplType, ...) \
        template<typename TService, typename TImplementation, typename... TDependencies>\
        struct AutoRegisterService\
        {\
            AutoRegisterService()\
            {\
                iCAX::Services::GetGlobalServiceProvider()->RegisterSingleton<TService, TImplementation, TDependencies...>();\
            }\
        };\
private:\
    static inline AutoRegisterService<ServiceType, ImplType, ##__VA_ARGS__> s_autoRegisterInstance{};

