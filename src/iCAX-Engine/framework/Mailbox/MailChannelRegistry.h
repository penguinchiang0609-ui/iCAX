#pragma once

#include "Data/uuid.h"
#include "MailChannel.h"
#include "MailExport.h"
#include "MailPostOffice.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace iCAX
{
    namespace Mail
    {
        /*
        * @brief 邮件通道注册表。
        * @details
        *   Registry 只管理 CMailChannel 的生命周期，不属于 ServiceProvider。
        *   ApplicationHost 持有应用级 Registry，并把同一个 Registry 显式注入 Product/Project。
        *   Registry 自身线程安全；返回的 PostOffice 可跨线程使用。
        */
        class _MAIL_EXP CMailChannelRegistry final
        {
        public:
            CMailChannelRegistry() = default;
            ~CMailChannelRegistry() = default;

            CMailChannelRegistry(IN const CMailChannelRegistry&) = delete;
            CMailChannelRegistry& operator=(IN const CMailChannelRegistry&) = delete;

        public:
            /*
            * @brief 创建通道。
            * @param [in] ChannelID_ 通道 ID，不能是 nil uuid。
            * @return true 表示成功创建；false 表示同 ID 通道已存在。
            */
            bool CreateChannel(IN const iCAX::Data::uuid& ChannelID_);

            /*
            * @brief 判断通道是否存在。
            * @param [in] ChannelID_ 通道 ID。
            * @return true 表示已存在。
            */
            bool HasChannel(IN const iCAX::Data::uuid& ChannelID_) const;

            /*
            * @brief 获取前端视角邮局。
            * @param [in] ChannelID_ 通道 ID。
            * @return 通道前端端点 PostOffice。
            * @throws std::logic_error 指定通道不存在。
            */
            CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const;

            /*
            * @brief 获取后端视角邮局。
            * @param [in] ChannelID_ 通道 ID。
            * @return 通道后端端点 PostOffice。
            * @throws std::logic_error 指定通道不存在。
            */
            CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const;

            /*
            * @brief 移除通道。
            * @param [in] ChannelID_ 通道 ID。
            * @return true 表示移除了已有通道。
            * @details 移除后旧 PostOffice 会因底层队列释放而失效。
            */
            bool RemoveChannel(IN const iCAX::Data::uuid& ChannelID_);

            /*
            * @brief 清空所有通道。
            * @details 清空后所有旧 PostOffice 都失效。
            */
            void ClearChannels();

        private:
            /*
            * @brief 校验通道 ID。
            * @param [in] ChannelID_ 待校验 ID。
            * @throws std::invalid_argument 当 ID 为 nil uuid 时抛出。
            */
            static void ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_);

        private:
            mutable std::mutex m_Mutex;
            std::unordered_map<iCAX::Data::uuid, std::unique_ptr<CMailChannel>> m_Channels;
        };
    }
}
