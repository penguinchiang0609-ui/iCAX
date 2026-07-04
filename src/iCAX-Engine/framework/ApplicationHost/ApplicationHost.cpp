#include "pch.h"
#include "ApplicationHost.h"

#include "ApplicationContext/FileApplicationConfigStore.h"
#include "CommandHandler/CommandTarget.h"
#include "Mailbox/MailChannelRegistry.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    class CStaticApplicationCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        using CommandRecord = std::pair<std::string, SubCommandFunc>;

        CStaticApplicationCommandTarget(
            IN std::string strMainName_,
            IN std::vector<CommandRecord> Commands_)
            : CCommandTarget(std::move(strMainName_))
        {
            for (auto& _Command : Commands_)
            {
                auto _SubName = _Command.first;
                if (!Bind(std::move(_Command.first), std::move(_Command.second)))
                {
                    throw std::runtime_error("Built-in application command is already bound: " + _SubName);
                }
            }
        }
    };

    std::string _ToApplicationHostStateText(IN iCAX::ApplicationHost::EApplicationHostState State_)
    {
        switch (State_)
        {
        case iCAX::ApplicationHost::EApplicationHostState::Stopped:
            return "Stopped";
        case iCAX::ApplicationHost::EApplicationHostState::Starting:
            return "Starting";
        case iCAX::ApplicationHost::EApplicationHostState::Running:
            return "Running";
        case iCAX::ApplicationHost::EApplicationHostState::Stopping:
            return "Stopping";
        case iCAX::ApplicationHost::EApplicationHostState::Faulted:
            return "Faulted";
        default:
            throw std::invalid_argument("Invalid ApplicationHost state");
        }
    }

    std::string _ToApplicationHostPhaseText(IN iCAX::ApplicationHost::EApplicationHostPhase Phase_)
    {
        switch (Phase_)
        {
        case iCAX::ApplicationHost::EApplicationHostPhase::None:
            return "None";
        case iCAX::ApplicationHost::EApplicationHostPhase::Starting:
            return "Starting";
        case iCAX::ApplicationHost::EApplicationHostPhase::Loading:
            return "Loading";
        case iCAX::ApplicationHost::EApplicationHostPhase::Running:
            return "Running";
        case iCAX::ApplicationHost::EApplicationHostPhase::Stopping:
            return "Stopping";
        case iCAX::ApplicationHost::EApplicationHostPhase::Unloading:
            return "Unloading";
        default:
            throw std::invalid_argument("Invalid ApplicationHost phase");
        }
    }

    iCAX::Data::ObjectMap _DecodeObjectPayload(IN const iCAX::Command::CCommandRequest& Request_)
    {
        auto _Payload = iCAX::ApplicationHost::DecodeApplicationHostPayload(Request_.Payload);
        if (!_Payload.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument("ApplicationHost command payload must be an object");
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
            throw std::invalid_argument("ApplicationHost payload field must be a string: " + strName_);
        }
        return _Iter->second.To<std::string>();
    }

    std::string _GetRequiredString(
        IN const iCAX::Data::ObjectMap& Payload_,
        IN const std::string& strName_)
    {
        auto _Value = _GetOptionalString(Payload_, strName_);
        if (_Value.empty())
        {
            throw std::invalid_argument("ApplicationHost payload field is required: " + strName_);
        }
        return _Value;
    }

    std::string _GetDefaultProjectName(IN const std::string& strProjectPath_)
    {
        auto _Name = std::filesystem::path(strProjectPath_).stem().string();
        return _Name.empty() ? std::string("Project") : _Name;
    }

    void _RequireExistingProjectFile(IN const std::string& strProjectPath_)
    {
        if (strProjectPath_.empty())
        {
            throw std::invalid_argument("Project path cannot be empty");
        }
        if (!std::filesystem::exists(strProjectPath_))
        {
            throw std::invalid_argument("Project file does not exist: " + strProjectPath_);
        }
        if (!std::filesystem::is_regular_file(strProjectPath_))
        {
            throw std::invalid_argument("Project path is not a file: " + strProjectPath_);
        }
    }

    std::string _ToProductFileResolveStatusText(IN iCAX::ApplicationHost::EProductFileResolveStatus Status_)
    {
        switch (Status_)
        {
        case iCAX::ApplicationHost::EProductFileResolveStatus::Resolved:
            return "Resolved";
        case iCAX::ApplicationHost::EProductFileResolveStatus::NotFound:
        default:
            return "NotFound";
        }
    }

    iCAX::Data::VariantArray _MakeStringArray(IN const std::vector<std::string>& Values_)
    {
        iCAX::Data::VariantArray _Array;
        _Array.reserve(Values_.size());
        for (const auto& _Value : Values_)
        {
            _Array.emplace_back(_Value);
        }
        return _Array;
    }

    void _ValidateProductDefinitions(IN const std::vector<iCAX::Product::CProductDefinition>& Products_)
    {
        std::set<std::string> _ProductIDs;
        std::set<std::string> _ProjectFileMagics;
        for (const auto& _Product : Products_)
        {
            if (_Product.ProductID.empty())
            {
                throw std::invalid_argument("ProductID cannot be empty");
            }
            if (!iCAX::Product::IsValidProductID(_Product.ProductID))
            {
                throw std::invalid_argument("ProductID contains unsafe characters: " + _Product.ProductID);
            }
            if (!_ProductIDs.insert(_Product.ProductID).second)
            {
                throw std::invalid_argument("ProductID must be unique: " + _Product.ProductID);
            }
            if (_Product.ProjectFile.Magic.empty())
            {
                throw std::invalid_argument("Product project file magic cannot be empty: " + _Product.ProductID);
            }
            if (_Product.ProjectFile.QuickSaveLogVersion == 0)
            {
                throw std::invalid_argument("Product quick save log version cannot be zero: " + _Product.ProductID);
            }
            if (!_ProjectFileMagics.insert(_Product.ProjectFile.Magic).second)
            {
                throw std::invalid_argument("Product project file magic must be unique: " + _Product.ProjectFile.Magic);
            }
        }
    }

    void _RequireApplicationMailboxContext(
        IN const iCAX::Product::IProductContext* pProductContext_,
        IN const iCAX::Project::IProjectContext* pProjectContext_)
    {
        if (pProductContext_ || pProjectContext_)
        {
            throw std::invalid_argument("ApplicationHost command must be sent to the application mailbox");
        }
    }

    iCAX::Command::CCommandResponse _MakeApplicationPayloadResponse(IN const iCAX::Data::Variant& Payload_)
    {
        iCAX::Command::CCommandResponse _Response;
        _Response.nStatus = iCAX::Command::ECommandStatusCode::Ok;
        _Response.Payload = iCAX::ApplicationHost::EncodeApplicationHostPayload(Payload_);
        return _Response;
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

    iCAX::Data::VariantArray _MakeRecentProjectsPayload(IN const iCAX::Product::CProductData& ProductData_)
    {
        iCAX::Data::VariantArray _RecentProjects;
        _RecentProjects.reserve(ProductData_.RecentProjects.size());
        for (const auto& _Item : ProductData_.RecentProjects)
        {
            iCAX::Data::ObjectMap _RecentProject;
            _RecentProject["path"] = _Item.ProjectPath;
            _RecentProject["displayName"] = _Item.DisplayName;
            _RecentProject["lastOpenedTime"] = _Item.LastOpenedTime;
            _RecentProjects.emplace_back(_RecentProject);
        }
        return _RecentProjects;
    }
}

iCAX::ApplicationHost::CApplicationHost::CApplicationHost()
    : m_Config()
    , m_ApplicationChannelID(iCAX::Data::GenerateNewUUID())
    , m_pCommandRegistry(std::make_shared<iCAX::Command::CCommandRegistry>())
    , m_pCommandDispatcher(std::make_unique<iCAX::Command::CCommandDispatcher>(m_pCommandRegistry))
    , m_pMailCommandHandler(std::make_unique<iCAX::MailHandler::CMailCommandHandler>())
{
    m_Config.strApplicationSettingsPath = "Setting/Application.Setting";
    m_Config.Descriptor.AppID = "icax";
    m_Config.Descriptor.AppName = "iCAX";
    m_Config.Paths.InstallDirectory = std::filesystem::current_path().string();
    m_Config.Paths.UserConfigDirectory = "Setting";
    m_Config.Paths.CacheDirectory = "Cache";
    m_Config.Paths.TempDirectory = "Temp";
    m_Config.Paths.LogDirectory = "Log";

    iCAX::Product::CProductDefinition _DefaultProduct;
    _DefaultProduct.ProductID = "icax.default";
    _DefaultProduct.ProductName = "iCAX Default Product";
    _DefaultProduct.ProductVersion = "1.0";
    _DefaultProduct.ProjectFile.Magic = "ICAX_DEFAULT";
    _DefaultProduct.ProjectFile.FormatVersion = "1.0";
    _DefaultProduct.ProjectFile.FileExtensions.push_back(".icax");
    m_Config.Products.push_back(_DefaultProduct);
    _ValidateProductDefinitions(m_Config.Products);

    RegisterBuiltInApplicationCommands();
}

iCAX::ApplicationHost::CApplicationHost::~CApplicationHost()
{
    Stop();
}

void iCAX::ApplicationHost::CApplicationHost::SetConfig(IN const ApplicationHostConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == EApplicationHostState::Starting
        || m_State == EApplicationHostState::Running
        || m_State == EApplicationHostState::Stopping)
    {
        throw std::logic_error("ApplicationHost config cannot be changed while host is running");
    }
    _ValidateProductDefinitions(Config_.Products);
    m_Config = Config_;
}

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

    // Start 对外表现为同步启动：等待工作线程完成 Load，避免前端拿到尚未创建的应用邮箱。
    m_StartupCondition.wait(_Lock, [this]() { return m_bStartupCompleted; });
    if (m_State == EApplicationHostState::Faulted && m_pWorkerException)
    {
        auto _Exception = m_pWorkerException;
        _Lock.unlock();
        Stop();
        std::rethrow_exception(_Exception);
    }
}

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
                // 防止工作线程内部触发 Stop 后自 join 死锁，只设置停止标记并唤醒等待者。
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

bool iCAX::ApplicationHost::CApplicationHost::IsRunning() const
{
    return GetState() == EApplicationHostState::Running;
}

iCAX::ApplicationHost::EApplicationHostState iCAX::ApplicationHost::CApplicationHost::GetState() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_State;
}

iCAX::ApplicationHost::EApplicationHostPhase iCAX::ApplicationHost::CApplicationHost::GetPhase() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_Phase;
}

iCAX::ApplicationHost::EApplicationHostStopReason iCAX::ApplicationHost::CApplicationHost::GetStopReason() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_StopReason;
}

std::optional<iCAX::ApplicationHost::ApplicationHostFault> iCAX::ApplicationHost::CApplicationHost::GetLastFault() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_LastFault;
}

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

bool iCAX::ApplicationHost::CApplicationHost::UnsubscribeEvent(IN uint64_t nSubscriptionID_)
{
    std::lock_guard<std::mutex> _Lock(m_EventMutex);
    return m_mapEventHandlers.erase(nSubscriptionID_) > 0;
}

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
            // 宿主线程只处理应用级邮箱；产品邮箱由 ProductRuntime 线程处理，项目邮箱由各 Project 工作线程处理。
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
                // 宿主主循环不追赶积压帧，避免长耗时命令导致空转补帧。
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
            m_pWorkerException = _Exception;
        }
        m_StartupCondition.notify_all();
        NotifyEvent(EApplicationHostEventCode::Faulted, _Message, _Exception);
    }
}

void iCAX::ApplicationHost::CApplicationHost::Load()
{
    m_pApplicationServiceProvider = std::make_shared<iCAX::Services::CServiceProvider>();
    m_pMailChannelRegistry = std::make_shared<iCAX::Mail::CMailChannelRegistry>();
    if (!m_pMailChannelRegistry->CreateChannel(m_ApplicationChannelID))
    {
        throw std::runtime_error("Application mail channel already exists");
    }
    m_pApplicationContext = CreateApplicationContext();
    {
        std::weak_ptr<iCAX::Mail::CMailChannelRegistry> _Registry = m_pMailChannelRegistry;
        const auto _ApplicationChannelID = m_ApplicationChannelID;
        m_pApplicationContext->BindRuntimeCapabilities(
            m_ApplicationChannelID,
            m_pApplicationServiceProvider,
            [_Registry, _ApplicationChannelID]() {
                auto _pRegistry = _Registry.lock();
                if (!_pRegistry)
                {
                    throw std::logic_error("Application mail channel registry is not available");
                }
                return _pRegistry->GetBackendPostOffice(_ApplicationChannelID);
            },
            [_Registry, _ApplicationChannelID]() {
                auto _pRegistry = _Registry.lock();
                if (!_pRegistry)
                {
                    throw std::logic_error("Application mail channel registry is not available");
                }
                return _pRegistry->GetFrontendPostOffice(_ApplicationChannelID);
            });
    }

    if (m_Config.StartupProductID)
    {
        (void)StartProduct(*m_Config.StartupProductID);
    }
}

void iCAX::ApplicationHost::CApplicationHost::MainLoop()
{
    DispatchApplicationMails();
}

void iCAX::ApplicationHost::CApplicationHost::Unload()
{
    std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> _Runtimes;
    {
        std::unique_lock<std::mutex> _Lock(m_ProductRuntimeMutex);
        // 如果其他线程正在启动或停止产品，等待其退出生命周期临界区后再卸载，避免 runtime 半创建半销毁。
        m_ProductRuntimeCondition.wait(_Lock, [this]() {
            return m_StartingProductIDs.empty() && m_StoppingProductIDs.empty();
        });
        _Runtimes.reserve(m_ProductRuntimes.size());
        for (const auto& [_ProductID, _pRuntime] : m_ProductRuntimes)
        {
            _Runtimes.push_back(_pRuntime);
        }
        m_ProductRuntimes.clear();
    }
    for (auto& _pRuntime : _Runtimes)
    {
        if (_pRuntime)
        {
            _pRuntime->Stop();
        }
    }

    if (m_pMailChannelRegistry && !m_ApplicationChannelID.is_nil())
    {
        (void)m_pMailChannelRegistry->RemoveChannel(m_ApplicationChannelID);
    }
    if (m_pApplicationServiceProvider)
    {
        m_pApplicationServiceProvider->UnloadAll();
        m_pApplicationServiceProvider.reset();
    }
    m_pMailChannelRegistry.reset();
    m_pApplicationContext.reset();
}

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

    for (auto& _Handler : _Handlers)
    {
        if (_Handler)
        {
            _Handler(_Event);
        }
    }
}

void iCAX::ApplicationHost::CApplicationHost::SetPhase(IN EApplicationHostPhase Phase_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_Phase = Phase_;
}

void iCAX::ApplicationHost::CApplicationHost::RecordFault(
    IN EApplicationHostPhase Phase_,
    IN const std::string& strMessage_,
    IN std::exception_ptr pException_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_LastFault = ApplicationHostFault{ Phase_, strMessage_, pException_ };
}

std::string iCAX::ApplicationHost::CApplicationHost::GetExceptionMessage(IN std::exception_ptr pException_)
{
    if (!pException_)
    {
        return "ApplicationHost exception pointer is empty";
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
        return "ApplicationHost caught a non-standard exception";
    }
}

void iCAX::ApplicationHost::CApplicationHost::DispatchApplicationMails()
{
    if (!m_pCommandDispatcher)
    {
        return;
    }

    if (!m_pMailChannelRegistry || !m_pMailChannelRegistry->HasChannel(m_ApplicationChannelID))
    {
        return;
    }

    auto _PostOffice = m_pMailChannelRegistry->GetBackendPostOffice(m_ApplicationChannelID);
    if (!_PostOffice.IsValid())
    {
        return;
    }

    m_pMailCommandHandler->DispatchAvailableMails(
        _PostOffice,
        *m_pCommandDispatcher,
        *m_pApplicationContext,
        nullptr,
        nullptr,
        [this]() { return AllocateBackendMailID(); });
}

uint64_t iCAX::ApplicationHost::CApplicationHost::AllocateBackendMailID()
{
    return m_nNextBackendMailID.fetch_add(1, std::memory_order_relaxed);
}

iCAX::Data::PropertyBag iCAX::ApplicationHost::CApplicationHost::LoadApplicationSettings() const
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

void iCAX::ApplicationHost::CApplicationHost::RegisterBuiltInApplicationCommands()
{
    std::vector<CStaticApplicationCommandTarget::CommandRecord> _Commands;
    _Commands.emplace_back(
        kAppGetStateName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_) {
            return HandleGetStateCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_);
        });
    _Commands.emplace_back(
        kAppListProductsName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_) {
            return HandleListProductsCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_);
        });
    _Commands.emplace_back(
        kAppStartProductName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_) {
            return HandleStartProductCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_);
        });
    _Commands.emplace_back(
        kAppStopProductName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_) {
            return HandleStopProductCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_);
        });
    _Commands.emplace_back(
        kAppResolveProjectFileName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_) {
            return HandleResolveProjectFileCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_);
        });
    _Commands.emplace_back(
        kAppOpenProjectFileName,
        [this](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_) {
            return HandleOpenProjectFileCommand(Request_, ApplicationContext_, pProductContext_, pProjectContext_);
        });

    auto _pCommandTarget = std::make_shared<CStaticApplicationCommandTarget>(kAppCommandMainName, std::move(_Commands));
    if (!m_pCommandRegistry->Register(_pCommandTarget))
    {
        throw std::runtime_error("Built-in application command target is already registered: " + _pCommandTarget->GetMainName());
    }
}

iCAX::Command::CCommandResponse iCAX::ApplicationHost::CApplicationHost::HandleGetStateCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_)
{
    _RequireApplicationMailboxContext(pProductContext_, pProjectContext_);
    return _MakeApplicationPayloadResponse(BuildApplicationStatePayload());
}

iCAX::Command::CCommandResponse iCAX::ApplicationHost::CApplicationHost::HandleListProductsCommand(
    IN const iCAX::Command::CCommandRequest&,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_)
{
    _RequireApplicationMailboxContext(pProductContext_, pProjectContext_);
    return _MakeApplicationPayloadResponse(BuildApplicationStatePayload());
}

iCAX::Command::CCommandResponse iCAX::ApplicationHost::CApplicationHost::HandleStartProductCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_)
{
    _RequireApplicationMailboxContext(pProductContext_, pProjectContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _pRuntime = StartProduct(_GetOptionalString(_Payload, "productId"));

    iCAX::Data::ObjectMap _Response;
    _Response["applicationChannelId"] = m_ApplicationChannelID;
    _Response["product"] = BuildProductPayload(_pRuntime->GetDefinition(), _pRuntime);
    _Response["state"] = BuildApplicationStatePayload();
    return _MakeApplicationPayloadResponse(iCAX::Data::Variant(_Response));
}

iCAX::Command::CCommandResponse iCAX::ApplicationHost::CApplicationHost::HandleStopProductCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_)
{
    _RequireApplicationMailboxContext(pProductContext_, pProjectContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _ProductID = _GetOptionalString(_Payload, "productId");
    if (_ProductID.empty())
    {
        throw std::invalid_argument("ApplicationHost payload field is required: productId");
    }
    (void)StopProduct(_ProductID);
    return _MakeApplicationPayloadResponse(BuildApplicationStatePayload());
}

iCAX::Command::CCommandResponse iCAX::ApplicationHost::CApplicationHost::HandleResolveProjectFileCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_)
{
    _RequireApplicationMailboxContext(pProductContext_, pProjectContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _ResolveResult = ResolveProjectFileProduct(_GetRequiredString(_Payload, "projectPath"));

    iCAX::Data::ObjectMap _Response;
    _Response["applicationChannelId"] = m_ApplicationChannelID;
    _Response["resolve"] = BuildProductFileResolvePayload(_ResolveResult);
    return _MakeApplicationPayloadResponse(iCAX::Data::Variant(_Response));
}

iCAX::Command::CCommandResponse iCAX::ApplicationHost::CApplicationHost::HandleOpenProjectFileCommand(
    IN const iCAX::Command::CCommandRequest& Request_,
    IN iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_)
{
    _RequireApplicationMailboxContext(pProductContext_, pProjectContext_);
    auto _Payload = _DecodeObjectPayload(Request_);

    const auto _ProjectPath = _GetRequiredString(_Payload, "projectPath");
    _RequireExistingProjectFile(_ProjectPath);
    auto _ProjectName = _GetOptionalString(_Payload, "projectName");
    if (_ProjectName.empty())
    {
        _ProjectName = _GetDefaultProjectName(_ProjectPath);
    }

    auto _CatalogName = _GetOptionalString(_Payload, "catalogName");
    if (_CatalogName.empty())
    {
        _CatalogName = _ProjectName;
    }

    auto _ResolveResult = ResolveProjectFileProduct(_ProjectPath);
    if (_ResolveResult.Status != EProductFileResolveStatus::Resolved)
    {
        throw std::invalid_argument("Project file product is not resolved: " + _ProjectPath);
    }

    auto _pRuntime = StartProduct(_ResolveResult.ProductID);
    auto _pCatalog = _pRuntime->OpenProjectCatalog(
        _CatalogName,
        _ProjectPath,
        _ProjectName,
        _ProjectPath);

    iCAX::Data::ObjectMap _Response;
    _Response["applicationChannelId"] = m_ApplicationChannelID;
    _Response["resolve"] = BuildProductFileResolvePayload(_ResolveResult);
    _Response["product"] = BuildProductPayload(_pRuntime->GetDefinition(), _pRuntime);
    _Response["catalog"] = _pRuntime->BuildProjectCatalogPayload(_pCatalog);
    _Response["state"] = BuildApplicationStatePayload();
    return _MakeApplicationPayloadResponse(iCAX::Data::Variant(_Response));
}

iCAX::Data::Variant iCAX::ApplicationHost::CApplicationHost::BuildApplicationStatePayload() const
{
    iCAX::Data::ObjectMap _Root;
    _Root["applicationChannelId"] = m_ApplicationChannelID;
    _Root["state"] = _ToApplicationHostStateText(GetState());
    _Root["phase"] = _ToApplicationHostPhaseText(GetPhase());

    iCAX::Data::VariantArray _Products;
    _Products.reserve(m_Config.Products.size());
    for (const auto& _Definition : m_Config.Products)
    {
        _Products.emplace_back(BuildProductPayload(_Definition, FindProductRuntime(_Definition.ProductID)));
    }
    _Root["productCount"] = static_cast<unsigned long long>(_Products.size());
    _Root["products"] = _Products;

    auto _Runtimes = SnapshotProductRuntimes();
    _Root["runningProductCount"] = static_cast<unsigned long long>(_Runtimes.size());

    auto _Fault = GetLastFault();
    _Root["faultMessage"] = _Fault ? _Fault->strMessage : std::string();
    return iCAX::Data::Variant(_Root);
}

iCAX::Data::Variant iCAX::ApplicationHost::CApplicationHost::BuildProductFileResolvePayload(
    IN const iCAX::ApplicationHost::CProductFileResolveResult& Result_) const
{
    iCAX::Data::ObjectMap _Resolve;
    _Resolve["projectPath"] = Result_.ProjectPath;
    _Resolve["status"] = _ToProductFileResolveStatusText(Result_.Status);
    _Resolve["productId"] = Result_.ProductID;
    _Resolve["candidateProductIds"] = _MakeStringArray(Result_.CandidateProductIDs);
    _Resolve["matchedByMagic"] = Result_.bMatchedByMagic;
    return iCAX::Data::Variant(_Resolve);
}

iCAX::Data::Variant iCAX::ApplicationHost::CApplicationHost::BuildProductPayload(
    IN const iCAX::Product::CProductDefinition& Definition_,
    IN const std::shared_ptr<iCAX::Product::CProductRuntime>& pRuntime_) const
{
    iCAX::Data::ObjectMap _Product;
    _Product["productId"] = Definition_.ProductID;
    _Product["productName"] = Definition_.ProductName;
    _Product["productVersion"] = Definition_.ProductVersion;
    _Product["frontendEntry"] = Definition_.FrontendEntry;
    _Product["defaultProjectStartupComponent"] = Definition_.DefaultProjectStartupComponent;
    _Product["projectFile"] = _MakeProductFilePayload(Definition_.ProjectFile);
    _Product["isStarted"] = pRuntime_ && pRuntime_->IsStarted();
    _Product["productChannelId"] = pRuntime_ ? pRuntime_->GetProductChannelID() : iCAX::Data::GenerateNilUUID();
    _Product["recentProjects"] = pRuntime_
        ? _MakeRecentProjectsPayload(pRuntime_->GetProductData())
        : iCAX::Data::VariantArray();

    if (pRuntime_)
    {
        _Product["runtime"] = pRuntime_->BuildProductStatePayload();
    }
    return iCAX::Data::Variant(_Product);
}

iCAX::Product::CProductDefinition iCAX::ApplicationHost::CApplicationHost::FindProductDefinition(
    IN const std::string& strProductID_) const
{
    std::string _ProductID = strProductID_;
    if (_ProductID.empty())
    {
        if (m_Config.Products.size() != 1)
        {
            throw std::invalid_argument("productId is required when ApplicationHost has zero or multiple products");
        }
        _ProductID = m_Config.Products.front().ProductID;
    }

    for (const auto& _Definition : m_Config.Products)
    {
        if (_Definition.ProductID == _ProductID)
        {
            return _Definition;
        }
    }
    throw std::runtime_error("Product definition not found: " + _ProductID);
}

std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> iCAX::ApplicationHost::CApplicationHost::SnapshotProductRuntimes() const
{
    std::lock_guard<std::mutex> _Lock(m_ProductRuntimeMutex);
    std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> _Runtimes;
    _Runtimes.reserve(m_ProductRuntimes.size());
    for (const auto& [_ProductID, _pRuntime] : m_ProductRuntimes)
    {
        _Runtimes.push_back(_pRuntime);
    }
    return _Runtimes;
}

std::vector<iCAX::Product::CProductDefinition> iCAX::ApplicationHost::CApplicationHost::GetProductDefinitions() const
{
    return m_Config.Products;
}

std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> iCAX::ApplicationHost::CApplicationHost::GetProductRuntimes() const
{
    return SnapshotProductRuntimes();
}

std::shared_ptr<iCAX::Product::CProductRuntime> iCAX::ApplicationHost::CApplicationHost::FindProductRuntime(
    IN const std::string& strProductID_) const
{
    std::lock_guard<std::mutex> _Lock(m_ProductRuntimeMutex);
    auto _Iter = m_ProductRuntimes.find(strProductID_);
    if (_Iter == m_ProductRuntimes.end())
    {
        return nullptr;
    }
    return _Iter->second;
}

std::shared_ptr<iCAX::Project::CProjectCatalog> iCAX::ApplicationHost::CApplicationHost::OpenProjectFile(
    IN const std::string& strProjectPath_,
    IN const std::string& strCatalogName_,
    IN const std::string& strProjectName_)
{
    _RequireExistingProjectFile(strProjectPath_);

    auto _ResolveResult = ResolveProjectFileProduct(strProjectPath_);
    if (_ResolveResult.Status != EProductFileResolveStatus::Resolved)
    {
        throw std::invalid_argument("Project file product is not resolved: " + strProjectPath_);
    }

    auto _ProjectName = strProjectName_.empty()
        ? _GetDefaultProjectName(strProjectPath_)
        : strProjectName_;
    auto _CatalogName = strCatalogName_.empty()
        ? _ProjectName
        : strCatalogName_;

    auto _pRuntime = StartProduct(_ResolveResult.ProductID);
    return _pRuntime->OpenProjectCatalog(
        _CatalogName,
        strProjectPath_,
        _ProjectName,
        strProjectPath_);
}

iCAX::ApplicationHost::CProductFileResolveResult iCAX::ApplicationHost::CApplicationHost::ResolveProjectFileProduct(
    IN const std::string& strProjectPath_) const
{
    return CProductFileResolver::Resolve(strProjectPath_, m_Config.Products);
}

std::shared_ptr<iCAX::Product::CProductRuntime> iCAX::ApplicationHost::CApplicationHost::StartProduct(
    IN const std::string& strProductID_)
{
    auto _Definition = FindProductDefinition(strProductID_);
    {
        std::unique_lock<std::mutex> _Lock(m_ProductRuntimeMutex);
        for (;;)
        {
            {
                std::lock_guard<std::mutex> _LifecycleLock(m_LifecycleMutex);
                if (m_State == EApplicationHostState::Stopping
                    || m_State == EApplicationHostState::Stopped
                    || m_State == EApplicationHostState::Faulted
                    || m_Phase == EApplicationHostPhase::Unloading)
                {
                    throw std::logic_error("ApplicationHost is not accepting product starts");
                }
            }
            if (!m_pApplicationContext
                || !m_pApplicationServiceProvider
                || !m_pMailChannelRegistry)
            {
                throw std::logic_error("ApplicationHost is not loaded");
            }

            if (m_StoppingProductIDs.find(_Definition.ProductID) != m_StoppingProductIDs.end())
            {
                // 同一 ProductID 正在停止时不允许创建新的 runtime，避免同一产品短暂并存两个后台实例。
                m_ProductRuntimeCondition.wait(_Lock);
                continue;
            }

            auto _Iter = m_ProductRuntimes.find(_Definition.ProductID);
            if (_Iter != m_ProductRuntimes.end())
            {
                return _Iter->second;
            }

            if (m_StartingProductIDs.insert(_Definition.ProductID).second)
            {
                // 当前线程获得该 ProductID 的创建权，退出锁后执行耗时的模块加载和 runtime 启动。
                break;
            }

            // 其他线程正在启动同一产品，等待它完成后复用已创建 runtime。
            m_ProductRuntimeCondition.wait(_Lock);
        }
    }

    std::shared_ptr<iCAX::Product::CProductRuntime> _pRuntime;
    try
    {
        _pRuntime = std::make_shared<iCAX::Product::CProductRuntime>(
            _Definition,
            m_pApplicationContext,
            m_pApplicationServiceProvider,
            m_pMailChannelRegistry,
            nullptr,
            m_Config.nFrameIntervalMilliseconds);
        _pRuntime->Start();
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> _Lock(m_ProductRuntimeMutex);
            m_StartingProductIDs.erase(_Definition.ProductID);
        }
        m_ProductRuntimeCondition.notify_all();
        throw;
    }

    std::shared_ptr<iCAX::Product::CProductRuntime> _pExistingRuntime;
    {
        std::lock_guard<std::mutex> _Lock(m_ProductRuntimeMutex);
        auto [_Iter, _Inserted] = m_ProductRuntimes.emplace(_Definition.ProductID, _pRuntime);
        if (!_Inserted)
        {
            _pExistingRuntime = _Iter->second;
        }
        m_StartingProductIDs.erase(_Definition.ProductID);
    }
    m_ProductRuntimeCondition.notify_all();

    if (_pExistingRuntime)
    {
        // 极端竞态下如果已有 runtime 先插入，停止本次新建 runtime，返回既有实例。
        _pRuntime->Stop();
        return _pExistingRuntime;
    }
    return _pRuntime;
}

bool iCAX::ApplicationHost::CApplicationHost::StopProduct(IN const std::string& strProductID_)
{
    std::shared_ptr<iCAX::Product::CProductRuntime> _pRuntime;
    {
        std::unique_lock<std::mutex> _Lock(m_ProductRuntimeMutex);
        for (;;)
        {
            m_ProductRuntimeCondition.wait(_Lock, [this, &strProductID_]() {
                return m_StartingProductIDs.find(strProductID_) == m_StartingProductIDs.end()
                    && m_StoppingProductIDs.find(strProductID_) == m_StoppingProductIDs.end();
            });

            auto _Iter = m_ProductRuntimes.find(strProductID_);
            if (_Iter == m_ProductRuntimes.end())
            {
                return false;
            }

            _pRuntime = _Iter->second;
            m_StoppingProductIDs.insert(strProductID_);
            break;
        }
    }

    try
    {
        if (_pRuntime)
        {
            _pRuntime->Stop();
        }
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> _Lock(m_ProductRuntimeMutex);
            m_StoppingProductIDs.erase(strProductID_);
        }
        m_ProductRuntimeCondition.notify_all();
        throw;
    }

    {
        std::lock_guard<std::mutex> _Lock(m_ProductRuntimeMutex);
        auto _Iter = m_ProductRuntimes.find(strProductID_);
        if (_Iter != m_ProductRuntimes.end() && _Iter->second == _pRuntime)
        {
            m_ProductRuntimes.erase(_Iter);
        }
        m_StoppingProductIDs.erase(strProductID_);
    }
    m_ProductRuntimeCondition.notify_all();
    return true;
}

iCAX::Mail::CMailPostOffice iCAX::ApplicationHost::CApplicationHost::GetApplicationFrontendPostOffice() const
{
    if (!m_pMailChannelRegistry)
    {
        throw std::logic_error("ApplicationHost is not loaded");
    }
    return m_pMailChannelRegistry->GetFrontendPostOffice(m_ApplicationChannelID);
}

void iCAX::ApplicationHost::CApplicationHost::SendFrontendEvent(
    IN uint64_t nTypeCode_,
    IN const std::string& strPayloadText_)
{
    if (!m_pMailChannelRegistry || m_ApplicationChannelID.is_nil())
    {
        throw std::logic_error("ApplicationHost mail channel is not available");
    }

    iCAX::Mail::MailHeader _Header;
    _Header.nMailId = AllocateBackendMailID();
    _Header.nOriginId = 0;
    _Header.nTypeCode = nTypeCode_;
    _Header.nStamp = iCAX::Mail::kMailOk;

    m_pMailChannelRegistry->GetBackendPostOffice(m_ApplicationChannelID).SendText(_Header, strPayloadText_);
}

iCAX::Mail::CMailPostOffice iCAX::ApplicationHost::CApplicationHost::GetProductFrontendPostOffice(
    IN const std::string& strProductID_) const
{
    auto _pRuntime = FindProductRuntime(strProductID_);
    if (!_pRuntime)
    {
        throw std::runtime_error("ProductRuntime not found");
    }
    return _pRuntime->GetProductFrontendPostOffice();
}

iCAX::Mail::CMailPostOffice iCAX::ApplicationHost::CApplicationHost::GetProjectFrontendPostOffice(
    IN const iCAX::Data::uuid& ProjectID_) const
{
    auto _Runtimes = SnapshotProductRuntimes();
    for (const auto& _pRuntime : _Runtimes)
    {
        if (_pRuntime && _pRuntime->FindProjectCatalogByProjectID(ProjectID_))
        {
            return _pRuntime->GetProjectFrontendPostOffice(ProjectID_);
        }
    }
    throw std::runtime_error("Project not found");
}

const iCAX::Data::uuid& iCAX::ApplicationHost::CApplicationHost::GetApplicationChannelID() const
{
    return m_ApplicationChannelID;
}

