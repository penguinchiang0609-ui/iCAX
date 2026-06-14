#pragma once
#include "Services.h"
#include "IMailPostOfficeService.h"
#include "ServicesHelper.h"
#include "Mailbox/MailChannel.h"
#include <mutex>
#include <unordered_map>

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 邮局服务
        */
        class CMailPostOfficeService : public IMailPostOfficeService
        {
        public:
            /*
            * @brief 构造函数
            */
            CMailPostOfficeService();

            /*
            * @brief 析构函数
            */
            virtual ~CMailPostOfficeService();

        public:
            /*
            * @brief 加载
            * @details
            *   服务需要的相关资源初始化，保证就绪
            */
            virtual void OnLoad() override;

            /*
            * @brief 卸载
            * @details
            *   释放服务占用的资源
            */
            virtual void OnUnload() override;

        public:
            /*
            * @brief 获取 backend 视角的邮局
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮局
            */
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ID_) override;

            /*
            * @brief 获取 frontend 视角的邮局
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮局
            */
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ID_) override;

        private:
            std::mutex m_Mutex;
            std::unordered_map<iCAX::Data::uuid, iCAX::Mail::CMailChannel> m_Channels; //!< 每个引擎一条双向邮件通道

            AUTO_REGIST_SERVICE(IMailPostOfficeService, CMailPostOfficeService);
        };
    }
}
