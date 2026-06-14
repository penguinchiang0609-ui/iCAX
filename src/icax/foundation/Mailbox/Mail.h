#pragma once
#include "MailExport.h"
#include <cstddef>
#include <cstdint>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 邮件状态
        */
        enum StampCode : uint16_t
        {
            kMailOk = 0,
            kMailUnknownType = 1,
            kMailNoHandler = 2,
            kMailInvalidPayload = 3,
            kMailExecutionError = 4,
            kMailTimeout = 5,
        };

        /*
        * @brief 邮件头
        */
        struct _MAIL_EXP MailHeader
        {
            uint64_t nMailId = 0;     //! 邮件ID
            uint64_t nOriginId = 0;   //! 原邮件ID，回复邮件时标识回复的是哪一封邮件，0 表示非回复邮件
            uint64_t nTypeCode = 0;   //! 过程调用ID，可以理解为 CommandCode，即由谁来处理该邮件。
            StampCode nStamp = kMailOk; //! 签章，业务错误码由各业务自行定义并在邮件体中传递。
        };

        /*
        * @brief 邮件数据
        */
        struct _MAIL_EXP MailData
        {
            size_t nSize = 0;
            uint8_t* pData = nullptr;
        };

        /*
        * @brief 邮件
        */
        struct _MAIL_EXP Mail
        {
            MailHeader Header;
            MailData Payload;
        };
    }
}
