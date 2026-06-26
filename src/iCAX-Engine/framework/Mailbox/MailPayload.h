#pragma once

#include "Mail.h"

#include <string>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 释放邮件负载。
        * @details
        *   用于释放 Receive 返回邮件的 Payload，或在复用 Mail 前清理旧 Payload。
        *   释放后 nSize 置 0，pData 置 nullptr。
        */
        _MAIL_EXP void ReleaseMailPayload(IN OUT Mail& Mail_) noexcept;

        /*
        * @brief 设置 UTF-8 文本负载。
        * @param [in,out] Mail_ 目标邮件。函数会先释放已有 Payload。
        * @param [in] strText_ UTF-8 文本；H5 bridge 侧通常传入 JS string 编码后的文本。
        * @details
        *   Mailbox 底层仍保存 bytes，这里只是把普通文本作为官方 payload 约定。
        *   不追加字符串结尾的 '\0'，nSize 即 UTF-8 字节长度。
        */
        _MAIL_EXP void SetMailPayloadText(IN OUT Mail& Mail_, IN const std::string& strText_);

        /*
        * @brief 读取 UTF-8 文本负载。
        * @param [in] Mail_ 源邮件。
        * @return Payload 按 UTF-8 文本解释后的字符串；空 Payload 返回空字符串。
        * @throws std::invalid_argument nSize 大于 0 但 pData 为空时抛出。
        */
        _MAIL_EXP std::string GetMailPayloadText(IN const Mail& Mail_);

        /*
        * @brief 创建带 UTF-8 文本负载的邮件。
        * @param [in] Header_ 邮件头。
        * @param [in] strText_ UTF-8 文本负载。
        * @return 新邮件；调用方拥有 Payload 内存，使用后调用 ReleaseMailPayload。
        */
        _MAIL_EXP Mail CreateTextMail(IN const MailHeader& Header_, IN const std::string& strText_);
    }
}
