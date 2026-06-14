#pragma once
#include <cstdint>
#include <Data/uuid.h>

namespace iCAX
{
    namespace Document
    {
        /*
        * @brief 仓储数据块
        */
        struct DomainBlock
        {
            //! 域ID
            iCAX::Data::uuid DomainID;
            //! 指向组件数据
            uint64_t ncomponentOffset;
            //! 组件数量
            uint32_t ncomponentCount;
        };
    }
}