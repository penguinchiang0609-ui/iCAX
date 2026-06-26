#pragma once

#include "IService.h"
#include "Data/uuid.h"
#include "Mailbox/MailPostOffice.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 邮件通道服务。
        * @details
        *   该服务以 ChannelID 管理所有 MailChannel。
        *   它只负责创建、查找和销毁通道，不绑定业务语义；Frontend/Backend 名称只是当前框架约定的两个端点别名。
        */
        class _SERVICE_EXP IMailChannelService : public IService
        {
        public:
            ~IMailChannelService() override = default;

        public:
            /*
            * @brief 创建通道。
            * @param [in] ChannelID_ 通道 ID，不能是 nil uuid。
            * @return true 表示成功创建；false 表示同 ID 通道已存在。
            */
            virtual bool CreateChannel(IN const iCAX::Data::uuid& ChannelID_) = 0;

            /*
            * @brief 判断通道是否存在。
            * @param [in] ChannelID_ 通道 ID。
            * @return true 表示已存在。
            */
            virtual bool HasChannel(IN const iCAX::Data::uuid& ChannelID_) const = 0;

            /*
            * @brief 获取前端视角邮局。
            * @param [in] ChannelID_ 通道 ID。
            * @return 前端端点 PostOffice。
            * @throws std::logic_error 指定通道不存在。
            */
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const = 0;

            /*
            * @brief 获取后端视角邮局。
            * @param [in] ChannelID_ 通道 ID。
            * @return 后端端点 PostOffice。
            * @throws std::logic_error 指定通道不存在。
            */
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const = 0;

            /*
            * @brief 移除通道。
            * @param [in] ChannelID_ 通道 ID。
            * @return true 表示移除了已有通道。
            * @details 移除后旧 PostOffice 会因底层队列释放而失效。
            */
            virtual bool RemoveChannel(IN const iCAX::Data::uuid& ChannelID_) = 0;

            /*
            * @brief 清空全部通道。
            * @details 清空后所有旧 PostOffice 都失效。
            */
            virtual void ClearChannels() = 0;
        };
    }
}
