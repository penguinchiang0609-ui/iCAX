#include "pch.h"
#include "CAXEngine.h"
#include <filesystem>
#include <string>
#include "Database/IMetaRegistry.h"
#include "Behaviour/IBehaviourRegistry.h"
#include <format>
#include "Data/VariantSerializer.h"
#include <fstream>
#include <sstream>
#include "PluginMeta.h"
#include "Startup.h"
#include "libxml/parser.h"
#include "libxml/tree.h"
#include "ProcedureCall/IPCRegistry.h"


//! 构造函数
iCAX::Engine::CCAXEngine::CCAXEngine()
    : m_Config()
{
    //! 默认值
    m_Config.strAppSettingPath = "Setting/CAX.Setting";
    m_Config.strPluginConfigPath = "Setting/Plugins.Setting";
    m_Config.strStartupPath = "Setting/Startup.Setting";
}

//!< 析构函数
iCAX::Engine::CCAXEngine::~CCAXEngine()
{
    Unload();
}

//! 设置配置信息
void iCAX::Engine::CCAXEngine::SetConfig(IN const CAXEnginConfig& Config_)
{
    m_Config = Config_;
}

//!< 加载
void iCAX::Engine::CCAXEngine::Load()
{
    //! 加载应用程序配置
    auto _pApplicationSetting = std::make_shared<iCAX::Data::PropertyBag>();
    *_pApplicationSetting = LoadApplicationSetting();

    //!< 构建仓储
    m_pRepository = iCAX::Database::GenerateRepository(iCAX::Data::GenerateNewUUID());
    //!< 构建宇宙
    m_pUniverse = iCAX::Behaviour::GenerateUniverse(m_pRepository, _pApplicationSetting);
    m_pMailPostOfficeService = iCAX::Services::GetGlobalServiceProvider()->Resolve<iCAX::Services::IMailPostOfficeService>();

    //! 加载插件
    auto _PluginPathes = LoadPluginMetas();
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
    for (const auto& _Meta : _PluginPathes)
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
                        _Meta.strComponentPath.c_str(), code
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
                        _Meta.strComponentPath.c_str(), code
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
    //! CCAXEngine不提供OnLoading\Onloaded\OnUnloading\OnUnloaded等事件回调，这些回调由业务产品侧在首Behaviour的OnWake\OnStart\OnDetsory等方法中自行处理
}

//!< 主线程循环
void iCAX::Engine::CCAXEngine::MainLoop()
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
            try
            {
                auto _fn = iCAX::PC::GetGlobalPCRegistry()->Find(_Mail.Header.nTypeCode);
                if (_fn != nullptr)
                {
                    _fn(&GetUniverse().GetContext(), &_Mail.Payload, nullptr);
                }
            }
            catch (...)
            {
            }
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
void iCAX::Engine::CCAXEngine::Unload()
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
}

static void _XmlElementToBag(const xmlNodePtr& elem, iCAX::Data::PropertySet& Set_)
{
    // 1. 处理属性
    for (xmlAttrPtr attr = elem->properties; attr != nullptr; attr = attr->next)
    {
        xmlChar* value = xmlNodeGetContent(attr->children);
        Set_[std::string(reinterpret_cast<const char*>(attr->name))] = std::string(reinterpret_cast<char*>(value));
        xmlFree(value);
    }

    // 2. 扫描所有子节点
    PropertyArray _Array;
    std::string _strChildrenName;
    for (xmlNodePtr cur = elem->children; cur != nullptr; cur = cur->next)
    {
        if (_strChildrenName.empty())
        {
            _strChildrenName = std::string(reinterpret_cast<const char*>(cur->name));
        }
        else if (_strChildrenName != std::string(reinterpret_cast<const char*>(cur->name)))
        {
            throw std::runtime_error("xml nodes not array");
        }
        iCAX::Data::PropertySet _Set;
        _XmlElementToBag(cur, _Set);
        _Array.push_back(_Set);
    }
    if (!_strChildrenName.empty())
    {
        Set_[_strChildrenName] = _Array;
    }
}

//! 加载应用程序配置
iCAX::Data::PropertyBag iCAX::Engine::CCAXEngine::LoadApplicationSetting() const
{
    iCAX::Data::PropertyBag _Bag;
    // 初始化 libxml2（可选，但推荐）
    xmlInitParser();
    xmlDocPtr doc = xmlReadFile(m_Config.strAppSettingPath.c_str(), NULL, 0);
    if (!doc)
    {
        throw std::runtime_error(std::format("app setting {} xml load failed", m_Config.strAppSettingPath));
    }
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root)
    {
        xmlFreeDoc(doc);
        throw std::runtime_error(std::format("app setting {} xml empty", m_Config.strAppSettingPath));
    }
    if (std::string(reinterpret_cast<const char*>(root->name)) != "Setting")
    {
        xmlFreeDoc(doc);
        throw std::runtime_error(std::format("app setting {} xml root is not Setting", m_Config.strAppSettingPath));
    }

    PropertySet _Set;
    _XmlElementToBag(root, _Set);

    // 释放文档
    xmlFreeDoc(doc);
    // 清理 libxml2 全局状态（可选）
    xmlCleanupParser();
    return iCAX::Data::PropertyBag(_Set);
}

//! 获取插件描述信息
std::vector<iCAX::Engine::PluginMeta> iCAX::Engine::CCAXEngine::LoadPluginMetas() const
{
    std::vector<iCAX::Engine::PluginMeta> _vecMetas;

    // 初始化 libxml2（可选，但推荐）
    xmlInitParser();
    xmlDocPtr doc = xmlReadFile(m_Config.strPluginConfigPath.c_str(), NULL, 0);
    if (!doc)
    {
        throw std::runtime_error(std::format("Plugins {} xml load failed", m_Config.strPluginConfigPath));
    }
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root)
    {
        xmlFreeDoc(doc);
        throw std::runtime_error(std::format("Plugins {} xml empty", m_Config.strPluginConfigPath));
    }
    if (std::string(reinterpret_cast<const char*>(root->name)) != "Plugins")
    {
        xmlFreeDoc(doc);
        throw std::runtime_error(std::format("Plugins {} xml root is not Plugins", m_Config.strPluginConfigPath));
    }
    // 遍历子节点
    for (xmlNodePtr cur = root->children; cur != nullptr; cur = cur->next)
    {
        iCAX::Engine::PluginMeta _Meta;
        if (cur->type == XML_ELEMENT_NODE)
        {
            // 遍历属性
            for (xmlAttrPtr attr = cur->properties; attr != nullptr; attr = attr->next)
            {
                xmlChar* value = xmlNodeGetContent(attr->children);
                if (std::string(reinterpret_cast<const char*>(attr->name)) == "Name")
                {
                    _Meta.strPluginName = reinterpret_cast<char*>(value);
                }
                if (std::string(reinterpret_cast<const char*>(attr->name)) == "ComponentPath")
                {
                    _Meta.strComponentPath = reinterpret_cast<char*>(value);
                }
                if (std::string(reinterpret_cast<const char*>(attr->name)) == "BehaviourPath")
                {
                    _Meta.strBehaviourPath = reinterpret_cast<char*>(value);
                }
                if (std::string(reinterpret_cast<const char*>(attr->name)) == "ServicePath")
                {
                    _Meta.strServicePath = reinterpret_cast<char*>(value);
                }
                xmlFree(value);
            }
            _vecMetas.push_back(_Meta);
        }
    }
    // 释放文档
    xmlFreeDoc(doc);
    // 清理 libxml2 全局状态（可选）
    xmlCleanupParser();
    return _vecMetas;
}

//! 加载启动项
iCAX::Engine::Startup iCAX::Engine::CCAXEngine::LoadStartup() const
{
    Startup _Startup;

    xmlInitParser();
    xmlDocPtr doc = xmlReadFile(m_Config.strStartupPath.c_str(), NULL, 0);
    if (!doc)
    {
        throw std::runtime_error(std::format("startup {} xml load failed", m_Config.strStartupPath));
    }
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root)
    {
        xmlFreeDoc(doc);
        throw std::runtime_error(std::format("startup {} xml empty", m_Config.strStartupPath));
    }
    if (std::string(reinterpret_cast<const char*>(root->name)) != "Startup")
    {
        xmlFreeDoc(doc);
        throw std::runtime_error(std::format("startup {} xml root is not Plugins", m_Config.strStartupPath));
    }
    //! 遍历属性
    for (xmlAttrPtr attr = root->properties; attr != nullptr; attr = attr->next)
    {
        xmlChar* value = xmlNodeGetContent(attr->children);
        if (std::string(reinterpret_cast<const char*>(attr->name)) == "FirstComponent")
        {
            _Startup.FirstComponent = reinterpret_cast<char*>(value);
        }
        xmlFree(value);
    }
    //! 释放文档
    xmlFreeDoc(doc);
    //! 清理 libxml2 全局状态（可选）
    xmlCleanupParser();
    return _Startup;
}
