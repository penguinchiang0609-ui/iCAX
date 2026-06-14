#pragma once
#include "Database.h"
#include <memory>
#include <string>
#include <list>
#include <map>

namespace iCAX
{
    namespace Database
    {
        /*
        * @brief 特性
        * @remark 为组件的说明信息，用法类似CSharp的Attribute
        */
        class _DATABASE_EXP IAttribute
        {
        public:
            /*
            * @brief 构造函数
            */
            IAttribute() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IAttribute() = default;
        };
    }
}