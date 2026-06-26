#pragma once
#include "MailExport.h"
#include <cstddef>
#include <cstdint>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 邮件处理状态戳。
        * @details
        *   Stamp 写在响应邮件 Header 中，用于让发送侧快速判断命令是否成功。
        *   Mailbox 本身只转运该值，不解释业务错误；更详细的错误文本或业务数据放在 Payload 中。
        */
        enum StampCode : uint16_t
        {
            kMailOk = 0,
            kMailNoHandler = 1,
            kMailInvalidPayload = 2,
            kMailExecutionError = 3,
            kMailTimeout = 4,
        };

        /*
        * @brief 邮件头。
        * @details
        *   nMailId 标识本封邮件；响应邮件会把请求邮件 ID 写到 nOriginId。
        *   nTypeCode 是上层业务类型码。当前 CommandHandler 使用它承载 64 位命令路由码。
        */
        struct _MAIL_EXP MailHeader
        {
            uint64_t nMailId = 0;        //!< 邮件自身 ID，由发送侧分配。
            uint64_t nOriginId = 0;      //!< 原邮件 ID；回复邮件标识回复哪一封请求，0 表示非回复邮件。
            uint64_t nTypeCode = 0;      //!< 上层业务类型码；Mailbox 不解析，只透传给上层。
            StampCode nStamp = kMailOk;  //!< 处理状态戳；业务错误细节由业务自行放入邮件体。
        };

        /*
        * @brief 邮件数据。
        * @details
        *   MailQueue::Enqueue/CMailPostOffice::Send 会深拷贝 pData。
        *   CMailPostOffice::Receive 返回的 Mail 由调用方拥有 Payload 内存，处理后应调用
        *   MailPayload.h 中的 ReleaseMailPayload 释放。
        *   普通 H5 命令建议通过 MailPayload.h 的文本辅助函数存取 UTF-8/JSON 字符串。
        */
        struct _MAIL_EXP MailData
        {
            size_t nSize = 0;       //!< 负载字节数；为 0 时 pData 可以为空。
            uint8_t* pData = nullptr; //!< 负载内存；Receive 后所有权转移给调用方。
        };

        /*
        * @brief 一封进程内邮件。
        * @details
        *   Mailbox 只提供队列语义，不绑定前端/后端，也不规定 Payload 编码格式。
        */
        struct _MAIL_EXP Mail
        {
            MailHeader Header;
            MailData Payload;
        };
    }
}
