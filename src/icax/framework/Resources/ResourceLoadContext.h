#pragma once

#include "ResourceInfo.h"

#include <map>
#include <string>
#include <typeindex>
#include <typeinfo>

namespace iCAX
{
    namespace Resource
    {
        /*
        * @brief 资源加载上下文。
        * @details
        *   由 ResourceLibrary/ResourcePoolAccess 创建并传给 loader。
        *   loader 不应该自行访问全局状态，而应优先依据 Context 中的信息做加载判断。
        */
        struct _RESOURCES_EXP CResourceLoadContext final
        {
            CResourceKey TargetKey; //!< 目标资源键。
            std::type_index TargetResourceType = std::type_index(typeid(void)); //!< 调用方请求的 C++ 资源类型。
            std::string Source; //!< 原始来源字符串，通常与 TargetKey.Source 一致。
            CResourceInfo Info; //!< 合并后的资源信息。
            std::map<std::string, std::string> Options; //!< 调用方传入的加载选项。
        };
    }
}
