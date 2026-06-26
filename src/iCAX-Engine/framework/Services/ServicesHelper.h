#pragma once
#include <memory>
#include "IService.h"
#include "ServiceProvider.h"
#include "ServiceRegistrationCatalog.h"


//! 服务自动注册宏
#define AUTO_REGIST_SERVICE(ServiceType, ImplType, ...) \
        template<typename TService, typename TImplementation, typename... TDependencies>\
        struct AutoRegisterService\
        {\
            AutoRegisterService()\
            {\
                iCAX::Services::CServiceRegistrationCatalog::Register([](iCAX::Services::CServiceProvider& Provider_)\
                {\
                    Provider_.RegisterSingleton<TService, TImplementation, TDependencies...>();\
                }, this);\
            }\
        };\
private:\
    static inline AutoRegisterService<ServiceType, ImplType, ##__VA_ARGS__> s_autoRegisterInstance{};

