#pragma once

#include "ResourcesExport.h"

#include <functional>
#include <string>
#include <vector>

namespace iCAX
{
    namespace Resource
    {
        class CResourceLoaderRegistry;

        /*
        * @brief 资源加载器自动注册目录。
        * @details
        *   宏注册时只保存回放函数，不直接污染某个 Registry。
        *   ProductRuntime 可以按已加载模块路径回放到产品/项目自己的 Registry 中。
        */
        class _RESOURCES_EXP CResourceLoaderRegistrationCatalog final
        {
        public:
            /*
            * @brief 注册回放函数类型。
            */
            using ReplayFunc = std::function<void(CResourceLoaderRegistry&, const std::string&)>;

            /*
            * @brief 一条加载器注册记录。
            */
            struct RegistrationRecord final
            {
                std::string ModulePath; //!< 注册对象所在模块路径。
                ReplayFunc Replay;      //!< 对目标 Registry 执行真实注册的函数。
            };

        private:
            CResourceLoaderRegistrationCatalog() = delete;
            ~CResourceLoaderRegistrationCatalog() = delete;

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
            * @brief 回放全部注册记录。
            */
            static void ReplayAll(IN CResourceLoaderRegistry& Registry_);

            /*
            * @brief 从指定下标开始增量回放。
            * @return 下一次回放应使用的起始下标。
            */
            static size_t ReplayFrom(IN size_t nFirstIndex_, IN CResourceLoaderRegistry& Registry_);

            /*
            * @brief 只回放指定模块路径的注册记录。
            */
            static void ReplayByModulePaths(IN CResourceLoaderRegistry& Registry_, IN const std::vector<std::string>& ModulePaths_);

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
