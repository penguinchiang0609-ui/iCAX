#pragma once
#include "Engine.h"
#include <string>


namespace iCAX
{
    namespace Engine
    {
        /*
        * @brief 启动项
        */
        struct Startup
        {
            std::string FirstComponent;//! 启动组件，务必保证关于启动组件的Behaviour已经声明实现，对应的Behaviour是整个程序的入口。只可注册一个behaviour用于首组件处理
        };
    }
}