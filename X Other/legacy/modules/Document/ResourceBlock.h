#pragma once
#include <cstdint>
#include "Data/uuid.h"

namespace iCAX
{
    namespace Document
    {
        /*
        * @brief 资源入口表
        */
        struct ResourceEntry
        {
            //! 资源UUID
            iCAX::Data::uuid ResourceID;
            //! 资源类型
            /*
            enum ResourceType
            {
                Unknown = 0,

                Texture2 = 1,
                Mesh = 2,
                Shader = 3,

                User = 1000
            };
            */
            uint32_t nType;
            //! 标志
            /*
            enum ResourceFlags
            {
                None = 0,
                Compressed = 1 << 0,
                External = 1 << 1,
                //Protected = 1 << 2,
                //RuntimeOnly = 1 << 3
            };
            */
            uint32_t nFlags;
            //! 数据偏移
            uint64_t nOffset;
            //! 数据大小
            uint64_t nSize;
        };

        /*
        * @brief 资源块
        */
        struct ResourceBlock
        {
            //! 资源数量
            uint32_t nResourceCount;
            //! 二进制数据区起始位置
            uint64_t nDataOffset;

            ResourceEntry* pEntries;//! 入口表

            uint8_t* pData;//! 数据部分
        };
    }
}