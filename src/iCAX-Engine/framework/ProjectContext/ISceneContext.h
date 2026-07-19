#pragma once

#include "Data/uuid.h"
#include "ProjectContextExport.h"

#include "Facades/FacadeEndpoint.h"

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
        * @brief Scene 作用域环境。
        * @details
        *   SceneContext 管理当前现场的 Repository、Undo/Redo、Transaction、Universe、
        *   ResourceLibrary、PDOHub 和服务环境。Scene Runtime 管理线程、调度、协程、
        *   Facade 分发以及 Context 的创建和销毁。
        */
        class _PROJECT_CONTEXT_EXP ISceneContext
        {
        public:
            ISceneContext();
            virtual ~ISceneContext();

            ISceneContext(const ISceneContext&) = delete;
            ISceneContext& operator=(const ISceneContext&) = delete;

        public:
            /*
            * @brief 获取 Scene ID。
            */
            virtual const iCAX::Data::uuid& GetSceneID() const = 0;

            /*
            * @brief 获取 Scene 通信通道 ID。
            */
            virtual const iCAX::Data::uuid& GetSceneChannelID() const = 0;

            /*
            * @brief 获取 Scene 名称。
            */
            virtual const std::string& GetSceneName() const = 0;

            /*
            * @brief 获取后端视角 Scene Facade 端点。
            */
            virtual iCAX::Interaction::CFacadeEndpoint GetBackendFacadeEndpoint() const = 0;

            /*
            * @brief 获取前端视角 Scene Facade 端点。
            */
            virtual iCAX::Interaction::CFacadeEndpoint GetFrontendFacadeEndpoint() const = 0;

            /*
            * @brief 判断是否为主 Scene。
            */
            virtual bool IsMainScene() const = 0;

            /*
            * @brief 判断是否为临时 Scene。
            */
            virtual bool IsTransientScene() const = 0;

            /*
            * @brief 获取 Scene Repository。
            */
            virtual iCAX::Database::IRepository& Database() = 0;
            virtual const iCAX::Database::IRepository& Database() const = 0;

            /*
            * @brief 获取 Scene 资源库。
            */
            virtual iCAX::Resource::CResourceLibrary& Resources() = 0;
            virtual const iCAX::Resource::CResourceLibrary& Resources() const = 0;

            /*
            * @brief 当前 Scene 是否配置 PDO Hub。
            */
            virtual bool HasPDOHub() const = 0;

            /*
            * @brief 获取 Scene PDO Hub。
            */
            virtual iCAX::PDO::IPDOHub& PDOHub() = 0;
            virtual const iCAX::PDO::IPDOHub& PDOHub() const = 0;

            /*
            * @brief 获取当前 Scene 可用的服务容器。
            */
            virtual iCAX::Services::CServiceProvider& Services() const = 0;
        };
    }
}
