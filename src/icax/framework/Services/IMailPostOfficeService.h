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
            * @param [in] ID_ 通信ID，通常是 ProjectID 或应用级入口ID
            * @return 邮局
            */
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 获取 frontend 视角的邮局
            * @param [in] ID_ 通信ID，通常是 ProjectID 或应用级入口ID
            * @return 邮局
            */
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 移除指定通信通道
            * @param [in] ID_ 通信通道ID，通常是 ProjectID 或应用级入口ID
            * @return 是否移除了已有通道
            */
            virtual bool RemovePostOffice(IN const iCAX::Data::uuid& ID_) = 0;

            /*
            * @brief 清空所有通信通道
            */
            virtual void ClearPostOffices() = 0;
        };
    }
}
