#pragma once
#include "PDO.h"
#include <cstdint>
#include <string>
#include "Data/uuid.h"

namespace iCAX
{
    namespace PDO
    {
        //! PDO唯一编码
        using PDOID = uint64_t;

        /*
        * @brief 创建PDOID
        * @param [in] strTypeName_
        * @param [in] strInstanceName_
        * @return PDOID
        */
        _PDO_EXP PDOID MakePDOID(IN const std::string& strTypeName_, IN const std::string& strInstanceName_);

        /*
        * @brief 方向
        */
        enum PDODirection
        {
            kDirectionNil = 0,
            kDirection2Inner = 1,           //! 外部写入，内部读取，则每一帧开始切换双缓冲
            kDirection2Externer = 2,        //! 内部写入，外部读取，每一帧结束切换双缓冲
            kDirtionBoth = 3                //! 双向
        };

        /*
        * @brief 状态对象头
        */
        struct _PDO_EXP PDODecl
        {
            uint32_t nVersion;              //! 版本号，用于解决不同版本间的兼容性问题
            PDOID nID;                      //! PDO ID
            PDODirection eDirection;        //! 方向
            int   nPayloadSize;             //! 载荷大小
        };
    }
}
