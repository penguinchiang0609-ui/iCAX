#pragma once
#include "Data.h"
#include <cstdint>
#include <string>

namespace iCAX
{
    namespace Data
    {
        //! 稳定 32 位名称 ID
        using StableId32 = uint32_t;

        //! 稳定 64 位组合 ID
        using StableId = uint64_t;

        /*
        * @brief 创建稳定 32 位名称 ID
        * @param [in] strName_ 名称
        * @return StableId32
        */
        _DATA_EXP StableId32 MakeStableId32(IN const std::string& strName_);

        /*
        * @brief 创建稳定 64 位组合 ID
        * @param [in] strScope_ 作用域名称
        * @param [in] strName_ 名称
        * @return StableId
        */
        _DATA_EXP StableId MakeStableId(IN const std::string& strScope_, IN const std::string& strName_);
    }
}
