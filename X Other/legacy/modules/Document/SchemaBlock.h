#pragma once
#include <cstdint>
#include <Data/Variant.h>
#include <string>

namespace iCAX
{
    namespace Document
    {
        /*
        * @brief 组件规约
        */
        struct Schema
        {
        public:
            std::string strClass;                   //! 规约类名
            uint32_t nVersion;                      //! 规约版本号
        };

        /*
        * @brief 数据规范
        * @details
        *   此处同一种组件只存在一个版本
        *   这么做的直接结果是高版本打开低版本时，打开即升级到高版本的数据。保存后，低版本软件将无法打开。（unity3d\unrealengine均如此）
        *   如此选择的原因是如果允许多个版本同时存在，System层即Behaviour逻辑代码会爆炸，难以维护
        */
        struct SchemaBlock
        {
        public:
            uint32_t nCount;                        //! 组件种类数
            Schema* pSchemas;                       //! 组件规约列表
        };
    }
}