#include "pch.h"
#include "ApplicationHost.h"
#include <filesystem>
#include <string>
#include <format>
#include <cstring>
#include <chrono>
#include <utility>
#include "ApplicationContext/FileApplicationConfigStore.h"
#include <stdexcept>

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
}


//! 构造函数
iCAX::ApplicationHost::CApplicationHost::CApplicationHost()
    : m_Config()
    , m_ApplicationMailID(iCAX::Data::GenerateNewUUID())
    , m_pCommandRegistry(std::make_shared<iCAX::Command::CCommandRegistry>())
    , m_pCommandDispatcher(std::make_unique<iCAX::Command::CCommandDispatcher>(m_pCommandRegistry))
    , m_CommandContext()
{
    //! 默认值
    m_Config.strApplicationSettingsPath = "Setting/Application.Setting";
    m_Config.strProductCatalogPath = "Products";
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
    Stop();
}

//! 设置配置信息
void iCAX::ApplicationHost::CApplicationHost::SetConfig(IN const ApplicationHostConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == EApplicationHostState::Starting
        || m_State == EApplicationHostState::Running
        || m_State == EApplicationHostState::Stopping)
    {
        throw std::logic_error("ApplicationHost config cannot be changed while host is running");
    }
    m_Config = Config_;
}

//! 启动后台工作线程
void iCAX::ApplicationHost::CApplicationHost::Start()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        if (m_WorkThread.joinable()
            && (m_State == EApplicationHostState::Stopped || m_State == EApplicationHostState::Faulted))
        {
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }

    std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == EApplicationHostState::Starting || m_State == EApplicationHostState::Running)
    {
        return;
    }
    if (m_State == EApplicationHostState::Stopping)
    {
        throw std::logic_error("ApplicationHost is stopping");
    }

    m_bStopRequested.store(false, std::memory_order_release);
    m_bStartupCompleted = false;
    m_StopReason = EApplicationHostStopReason::None;
    m_Phase = EApplicationHostPhase::Starting;
    m_LastFault.reset();
    m_pWorkerException = nullptr;
    m_State = EApplicationHostState::Starting;
    m_WorkThread = std::thread(&CApplicationHost::WorkerMain, this);

    m_StartupCondition.wait(_Lock, [this]() { return m_bStartupCompleted; });
    if (m_State == EApplicationHostState::Faulted && m_pWorkerException)
    {
        auto _Exception = m_pWorkerException;
        _Lock.unlock();
        Stop();
        std::rethrow_exception(_Exception);
    }
}

//! 停止后台工作线程
void iCAX::ApplicationHost::CApplicationHost::Stop()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_bStopRequested.store(true, std::memory_order_release);
        if ((m_State == EApplicationHostState::Starting || m_State == EApplicationHostState::Running)
            && m_WorkThread.joinable())
        {
            m_State = EApplicationHostState::Stopping;
            m_StopReason = EApplicationHostStopReason::Requested;
        }

        if (m_WorkThread.joinable())
        {
            if (m_WorkThread.get_id() == std::this_thread::get_id())
            {
                m_StartupCondition.notify_all();
                return;
            }
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }

    m_StartupCondition.notify_all();
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }
}

//! 是否正在运行
bool iCAX::ApplicationHost::CApplicationHost::IsRunning() const
{
    return GetState() == EApplicationHostState::Running;
}

//! 获取运行状态
iCAX::ApplicationHost::EApplicationHostState iCAX::ApplicationHost::CApplicationHost::GetState() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_State;
}

//! 获取当前运行阶段
iCAX::ApplicationHost::EApplicationHostPhase iCAX::ApplicationHost::CApplicationHost::GetPhase() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_Phase;
}

//! 获取当前停止原因
iCAX::ApplicationHost::EApplicationHostStopReason iCAX::ApplicationHost::CApplicationHost::GetStopReason() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_StopReason;
}

//! 获取最后一次故障
std::optional<iCAX::ApplicationHost::ApplicationHostFault> iCAX::ApplicationHost::CApplicationHost::GetLastFault() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_LastFault;
}

//! 订阅宿主事件
uint64_t iCAX::ApplicationHost::CApplicationHost::SubscribeEvent(IN ApplicationHostEventHandler Handler_)
{
    if (!Handler_)
    {
        throw std::invalid_argument("ApplicationHost event handler cannot be empty");
    }

    std::lock_guard<std::mutex> _Lock(m_EventMutex);
    const uint64_t _SubscriptionID = m_nNextEventSubscriptionID++;
    m_mapEventHandlers.emplace(_SubscriptionID, std::move(Handler_));
    return _SubscriptionID;
}

//! 取消订阅宿主事件
bool iCAX::ApplicationHost::CApplicationHost::UnsubscribeEvent(IN uint64_t nSubscriptionID_)
{
    std::lock_guard<std::mutex> _Lock(m_EventMutex);
    return m_mapEventHandlers.erase(nSubscriptionID_) > 0;
}

//! 工作线程入口
void iCAX::ApplicationHost::CApplicationHost::WorkerMain()
{
    NotifyEvent(EApplicationHostEventCode::Starting);

    try
    {
        SetPhase(EApplicationHostPhase::Loading);
        Load();

        bool _bShouldRun = !m_bStopRequested.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            m_State = _bShouldRun ? EApplicationHostState::Running : EApplicationHostState::Stopping;
            m_Phase = _bShouldRun ? EApplicationHostPhase::Running : EApplicationHostPhase::Stopping;
            if (!_bShouldRun)
            {
                m_StopReason = EApplicationHostStopReason::Requested;
            }
            m_bStartupCompleted = true;
        }
        m_StartupCondition.notify_all();

        if (_bShouldRun)
        {
            NotifyEvent(EApplicationHostEventCode::Started);
        }

        const uint32_t _nFrameIntervalMilliseconds = m_Config.nFrameIntervalMilliseconds == 0
            ? 1
            : m_Config.nFrameIntervalMilliseconds;
        const auto _FrameInterval = std::chrono::milliseconds(_nFrameIntervalMilliseconds);
        auto _NextFrameTime = std::chrono::steady_clock::now();

        while (!m_bStopRequested.load(std::memory_order_acquire))
        {
            SetPhase(EApplicationHostPhase::Running);
            MainLoop();
            _NextFrameTime += _FrameInterval;
            {
                std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
                m_StartupCondition.wait_until(
                    _Lock,
                    _NextFrameTime,
                    [this]() { return m_bStopRequested.load(std::memory_order_acquire); });
            }
            if (_NextFrameTime < std::chrono::steady_clock::now() - _FrameInterval)
            {
                _NextFrameTime = std::chrono::steady_clock::now();
            }
        }

        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            m_State = EApplicationHostState::Stopping;
            m_StopReason = EApplicationHostStopReason::Requested;
            m_Phase = EApplicationHostPhase::Stopping;
        }
        NotifyEvent(EApplicationHostEventCode::Stopping);

        SetPhase(EApplicationHostPhase::Unloading);
        Unload();

        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            m_State = EApplicationHostState::Stopped;
            m_Phase = EApplicationHostPhase::None;
        }
        NotifyEvent(EApplicationHostEventCode::Stopped);
    }
    catch (...)
    {
        auto _Exception = std::current_exception();
        EApplicationHostPhase _FaultPhase = EApplicationHostPhase::None;
        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            _FaultPhase = m_Phase;
        }
        const auto _StopReason = _FaultPhase == EApplicationHostPhase::Loading || _FaultPhase == EApplicationHostPhase::Starting
            ? EApplicationHostStopReason::StartFailed
            : EApplicationHostStopReason::Faulted;
        const auto _Message = std::format(
            "ApplicationHost faulted during phase {}: {}",
            static_cast<int>(_FaultPhase),
            GetExceptionMessage(_Exception));

        try
        {
            Unload();
        }
        catch (...)
        {
        }

        RecordFault(_FaultPhase, _Message, _Exception);
        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            m_StopReason = _StopReason;
            m_State = EApplicationHostState::Faulted;
            m_Phase = _FaultPhase;
            m_bStartupCompleted = true;
        }
        m_StartupCondition.notify_all();
        NotifyEvent(EApplicationHostEventCode::Faulted, _Message, _Exception);
    }
}

//! 发布宿主事件
void iCAX::ApplicationHost::CApplicationHost::NotifyEvent(
    IN EApplicationHostEventCode Code_,
    IN const std::string& strMessage_,
    IN std::exception_ptr pException_)
{
    std::vector<ApplicationHostEventHandler> _Handlers;
    {
        std::lock_guard<std::mutex> _Lock(m_EventMutex);
        _Handlers.reserve(m_mapEventHandlers.size());
        for (const auto& _Pair : m_mapEventHandlers)
        {
            _Handlers.push_back(_Pair.second);
        }
    }

    ApplicationHostEvent _Event;
    _Event.Code = Code_;
    _Event.strMessage = strMessage_;
    _Event.pException = pException_;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        _Event.State = m_State;
        _Event.StopReason = m_StopReason;
        _Event.Phase = m_Phase;
    }

    for (const auto& _Handler : _Handlers)
    {
        try
        {
            _Handler(_Event);
        }
        catch (...)
        {
        }
    }
}

//! 设置运行阶段
void iCAX::ApplicationHost::CApplicationHost::SetPhase(IN EApplicationHostPhase Phase_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_Phase = Phase_;
}

//! 记录故障
void iCAX::ApplicationHost::CApplicationHost::RecordFault(
    IN EApplicationHostPhase Phase_,
    IN const std::string& strMessage_,
    IN std::exception_ptr pException_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_LastFault = ApplicationHostFault{ Phase_, strMessage_, pException_ };
    m_pWorkerException = pException_;
}

//! 从异常中提取消息
std::string iCAX::ApplicationHost::CApplicationHost::GetExceptionMessage(IN std::exception_ptr pException_)
{
    if (!pException_)
    {
        return {};
    }

    try
    {
        std::rethrow_exception(pException_);
    }
    catch (const std::exception& _Error)
    {
        return _Error.what();
    }
    catch (...)
    {
        return "Unknown exception";
    }
}

//!< 加载
void iCAX::ApplicationHost::CApplicationHost::Load()
{
    m_pApplicationContext = CreateApplicationContext();
    m_pApplicationSetting = std::make_shared<iCAX::Data::PropertyBag>(m_pApplicationContext->GetSettings().GetProperties());
    m_pMailPostOfficeService = iCAX::Services::GetGlobalServiceProvider()->Resolve<iCAX::Services::IMailPostOfficeService>();

    m_pProjectManager = std::make_shared<iCAX::Project::CProjectManager>(m_pApplicationSetting);
    for (const auto& _Product : LoadProductDefinitions())
    {
        LoadProductModules(_Product);
        m_pProjectManager->Products().Set(_Product);
    }

    PrepareCommandContext();

    for (const auto& _StartupProject : m_Config.StartupProjects)
    {
        (void)OpenProject(_StartupProject.strProductID, _StartupProject.strProjectName, _StartupProject.strProjectPath);
    }
}

//!< 主线程循环
void iCAX::ApplicationHost::CApplicationHost::MainLoop()
{
    DispatchBackendMails();
}

//! 卸载
void iCAX::ApplicationHost::CApplicationHost::Unload()
{
    if (m_pProjectManager)
    {
        m_pProjectManager->CloseAll();
        m_pProjectManager.reset();
    }
    if (m_pMailPostOfficeService)
    {
        m_pMailPostOfficeService->ClearPostOffices();
        m_pMailPostOfficeService.reset();
    }
    iCAX::Services::GetGlobalServiceProvider()->UnloadAll();
    m_CommandContext.Clear();
    m_pApplicationSetting.reset();
    m_pApplicationContext.reset();
}

//! 准备命令上下文依赖
void iCAX::ApplicationHost::CApplicationHost::PrepareCommandContext()
{
    m_CommandContext.Clear();
    PopulateCommandContext(m_CommandContext, nullptr);
}

void iCAX::ApplicationHost::CApplicationHost::PopulateCommandContext(
    IN OUT iCAX::Command::CCommandContext& Context_,
    IN const std::shared_ptr<iCAX::Project::CProjectSession>& pProject_) const
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
    if (m_pProjectManager)
    {
        Context_.SetDependency<iCAX::Project::CProjectManager>(m_pProjectManager);
    }
    if (m_pMailPostOfficeService)
    {
        Context_.SetDependency<iCAX::Services::IMailPostOfficeService>(m_pMailPostOfficeService);
    }
    if (pProject_)
    {
        Context_.SetDependency<iCAX::Project::CProjectSession>(pProject_);
        Context_.SetDependency<iCAX::Database::IRepository>(
            std::shared_ptr<iCAX::Database::IRepository>(pProject_, &pProject_->Database()));
        Context_.SetDependency<iCAX::Behaviour::IUniverse>(
            std::shared_ptr<iCAX::Behaviour::IUniverse>(pProject_, &pProject_->Universe()));
        Context_.SetDependency<iCAX::Resource::CResourceLibrary>(
            std::shared_ptr<iCAX::Resource::CResourceLibrary>(pProject_, &pProject_->Resources()));
    }
}

//! 处理后台收到的邮件命令
void iCAX::ApplicationHost::CApplicationHost::DispatchBackendMails()
{
    if (!m_pMailPostOfficeService || !m_pCommandDispatcher)
    {
        return;
    }

    DispatchBackendMails(m_pMailPostOfficeService->GetBackendPostOffice(m_ApplicationMailID), nullptr);
}

void iCAX::ApplicationHost::CApplicationHost::DispatchBackendMails(
    IN const iCAX::Mail::CMailPostOffice& PostOffice_,
    IN const std::shared_ptr<iCAX::Project::CProjectSession>& pProject_)
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
            PopulateCommandContext(_Context, pProject_);
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

//! 分配后台发出的邮件ID
uint64_t iCAX::ApplicationHost::CApplicationHost::AllocateBackendMailID()
{
    return m_nNextBackendMailID.fetch_add(1, std::memory_order_relaxed);
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

std::vector<iCAX::Project::CProductDefinition> iCAX::ApplicationHost::CApplicationHost::LoadProductDefinitions() const
{
    iCAX::Project::CProductCatalog _Loader;
    std::vector<iCAX::Project::CProductDefinition> _Products;

    if (!m_Config.strProductCatalogPath.empty())
    {
        auto _Loaded = _Loader.LoadManifestDirectory(m_Config.strProductCatalogPath);
        _Products.insert(_Products.end(), _Loaded.begin(), _Loaded.end());
    }

    for (const auto& _Path : m_Config.ProductManifestPaths)
    {
        _Products.push_back(_Loader.LoadManifestFile(_Path));
    }

    return _Products;
}

void iCAX::ApplicationHost::CApplicationHost::LoadProductModules(IN const iCAX::Project::CProductDefinition& Product_) const
{
    auto _LoadModule = [](IN const std::string& strPath_, IN const std::string& strKind_) {
        if (strPath_.empty())
        {
            return;
        }

        HMODULE hMod = LoadLibraryA(strPath_.c_str());
        if (!hMod)
        {
            DWORD code = GetLastError();
            throw std::runtime_error(
                std::format(
                    "[Product Module Load Failure]\n"
                    "  Kind   : {}\n"
                    "  Path   : {}\n"
                    "  Error  : Win32 code {}\n",
                    strKind_, strPath_, code));
        }
    };

    for (const auto& _Path : Product_.ComponentModules)
    {
        _LoadModule(_Path, "component");
    }
    for (const auto& _Path : Product_.BehaviourModules)
    {
        _LoadModule(_Path, "behaviour");
    }
    for (const auto& _Path : Product_.ServiceModules)
    {
        _LoadModule(_Path, "service");
    }
    for (const auto& _Path : Product_.CommandModules)
    {
        _LoadModule(_Path, "command");
    }
    for (const auto& _Module : Product_.PluginModules)
    {
        _LoadModule(_Module.ComponentPath, "component");
        _LoadModule(_Module.BehaviourPath, "behaviour");
        _LoadModule(_Module.ServicePath, "service");
        _LoadModule(_Module.CommandPath, "command");
    }
}

iCAX::Project::CProjectManager& iCAX::ApplicationHost::CApplicationHost::GetProjectManager() const
{
    if (!m_pProjectManager)
    {
        throw std::logic_error("ProjectManager is not loaded");
    }
    return *m_pProjectManager;
}

std::shared_ptr<iCAX::Project::CProjectSession> iCAX::ApplicationHost::CApplicationHost::GetActiveProject() const
{
    if (!m_pProjectManager)
    {
        return nullptr;
    }
    return m_pProjectManager->GetActiveProject();
}

std::shared_ptr<iCAX::Project::CProjectSession> iCAX::ApplicationHost::CApplicationHost::OpenProject(
    IN const std::string& strProductID_,
    IN const std::string& strProjectName_,
    IN const std::string& strProjectPath_)
{
    auto& _ProjectManager = GetProjectManager();
    auto _pProject = _ProjectManager.OpenProject(strProductID_, strProjectName_, strProjectPath_);
    std::weak_ptr<iCAX::Project::CProjectSession> _WeakProject = _pProject;
    _pProject->SetFrameHandler(
        [this, _WeakProject](
            iCAX::Project::CProjectSession&,
            const iCAX::Mail::CMailPostOffice& BackendPostOffice_) {
            auto _pLockedProject = _WeakProject.lock();
            if (!_pLockedProject)
            {
                return;
            }
            DispatchBackendMails(BackendPostOffice_, _pLockedProject);
        });
    _pProject->Start();
    return _pProject;
}

bool iCAX::ApplicationHost::CApplicationHost::CloseProject(IN const iCAX::Data::uuid& ProjectID_)
{
    if (!m_pProjectManager)
    {
        return false;
    }

    return m_pProjectManager->CloseProject(ProjectID_);
}

iCAX::Mail::CMailPostOffice iCAX::ApplicationHost::CApplicationHost::GetProjectFrontendPostOffice(IN const iCAX::Data::uuid& ProjectID_) const
{
    if (!m_pProjectManager)
    {
        throw std::runtime_error("Project not found");
    }
    auto _pProject = m_pProjectManager->FindProject(ProjectID_);
    if (!_pProject)
    {
        throw std::runtime_error("Project not found");
    }
    return _pProject->GetFrontendPostOffice();
}

iCAX::Mail::CMailPostOffice iCAX::ApplicationHost::CApplicationHost::GetApplicationFrontendPostOffice() const
{
    if (!m_pMailPostOfficeService)
    {
        throw std::logic_error("MailPostOfficeService is not loaded");
    }
    return m_pMailPostOfficeService->GetFrontendPostOffice(m_ApplicationMailID);
}

const iCAX::Data::uuid& iCAX::ApplicationHost::CApplicationHost::GetApplicationMailID() const
{
    return m_ApplicationMailID;
}
