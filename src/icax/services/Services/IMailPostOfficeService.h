#pragma once
#include "Services.h"
#include "IService.h"
#include "Mailbox/MailPostOffice.h"
#include "Data/uuid.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 邮局服务接口
        */
        class _SERVICE_EXP IMailPostOfficeService : public IService
        {
        public:
            /*
            * @brief 构造函数
            */
            IMailPostOfficeService() = default;

            /*
            * @brief 析构函数
            */
            virtual ~IMailPostOfficeService() = default;

        public:
            /*
            * @brief 获取 backend 视角的邮局
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮局
            */
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 获取 frontend 视角的邮局
            * @param [in] ID_ 引擎ID，允许多引擎并行存在
            * @return 邮局
            */
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ID_) = 0;
        };
    }
}
