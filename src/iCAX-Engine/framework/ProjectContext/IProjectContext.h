#pragma once

#include "Data/PropertyBag.h"
#include "Data/uuid.h"
#include "Mailbox/MailPostOffice.h"
#include "ProjectContextExport.h"

#include <string>

namespace iCAX
{
    namespace PDO
    {
        class IPDOHub;
    }

    namespace Database
    {
        class IRepository;
    }

    namespace Resource
    {
        class CResourceLibrary;
    }

    namespace Services
    {
        class CServiceProvider;
    }

    namespace Project
    {
        /*
        * @brief 项目级运行上下文。
        * @details
        *   ProjectContext 表达当前项目现场。它拥有项目数据访问入口、资源库和当前项目可用的服务入口。
        */
        class _PROJECT_CONTEXT_EXP IProjectContext
        {
        public:
            IProjectContext();
            virtual ~IProjectContext();

            IProjectContext(const IProjectContext&) = delete;
            IProjectContext& operator=(const IProjectContext&) = delete;

        public:
            /*
            * @brief 获取项目 ID。
            */
            virtual const iCAX::Data::uuid& GetProjectID() const = 0;

            /*
            * @brief 获取项目通信通道 ID。
            */
            virtual const iCAX::Data::uuid& GetProjectChannelID() const = 0;

            /*
            * @brief 获取后端视角项目邮局。
            */
            virtual iCAX::Mail::CMailPostOffice GetBackendPostOffice() const;

            /*
            * @brief 获取前端视角项目邮局。
            */
            virtual iCAX::Mail::CMailPostOffice GetFrontendPostOffice() const;

            /*
            * @brief 获取项目名称。
            */
            virtual const std::string& GetProjectName() const = 0;

            /*
            * @brief 获取项目路径。
            */
            virtual const std::string& GetProjectPath() const = 0;

            /*
            * @brief 获取项目级参数。
            * @details 项目级设置跟项目文件走，不承载产品级设置或应用级设置。
            */
            virtual iCAX::Data::PropertyBag& Settings() = 0;
            virtual const iCAX::Data::PropertyBag& Settings() const = 0;

            /*
            * @brief 获取项目 Repository。
            */
            virtual iCAX::Database::IRepository& Database() = 0;
            virtual const iCAX::Database::IRepository& Database() const = 0;

            /*
            * @brief 获取项目资源库。
            */
            virtual iCAX::Resource::CResourceLibrary& Resources() = 0;
            virtual const iCAX::Resource::CResourceLibrary& Resources() const = 0;

            /*
            * @brief 当前项目是否配置 PDO Hub。
            * @return true 表示项目拥有自己的高频共享内存 PDO 通道。
            */
            virtual bool HasPDOHub() const;

            /*
            * @brief 获取项目 PDO Hub。
            * @return 项目自己的 PDO Hub。
            * @throws std::logic_error 当前项目未配置 PDO Hub 时抛出。
            */
            virtual iCAX::PDO::IPDOHub& PDOHub();
            virtual const iCAX::PDO::IPDOHub& PDOHub() const;

            /*
            * @brief 获取当前项目可用的服务容器。
            */
            virtual iCAX::Services::CServiceProvider& Services() const = 0;
        };
    }
}
