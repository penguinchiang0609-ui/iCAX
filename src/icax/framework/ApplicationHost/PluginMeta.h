#pragma once
#include "ApplicationHostExport.h"
#include <string>

namespace iCAX
{
    namespace ApplicationHost
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