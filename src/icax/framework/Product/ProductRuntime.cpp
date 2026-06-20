#include "pch.h"
#include "ProductRuntime.h"

#include "CommandHandler/FunctionCommandHandler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    iCAX::Command::CCommandRequest _MailToCommandRequest(IN const iCAX::Mail::Mail& Mail_)
    {
        iCAX::Command::CCommandRequest _Request;
        _Request.nCommandID = Mail_.Header.nMailId;
        _Request.nOriginID = Mail_.Header.nOriginId;
        _Request.nTypeCode = Mail_.Header.nTypeCode;

        if (Mail_.Payload.nSize > 0)
        {
            if (Mail_.Payload.pData == nullptr)
            {
                throw std::invalid_argument("mail payload data is null");
            }
            _Request.Payload.assign(Mail_.Payload.pData, Mail_.Payload.pData + Mail_.Payload.nSize);
        }
        return _Request;
    }

    iCAX::Mail::StampCode _CommandStatusToMailStamp(IN iCAX::Command::ECommandStatusCode Status_)
    {
        switch (Status_)
        {
        case iCAX::Command::ECommandStatusCode::Ok:
            return iCAX::Mail::kMailOk;
        case iCAX::Command::ECommandStatusCode::NoHandler:
            return iCAX::Mail::kMailNoHandler;
        case iCAX::Command::ECommandStatusCode::InvalidRequest:
            return iCAX::Mail::kMailInvalidPayload;
        case iCAX::Command::ECommandStatusCode::ExecutionError:
        default:
            return iCAX::Mail::kMailExecutionError;
        }
    }

    iCAX::Mail::Mail _CommandResponseToMail(
        IN const iCAX::Mail::Mail& RequestMail_,
        IN const iCAX::Command::CCommandResponse& Response_,
        IN uint64_t nResponseMailID_)
    {
        iCAX::Mail::Mail _Mail;
        _Mail.Header.nMailId = nResponseMailID_;
        _Mail.Header.nOriginId = RequestMail_.Header.nMailId;
        _Mail.Header.nTypeCode = Response_.nTypeCode;
        _Mail.Header.nStamp = _CommandStatusToMailStamp(Response_.nStatus);

        const auto* _pPayload = &Response_.Payload;
        std::vector<uint8_t> _ErrorPayload;
        if (_pPayload->empty() && !Response_.strError.empty())
        {
            _ErrorPayload.assign(Response_.strError.begin(), Response_.strError.end());
            _pPayload = &_ErrorPayload;
        }

        if (!_pPayload->empty())
        {
            _Mail.Payload.nSize = _pPayload->size();
            _Mail.Payload.pData = new uint8_t[_Mail.Payload.nSize];
            std::memcpy(_Mail.Payload.pData, _pPayload->data(), _Mail.Payload.nSize);
        }
        return _Mail;
    }

    void _ReleaseMailPayload(IN OUT iCAX::Mail::Mail& Mail_) noexcept
    {
        delete[] Mail_.Payload.pData;
        Mail_.Payload.pData = nullptr;
        Mail_.Payload.nSize = 0;
    }

    std::string _ToProjectRoleText(IN iCAX::Project::EProjectRole Role_)
    {
        switch (Role_)
        {
        case iCAX::Project::EProjectRole::Main:
            return "Main";
        case iCAX::Project::EProjectRole::Transient:
            return "Transient";
        default:
            return "Unknown";
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
            return "Unknown";
        }
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
        _Project["projectMailId"] = pProjectRuntime_->GetProjectMailID();
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
        return iCAX::Data::Variant(_Project);
    }

    iCAX::Data::Variant _MakeProductFilePayload(IN const iCAX::Product::CProductFileDefinition& Definition_)
    {
        iCAX::Data::ObjectMap _File;
        _File["magic"] = Definition_.Magic;
        _File["formatVersion"] = Definition_.FormatVersion;
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

    std::string _GetProductDataRoot(IN const iCAX::Application::CApplicationContext& Context_)
    {
        std::filesystem::path _Root = Context_.GetPaths().UserConfigDirectory.empty()
            ? std::filesystem::path("Setting")
            : std::filesystem::path(Context_.GetPaths().UserConfigDirectory);
        return (_Root / "Products").string();
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

    void _RequireProductCommandContext(IN iCAX::Command::ICommandContext& Context_)
    {
        if (Context_.GetDependency<iCAX::Project::IProjectRuntime>()
            || Context_.GetDependency<iCAX::Project::CProject>())
        {
            throw std::invalid_argument("Product command must be sent to the product mailbox");
        }
        if (!Context_.GetDependency<iCAX::Product::CProductRuntime>())
        {
            throw std::invalid_argument("Product command context is missing ProductRuntime");
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

        char _Buffer[MAX_PATH]{};
        const auto _Length = GetFullPathNameA(strPath_.c_str(), MAX_PATH, _Buffer, nullptr);
        if (_Length == 0 || _Length >= MAX_PATH)
        {
            return strPath_;
        }
        return std::string(_Buffer, _Length);
    }

    std::string _GetLoadedModulePath(IN HMODULE hModule_)
    {
        if (!hModule_)
        {
            return {};
        }

        char _Buffer[MAX_PATH]{};
        const auto _Length = GetModuleFileNameA(hModule_, _Buffer, MAX_PATH);
        if (_Length == 0 || _Length >= MAX_PATH)
        {
            return {};
        }
        return _NormalizeModulePath(std::string(_Buffer, _Length));
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
}

iCAX::Product::CProductRuntime::CProductRuntime(
    IN const CProductDefinition& Definition_,
    IN std::shared_ptr<iCAX::Application::CApplicationContext> pApplicationContext_,
    IN std::shared_ptr<iCAX::Data::PropertyBag> pApplicationSettings_,
    IN std::shared_ptr<iCAX::Services::CServiceProvider> pApplicationServiceProvider_,
    IN std::shared_ptr<iCAX::Database::IMetaRegistry> pApplicationMetaRegistry_,
    IN std::shared_ptr<iCAX::Behaviour::IBehaviourRegistry> pApplicationBehaviourRegistry_,
    IN std::shared_ptr<iCAX::Resource::CResourceLoaderRegistry> pApplicationResourceLoaderRegistry_,
    IN std::shared_ptr<IProductDataStore> pProductDataStore_)
    : m_Definition(Definition_)
    , m_ProductData()
    , m_ProductMailID(iCAX::Data::GenerateNewUUID())
    , m_pApplicationContext(std::move(pApplicationContext_))
    , m_pApplicationSettings(pApplicationSettings_ ? std::move(pApplicationSettings_) : std::make_shared<iCAX::Data::PropertyBag>())
    , m_pApplicationServiceProvider(std::move(pApplicationServiceProvider_))
    , m_pMailChannelService(m_pApplicationServiceProvider ? m_pApplicationServiceProvider->Resolve<iCAX::Services::IMailChannelService>() : nullptr)
    , m_pApplicationMetaRegistry(std::move(pApplicationMetaRegistry_))
    , m_pApplicationBehaviourRegistry(std::move(pApplicationBehaviourRegistry_))
    , m_pApplicationResourceLoaderRegistry(std::move(pApplicationResourceLoaderRegistry_))
    , m_pProductDataStore(std::move(pProductDataStore_))
    , m_pCommandRegistry(std::make_shared<iCAX::Command::CCommandRegistry>())
    , m_pCommandDispatcher(std::make_unique<iCAX::Command::CCommandDispatcher>(m_pCommandRegistry))
{
    if (m_Definition.ProductID.empty())
    {
        throw std::invalid_argument("ProductID cannot be empty");
    }
    if (m_Definition.ProductName.empty())
    {
        m_Definition.ProductName = m_Definition.ProductID;
    }
    if (m_Definition.ProjectFile.Magic.empty())
    {
        throw std::invalid_argument("Product project file magic cannot be empty: " + m_Definition.ProductID);
    }
    if (!m_pApplicationContext)
    {
        throw std::invalid_argument("ApplicationContext cannot be null");
    }
    if (!m_pApplicationServiceProvider)
    {
        throw std::invalid_argument("Application ServiceProvider cannot be null");
    }
    if (!m_pMailChannelService)
    {
        throw std::invalid_argument("MailChannelService cannot be null");
    }
    if (!m_pApplicationMetaRegistry)
    {
        throw std::invalid_argument("Application MetaRegistry cannot be null");
    }
    if (!m_pApplicationBehaviourRegistry)
    {
        throw std::invalid_argument("Application BehaviourRegistry cannot be null");
    }
    if (!m_pApplicationResourceLoaderRegistry)
    {
        throw std::invalid_argument("Application ResourceLoaderRegistry cannot be null");
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
    iCAX::Database::CMetaRegistrationCatalog::ReplayByModulePaths(*m_pApplicationMetaRegistry, m_LoadedModulePaths);
    iCAX::Behaviour::CBehaviourRegistrationCatalog::ReplayByModulePaths(*m_pApplicationBehaviourRegistry, m_LoadedModulePaths);
    iCAX::Services::CServiceRegistrationCatalog::ReplayByModulePaths(*m_pApplicationServiceProvider, m_LoadedModulePaths);
    iCAX::Resource::CResourceLoaderRegistrationCatalog::ReplayByModulePaths(*m_pApplicationResourceLoaderRegistry, m_LoadedModulePaths);
    PrepareCommandContext();
    if (!m_pMailChannelService->CreateChannel(m_ProductMailID))
    {
        throw std::runtime_error("Product mail channel already exists");
    }
    m_bStarted = true;
}

void iCAX::Product::CProductRuntime::Stop()
{
    bool _bWasStarted = false;
    {
        std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
        _bWasStarted = m_bStarted;
        m_bStarted = false;
    }

    auto _Catalogs = SnapshotProjectCatalogs();
    auto _Runtimes = SnapshotProjectRuntimes();
    if (!_bWasStarted && _Catalogs.empty() && _Runtimes.empty())
    {
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
    if (m_pMailChannelService && !m_ProductMailID.is_nil())
    {
        (void)m_pMailChannelService->RemoveChannel(m_ProductMailID);
    }
    m_CommandContext.Clear();
}

bool iCAX::Product::CProductRuntime::IsStarted() const
{
    std::lock_guard<std::mutex> _Lock(m_RuntimeMutex);
    return m_bStarted;
}

const iCAX::Product::CProductDefinition& iCAX::Product::CProductRuntime::GetDefinition() const
{
    return m_Definition;
}

iCAX::Product::CProductData iCAX::Product::CProductRuntime::GetProductData() const
{
    return SnapshotProductData();
}

const std::string& iCAX::Product::CProductRuntime::GetProductID() const
{
    return m_Definition.ProductID;
}

const iCAX::Data::uuid& iCAX::Product::CProductRuntime::GetProductMailID() const
{
    return m_ProductMailID;
}

iCAX::Mail::CMailPostOffice iCAX::Product::CProductRuntime::GetProductFrontendPostOffice() const
{
    EnsureStarted();
    return m_pMailChannelService->GetFrontendPostOffice(m_ProductMailID);
}

void iCAX::Product::CProductRuntime::DispatchProductMails()
{
    if (!IsStarted() || !m_pCommandDispatcher)
    {
        return;
    }

    DispatchProjectMails(m_pMailChannelService->GetBackendPostOffice(m_ProductMailID), nullptr);
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
    _CatalogInfo.pApplicationSettings = m_pApplicationSettings;
    _CatalogInfo.pMetaRegistry = m_pApplicationMetaRegistry;
    _CatalogInfo.pBehaviourRegistry = m_pApplicationBehaviourRegistry;
    _CatalogInfo.pMailChannelService = m_pMailChannelService;
    _CatalogInfo.ResourceLoaderRegistryFactory = [pResourceLoaderRegistry = m_pApplicationResourceLoaderRegistry]() {
        return pResourceLoaderRegistry;
    };
    auto _pCatalog = std::make_shared<iCAX::Project::CProjectCatalog>(_CatalogInfo);
    auto _pProject = _pCatalog->OpenMainProject(
        _ProjectName,
        _ProjectPath,
        m_Definition.DefaultProjectStartupComponent);
    auto _pProjectRuntime = iCAX::Project::CreateLocalProjectRuntime(_pProject);

    bool _bCatalogRegistered = false;
    try
    {
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
    }
    catch (...)
    {
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

    RecordRecentProject(_ProjectPath, _ProjectName);
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
    _Root["productMailId"] = m_ProductMailID;
    _Root["isStarted"] = IsStarted();

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

iCAX::Command::ICommandContext& iCAX::Product::CProductRuntime::GetCommandContext()
{
    return m_CommandContext;
}

const iCAX::Command::ICommandContext& iCAX::Product::CProductRuntime::GetCommandContext() const
{
    return m_CommandContext;
}

void iCAX::Product::CProductRuntime::EnsureStarted() const
{
    if (!IsStarted())
    {
        throw std::logic_error("ProductRuntime is not started");
    }
}

void iCAX::Product::CProductRuntime::PrepareCommandContext()
{
    m_CommandContext.Clear();
    PopulateCommandContext(m_CommandContext, nullptr);
}

void iCAX::Product::CProductRuntime::PopulateCommandContext(
    IN OUT iCAX::Command::CCommandContext& Context_,
    IN const std::shared_ptr<iCAX::Project::IProjectRuntime>& pProjectRuntime_) const
{
    Context_.Clear();

    if (m_pCommandRegistry)
    {
        Context_.SetDependency<iCAX::Command::CCommandRegistry>(m_pCommandRegistry);
    }
    if (m_pApplicationContext)
    {
        Context_.SetDependency<iCAX::Application::CApplicationContext>(m_pApplicationContext);
        Context_.SetDependency<iCAX::Application::IApplicationContext>(m_pApplicationContext);
    }
    {
        auto _pProductRuntime = std::const_pointer_cast<CProductRuntime>(shared_from_this());
        Context_.SetDependency<CProductRuntime>(_pProductRuntime);
    }
    {
        auto _pCatalogs = std::make_shared<std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>>>(SnapshotProjectCatalogs());
        Context_.SetDependency<std::vector<std::shared_ptr<iCAX::Project::CProjectCatalog>>>(_pCatalogs);
    }
    if (m_pApplicationServiceProvider)
    {
        Context_.SetDependency<iCAX::Services::CServiceProvider>(m_pApplicationServiceProvider);
    }
    if (m_pMailChannelService)
    {
        Context_.SetDependency<iCAX::Services::IMailChannelService>(m_pMailChannelService);
    }
    if (m_pApplicationMetaRegistry)
    {
        Context_.SetDependency<iCAX::Database::IMetaRegistry>(m_pApplicationMetaRegistry);
    }
    if (m_pApplicationBehaviourRegistry)
    {
        Context_.SetDependency<iCAX::Behaviour::IBehaviourRegistry>(m_pApplicationBehaviourRegistry);
    }
    if (pProjectRuntime_)
    {
        Context_.SetDependency<iCAX::Project::IProjectRuntime>(pProjectRuntime_);

        auto _pCatalog = FindProjectCatalogByProjectID(pProjectRuntime_->GetProjectID());
        if (_pCatalog)
        {
            Context_.SetDependency<iCAX::Project::CProjectCatalog>(_pCatalog);
        }
        auto _pProject = pProjectRuntime_->GetLocalProject();
        if (!_pProject)
        {
            return;
        }
        Context_.SetDependency<iCAX::Project::CProject>(_pProject);
        Context_.SetDependency<iCAX::Database::IRepository>(
            std::shared_ptr<iCAX::Database::IRepository>(_pProject, &_pProject->Database()));
        Context_.SetDependency<iCAX::Behaviour::IUniverse>(
            std::shared_ptr<iCAX::Behaviour::IUniverse>(_pProject, &_pProject->Universe()));
        Context_.SetDependency<iCAX::Resource::CResourceLibrary>(
            std::shared_ptr<iCAX::Resource::CResourceLibrary>(_pProject, &_pProject->Resources()));
    }
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

    auto _Mails = PostOffice_.Receive();
    for (auto& _Mail : _Mails)
    {
        iCAX::Command::CCommandResponse _Response;
        _Response.nCommandID = _Mail.Header.nMailId;
        _Response.nOriginID = _Mail.Header.nOriginId;
        _Response.nTypeCode = _Mail.Header.nTypeCode;

        try
        {
            auto _Request = _MailToCommandRequest(_Mail);
            iCAX::Command::CCommandContext _Context;
            PopulateCommandContext(_Context, pProjectRuntime_);
            _Response = m_pCommandDispatcher->Dispatch(_Request, _Context);
        }
        catch (const std::invalid_argument& _Error)
        {
            _Response.nStatus = iCAX::Command::ECommandStatusCode::InvalidRequest;
            _Response.strError = _Error.what();
        }
        catch (const std::exception& _Error)
        {
            _Response.nStatus = iCAX::Command::ECommandStatusCode::ExecutionError;
            _Response.strError = _Error.what();
        }
        catch (...)
        {
            _Response.nStatus = iCAX::Command::ECommandStatusCode::ExecutionError;
            _Response.strError = "Unknown command dispatch error";
        }

        auto _ResponseMail = _CommandResponseToMail(_Mail, _Response, AllocateBackendMailID());
        PostOffice_.Send(_ResponseMail);
        _ReleaseMailPayload(_Mail);
    }
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

    try
    {
        SaveProductData(_ProductData);
    }
    catch (...)
    {
        // Recent project persistence must not turn a successful project open into a failure.
    }
}

void iCAX::Product::CProductRuntime::LoadProductModules()
{
    auto _LoadModule = [](IN const std::string& strPath_, IN const std::string& strKind_) -> std::string {
        if (strPath_.empty())
        {
            return {};
        }

        HMODULE hMod = LoadLibraryA(strPath_.c_str());
        if (!hMod)
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
        return _GetLoadedModulePath(hMod);
    };

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
    m_pCommandRegistry->Set(
        kProductGetStateCommand,
        std::make_shared<iCAX::Command::CFunctionCommandHandler>(
            [this](
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_) {
                return HandleGetStateCommand(Request_, Context_);
            }));

    m_pCommandRegistry->Set(
        kProductListProjectCatalogsCommand,
        std::make_shared<iCAX::Command::CFunctionCommandHandler>(
            [this](
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_) {
                return HandleListProjectCatalogsCommand(Request_, Context_);
            }));

    m_pCommandRegistry->Set(
        kProductOpenProjectCatalogCommand,
        std::make_shared<iCAX::Command::CFunctionCommandHandler>(
            [this](
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_) {
                return HandleOpenProjectCatalogCommand(Request_, Context_);
            }));

    m_pCommandRegistry->Set(
        kProductCloseProjectCatalogCommand,
        std::make_shared<iCAX::Command::CFunctionCommandHandler>(
            [this](
                IN const iCAX::Command::CCommandRequest& Request_,
                IN iCAX::Command::ICommandContext& Context_) {
                return HandleCloseProjectCatalogCommand(Request_, Context_);
            }));
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleGetStateCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Command::ICommandContext& Context_)
{
    _RequireProductCommandContext(Context_);
    return _MakeProductPayloadResponse(BuildProductStatePayload());
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleListProjectCatalogsCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Command::ICommandContext& Context_)
{
    _RequireProductCommandContext(Context_);
    return _MakeProductPayloadResponse(BuildProductStatePayload());
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleOpenProjectCatalogCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Command::ICommandContext& Context_)
{
    _RequireProductCommandContext(Context_);
    auto _Payload = _DecodeObjectPayload(Request_);

    auto _pCatalog = OpenProjectCatalog(
        _GetOptionalString(_Payload, "catalogName"),
        _GetOptionalString(_Payload, "catalogPath"),
        _GetOptionalString(_Payload, "projectName"),
        _GetOptionalString(_Payload, "projectPath"));

    iCAX::Data::ObjectMap _Response;
    _Response["productId"] = m_Definition.ProductID;
    _Response["productMailId"] = m_ProductMailID;
    _Response["catalog"] = BuildProjectCatalogPayload(_pCatalog);
    _Response["state"] = BuildProductStatePayload();
    return _MakeProductPayloadResponse(iCAX::Data::Variant(_Response));
}

iCAX::Command::CCommandResponse iCAX::Product::CProductRuntime::HandleCloseProjectCatalogCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Command::ICommandContext& Context_)
{
    _RequireProductCommandContext(Context_);
    auto _Payload = _DecodeObjectPayload(Request_);
    (void)CloseProjectCatalog(_GetRequiredUUID(_Payload, "catalogId"));
    return _MakeProductPayloadResponse(BuildProductStatePayload());
}
