#pragma once
#include <memory>
#include <unordered_map>
#include <typeindex>
#include <functional>
#include "Services.h"
#include <stdexcept>
#include "IService.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 服务供应商
        */
        class _SERVICE_EXP CServiceProvider final
        {
        public:
            /*
            * @brief 构造函数
            */
            CServiceProvider();

            /*
            * @brief 析构函数
            */
            ~CServiceProvider();

            //!< 禁用
            CServiceProvider(IN const CServiceProvider&) = delete;
            CServiceProvider& operator=(IN const CServiceProvider&) = delete;
            CServiceProvider(IN const CServiceProvider&&) = delete;
            CServiceProvider& operator=(IN const CServiceProvider&&) = delete;

        public:
            /*
            * @brief 注册单例
            * @param_template [in] TService 服务接口类型
            * @param_template [in] TImplementation 服务实现类型
            * @param_template [in] ...TDependencies 依赖的其他服务接口类型
            */
            template<typename TService, typename TImplementation, typename... TDependencies>
            void RegisterSingleton()
            {
                static_assert(std::is_base_of<TService, TImplementation>::value,
                    "TImplementation must be derived from TService");

                // 类型检查，确保TImplementation满足IService接口
                static_assert(std::is_base_of<IService, TService>::value,
                    "TService must implement IService interface");

                std::type_index _nTypeIndex = std::type_index(typeid(TService));
                m_mapFactories[_nTypeIndex] = [this]()
                    {
                        /*return std::make_shared<TImplementation>(Resolve<TDependencies>()...);*/
                        auto _pInstance = std::make_shared<TImplementation>(Resolve<TDependencies>()...);
                        _pInstance->OnLoad();
                        return _pInstance;
                    };
                m_mapSingletonMask[_nTypeIndex] = true;
            }

            /*
            * @brief 获取服务
            * @param_template [in] TService 服务类型
            * @return std::shared_ptr<TService>
            */
            template<typename TService>
            std::shared_ptr<TService> Resolve()
            {
                std::type_index _TypeIndex = std::type_index(typeid(TService));

                // 如果是单例，返回已注册的单例实例
                auto _IteSingleton = m_mapSingletons.find(_TypeIndex);
                if (_IteSingleton != m_mapSingletons.end())
                {
                    return std::static_pointer_cast<TService>(_IteSingleton->second);
                }

                // 否则，使用工厂创建新实例
                auto _IteFactory = m_mapFactories.find(_TypeIndex);
                if (_IteFactory != m_mapFactories.end())
                {
                    auto _pService = std::static_pointer_cast<TService>(_IteFactory->second());
                    if (m_mapSingletonMask[_TypeIndex])
                    {
                        m_mapSingletons[_TypeIndex] = _pService;
                    }
                    return _pService;
                }

                throw std::runtime_error("Service not registered");
            }

            /*
            * @brief 卸载所有
            * @details
            *   每个服务有加载与卸载方法，用于服务内部的资源准备与销毁
            *   卸载后如果再使用，会重新实例化，走加载
            */
            void UnloadAll();

        private:
            std::unordered_map<std::type_index, std::function<std::shared_ptr<IService>()>> m_mapFactories;
            std::unordered_map<std::type_index, bool> m_mapSingletonMask;
            std::unordered_map<std::type_index, std::shared_ptr<IService>> m_mapSingletons;
        };

        /*
        * @brief 获取全局服务供应商
        * @return std::shared_ptr<CServiceProvider>
        */
        _SERVICE_EXP std::shared_ptr<CServiceProvider> GetGlobalServiceProvider();

        //!< 简单示例
        //class ILogger 
        //{
        //public:
        //    virtual ~ILogger() = default;
        //    virtual void Log(const std::string& message) = 0;
        //};
        //class ConsoleLogger : public ILogger 
        //{
        //public:
        //    void Log(const std::string& message) override
        //    {
        //        std::cout << "ConsoleLogger: " << message << std::endl;
        //    }
        //};
        //void Test()
        //{
        //    // 注册服务：
        //    CServiceProvider container;
        //    // 注册 ILogger 服务为 ConsoleLogger 实现
        //    container.Register<ILogger, ConsoleLogger>();
        //    // 解析服务：
        //    auto logger = container.Resolve<ILogger>();
        //    logger->Log("Hello, IoC!");
        //}

        //!< 依赖注入示例
        //class ILogger 
        //{
        //public:
        //    virtual ~ILogger() = default;
        //    virtual void Log(const std::string& message) = 0;
        //};
        //class IFileWriter {
        //public:
        //    virtual ~IFileWriter() = default;
        //    virtual void WriteToFile(const std::string& content) = 0;
        //};
        //class FileWriter : public IFileWriter {
        //public:
        //    void WriteToFile(const std::string& content) override {
        //        // 假设将内容写入文件
        //        std::cout << "Writing to file: " << content << std::endl;
        //    }
        //};
        //class FileLogger : public ILogger {
        //public:
        //    FileLogger(std::shared_ptr<IFileWriter> fileWriter)
        //        : fileWriter(fileWriter) {}
        //    void Log(const std::string& message) override {
        //        fileWriter->WriteToFile("FileLogger: " + message);
        //    }
        //private:
        //    std::shared_ptr<IFileWriter> fileWriter;
        //};
        //void Test()
        //{
        //    CServiceProvider container;
        //    // 注册 FileWriter 为 IFileWriter 的多例实现
        //    container.Register<IFileWriter, FileWriter>();
        //    // 注册 FileLogger 为 ILogger 的多例实现，FileLogger 依赖于 IFileWriter
        //    container.Register<ILogger, FileLogger, IFileWriter>();
        //    // 解析 ILogger 服务
        //    auto logger = container.Resolve<ILogger>();
        //    logger->Log("Hello, IoC with Dependency Injection!");
        //}

    }
}