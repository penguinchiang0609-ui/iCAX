#pragma once
#include "PDO.h"
#include <cstdint>
#include <string>
#include "Data/uuid.h"

namespace iCAX
{
    namespace PDO
    {
        /*
        * @brief PDO 唯一编码。
        * @details
        *   PDOID 由类型名和实例名生成，用来在 Hub 中定位一个高频数据槽。
        *   它只用于进程内约定，不承担跨版本数据结构协商职责。
        */
        using PDOID = uint64_t;

        /*
        * @brief 创建 PDOID。
        * @param [in] strTypeName_ PDO 数据类型名称。
        * @param [in] strInstanceName_ 同类型下的实例名称。
        * @return 稳定的 PDOID；同一组输入在不同运行中保持一致。
        */
        _PDO_EXP PDOID MakePDOID(IN const std::string& strTypeName_, IN const std::string& strInstanceName_);

        /*
        * @brief PDO 数据流向。
        * @details
        *   PDO 用于高频可覆盖数据，不用于可靠命令。
        *   方向决定 Application/Project 帧循环在帧前还是帧后交换双缓冲。
        */
        enum PDODirection
        {
            kDirectionNil = 0,          //!< 未声明方向。
            kDirection2Inner = 1,       //!< 外部写入、内部读取；每帧开始交换。
            kDirection2External = 2,    //!< 内部写入、外部读取；每帧结束交换。
            kDirectionBoth = 3          //!< 双向数据；帧前和帧后都可能交换。
        };

        /*
        * @brief PDO 槽声明。
        * @details 描述一个双缓冲槽的版本、ID、方向和固定负载大小。
        */
        struct _PDO_EXP PDODecl
        {
            uint32_t nVersion;          //!< 数据结构版本号，用于运行期版本协商。
            PDOID nID;                  //!< PDO ID。
            PDODirection eDirection;    //!< 数据流向。
            int   nPayloadSize;         //!< 固定载荷大小，单位字节。
        };
    }
}
