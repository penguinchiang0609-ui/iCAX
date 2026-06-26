#pragma once
#include <memory>
#include <unordered_map>
#include <typeindex>
#include <functional>
#include <mutex>
#include "Services.h"
#include <stdexcept>
#include "IService.h"

namespace iCAX
{
    namespace Services
    {
        /*
        * @brief 服务供应商
        * @details
        *   CServiceProvider 是一个服务容器实例，保存服务工厂和已创建的单例。
        *   它和自动注册 Catalog 是两层：Catalog 保存注册动作，Provider 保存某个 Application/Product 实际可用的服务。
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
            * @details
            *   注册时只保存工厂，不立即创建实例。
            *   首次 Resolve 时会递归解析依赖、构造实现对象、调用 OnLoad，然后缓存为单例。
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
                std::type_index _ImplementationType = std::type_index(typeid(TImplementation));

                std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);
                auto _IteImplementation = m_mapImplementationTypes.find(_nTypeIndex);
                if (_IteImplementation != m_mapImplementationTypes.end())
                {
                    if (_IteImplementation->second == _ImplementationType)
                    {
                        return;
                    }
                    throw std::runtime_error("Service already registered with different implementation");
                }

                m_mapFactories[_nTypeIndex] = [this]()
                    {
                        /*return std::make_shared<TImplementation>(Resolve<TDependencies>()...);*/
                        auto _pInstance = std::make_shared<TImplementation>(Resolve<TDependencies>()...);
                        _pInstance->OnLoad();
                        return _pInstance;
                    };
                m_mapSingletonMask[_nTypeIndex] = true;
                m_mapImplementationTypes.emplace(_nTypeIndex, _ImplementationType);
            }

            /*
            * @brief 获取服务
            * @param_template [in] TService 服务类型
            * @return 已解析的服务实例。
            * @throws std::runtime_error 服务未注册时抛出。
            * @details 单例服务首次解析后会缓存，后续解析返回同一实例。
            */
            template<typename TService>
            std::shared_ptr<TService> Resolve()
            {
                std::type_index _TypeIndex = std::type_index(typeid(TService));
                std::lock_guard<std::recursive_mutex> _Lock(m_Mutex);

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
            mutable std::recursive_mutex m_Mutex;
            std::unordered_map<std::type_index, std::function<std::shared_ptr<IService>()>> m_mapFactories;
            std::unordered_map<std::type_index, bool> m_mapSingletonMask;
            std::unordered_map<std::type_index, std::type_index> m_mapImplementationTypes;
            std::unordered_map<std::type_index, std::shared_ptr<IService>> m_mapSingletons;
        };

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
