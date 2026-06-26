#pragma once

#include "IMailChannelService.h"
#include "Mailbox/MailChannel.h"
#include "ServicesHelper.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief IMailChannelService 的默认实现。
        * @details
        *   以 uuid 为 key 保存 MailChannel。所有公开方法内部加锁，因此通道表本身线程安全。
        *   单个 Channel 内部的队列线程安全，返回的 PostOffice 可跨线程使用。
        */
        class _SERVICE_EXP CMailChannelService final : public IMailChannelService
        {
            AUTO_REGIST_SERVICE(IMailChannelService, CMailChannelService)

        public:
            CMailChannelService() = default;
            ~CMailChannelService() override = default;

            CMailChannelService(IN const CMailChannelService&) = delete;
            CMailChannelService& operator=(IN const CMailChannelService&) = delete;

        public:
            /*
            * @brief 服务加载回调。
            * @details 当前实现无需预加载资源。
            */
            void OnLoad() override;

            /*
            * @brief 服务卸载回调。
            * @details 卸载时清空所有通道，使旧 PostOffice 失效。
            */
            void OnUnload() override;

            /*
            * @brief 创建通道。
            * @param [in] ChannelID_ 通道 ID，不能是 nil uuid。
            * @return true 表示创建成功，false 表示同 ID 已存在。
            */
            bool CreateChannel(IN const iCAX::Data::uuid& ChannelID_) override;

            /*
            * @brief 判断通道是否存在。
            * @param [in] ChannelID_ 通道 ID。
            * @return true 表示已存在。
            */
            bool HasChannel(IN const iCAX::Data::uuid& ChannelID_) const override;

            /*
            * @brief 获取前端端点 PostOffice。
            * @param [in] ChannelID_ 通道 ID。
            * @return 通道 EndA 的 PostOffice。
            */
            iCAX::Mail::CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const override;

            /*
            * @brief 获取后端端点 PostOffice。
            * @param [in] ChannelID_ 通道 ID。
            * @return 通道 EndB 的 PostOffice。
            */
            iCAX::Mail::CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ChannelID_) const override;

            /*
            * @brief 移除通道。
            * @param [in] ChannelID_ 通道 ID。
            * @return true 表示移除了已有通道。
            */
            bool RemoveChannel(IN const iCAX::Data::uuid& ChannelID_) override;

            /*
            * @brief 清空所有通道。
            */
            void ClearChannels() override;

        private:
            /*
            * @brief 校验通道 ID。
            * @param [in] ChannelID_ 待校验 ID。
            * @throws std::invalid_argument 当 ID 为 nil uuid 时抛出。
            */
            static void ValidateChannelID(IN const iCAX::Data::uuid& ChannelID_);

        private:
            mutable std::mutex m_Mutex;
            std::unordered_map<iCAX::Data::uuid, std::unique_ptr<iCAX::Mail::CMailChannel>> m_Channels;
        };
    }
}
