#pragma once
#include "Engine.h"
#include <string>

namespace iCAX
{
    namespace Engine
    {
        /*
        * @brief 插件元数据
        */
        struct  PluginMeta
        {
            std::string strPluginName;
            std::string strComponentPath;
            std::string strBehaviourPath;
            std::string strServicePath;
        };
    }
}