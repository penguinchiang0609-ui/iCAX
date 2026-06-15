#pragma once
#include <cstdint>
#include "ProcedureCall.h"
#include <string>

namespace iCAX
{
    namespace PC
    {
        /*
        * @brief 过程调用函数签名
        * @param [in] Context_ 上下文
        * @param [in] IParam_ 输入参数
        * @param [out] OParam_ 输出参数
        * @return int 调用结果 0 表示成功，其他值表示错误码
        */
        using PCFunc = int (*)(IN OUT void* Context_, IN const void* IParam_, OUT void* OParam_);

#define PC_SUCCESS 0
#define PC_FAILED -1
 //! 其他值，各自根据自己的业务自行定义返回值含义

        //! PC唯一编码
        using PCID = uint64_t;

        /*
        * @brief 创建PCID
        * @param [in] nModule_
        * @param [in] nName_
        * @return PCID
        */
        _PC_EXP PCID MakePCID(IN const std::string& nModule_, IN const std::string& nName_);
    }
}