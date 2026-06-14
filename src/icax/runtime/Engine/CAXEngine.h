#pragma once
#include "Engine.h"
#include "Behaviour/IUniverseContext.h"
#include "Behaviour/IUniverse.h"
#include "Data/Variant.h"
#include "Data/PropertyBag.h"
#include "Services/ServiceProvider.h"
#include <vector>
#include "CAXEnginConfig.h"
#include "Services/IMailPostOfficeService.h"


namespace iCAX
{
    namespace Engine
    {
        /*
        * @brief 引擎
        */
        class _ENGINE_EXP CCAXEngine
        {
        //public:
        //    using ExceptionCallback = std::function<void()>;

        public:
            /*
            * @brief 构造函数
            */
            CCAXEngine();

            /*
            * @brief 析构函数
            */
            virtual ~CCAXEngine();

        public:
            /*
            * @brief 设置应用程序配置路径
            * @param [in] 配置信息
            */
            void SetConfig(IN const CAXEnginConfig& Config_);

            //! TODO: 后续需要增加异常处理句柄，用于统一处理所有异常
            ///*
            //* @brief 设置异常处理函数
            //* @param [in] OnException_
            //*/
            //void SetExceptionHandle(IN ExceptionCallback OnException_);

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
            * @return iCAX::Data::PropertyBag
            */
            iCAX::Data::PropertyBag LoadApplicationSetting() const;

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

        private:
            CAXEnginConfig m_Config;
            std::shared_ptr<iCAX::Services::IMailPostOfficeService> m_pMailPostOfficeService; //!< 邮局服务
            std::shared_ptr<iCAX::Database::IRepository> m_pRepository;             //!< 仓储 Eentity+Component
            std::shared_ptr<iCAX::Behaviour::IUniverse> m_pUniverse;                   //!< 系统 System
        };
    }
}
