#pragma once

#include "ResourcesExport.h"
#include "ResourceLibrary.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace iCAX::Resource
{
    /*
    * @brief 资源项目持久化 Codec 的自动注册目录。
    * @details
    *   插件静态初始化阶段只登记回放动作；ProductRuntime 按当前产品实际加载的
    *   模块路径，把这些动作回放到每个新建的 ResourceLibrary。
    */
    class _RESOURCES_EXP CResourcePersistenceRegistrationCatalog final
    {
    public:
        using ReplayFunc = std::function<void(CResourceLibrary&)>;

        struct RegistrationRecord final
        {
            std::string ModulePath;
            ReplayFunc Replay;
        };

        static void Register(IN ReplayFunc Func_);
        static void Register(
            IN ReplayFunc Func_,
            IN const void* pModuleAddress_);
        static void ReplayAll(IN CResourceLibrary& Library_);
        static size_t ReplayFrom(
            IN size_t nFirstIndex_,
            IN CResourceLibrary& Library_);
        static void ReplayByModulePaths(
            IN CResourceLibrary& Library_,
            IN const std::vector<std::string>& ModulePaths_);
        static size_t Count();

    private:
        static std::vector<RegistrationRecord>& GetRegistrations();
    };
}

/*
* @brief 在当前插件模块中登记一个项目资源持久化 Codec。
* @param ResourceTypeID_ 跨版本稳定的资源类型字符串。
* @param ResourceClass_ 运行期 C++ 资源类型。
* @param CodecFactory_ 无参可调用对象，返回 CResourceVersionCodec。
*/
#define ICAX_RESOURCE_PERSISTENCE_CONCAT_IMPL(Left_, Right_) Left_##Right_
#define ICAX_RESOURCE_PERSISTENCE_CONCAT(Left_, Right_) \
    ICAX_RESOURCE_PERSISTENCE_CONCAT_IMPL(Left_, Right_)
#define ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC( \
    ResourceTypeID_, ResourceClass_, CodecFactory_) \
    ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC_IMPL( \
        ResourceTypeID_, ResourceClass_, CodecFactory_, __COUNTER__)
#define ICAX_REGISTER_RESOURCE_PERSISTENCE_CODEC_IMPL( \
    ResourceTypeID_, ResourceClass_, CodecFactory_, UniqueID_) \
    namespace \
    { \
        struct ICAX_RESOURCE_PERSISTENCE_CONCAT( \
            CResourcePersistenceRegistrar_, UniqueID_) final \
        { \
            ICAX_RESOURCE_PERSISTENCE_CONCAT( \
                CResourcePersistenceRegistrar_, UniqueID_)() \
            { \
                ::iCAX::Resource::CResourcePersistenceRegistrationCatalog::Register( \
                    [](::iCAX::Resource::CResourceLibrary& Library_) { \
                        if (!Library_.RegisterPersistenceCodec<ResourceClass_>( \
                            ResourceTypeID_, (CodecFactory_)())) \
                        { \
                            throw std::runtime_error( \
                                "Resource persistence codec is already registered: " \
                                ResourceTypeID_); \
                        } \
                    }, this); \
            } \
        }; \
        const ICAX_RESOURCE_PERSISTENCE_CONCAT( \
            CResourcePersistenceRegistrar_, UniqueID_) \
            ICAX_RESOURCE_PERSISTENCE_CONCAT( \
                g_ResourcePersistenceRegistrar_, UniqueID_); \
    }
