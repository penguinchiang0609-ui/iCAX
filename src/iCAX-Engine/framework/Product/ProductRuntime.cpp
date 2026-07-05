#include "pch.h"
#include "ProductRuntime.h"

#include "CommandHandler/CommandRegistrationCatalog.h"
#include "CommandHandler/CommandTarget.h"
#include "Project/ProjectCommands.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <format>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    class CStaticProductCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        using CommandRecord = std::pair<std::string, SubCommandFunc>;

        CStaticProductCommandTarget(
            IN std::string strMainName_,
            IN std::vector<CommandRecord> Commands_)
            : CCommandTarget(std::move(strMainName_))
        {
            for (auto& _Command : Commands_)
            {
                auto _SubName = _Command.first;
                if (!Bind(std::move(_Command.first), std::move(_Command.second)))
                {
                    throw std::runtime_error("Built-in product command is already bound: " + _SubName);
                }
            }
        }
    };

    std::string _ToProjectRoleText(IN iCAX::Project::EProjectRole Role_)
    {
        switch (Role_)
        {
        case iCAX::Project::EProjectRole::Main:
            return "Main";
        case iCAX::Project::EProjectRole::Transient:
            return "Transient";
        default:
            throw std::invalid_argument("Invalid project role");
        }
    }

    std::string _ToProjectStateText(IN iCAX::Project::EProjectState State_)
    {
        switch (State_)
        {
        case iCAX::Project::EProjectState::Created:
            return "Created";
        case iCAX::Project::EProjectState::Starting:
            return "Starting";
        case iCAX::Project::EProjectState::Running:
            return "Running";
        case iCAX::Project::EProjectState::Stopping:
            return "Stopping";
        case iCAX::Project::EProjectState::Stopped:
            return "Stopped";
        case iCAX::Project::EProjectState::Faulted:
            return "Faulted";
        default:
            throw std::invalid_argument("Invalid project state");
        }
    }

    iCAX::Data::Variant _MakePDOPayload(IN const iCAX::Project::CProjectPDODescriptor& Descriptor_);

    iCAX::Data::Variant _MakeUndoRedoStatePayload(IN const iCAX::Database::IRepository& Repository_)
    {
        auto _MakeStepArray = [](
            IN const std::vector<std::tuple<iCAX::Data::uuid, std::string>>& Steps_) {
            iCAX::Data::VariantArray _Array;
            _Array.reserve(Steps_.size());
            for (const auto& [_ID, _Name] : Steps_)
            {
                iCAX::Data::ObjectMap _Step;
                _Step["id"] = _ID;
                _Step["name"] = _Name;
                _Array.emplace_back(std::move(_Step));
            }
            return _Array;
        };

        iCAX::Data::ObjectMap _State;
        _State["canUndo"] = Repository_.CanUndo();
        _State["canRedo"] = Repository_.CanRedo();
        _State["undoSteps"] = _MakeStepArray(Repository_.GetUndoArray());
        _State["redoSteps"] = _MakeStepArray(Repository_.GetRedoArray());
        return iCAX::Data::Variant(_State);
    }

    iCAX::Data::Variant _MakeProjectPayload(IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_)
    {
        iCAX::Data::ObjectMap _Project;
        if (!pProjectRuntime_)
        {
            return iCAX::Data::Variant(_Project);
        }

        const auto _State = pProjectRuntime_->GetState();
        _Project["projectId"] = pProjectRuntime_->GetProjectID();
        _Project["projectChannelId"] = pProjectRuntime_->GetProjectChannelID();
        _Project["projectName"] = pProjectRuntime_->GetProjectName();
        _Project["projectPath"] = pProjectRuntime_->GetProjectPath();
        _Project["startupComponent"] = pProjectRuntime_->GetStartupComponent();
        _Project["role"] = _ToProjectRoleText(pProjectRuntime_->GetRole());
        _Project["state"] = _ToProjectStateText(_State);
        _Project["isMainProject"] = pProjectRuntime_->IsMainProject();
        _Project["isTransientProject"] = pProjectRuntime_->IsTransientProject();
        _Project["isOpen"] = pProjectRuntime_->IsOpen();
        _Project["isRunning"] = _State == iCAX::Project::EProjectState::Running;

        auto _Fault = pProjectRuntime_->GetLastFault();
        _Project["faultMessage"] = _Fault ? _Fault->Message : std::string();
        _Project["pdo"] = _MakePDOPayload(pProjectRuntime_->GetPDODescriptor());
        return iCAX::Data::Variant(_Project);
    }

    iCAX::Data::Variant _MakeProductFilePayload(IN const iCAX::Product::CProductFileDefinition& Definition_)
    {
        iCAX::Data::ObjectMap _File;
        _File["magic"] = Definition_.Magic;
        _File["formatVersion"] = Definition_.FormatVersion;
        _File["quickSaveLogMagic"] = Definition_.QuickSaveLogMagic.empty()
            ? Definition_.Magic + ".QuickSaveLog"
            : Definition_.QuickSaveLogMagic;
        _File["quickSaveLogVersion"] = static_cast<unsigned int>(Definition_.QuickSaveLogVersion);
        _File["magicOffset"] = static_cast<unsigned long long>(Definition_.MagicOffset);
        _File["probeBytes"] = static_cast<unsigned long long>(Definition_.ProbeBytes);

        iCAX::Data::VariantArray _Extensions;
        _Extensions.reserve(Definition_.FileExtensions.size());
        for (const auto& _Extension : Definition_.FileExtensions)
        {
            _Extensions.emplace_back(_Extension);
        }
        _File["fileExtensions"] = _Extensions;
        return iCAX::Data::Variant(_File);
    }

    std::string _GetQuickSaveLogMagic(IN const iCAX::Product::CProductFileDefinition& Definition_)
    {
        return Definition_.QuickSaveLogMagic.empty()
            ? Definition_.Magic + ".QuickSaveLog"
            : Definition_.QuickSaveLogMagic;
    }

    iCAX::Data::Variant _MakeRecentProjectPayload(IN const iCAX::Product::CRecentProjectItem& Item_)
    {
        iCAX::Data::ObjectMap _RecentProject;
        _RecentProject["path"] = Item_.ProjectPath;
        _RecentProject["displayName"] = Item_.DisplayName;
        _RecentProject["lastOpenedTime"] = Item_.LastOpenedTime;
        return iCAX::Data::Variant(_RecentProject);
    }

    iCAX::Data::VariantArray _MakeRecentProjectsPayload(IN const iCAX::Product::CProductData& ProductData_)
    {
        iCAX::Data::VariantArray _RecentProjects;
        _RecentProjects.reserve(ProductData_.RecentProjects.size());
        for (const auto& _Item : ProductData_.RecentProjects)
        {
            _RecentProjects.emplace_back(_MakeRecentProjectPayload(_Item));
        }
        return _RecentProjects;
    }

    std::string _WideToUtf8LikeText(IN const std::wstring& Text_)
    {
        std::string _Text;
        _Text.reserve(Text_.size());
        for (const auto _Char : Text_)
        {
            _Text.push_back(static_cast<char>(_Char));
        }
        return _Text;
    }

    std::wstring _UTF8ToWide(IN const std::string& Text_)
    {
        if (Text_.empty())
        {
            return {};
        }

        const int _Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text_.data(), static_cast<int>(Text_.size()), nullptr, 0);
        if (_Length <= 0)
        {
            throw std::runtime_error("Invalid UTF-8 path text");
        }

        std::wstring _Result(static_cast<size_t>(_Length), L'\0');
        const int _Written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text_.data(), static_cast<int>(Text_.size()), _Result.data(), _Length);
        if (_Written != _Length)
        {
            throw std::runtime_error("Failed to convert UTF-8 path text");
        }
        return _Result;
    }

    std::string _WideToUTF8(IN const std::wstring& Text_)
    {
        if (Text_.empty())
        {
            return {};
        }

        const int _Length = WideCharToMultiByte(CP_UTF8, 0, Text_.data(), static_cast<int>(Text_.size()), nullptr, 0, nullptr, nullptr);
        if (_Length <= 0)
        {
            throw std::runtime_error("Failed to measure UTF-8 path text");
        }

        std::string _Result(static_cast<size_t>(_Length), '\0');
        const int _Written = WideCharToMultiByte(CP_UTF8, 0, Text_.data(), static_cast<int>(Text_.size()), _Result.data(), _Length, nullptr, nullptr);
        if (_Written != _Length)
        {
            throw std::runtime_error("Failed to convert UTF-8 path text");
        }
        return _Result;
    }

    std::filesystem::path _PathFromUTF8(IN const std::string& Text_)
    {
        std::u8string _Text(Text_.begin(), Text_.end());
        return std::filesystem::path(_Text);
    }

    std::string _PathToUTF8(IN const std::filesystem::path& Path_)
    {
        auto _Text = Path_.u8string();
        return std::string(_Text.begin(), _Text.end());
    }

    char _ToLowerASCII(IN const char ch_) noexcept
    {
        return ch_ >= 'A' && ch_ <= 'Z'
            ? static_cast<char>(ch_ - 'A' + 'a')
            : ch_;
    }

    iCAX::Data::Variant _MakePDOPayload(IN const iCAX::Project::CProjectPDODescriptor& Descriptor_)
    {
        iCAX::Data::ObjectMap _PDO;
        _PDO["enabled"] = Descriptor_.bEnabled;
        _PDO["sharedArenaName"] = _WideToUtf8LikeText(Descriptor_.SharedArenaName);
        _PDO["sharedArenaSize"] = static_cast<unsigned long long>(Descriptor_.nSharedArenaSize);

        iCAX::Data::VariantArray _Declarations;
        _Declarations.reserve(Descriptor_.Declarations.size());
        for (const auto& _Decl : Descriptor_.Declarations)
        {
            iCAX::Data::ObjectMap _DeclObject;
            _DeclObject["id"] = static_cast<unsigned long long>(_Decl.nID);
            _DeclObject["version"] = static_cast<unsigned int>(_Decl.nVersion);
            _DeclObject["direction"] = static_cast<unsigned int>(_Decl.eDirection);
            _DeclObject["payloadSize"] = static_cast<int>(_Decl.nPayloadSize);
            _Declarations.emplace_back(std::move(_DeclObject));
        }
        _PDO["declarations"] = _Declarations;
        return iCAX::Data::Variant(_PDO);
    }

    std::string _GetProductDataRoot(IN const iCAX::Application::CApplicationContext& Context_)
    {
        std::filesystem::path _Root = Context_.GetPaths().UserConfigDirectory.empty()
            ? std::filesystem::path("Setting")
            : _PathFromUTF8(Context_.GetPaths().UserConfigDirectory);
        return _PathToUTF8(_Root / "Products");
    }

    bool _ShouldRecordRecentProject(IN const std::string& strProjectPath_)
    {
        return !strProjectPath_.empty()
            && strProjectPath_.find("://") == std::string::npos;
    }

    std::string _MakeCurrentUtcTimeText()
    {
        const auto _Now = std::chrono::system_clock::now();
        const auto _Time = std::chrono::system_clock::to_time_t(_Now);
        std::tm _Tm{};
        gmtime_s(&_Tm, &_Time);

        std::ostringstream _Stream;
        _Stream << std::put_time(&_Tm, "%Y-%m-%dT%H:%M:%SZ");
        return _Stream.str();
    }

    iCAX::Data::ObjectMap _DecodeObjectPayload(IN const iCAX::Command::CCommandRequest& Request_)
    {
        auto _Payload = iCAX::Product::DecodeProductPayload(Request_.Payload);
        if (!_Payload.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument("Product command payload must be an object");
        }
        return _Payload.To<iCAX::Data::ObjectMap>();
    }

    std::string _GetOptionalString(
        IN const iCAX::Data::ObjectMap& Payload_,
        IN const std::string& strName_,
        IN const std::string& strDefault_ = std::string())
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            return strDefault_;
        }
        if (!_Iter->second.Is<std::string>())
        {
            throw std::invalid_argument("Product payload field must be a string: " + strName_);
        }
        return _Iter->second.To<std::string>();
    }

    iCAX::Data::uuid _GetRequiredUUID(
        IN const iCAX::Data::ObjectMap& Payload_,
        IN const std::string& strName_)
    {
        auto _Iter = Payload_.find(strName_);
        if (_Iter == Payload_.end() || _Iter->second.Is<std::monostate>())
        {
            throw std::invalid_argument("Product payload field is required: " + strName_);
        }
        if (_Iter->second.Is<iCAX::Data::uuid>())
        {
            auto _ID = _Iter->second.To<iCAX::Data::uuid>();
            if (_ID.is_nil())
            {
                throw std::invalid_argument("Product payload field cannot be nil uuid: " + strName_);
            }
            return _ID;
        }
        if (_Iter->second.Is<std::string>())
        {
            const auto _Text = _Iter->second.To<std::string>();
            auto _ID = iCAX::Data::uuid::from_string(_Text);
            if (!_ID || _ID->is_nil())
            {
                throw std::invalid_argument("Product payload field is not a valid uuid: " + strName_);
            }
            return *_ID;
        }

        throw std::invalid_argument("Product payload field must be a uuid or string: " + strName_);
    }

    void _RequireProductMailboxContext(
        IN const iCAX::Product::IProductContext* pProductContext_,
        IN const iCAX::Project::IProjectContext* pProjectContext_,
        IN const iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (pProjectContext_ || pSceneContext_)
        {
            throw std::invalid_argument("Product command must be sent to the product mailbox");
        }
        if (!pProductContext_)
        {
            throw std::invalid_argument("Product command context is missing ProductContext");
        }
    }

    void _RequireProjectMailboxContext(
        IN const iCAX::Product::IProductContext* pProductContext_,
        IN const iCAX::Project::IProjectContext* pProjectContext_,
        IN const iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (!pProductContext_)
        {
            throw std::invalid_argument("Project command context is missing ProductContext");
        }
        if (!pProjectContext_)
        {
            throw std::invalid_argument("Project command context is missing ProjectContext");
        }
        if (!pSceneContext_)
        {
            throw std::invalid_argument("Project command context is missing SceneContext");
        }
    }

    iCAX::Command::CCommandResponse _MakeProductPayloadResponse(IN const iCAX::Data::Variant& Payload_)
    {
        iCAX::Command::CCommandResponse _Response;
        _Response.nStatus = iCAX::Command::ECommandStatusCode::Ok;
        _Response.Payload = iCAX::Product::EncodeProductPayload(Payload_);
        return _Response;
    }

    std::string _NormalizeModulePath(IN const std::string& strPath_)
    {
        if (strPath_.empty())
        {
            return {};
        }

        const auto _WidePath = _UTF8ToWide(strPath_);
        const auto _RequiredLength = GetFullPathNameW(_WidePath.c_str(), 0, nullptr, nullptr);
        if (_RequiredLength == 0)
        {
            return strPath_;
        }

        std::vector<wchar_t> _Buffer(static_cast<size_t>(_RequiredLength) + 1);
        const auto _Length = GetFullPathNameW(_WidePath.c_str(), static_cast<DWORD>(_Buffer.size()), _Buffer.data(), nullptr);
        if (_Length == 0 || _Length >= _Buffer.size())
        {
            return strPath_;
        }

        std::string _Normalized = _WideToUTF8(std::wstring(_Buffer.data(), _Length));
        std::replace(_Normalized.begin(), _Normalized.end(), '/', '\\');
        std::transform(
            _Normalized.begin(),
            _Normalized.end(),
            _Normalized.begin(),
            [](IN const char ch_) { return _ToLowerASCII(ch_); });
        return _Normalized;
    }

    std::string _GetLoadedModulePath(IN HMODULE hModule_)
    {
        if (!hModule_)
        {
            return {};
        }

        std::vector<wchar_t> _Buffer(MAX_PATH);
        while (true)
        {
            const auto _Length = GetModuleFileNameW(hModule_, _Buffer.data(), static_cast<DWORD>(_Buffer.size()));
            if (_Length == 0)
            {
                return {};
            }
            if (_Length < _Buffer.size() - 1)
            {
                return _NormalizeModulePath(_WideToUTF8(std::wstring(_Buffer.data(), _Length)));
            }
            _Buffer.resize(_Buffer.size() * 2);
        }
    }

    std::mutex& _GetLoadedModuleMutex()
    {
        static std::mutex _Mutex;
        return _Mutex;
    }

    std::map<std::string, HMODULE>& _GetLoadedModules()
    {
        static std::map<std::string, HMODULE> _Modules;
        return _Modules;
    }

    HMODULE _LoadProductModuleOnce(IN const std::string& strPath_, IN const std::string& strKind_)
    {
        if (strPath_.empty())
        {
            return nullptr;
        }

        const auto _RequestedPath = _NormalizeModulePath(strPath_);
        {
            std::lock_guard<std::mutex> _Lock(_GetLoadedModuleMutex());
            auto _Iter = _GetLoadedModules().find(_RequestedPath);
            if (_Iter != _GetLoadedModules().end())
            {
                return _Iter->second;
            }
        }

        const auto _LoadPath = _RequestedPath.empty() ? strPath_ : _RequestedPath;
        HMODULE _Module = LoadLibraryW(_UTF8ToWide(_LoadPath).c_str());
        if (!_Module)
        {
            DWORD code = GetLastError();
            throw std::runtime_error(
                std::format(
                    "[Product Module Load Failure]\n"
                    "  Kind      : {}\n"
                    "  Path      : {}\n"
                    "  Error     : Win32 code {}\n",
                    strKind_, strPath_, code));
        }

        auto _LoadedPath = _GetLoadedModulePath(_Module);
        if (_LoadedPath.empty())
        {
            _LoadedPath = _RequestedPath;
        }

        {
            std::lock_guard<std::mutex> _Lock(_GetLoadedModuleMutex());
            auto [_Iter, _Inserted] = _GetLoadedModules().emplace(_LoadedPath, _Module);
            if (!_Inserted)
            {
                FreeLibrary(_Module);
                return _Iter->second;
            }
        }
        return _Module;
    }

    void _AppendUniqueModulePath(IN OUT std::vector<std::string>& ModulePaths_, IN const std::string& strPath_)
    {
        auto _Path = _NormalizeModulePath(strPath_);
        if (_Path.empty())
        {
            return;
        }
        if (std::find(ModulePaths_.begin(), ModulePaths_.end(), _Path) == ModulePaths_.end())
        {
            ModulePaths_.push_back(std::move(_Path));
        }
    }

    std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> _CreateResourceLoaderRegistryFromModulePaths(
        IN const std::vector<std::string>& ModulePaths_)
    {
        auto _pRegistry = std::make_shared<iCAX::Resource::CResourceLoaderRegistry>();
        iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayByModulePaths(*_pRegistry, ModulePaths_);
        return _pRegistry;
    }
}

iCAX::Product::CProductRuntime::CProductRuntime(
    IN const CProductDefinition& Definition_,
    IN std::shared_ptr<iCAX::Application::CApplicationContext> pApplicationContext_,
    IN std::shared_ptr<iCAX::Services::CServiceProvider> pApplicationServiceProvider_,
    IN std::shared_ptr<iCAX::Mail::CMailChannelRegistry> pMailChannelRegistry_,
    IN std::shared_ptr<IProductDataStore> pProductDataStore_,
    IN uint32_t nFrameIntervalMilliseconds_)
    : m_Definition(Definition_)
    , m_ProductData()
    , m_ProductChannelID(iCAX::Data::GenerateNewUUID())
    , m_pApplicationContext(std::move(pApplicationContext_))
    , m_pApplicationServiceProvider(std::move(pApplicationServiceProvider_))
    , m_pMailChannelRegistry(std::move(pMailChannelRegistry_))
    , m_pProductMetaRegistry(iCAX::Database::CreateMetaRegistry())
    , m_pProductBehaviourRegistry(iCAX::Behaviour::CreateBehaviourRegistry())
    , m_pProductResourceLoaderRegistry(std::make_shared<iCAX::Resource::CResourceLoaderRegistry>())
    , m_pProductDataStore(std::move(pProductDataStore_))
    , m_pCommandRegistry(std::make_shared<iCAX::Command::CCommandRegistry>())
    , m_pCommandDispatcher(std::make_unique<iCAX::Command::CCommandDispatcher>(m_pCommandRegistry))
    , m_pMailCommandHandler(std::make_unique<iCAX::MailHandler::CMailCommandHandler>())
    , m_nFrameIntervalMilliseconds(nFrameIntervalMilliseconds_ == 0 ? 1 : nFrameIntervalMilliseconds_)
{
    if (!IsValidProductID(m_Definition.ProductID))
    {
        throw std::invalid_argument("ProductID contains unsafe characters: " + m_Definition.ProductID);
    }
    if (m_Definition.ProductName.empty())
    {
        m_Definition.ProductName = m_Definition.ProductID;
    }
    if (m_Definition.ProjectFile.Magic.empty())
    {
        throw std::invalid_argument("Product project file magic cannot be empty: " + m_Definition.ProductID);
    }
    if (m_Definition.ProjectFile.QuickSaveLogVersion == 0)
    {
        throw std::invalid_argument("Product quick save log version cannot be zero: " + m_Definition.ProductID);
    }
    if (!m_pApplicationContext)
    {
        throw std::invalid_argument("ApplicationContext cannot be null");
    }
    if (!m_pApplicationServiceProvider)
    {
        throw std::invalid_argument("Application ServiceProvider cannot be null");
    }
    if (!m_pMailChannelRegistry)
    {
        throw std::invalid_argument("MailChannelRegistry cannot be null");
    }
    if (!m_pProductMetaRegistry)
    {
        throw std::invalid_argument("Product MetaRegistry cannot be null");
    }
    if (!m_pProductBehaviourRegistry)
    {
        throw std::invalid_argument("Product BehaviourRegistry cannot be null");
    }
    if (!m_pProductResourceLoaderRegistry)
    {
        throw std::invalid_argument("Product ResourceLoaderRegistry cannot be null");
    }
    if (!m_pProductDataStore)
    {
        m_pProductDataStore = std::make_shared<CFileProductDataStore>(_GetProductDataRoot(*m_pApplicationContext));
    }

    RegisterBuiltInProductCommands();
}

iCAX::Product::CProductRuntime::~CProductRuntime()
{
    Stop();
}

void iCAX::Product::CProductRuntime::Start()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
        if (m_WorkThread.joinable() && !m_bStarted)
        {
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }

    std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
    if (m_bStarted)
    {
        return;
    }

    LoadProductData();
    if (!m_bModulesLoaded)
    {
        LoadProductModules();
        m_bModulesLoaded = true;
    }
    if (!m_bRegistrationsReplayed)
    {
        // 模块加载后，DLL 内静态注册对象已经把注册动作写入各 Catalog。
        // 这里按已加载模块路径回放到产品自己的注册表，避免不同产品互相污染。
        iCAX::Database::CMetaRegistrationCatalog::ReplayByModulePaths(*m_pProductMetaRegistry, m_LoadedModulePaths);
        iCAX::Behaviour::CBehaviourRegistrationCatalog::ReplayByModulePaths(*m_pProductBehaviourRegistry, m_LoadedModulePaths);
        iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(*m_pApplicationServiceProvider, m_LoadedModulePaths);
        iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayByModulePaths(*m_pProductResourceLoaderRegistry, m_LoadedModulePaths);
        iCAX::Command::CCommandRegistrationCatalog::ReplayByModulePaths(*m_pCommandRegistry, m_LoadedModulePaths);
        m_bRegistrationsReplayed = true;
    }
    if (!m_pMailChannelRegistry->CreateChannel(m_ProductChannelID))
    {
        throw std::runtime_error("Product mail channel already exists");
    }
    m_bStarted = true;
    m_bFaulted = false;
    m_strFaultMessage.clear();
    m_pFaultException = nullptr;
    m_bStopWorkerRequested.store(false, std::memory_order_release);

    try
    {
        m_WorkThread = std::thread(&CProductRuntime::WorkerMain, this);
    }
    catch (...)
    {
        m_bStarted = false;
        (void)m_pMailChannelRegistry->RemoveChannel(m_ProductChannelID);
        throw;
    }
}

void iCAX::Product::CProductRuntime::Stop()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
        m_bStopWorkerRequested.store(true, std::memory_order_release);
        m_bStarted = false;
        if (m_WorkThread.joinable())
        {
            if (m_WorkThread.get_id() == std::this_thread::get_id())
            {
                m_WorkerCondition.notify_all();
                return;
            }
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }

    m_WorkerCondition.notify_all();
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }

    CloseRuntimeObjects();
}

void iCAX::Product::CProductRuntime::CloseRuntimeObjects()
{
    auto _Catalogs = SnapshotProjectCatalogs();
    auto _Runtimes = SnapshotProjectRuntimes();
    if (_Catalogs.empty() && _Runtimes.empty())
    {
        if (m_pMailChannelRegistry && !m_ProductChannelID.is_nil())
        {
            (void)m_pMailChannelRegistry->RemoveChannel(m_ProductChannelID);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> _Lock(m_ProjectCatalogMutex);
        m_ProjectCatalogs.clear();
    }
    {
        std::lock_guard<std::mutex> _Lock(m_ProjectRuntimeMutex);
        m_ProjectRuntimes.clear();
    }
    for (auto& _pRuntime : _Runtimes)
    {
        if (_pRuntime)
        {
            _pRuntime->Close();
        }
    }
    for (auto& _pCatalog : _Catalogs)
    {
        if (_pCatalog)
        {
            _pCatalog->CloseAll();
        }
    }
    if (m_pMailChannelRegistry && !m_ProductChannelID.is_nil())
    {
        (void)m_pMailChannelRegistry->RemoveChannel(m_ProductChannelID);
    }
}

bool iCAX::Product::CProductRuntime::IsStarted() const
{
    std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
    return m_bStarted;
}

bool iCAX::Product::CProductRuntime::IsFaulted() const
{
    std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
    return m_bFaulted;
}

std::string iCAX::Product::CProductRuntime::GetFaultMessage() const
{
    std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
    return m_strFaultMessage;
}

const iCAX::Product::CProductDefinition& iCAX::Product::CProductRuntime::GetDefinition() const
{
    return m_Definition;
}

iCAX::Product::CProductData iCAX::Product::CProductRuntime::GetProductData() const
{
    return SnapshotProductData();
}

iCAX::Data::PropertyBag iCAX::Product::CProductRuntime::GetSettings() const
{
    std::lock_guard<std::mutex> _Lock(m_ProductDataMutex);
    return m_ProductData.Settings;
}

void iCAX::Product::CProductRuntime::ReplaceSettings(IN const iCAX::Data::PropertyBag& Settings_)
{
    CProductData _ProductData;
    {
        std::lock_guard<std::mutex> _Lock(m_ProductDataMutex);
        m_ProductData.Settings = Settings_;
        _ProductData = m_ProductData;
    }

    SaveProductData(_ProductData);
}

const std::string& iCAX::Product::CProductRuntime::GetProductID() const
{
    return m_Definition.ProductID;
}

const iCAX::Data::uuid& iCAX::Product::CProductRuntime::GetProductChannelID() const
{
    return m_ProductChannelID;
}

iCAX::Mail::CMailPostOffice iCAX::Product::CProductRuntime::GetProductFrontendPostOffice() const
{
    return GetFrontendPostOffice();
}

iCAX::Mail::CMailPostOffice iCAX::Product::CProductRuntime::GetBackendPostOffice() const
{
    EnsureStarted();
    return m_pMailChannelRegistry->GetBackendPostOffice(m_ProductChannelID);
}

iCAX::Mail::CMailPostOffice iCAX::Product::CProductRuntime::GetFrontendPostOffice() const
{
    EnsureStarted();
    return m_pMailChannelRegistry->GetFrontendPostOffice(m_ProductChannelID);
}

void iCAX::Product::CProductRuntime::SendFrontendEvent(
    IN uint64_t nTypeCode_,
    IN const std::string& strPayloadText_)
{
    EnsureStarted();

    iCAX::Mail::MailHeader _Header;
    _Header.nMailId = AllocateBackendMailID();
    _Header.nOriginId = 0;
    _Header.nTypeCode = nTypeCode_;
    _Header.nStamp = iCAX::Mail::kMailOk;

    GetBackendPostOffice().SendText(_Header, strPayloadText_);
}

void iCAX::Product::CProductRuntime::DispatchProductMails()
{
    if (!IsStarted() || !m_pCommandDispatcher)
    {
        return;
    }

    DispatchProjectMails(GetBackendPostOffice(), nullptr);
}

std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>> iCAX::Product::CProductRuntime::GetProjectCatalogs() const
{
    return SnapshotProjectCatalogs();
}

std::shared_ptr<iCAX::Project::CProjectCatalog> iCAX::Product::CProductRuntime::FindProjectCatalog(
    IN const iCAX::Data::uuid& CatalogID_) const
{
    std::lock_guard<std::mutex> _Lock(m_ProjectCatalogMutex);
    auto _Iter = m_ProjectCatalogs.find(CatalogID_);
    if (_Iter == m_ProjectCatalogs.end())
    {
        return nullptr;
    }
    return _Iter->second;
}

std::shared_ptr<iCAX::Project::CProjectCatalog> iCAX::Product::CProductRuntime::FindProjectCatalogByProjectID(
    IN const iCAX::Data::uuid& ProjectID_) const
{
    auto _Catalogs = SnapshotProjectCatalogs();
    for (const auto& _pCatalog : _Catalogs)
    {
        if (_pCatalog && _pCatalog->FindProject(ProjectID_))
        {
            return _pCatalog;
        }
    }
    return nullptr;
}

std::shared_ptr<iCAX::Project::CProjectCatalog> iCAX::Product::CProductRuntime::OpenProjectCatalog(
    IN const std::string& strCatalogName_,
    IN const std::string& strCatalogPath_,
    IN const std::string& strProjectName_,
    IN const std::string& strProjectPath_)
{
    EnsureStarted();

    const auto _CatalogName = !strCatalogName_.empty()
        ? strCatalogName_
        : (!strProjectName_.empty() ? strProjectName_ : std::string("Project Catalog"));
    const auto _CatalogPath = !strCatalogPath_.empty()
        ? strCatalogPath_
        : strProjectPath_;
    const auto _ProjectName = !strProjectName_.empty()
        ? strProjectName_
        : _CatalogName;
    const auto _ProjectPath = !strProjectPath_.empty()
        ? strProjectPath_
        : _CatalogPath;

    iCAX::Project::CProjectCatalogCreateInfo _CatalogInfo;
    _CatalogInfo.CatalogName = _CatalogName;
    _CatalogInfo.CatalogPath = _CatalogPath;
    _CatalogInfo.pApplicationContext = m_pApplicationContext;
    _CatalogInfo.pProductContext = std::static_pointer_cast<IProductContext>(shared_from_this());
    _CatalogInfo.pServiceProvider = m_pApplicationServiceProvider;
    _CatalogInfo.pMetaRegistry = m_pProductMetaRegistry;
    _CatalogInfo.pBehaviourRegistry = m_pProductBehaviourRegistry;
    _CatalogInfo.pMailChannelRegistry = m_pMailChannelRegistry;
    _CatalogInfo.bEnablePDOHub = m_Definition.bEnablePDOHub;
    _CatalogInfo.PDOHubCreateInfo = m_Definition.PDOHubCreateInfo;
    const auto _ModulePaths = m_LoadedModulePaths;
    // 每个 Scene 都创建自己的资源 loader registry。
    // registry 内容来自产品模块回放，但资源缓存和加载器集合归 Scene 隔离。
    _CatalogInfo.ResourceLoaderRegistryFactory = [_ModulePaths]() {
        return _CreateResourceLoaderRegistryFromModulePaths(_ModulePaths);
    };
    auto _pCatalog = std::make_shared<iCAX::Project::CProjectCatalog>(_CatalogInfo);
    auto _pProject = _pCatalog->OpenMainProject(
        _ProjectName,
        _ProjectPath,
        m_Definition.DefaultProjectStartupComponent);
    auto _pProjectRuntime = iCAX::Project::CreateLocalProjectRuntime(_pProject);
    const auto _QuickSaveLogMagic = _GetQuickSaveLogMagic(m_Definition.ProjectFile);
    const auto _QuickSaveLogVersion = m_Definition.ProjectFile.QuickSaveLogVersion;

    bool _bCatalogRegistered = false;
    try
    {
        if (!_pProject->GetQuickSaveLogPath().empty())
        {
            _pProject->ReplayQuickSaveLog(_QuickSaveLogMagic, _QuickSaveLogVersion);
            _pProject->OpenQuickSaveLog(false, _QuickSaveLogMagic, _QuickSaveLogVersion);
        }
        {
            std::lock_guard<std::mutex> _Lock(m_ProjectCatalogMutex);
            const auto _CatalogID = _pCatalog->GetCatalogID();
            if (m_ProjectCatalogs.find(_CatalogID) != m_ProjectCatalogs.end())
            {
                throw std::runtime_error("ProjectCatalog already exists");
            }
            m_ProjectCatalogs.emplace(_CatalogID, _pCatalog);
            _bCatalogRegistered = true;
        }
        RegisterProjectRuntime(_pProjectRuntime);
        StartProject(_pProjectRuntime);
        RecordRecentProject(_ProjectPath, _ProjectName);
    }
    catch (...)
    {
        // 打开流程跨 catalog、project runtime 和工作线程；任一步失败都要撤销已登记对象，避免残留半开项目。
        (void)RemoveProjectRuntime(_pProject->GetProjectID());
        if (_bCatalogRegistered)
        {
            (void)CloseProjectCatalog(_pCatalog->GetCatalogID());
        }
        else
        {
            _pCatalog->CloseAll();
        }
        throw;
    }

    return _pCatalog;
}

std::shared_ptr<iCAX::Project::CProject> iCAX::Product::CProductRuntime::OpenTransientProject(
    IN const iCAX::Data::uuid& CatalogID_,
    IN const std::string& strProjectName_,
    IN const std::string& strProjectPath_)
{
    EnsureStarted();

    auto _pCatalog = FindProjectCatalog(CatalogID_);
    if (!_pCatalog)
    {
        throw std::runtime_error("ProjectCatalog not found");
    }

    auto _pProject = _pCatalog->OpenTransientProject(
        strProjectName_,
        strProjectPath_,
        m_Definition.DefaultProjectStartupComponent);
    auto _pProjectRuntime = iCAX::Project::CreateLocalProjectRuntime(_pProject);

    try
    {
        RegisterProjectRuntime(_pProjectRuntime);
        StartProject(_pProjectRuntime);
    }
    catch (...)
    {
        (void)RemoveProjectRuntime(_pProject->GetProjectID());
        (void)_pCatalog->CloseProject(_pProject->GetProjectID());
        throw;
    }
    return _pProject;
}

bool iCAX::Product::CProductRuntime::CloseProject(IN const iCAX::Data::uuid& ProjectID_)
{
    auto _pCatalog = FindProjectCatalogByProjectID(ProjectID_);
    if (!_pCatalog)
    {
        return false;
    }

    auto _pRuntime = RemoveProjectRuntime(ProjectID_);
    if (_pRuntime)
    {
        _pRuntime->Close();
    }
    return _pCatalog->CloseProject(ProjectID_);
}

bool iCAX::Product::CProductRuntime::CloseProjectCatalog(IN const iCAX::Data::uuid& CatalogID_)
{
    std::shared_ptr<iCAX::Project::CProjectCatalog> _pCatalog;
    {
        std::lock_guard<std::mutex> _Lock(m_ProjectCatalogMutex);
        auto _Iter = m_ProjectCatalogs.find(CatalogID_);
        if (_Iter == m_ProjectCatalogs.end())
        {
            return false;
        }
        _pCatalog = _Iter->second;
        m_ProjectCatalogs.erase(_Iter);
    }

    if (_pCatalog)
    {
        for (const auto& _ProjectID : _pCatalog->GetProjectIDs())
        {
            auto _pRuntime = RemoveProjectRuntime(_ProjectID);
            if (_pRuntime)
            {
                _pRuntime->Close();
            }
        }
        _pCatalog->CloseAll();
    }
    return true;
}

iCAX::Mail::CMailPostOffice iCAX::Product::CProductRuntime::GetProjectFrontendPostOffice(
    IN const iCAX::Data::uuid& ProjectID_) const
{
    auto _pRuntime = FindProjectRuntime(ProjectID_);
    if (!_pRuntime)
    {
        throw std::runtime_error("Project not found");
    }
    return _pRuntime->GetFrontendPostOffice();
}

iCAX::Data::Variant iCAX::Product::CProductRuntime::BuildProductStatePayload() const
{
    iCAX::Data::ObjectMap _Root;
    _Root["productId"] = m_Definition.ProductID;
    _Root["productName"] = m_Definition.ProductName;
    _Root["productVersion"] = m_Definition.ProductVersion;
    _Root["frontendEntry"] = m_Definition.FrontendEntry;
    _Root["defaultProjectStartupComponent"] = m_Definition.DefaultProjectStartupComponent;
    _Root["projectFile"] = _MakeProductFilePayload(m_Definition.ProjectFile);
    _Root["productChannelId"] = m_ProductChannelID;
    _Root["isStarted"] = IsStarted();
    _Root["isFaulted"] = IsFaulted();
    _Root["faultMessage"] = GetFaultMessage();

    _Root["recentProjects"] = _MakeRecentProjectsPayload(SnapshotProductData());

    auto _CatalogsSnapshot = SnapshotProjectCatalogs();
    iCAX::Data::VariantArray _Catalogs;
    _Catalogs.reserve(_CatalogsSnapshot.size());
    for (const auto& _pCatalog : _CatalogsSnapshot)
    {
        _Catalogs.emplace_back(BuildProjectCatalogPayload(_pCatalog));
    }
    _Root["catalogCount"] = static_cast<unsigned long long>(_Catalogs.size());
    _Root["catalogs"] = _Catalogs;

    return iCAX::Data::Variant(_Root);
}

iCAX::Data::Variant iCAX::Product::CProductRuntime::BuildProjectCatalogPayload(
    IN const std::shared_ptr<iCAX::Project::CProjectCatalog>& pCatalog_) const
{
    iCAX::Data::ObjectMap _Catalog;
    if (!pCatalog_)
    {
        return iCAX::Data::Variant(_Catalog);
    }

    auto _pMainProject = pCatalog_->GetMainProject();
    auto _pMainProjectRuntime = _pMainProject ? FindProjectRuntime(_pMainProject->GetProjectID()) : nullptr;
    _Catalog["catalogId"] = pCatalog_->GetCatalogID();
    _Catalog["catalogName"] = pCatalog_->GetCatalogName();
    _Catalog["catalogPath"] = pCatalog_->GetCatalogPath();
    _Catalog["hasMainProject"] = _pMainProject != nullptr;
    _Catalog["mainProjectId"] = _pMainProject ? _pMainProject->GetProjectID() : iCAX::Data::GenerateNilUUID();
    _Catalog["mainProject"] = _MakeProjectPayload(_pMainProjectRuntime);
    _Catalog["projectCount"] = static_cast<unsigned long long>(pCatalog_->Count());
    _Catalog["transientProjectCount"] = static_cast<unsigned long long>(pCatalog_->TransientCount());

    iCAX::Data::VariantArray _Projects;
    auto _Snapshot = pCatalog_->GetProjects();
    _Projects.reserve(_Snapshot.size());
    for (const auto& _pProject : _Snapshot)
    {
        _Projects.emplace_back(_MakeProjectPayload(_pProject ? FindProjectRuntime(_pProject->GetProjectID()) : nullptr));
    }
    _Catalog["projects"] = _Projects;
    return iCAX::Data::Variant(_Catalog);
}

iCAX::Command::CCommandRegistry& iCAX::Product::CProductRuntime::GetCommandRegistry() const
{
    return *m_pCommandRegistry;
}

iCAX::Services::CServiceProvider& iCAX::Product::CProductRuntime::GetServiceProvider() const
{
    return *m_pApplicationServiceProvider;
}

iCAX::Database::IMetaRegistry& iCAX::Product::CProductRuntime::GetMetaRegistry() const
{
    return *m_pProductMetaRegistry;
}

iCAX::Behaviour::IBehaviourRegistry& iCAX::Product::CProductRuntime::GetBehaviourRegistry() const
{
    return *m_pProductBehaviourRegistry;
}

iCAX::Resource::CResourceLoaderRegistry& iCAX::Product::CProductRuntime::GetResourceLoaderRegistry() const
{
    return *m_pProductResourceLoaderRegistry;
}

void iCAX::Product::CProductRuntime::EnsureStarted() const
{
    if (!IsStarted())
    {
        throw std::logic_error("ProductRuntime is not started");
    }
}

void iCAX::Product::CProductRuntime::WorkerMain()
{
    try
    {
        const auto _FrameInterval = std::chrono::milliseconds(m_nFrameIntervalMilliseconds);
        auto _NextFrameTime = std::chrono::steady_clock::now();
        while (!m_bStopWorkerRequested.load(std::memory_order_acquire))
        {
            DispatchProductMails();

            _NextFrameTime += _FrameInterval;
            {
                std::unique_lock<std::mutex> _Lock(m_RuntimeMutex);
                m_WorkerCondition.wait_until(
                    _Lock,
                    _NextFrameTime,
                    [this]() { return m_bStopWorkerRequested.load(std::memory_order_acquire); });
            }
            if (_NextFrameTime < std::chrono::steady_clock::now() - _FrameInterval)
            {
                _NextFrameTime = std::chrono::steady_clock::now();
            }
        }
    }
    catch (...)
    {
        auto _Exception = std::current_exception();
        std::string _Message = "ProductRuntime faulted";
        try
        {
            std::rethrow_exception(_Exception);
        }
        catch (const std::exception& _Error)
        {
            _Message = _Error.what();
        }
        catch (...)
        {
            _Message = "ProductRuntime caught a non-standard exception";
        }

        RecordFault(_Message, _Exception);
        CloseRuntimeObjects();
    }
}

void iCAX::Product::CProductRuntime::RecordFault(IN const std::string& strMessage_, IN std::exception_ptr pException_)
{
    std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
    m_bStarted = false;
    m_bFaulted = true;
    m_strFaultMessage = strMessage_;
    m_pFaultException = pException_;
}

std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>> iCAX::Product::CProductRuntime::SnapshotProjectCatalogs() const
{
    std::lock_guard<std::mutex> _Lock(m_ProjectCatalogMutex);
    std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>> _Catalogs;
    _Catalogs.reserve(m_ProjectCatalogs.size());
    for (const auto& [_ID, _pCatalog] : m_ProjectCatalogs)
    {
        _Catalogs.push_back(_pCatalog);
    }
    return _Catalogs;
}

std::vector<std::shared_ptr<iCAX::Project::IProjectRuntime>> iCAX::Product::CProductRuntime::SnapshotProjectRuntimes() const
{
    std::lock_guard<std::mutex> _Lock(m_ProjectRuntimeMutex);
    std::vector<std::shared_ptr<iCAX::Project::IProjectRuntime>> _Runtimes;
    _Runtimes.reserve(m_ProjectRuntimes.size());
    for (const auto& [_ID, _pRuntime] : m_ProjectRuntimes)
    {
        _Runtimes.push_back(_pRuntime);
    }
    return _Runtimes;
}

iCAX::Product::CProductData iCAX::Product::CProductRuntime::SnapshotProductData() const
{
    std::lock_guard<std::mutex> _Lock(m_ProductDataMutex);
    return m_ProductData;
}

std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> iCAX::Product::CProductRuntime::CreateProjectResourceLoaderRegistry() const
{
    return _CreateResourceLoaderRegistryFromModulePaths(m_LoadedModulePaths);
}

std::shared_ptr<iCAX::Project::IProjectRuntime> iCAX::Product::CProductRuntime::FindProjectRuntime(
    IN const iCAX::Data::uuid& ProjectID_) const
{
    std::lock_guard<std::mutex> _Lock(m_ProjectRuntimeMutex);
    auto _Iter = m_ProjectRuntimes.find(ProjectID_);
    if (_Iter == m_ProjectRuntimes.end())
    {
        return nullptr;
    }
    return _Iter->second;
}

void iCAX::Product::CProductRuntime::RegisterProjectRuntime(
    IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_)
{
    if (!pProjectRuntime_)
    {
        throw std::invalid_argument("ProjectRuntime cannot be null");
    }

    std::lock_guard<std::mutex> _Lock(m_ProjectRuntimeMutex);
    const auto _ProjectID = pProjectRuntime_->GetProjectID();
    if (m_ProjectRuntimes.find(_ProjectID) != m_ProjectRuntimes.end())
    {
        throw std::runtime_error("ProjectRuntime already exists");
    }
    m_ProjectRuntimes.emplace(_ProjectID, pProjectRuntime_);
}

std::shared_ptr<iCAX::Project::IProjectRuntime> iCAX::Product::CProductRuntime::RemoveProjectRuntime(
    IN const iCAX::Data::uuid& ProjectID_)
{
    std::lock_guard<std::mutex> _Lock(m_ProjectRuntimeMutex);
    auto _Iter = m_ProjectRuntimes.find(ProjectID_);
    if (_Iter == m_ProjectRuntimes.end())
    {
        return nullptr;
    }
    auto _pRuntime = _Iter->second;
    m_ProjectRuntimes.erase(_Iter);
    return _pRuntime;
}

void iCAX::Product::CProductRuntime::DispatchProjectMails(
    IN const iCAX::Mail::CMailPostOffice& PostOffice_,
    IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_)
{
    if (!PostOffice_.IsValid() || !m_pCommandDispatcher)
    {
        return;
    }

    iCAX::Project::IProjectContext* _pProjectContext = nullptr;
    iCAX::Project::ISceneContext* _pSceneContext = nullptr;
    auto _pLocalProject = pProjectRuntime_ ? pProjectRuntime_->GetLocalProject() : nullptr;
    if (_pLocalProject)
    {
        _pProjectContext = static_cast<iCAX::Project::IProjectContext*>(_pLocalProject.get());
        _pSceneContext = &_pLocalProject->GetMainScene();
    }

    m_pMailCommandHandler->DispatchAvailableMails(
        PostOffice_,
        *m_pCommandDispatcher,
        *m_pApplicationContext,
        static_cast<iCAX::Product::IProductContext*>(this),
        _pProjectContext,
        _pSceneContext,
        [this]() { return AllocateBackendMailID(); });
}

void iCAX::Product::CProductRuntime::StartProject(IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_)
{
    if (!pProjectRuntime_)
    {
        throw std::invalid_argument("ProjectRuntime cannot be null");
    }

    std::weak_ptr<CProductRuntime> _WeakRuntime = shared_from_this();
    std::weak_ptr<iCAX::Project::IProjectRuntime> _WeakProjectRuntime = pProjectRuntime_;
    pProjectRuntime_->SetFrameHandler(
        [_WeakRuntime, _WeakProjectRuntime](
            iCAX::Project::IProjectRuntime&,
            const iCAX::Mail::CMailPostOffice& BackendPostOffice_) {
            auto _pRuntime = _WeakRuntime.lock();
            auto _pProjectRuntime = _WeakProjectRuntime.lock();
            if (!_pRuntime || !_pProjectRuntime)
            {
                return;
            }
            // 主 Scene 线程每帧进入这里，产品 runtime 只做邮件分发，不直接驱动 Scene 数据。
            _pRuntime->DispatchProjectMails(BackendPostOffice_, _pProjectRuntime);
        });
    pProjectRuntime_->Start();
}

uint64_t iCAX::Product::CProductRuntime::AllocateBackendMailID()
{
    return m_nNextBackendMailID.fetch_add(1, std::memory_order_relaxed);
}

void iCAX::Product::CProductRuntime::LoadProductData()
{
    if (!m_pProductDataStore)
    {
        throw std::logic_error("ProductDataStore is not available");
    }

    auto _ProductData = m_pProductDataStore->Load(m_Definition.ProductID);
    {
        std::lock_guard<std::mutex> _Lock(m_ProductDataMutex);
        m_ProductData = std::move(_ProductData);
    }
}

void iCAX::Product::CProductRuntime::SaveProductData(IN const CProductData& ProductData_) const
{
    if (!m_pProductDataStore)
    {
        throw std::logic_error("ProductDataStore is not available");
    }
    m_pProductDataStore->Save(m_Definition.ProductID, ProductData_);
}

void iCAX::Product::CProductRuntime::RecordRecentProject(
    IN const std::string& strProjectPath_,
    IN const std::string& strDisplayName_)
{
    if (!_ShouldRecordRecentProject(strProjectPath_))
    {
        return;
    }

    CProductData _ProductData;
    {
        std::lock_guard<std::mutex> _Lock(m_ProductDataMutex);
        auto& _RecentProjects = m_ProductData.RecentProjects;
        _RecentProjects.erase(
            std::remove_if(
                _RecentProjects.begin(),
                _RecentProjects.end(),
                [&strProjectPath_](IN const CRecentProjectItem& Item_) {
                    return Item_.ProjectPath == strProjectPath_;
                }),
            _RecentProjects.end());

        CRecentProjectItem _Item;
        _Item.ProjectPath = strProjectPath_;
        _Item.DisplayName = strDisplayName_;
        _Item.LastOpenedTime = _MakeCurrentUtcTimeText();
        _RecentProjects.insert(_RecentProjects.begin(), std::move(_Item));
        if (_RecentProjects.size() > 20)
        {
            _RecentProjects.resize(20);
        }
        _ProductData = m_ProductData;
    }

    SaveProductData(_ProductData);
}

void iCAX::Product::CProductRuntime::LoadProductModules()
{
    auto _LoadModule = [](IN const std::string& strPath_, IN const std::string& strKind_) -> std::string {
        return _GetLoadedModulePath(_LoadProductModuleOnce(strPath_, strKind_));
    };

    // LoadLibrary 只负责把 DLL 装入进程；模块内静态注册对象会登记到各 RegistrationCatalog。
    // 真正写入产品注册表发生在 Start() 的 ReplayByModulePaths 阶段。
    for (const auto& _Path : m_Definition.Modules.ComponentModules)
    {
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Path, "component"));
    }
    for (const auto& _Path : m_Definition.Modules.BehaviourModules)
    {
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Path, "behaviour"));
    }
    for (const auto& _Path : m_Definition.Modules.ServiceModules)
    {
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Path, "service"));
    }
    for (const auto& _Path : m_Definition.Modules.CommandModules)
    {
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Path, "command"));
    }
    for (const auto& _Module : m_Definition.Modules.ModuleGroups)
    {
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Module.strComponentPath, "component"));
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Module.strBehaviourPath, "behaviour"));
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Module.strServicePath, "service"));
        _AppendUniqueModulePath(m_LoadedModulePaths, _LoadModule(_Module.strCommandPath, "command"));
    }
}

void iCAX::Product::CProductRuntime::RegisterBuiltInProductCommands()
{
    std::vector<CStaticProductCommandTarget::CommandRecord> _Commands;
    _Commands.emplace_back(
        kProductGetStateName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleGetStateCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Commands.emplace_back(
        kProductListProjectCatalogsName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleListProjectCatalogsCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Commands.emplace_back(
        kProductOpenProjectCatalogName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleOpenProjectCatalogCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Commands.emplace_back(
        kProductCloseProjectCatalogName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleCloseProjectCatalogCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });

    auto _pCommandTarget = std::make_shared<CStaticProductCommandTarget>(kProductCommandMainName, std::move(_Commands));
    if (!m_pCommandRegistry->Register(_pCommandTarget))
    {
        throw std::runtime_error("Built-in product command target is already registered: " + _pCommandTarget->GetMainName());
    }

    std::vector<CStaticProductCommandTarget::CommandRecord> _ProjectCommands;
    _ProjectCommands.emplace_back(
        iCAX::Project::kProjectGetStateName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleProjectGetStateCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _ProjectCommands.emplace_back(
        iCAX::Project::kProjectUndoName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleProjectUndoCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _ProjectCommands.emplace_back(
        iCAX::Project::kProjectRedoName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleProjectRedoCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _ProjectCommands.emplace_back(
        iCAX::Project::kProjectGetUndoRedoStateName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleProjectGetUndoRedoStateCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });

    auto _pProjectCommandTarget = std::make_shared<CStaticProductCommandTarget>(
        iCAX::Project::kProjectCommandMainName,
        std::move(_ProjectCommands));
    if (!m_pCommandRegistry->Register(_pProjectCommandTarget))
    {
        throw std::runtime_error("Built-in project command target is already registered: " + _pProjectCommandTarget->GetMainName());
    }
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleGetStateCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProductMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);
    return _MakeProductPayloadResponse(BuildProductStatePayload());
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleListProjectCatalogsCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProductMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);
    return _MakeProductPayloadResponse(BuildProductStatePayload());
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleOpenProjectCatalogCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProductMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);

    auto _pCatalog = OpenProjectCatalog(
        _GetOptionalString(_Payload, "catalogName"),
        _GetOptionalString(_Payload, "catalogPath"),
        _GetOptionalString(_Payload, "projectName"),
        _GetOptionalString(_Payload, "projectPath"));

    iCAX::Data::ObjectMap _Response;
    _Response["productId"] = m_Definition.ProductID;
    _Response["productChannelId"] = m_ProductChannelID;
    _Response["catalog"] = BuildProjectCatalogPayload(_pCatalog);
    _Response["state"] = BuildProductStatePayload();
    return _MakeProductPayloadResponse(iCAX::Data::Variant(_Response));
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleCloseProjectCatalogCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProductMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    (void)CloseProjectCatalog(_GetRequiredUUID(_Payload, "catalogId"));
    return _MakeProductPayloadResponse(BuildProductStatePayload());
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleProjectGetStateCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProjectMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);

    auto _Payload = _MakeProjectPayload(FindProjectRuntime(pProjectContext_->GetProjectID()));
    auto _Project = _Payload.Is<iCAX::Data::ObjectMap>()
        ? _Payload.To<iCAX::Data::ObjectMap>()
        : iCAX::Data::ObjectMap{};
    _Project["undoRedo"] = _MakeUndoRedoStatePayload(pSceneContext_->Database());
    return _MakeProductPayloadResponse(iCAX::Data::Variant(_Project));
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleProjectGetUndoRedoStateCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProjectMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);
    return _MakeProductPayloadResponse(_MakeUndoRedoStatePayload(pSceneContext_->Database()));
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleProjectUndoCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProjectMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);

    auto& _Repository = pSceneContext_->Database();
    const auto _Applied = _Repository.Undo();
    auto _Payload = _MakeUndoRedoStatePayload(_Repository).To<iCAX::Data::ObjectMap>();
    _Payload["applied"] = _Applied;
    return _MakeProductPayloadResponse(iCAX::Data::Variant(_Payload));
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleProjectRedoCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireProjectMailboxContext(pProductContext_, pProjectContext_, pSceneContext_);

    auto& _Repository = pSceneContext_->Database();
    const auto _Applied = _Repository.Redo();
    auto _Payload = _MakeUndoRedoStatePayload(_Repository).To<iCAX::Data::ObjectMap>();
    _Payload["applied"] = _Applied;
    return _MakeProductPayloadResponse(iCAX::Data::Variant(_Payload));
}

