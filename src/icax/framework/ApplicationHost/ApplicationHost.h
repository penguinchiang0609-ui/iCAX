#pragma once
#include "ApplicationHostExport.h"
#include "Behaviour/IUniverseContext.h"
#include "Behaviour/IUniverse.h"
#include "Data/Variant.h"
#include "Data/PropertyBag.h"
#include "ApplicationContext/ApplicationContext.h"
#include "Services/ServiceProvider.h"
#include <memory>
#include <vector>
#include "ApplicationHostConfig.h"
#include "Services/IMailPostOfficeService.h"


namespace iCAX
{
    namespace ApplicationHost
    {
        /*
        * @brief 应用宿主
        */
        class _APPLICATION_HOST_EXP CApplicationHost
        {
        public:
            /*
            * @brief 构造函数
            */
            CApplicationHost();

            /*
            * @brief 析构函数
            */
            virtual ~CApplicationHost();

        public:
            /*
            * @brief 设置应用程序配置路径
            * @param [in] 配置信息
            */
            void SetConfig(IN const ApplicationHostConfig& Config_);

            /*
            * @brief 加载
            */
            void Load();

            /*
            * @brief 主循环
            */
            void MainLoop();

            /*
            * @brief 关闭
            */
            void Unload();

        private:
            /*
            * @brief 加载应用程序配置
            * @return iCAX::Application::CApplicationSettings
            */
            iCAX::Application::CApplicationSettings LoadApplicationSettings() const;

            /*
            * @brief 创建应用上下文
            * @return std::shared_ptr<iCAX::Application::CApplicationContext>
            */
            std::shared_ptr<iCAX::Application::CApplicationContext> CreateApplicationContext() const;

            /*
            * @brief 获取插件描述信息
            * @return std::vector<PluginMeta>
            */
            std::vector<struct PluginMeta> LoadPluginMetas() const;

            /*
            * @brief 加载启动项
            */
            struct Startup LoadStartup() const;

        public:
            /*
            * @brief 获取仓储
            * @return iCAX::Database::IRepository&
            */
            iCAX::Database::IRepository& GetRepository() const
            {
                return *m_pRepository;
            }

            /*
            * @brief 获取宇宙
            * @return iCAX::Behaviour::IUniverse&
            */
            iCAX::Behaviour::IUniverse& GetUniverse() const
            {
                return *m_pUniverse;
            }

            /*
            * @brief 获取应用上下文
            * @return const iCAX::Application::IApplicationContext&
            */
            const iCAX::Application::IApplicationContext& GetApplicationContext() const
            {
                return *m_pApplicationContext;
            }

        private:
            ApplicationHostConfig m_Config;
            std::shared_ptr<iCAX::Application::CApplicationContext> m_pApplicationContext; //!< 应用上下文
            std::shared_ptr<iCAX::Services::IMailPostOfficeService> m_pMailPostOfficeService; //!< 邮局服务
            std::shared_ptr<iCAX::Database::IRepository> m_pRepository;             //!< 仓储 Eentity+Component
            std::shared_ptr<iCAX::Behaviour::IUniverse> m_pUniverse;                   //!< 系统 System
        };
    }
}
