#include "pch.h"
#include "ApplicationHost.h"
#include <filesystem>
#include <string>
#include "Database/IMetaRegistry.h"
#include "Behaviour/IBehaviourRegistry.h"
#include <format>
#include "Data/VariantSerializer.h"
#include "ApplicationContext/FileApplicationConfigStore.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "PluginMeta.h"
#include "Startup.h"

namespace
{
    iCAX::Data::Variant _LoadVariantFile(IN const std::string& strPath_, IN const std::string& strLabel_)
    {
        if (strPath_.empty())
        {
            throw std::invalid_argument(strLabel_ + " path cannot be empty");
        }

        std::ifstream _Input(strPath_, std::ios::binary);
        if (!_Input)
        {
            throw std::runtime_error("Failed to open " + strLabel_ + ": " + strPath_);
        }

        std::ostringstream _Buffer;
        _Buffer << _Input.rdbuf();
        return iCAX::Data::VariantSerializer::Deserialize(_Buffer.str());
    }

    iCAX::Data::ObjectMap _RequireObject(IN const iCAX::Data::Variant& Value_, IN const std::string& strLabel_)
    {
        if (!Value_.Is<iCAX::Data::ObjectMap>())
        {
            throw std::runtime_error(strLabel_ + " must be an object");
        }
        return Value_.To<iCAX::Data::ObjectMap>();
    }

    iCAX::Data::VariantArray _RequireArray(IN const iCAX::Data::ObjectMap& Object_, IN const std::string& strKey_, IN const std::string& strLabel_)
    {
        auto _Iter = Object_.find(strKey_);
        if (_Iter == Object_.end() || !_Iter->second.Is<iCAX::Data::VariantArray>())
        {
            throw std::runtime_error(strLabel_ + "." + strKey_ + " must be an array");
        }
        return _Iter->second.To<iCAX::Data::VariantArray>();
    }

    std::string _RequireString(IN const iCAX::Data::ObjectMap& Object_, IN const std::string& strKey_, IN const std::string& strLabel_)
    {
        auto _Iter = Object_.find(strKey_);
        if (_Iter == Object_.end() || !_Iter->second.Is<std::string>())
        {
            throw std::runtime_error(strLabel_ + "." + strKey_ + " must be a string");
        }
        return _Iter->second.To<std::string>();
    }

    std::string _OptionalString(IN const iCAX::Data::ObjectMap& Object_, IN const std::string& strKey_, IN const std::string& strLabel_)
    {
        auto _Iter = Object_.find(strKey_);
        if (_Iter == Object_.end())
        {
            return {};
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::runtime_error(strLabel_ + "." + strKey_ + " must be a string");
        }
        return _Iter->second.To<std::string>();
    }
}


//! 构造函数
iCAX::ApplicationHost::CApplicationHost::CApplicationHost()
    : m_Config()
{
    //! 默认值
    m_Config.strApplicationSettingsPath = "Setting/Application.Setting";
    m_Config.strPluginManifestPath = "Setting/Plugins.Setting";
    m_Config.strStartupPath = "Setting/Startup.Setting";
    m_Config.Descriptor.AppID = "icax";
    m_Config.Descriptor.AppName = "iCAX";
    m_Config.Paths.InstallDirectory = std::filesystem::current_path().string();
    m_Config.Paths.UserConfigDirectory = "Setting";
    m_Config.Paths.CacheDirectory = "Cache";
    m_Config.Paths.TempDirectory = "Temp";
    m_Config.Paths.LogDirectory = "Log";
}

//!< 析构函数
iCAX::ApplicationHost::CApplicationHost::~CApplicationHost()
{
    Unload();
}

//! 设置配置信息
void iCAX::ApplicationHost::CApplicationHost::SetConfig(IN const ApplicationHostConfig& Config_)
{
    m_Config = Config_;
}

//!< 加载
void iCAX::ApplicationHost::CApplicationHost::Load()
{
    //! 加载应用上下文
    m_pApplicationContext = CreateApplicationContext();
    auto _pApplicationSetting = std::make_shared<iCAX::Data::PropertyBag>(m_pApplicationContext->GetSettings().GetProperties());

    //!< 构建仓储
    m_pRepository = iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID());
    //!< 构建宇宙
    m_pUniverse = iCAX::Behaviour::GenerateUniverse(m_pRepository, _pApplicationSetting);
    m_pMailPostOfficeService = iCAX::Services::GetGlobalServiceProvider()->Resolve<iCAX::Services::IMailPostOfficeService>();

    //! 加载插件
    auto _PluginMetas = LoadPluginMetas();
    ////! TODO: 目前只支持放到exe同级目录，后续需要支持每个插件单独目录
    //SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    //AddDllDirectory(L"third party");
    //for (auto& entry : std::filesystem::recursive_directory_iterator("Plugins"))
    //{
    //    if (entry.is_directory())
    //    {
    //        AddDllDirectory(entry.path().c_str());
    //    }
    //}
    for (const auto& _Meta : _PluginMetas)
    {
        //! 注册插件组件
        if (!_Meta.strComponentPath.empty())
        {
            HMODULE hMod = LoadLibraryA(_Meta.strComponentPath.c_str());//! 已经采用了自注册，加载了模块之后会自动
            if (!hMod)
            {
                DWORD code = GetLastError();

                throw std::runtime_error(
                    std::format(
                        "[Plugin Load Failure]\n"
                        "  Path   : {}\n"
                        "  Error  : Win32 code {}\n",
                        _Meta.strBehaviourPath.c_str(), code
                    ));
            }
        }
        //! 注册插件行为
        if (!_Meta.strBehaviourPath.empty())
        {
            HMODULE hMod = LoadLibraryA(_Meta.strBehaviourPath.c_str());//! 已经采用了自注册，加载了模块之后会自动
            if (!hMod)
            {
                DWORD code = GetLastError();

                throw std::runtime_error(
                    std::format(
                        "[Plugin Load Failure]\n"
                        "  Path   : {}\n"
                        "  Error  : Win32 code {}\n",
                        _Meta.strServicePath.c_str(), code
                    ));
            }
        }
        if (!_Meta.strServicePath.empty())
        {
            HMODULE hMod = LoadLibraryA(_Meta.strServicePath.c_str());//! 已经采用了自注册，加载了模块之后会自动
            if (!hMod)
            {
                DWORD code = GetLastError();

                throw std::runtime_error(
                    std::format(
                        "[Plugin Load Failure]\n"
                        "  Path   : {}\n"
                        "  Error  : Win32 code {}\n",
                        _Meta.strComponentPath.c_str(), code
                    ));
            }
        }
    }

    auto _Startup = LoadStartup();

    //! 初始化首行为以及首组件
    auto _vecIndexs = iCAX::Behaviour::GetGlobalBehaviourRegistry()->GetTypeIndexByComponentType(_Startup.FirstComponent);
    if (_vecIndexs.empty())
    {
        throw std::runtime_error("first behaviour to component not exist");
    }
    if (_vecIndexs.size() >= 2)
    {
        throw std::runtime_error("first behaviour to component not only one");
    }
    m_pUniverse->GetRootWorld().BindBehaviourByIndex(_vecIndexs[0]);
    m_pRepository->GetMetaEntity()->AddComponent(_Startup.FirstComponent);

    //! 后续程序交由首Behaviour进行管理，即完全交由具体的产品进行定制
    //! CApplicationHost不提供OnLoading\Onloaded\OnUnloading\OnUnloaded等事件回调，这些回调由业务产品侧在首Behaviour的OnWake\OnStart\OnDetsory等方法中自行处理
}

//!< 主线程循环
void iCAX::ApplicationHost::CApplicationHost::MainLoop()
{
    //! 帧前PDO双缓冲交换
    m_pUniverse->PreSwapPDO();

    //! 检查邮局，处理邮件输入
    if (m_pMailPostOfficeService)
    {
        auto _BackendPostOffice = m_pMailPostOfficeService->GetBackendPostOffice(m_pUniverse->GetID());
        auto _Mails = _BackendPostOffice.Receive();
        for (auto& _Mail : _Mails)
        {
            //! 命令分发由 CommandHandler 适配层接入，ApplicationHost 不再把 Mail nTypeCode 当作过程函数执行。
            delete[] _Mail.Payload.pData;
            _Mail.Payload.pData = nullptr;
            _Mail.Payload.nSize = 0;
        }
    }

    //! 执行系统更新
    m_pUniverse->Tick();

    //! 帧后双缓冲交换
    m_pUniverse->PostSwapPDO();
}

//! 卸载
void iCAX::ApplicationHost::CApplicationHost::Unload()
{
    iCAX::Services::GetGlobalServiceProvider()->UnloadAll();//! 清空服务
    if (m_pUniverse)
    {
        m_pUniverse->Cleanup(true);//!清空系统
        m_pUniverse.reset();
    }
    if (m_pRepository)
    {
        m_pRepository->CleanUp(true);//! 清空数据
        m_pRepository.reset();
    }
    m_pApplicationContext.reset();
}

//! 加载应用程序配置
iCAX::Application::CApplicationSettings iCAX::ApplicationHost::CApplicationHost::LoadApplicationSettings() const
{
    iCAX::Application::CFileApplicationConfigStore _Store;
    return _Store.Load(m_Config.strApplicationSettingsPath);
}

std::shared_ptr<iCAX::Application::CApplicationContext> iCAX::ApplicationHost::CApplicationHost::CreateApplicationContext() const
{
    return std::make_shared<iCAX::Application::CApplicationContext>(
        m_Config.Descriptor,
        m_Config.Paths,
        LoadApplicationSettings());
}

//! 获取插件描述信息
std::vector<iCAX::ApplicationHost::PluginMeta> iCAX::ApplicationHost::CApplicationHost::LoadPluginMetas() const
{
    auto _Root = _RequireObject(_LoadVariantFile(m_Config.strPluginManifestPath, "plugin manifest"), "plugin manifest");
    auto _Plugins = _RequireArray(_Root, "plugins", "plugin manifest");

    std::vector<iCAX::ApplicationHost::PluginMeta> _vecMetas;
    _vecMetas.reserve(_Plugins.size());
    for (size_t i = 0; i < _Plugins.size(); ++i)
    {
        auto _Plugin = _RequireObject(_Plugins[i], std::format("plugin manifest.plugins[{}]", i));
        PluginMeta _Meta;
        _Meta.strPluginName = _RequireString(_Plugin, "name", std::format("plugin manifest.plugins[{}]", i));
        _Meta.strComponentPath = _OptionalString(_Plugin, "componentPath", std::format("plugin manifest.plugins[{}]", i));
        _Meta.strBehaviourPath = _OptionalString(_Plugin, "behaviourPath", std::format("plugin manifest.plugins[{}]", i));
        _Meta.strServicePath = _OptionalString(_Plugin, "servicePath", std::format("plugin manifest.plugins[{}]", i));
        _vecMetas.push_back(_Meta);
    }
    return _vecMetas;
}

//! 加载启动项
iCAX::ApplicationHost::Startup iCAX::ApplicationHost::CApplicationHost::LoadStartup() const
{
    auto _Root = _RequireObject(_LoadVariantFile(m_Config.strStartupPath, "startup"), "startup");
    Startup _Startup;
    _Startup.FirstComponent = _RequireString(_Root, "firstComponent", "startup");
    return _Startup;
}
