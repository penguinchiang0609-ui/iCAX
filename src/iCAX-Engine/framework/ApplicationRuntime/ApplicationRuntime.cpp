#include "pch.h"
#include "ApplicationRuntime.h"

#include "ApplicationContext/FileApplicationConfigStore.h"
#include "Facades/Facade.h"
#include "Facades/FacadeChannelRegistry.h"


namespace
{
    class CStaticApplicationFacade final : public iCAX::Interaction::CFacade
    {
    public:
        using MethodRecord = std::pair<std::string, MethodFunc>;

        CStaticApplicationFacade(
            IN std::string strFacadeName_,
            IN std::vector<MethodRecord> Methods_)
            : CFacade(std::move(strFacadeName_))
        {
            for (auto& _Method : Methods_)
            {
                auto _MethodName = _Method.first;
                if (!ExposeMethod(std::move(_Method.first), std::move(_Method.second)))
                {
                    throw std::runtime_error("Built-in application method is already exposed: " + _MethodName);
                }
            }
        }
    };

    std::string _ToApplicationRuntimeStateText(IN iCAX::Application::EApplicationRuntimeState State_)
    {
        switch (State_)
        {
        case iCAX::Application::EApplicationRuntimeState::Stopped:
            return "Stopped";
        case iCAX::Application::EApplicationRuntimeState::Starting:
            return "Starting";
        case iCAX::Application::EApplicationRuntimeState::Running:
            return "Running";
        case iCAX::Application::EApplicationRuntimeState::Stopping:
            return "Stopping";
        case iCAX::Application::EApplicationRuntimeState::Faulted:
            return "Faulted";
        default:
            throw std::invalid_argument("Invalid ApplicationRuntime state");
        }
    }

    std::string _ToApplicationRuntimePhaseText(IN iCAX::Application::EApplicationRuntimePhase Phase_)
    {
        switch (Phase_)
        {
        case iCAX::Application::EApplicationRuntimePhase::None:
            return "None";
        case iCAX::Application::EApplicationRuntimePhase::Starting:
            return "Starting";
        case iCAX::Application::EApplicationRuntimePhase::Loading:
            return "Loading";
        case iCAX::Application::EApplicationRuntimePhase::Running:
            return "Running";
        case iCAX::Application::EApplicationRuntimePhase::Stopping:
            return "Stopping";
        case iCAX::Application::EApplicationRuntimePhase::Unloading:
            return "Unloading";
        default:
            throw std::invalid_argument("Invalid ApplicationRuntime phase");
        }
    }

    iCAX::Data::ObjectMap _DecodeObjectPayload(IN const iCAX::Interaction::CInvocation& Request_)
    {
        auto _Payload = iCAX::Application::DecodeApplicationRuntimePayload(Request_.Payload);
        if (!_Payload.Is<iCAX::Data::ObjectMap>())
        {
            throw std::invalid_argument("ApplicationRuntime facade call payload must be an object");
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
            throw std::invalid_argument("ApplicationRuntime payload field must be a string: " + strName_);
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
            throw std::invalid_argument("ApplicationRuntime payload field is required: " + strName_);
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

    std::string _ToProductFileResolveStatusText(IN iCAX::Application::EProductFileResolveStatus Status_)
    {
        switch (Status_)
        {
        case iCAX::Application::EProductFileResolveStatus::Resolved:
            return "Resolved";
        case iCAX::Application::EProductFileResolveStatus::NotFound:
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

    void _RequireApplicationFacadeContext(
        IN const iCAX::Product::IProductContext* pProductContext_,
        IN const iCAX::Project::IProjectContext* pProjectContext_,
        IN const iCAX::Project::ISceneContext* pSceneContext_)
    {
        if (pProductContext_ || pProjectContext_ || pSceneContext_)
        {
            throw std::invalid_argument("ApplicationRuntime Facade invocation requires the application scope");
        }
    }

    iCAX::Interaction::CInvocationResult _MakeApplicationPayloadResponse(IN const iCAX::Data::Variant& Payload_)
    {
        iCAX::Interaction::CInvocationResult _Response;
        _Response.nStatus = iCAX::Interaction::EInvocationStatus::Ok;
        _Response.Payload = iCAX::Application::EncodeApplicationRuntimePayload(Payload_);
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

iCAX::Application::CApplicationRuntime::CApplicationRuntime()
    : m_Config()
    , m_pApplicationTaskScheduler(std::make_shared<iCAX::Tasks::EventLoopTaskScheduler>())
    , m_ApplicationChannelID(iCAX::Data::GenerateNewUUID())
    , m_pFacadeRegistry(std::make_shared<iCAX::Interaction::CFacadeRegistry>())
    , m_pFacadeInvoker(std::make_unique<iCAX::Interaction::CFacadeInvoker>(m_pFacadeRegistry))
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

    RegisterBuiltInApplicationFacades();
}

iCAX::Application::CApplicationRuntime::~CApplicationRuntime()
{
    Stop();
}

void iCAX::Application::CApplicationRuntime::SetConfig(IN const ApplicationRuntimeConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == EApplicationRuntimeState::Starting
        || m_State == EApplicationRuntimeState::Running
        || m_State == EApplicationRuntimeState::Stopping)
    {
        throw std::logic_error("ApplicationRuntime config cannot be changed while runtime is running");
    }
    _ValidateProductDefinitions(Config_.Products);
    m_Config = Config_;
}

void iCAX::Application::CApplicationRuntime::Start()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        if (m_WorkThread.joinable()
            && (m_State == EApplicationRuntimeState::Stopped || m_State == EApplicationRuntimeState::Faulted))
        {
            _ThreadToJoin = std::move(m_WorkThread);
        }
    }
    if (_ThreadToJoin.joinable())
    {
        _ThreadToJoin.join();
    }

    std::unique_lock<std::mutex> _Lock(m_LifecycleMutex);
    if (m_State == EApplicationRuntimeState::Starting || m_State == EApplicationRuntimeState::Running)
    {
        return;
    }
    if (m_State == EApplicationRuntimeState::Stopping)
    {
        throw std::logic_error("ApplicationRuntime is stopping");
    }

    m_bStopRequested.store(false, std::memory_order_release);
    m_bStartupCompleted = false;
    m_StopReason = EApplicationRuntimeStopReason::None;
    m_Phase = EApplicationRuntimePhase::Starting;
    m_LastFault.reset();
    m_pWorkerException = nullptr;
    m_State = EApplicationRuntimeState::Starting;
    m_WorkThread = std::thread(&CApplicationRuntime::WorkerMain, this);

    // Start 对外表现为同步启动：等待工作线程完成 Load，避免前端拿到尚未创建的应用邮箱。
    m_StartupCondition.wait(_Lock, [this]() { return m_bStartupCompleted; });
    if (m_State == EApplicationRuntimeState::Faulted && m_pWorkerException)
    {
        auto _Exception = m_pWorkerException;
        _Lock.unlock();
        Stop();
        std::rethrow_exception(_Exception);
    }
}

void iCAX::Application::CApplicationRuntime::Stop()
{
    std::thread _ThreadToJoin;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_bStopRequested.store(true, std::memory_order_release);
        if ((m_State == EApplicationRuntimeState::Starting || m_State == EApplicationRuntimeState::Running)
            && m_WorkThread.joinable())
        {
            m_State = EApplicationRuntimeState::Stopping;
            m_StopReason = EApplicationRuntimeStopReason::Requested;
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

bool iCAX::Application::CApplicationRuntime::IsRunning() const
{
    return GetState() == EApplicationRuntimeState::Running;
}

iCAX::Application::EApplicationRuntimeState iCAX::Application::CApplicationRuntime::GetState() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_State;
}

iCAX::Application::EApplicationRuntimePhase iCAX::Application::CApplicationRuntime::GetPhase() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_Phase;
}

iCAX::Application::EApplicationRuntimeStopReason iCAX::Application::CApplicationRuntime::GetStopReason() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_StopReason;
}

std::optional<iCAX::Application::ApplicationRuntimeFault> iCAX::Application::CApplicationRuntime::GetLastFault() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    return m_LastFault;
}

uint64_t iCAX::Application::CApplicationRuntime::SubscribeEvent(IN ApplicationRuntimeEventHandler Handler_)
{
    if (!Handler_)
    {
        throw std::invalid_argument("ApplicationRuntime event handler cannot be empty");
    }

    std::lock_guard<std::mutex> _Lock(m_EventMutex);
    const uint64_t _SubscriptionID = m_nNextEventSubscriptionID++;
    m_mapEventHandlers.emplace(_SubscriptionID, std::move(Handler_));
    return _SubscriptionID;
}

bool iCAX::Application::CApplicationRuntime::UnsubscribeEvent(IN uint64_t nSubscriptionID_)
{
    std::lock_guard<std::mutex> _Lock(m_EventMutex);
    return m_mapEventHandlers.erase(nSubscriptionID_) > 0;
}

iCAX::Tasks::TaskSchedulerPtr
iCAX::Application::CApplicationRuntime::GetApplicationTaskScheduler() const noexcept
{
    return m_pApplicationTaskScheduler;
}

void iCAX::Application::CApplicationRuntime::PauseCoroutine(
    IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_)
{
    RequireApplicationCoroutineRuntimeOnWorker().Pause(Handle_);
}

void iCAX::Application::CApplicationRuntime::ResumeCoroutine(
    IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_)
{
    RequireApplicationCoroutineRuntimeOnWorker().Resume(Handle_);
}

void iCAX::Application::CApplicationRuntime::CancelCoroutine(
    IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_)
{
    RequireApplicationCoroutineRuntimeOnWorker().Cancel(Handle_);
}

void iCAX::Application::CApplicationRuntime::CancelAllCoroutines()
{
    RequireApplicationCoroutineRuntimeOnWorker().CancelAll();
}

bool iCAX::Application::CApplicationRuntime::IsCoroutineRunning(
    IN const iCAX::Coroutines::CCoroutineHandleBase& Handle_) const
{
    return RequireApplicationCoroutineRuntimeOnWorker().IsRunning(Handle_);
}

void iCAX::Application::CApplicationRuntime::InitializeApplicationCoroutinesOnWorker()
{
    auto _Runtime = std::make_unique<iCAX::Coroutines::CCoroutineRuntime>(
        m_pApplicationTaskScheduler);

    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_pApplicationCoroutineRuntime = std::move(_Runtime);
}

void iCAX::Application::CApplicationRuntime::ShutdownApplicationCoroutinesOnWorker() noexcept
{
    std::unique_ptr<iCAX::Coroutines::CCoroutineRuntime> _Runtime;
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        _Runtime = std::move(m_pApplicationCoroutineRuntime);
        m_WorkThreadID = {};
    }

    m_pApplicationTaskScheduler->Clear();
    try
    {
        if (_Runtime)
        {
            _Runtime->CancelAll();
        }
    }
    catch (...)
    {
    }
    const auto _CancellationContinuationCount = m_pApplicationTaskScheduler->PendingCount();
    for (std::size_t _Index = 0; _Index < _CancellationContinuationCount; ++_Index)
    {
        try
        {
            if (!m_pApplicationTaskScheduler->RunOne())
            {
                break;
            }
        }
        catch (...)
        {
        }
    }
    m_pApplicationTaskScheduler->Clear();
}

iCAX::Coroutines::CCoroutineRuntime&
iCAX::Application::CApplicationRuntime::RequireApplicationCoroutineRuntimeOnWorker()
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    if (m_WorkThreadID != std::this_thread::get_id()
        || !m_pApplicationCoroutineRuntime)
    {
        throw std::logic_error(
            "ApplicationRuntime coroutine operations must run on the active ApplicationRuntime worker thread");
    }
    return *m_pApplicationCoroutineRuntime;
}

const iCAX::Coroutines::CCoroutineRuntime&
iCAX::Application::CApplicationRuntime::RequireApplicationCoroutineRuntimeOnWorker() const
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    if (m_WorkThreadID != std::this_thread::get_id()
        || !m_pApplicationCoroutineRuntime)
    {
        throw std::logic_error(
            "ApplicationRuntime coroutine operations must run on the active ApplicationRuntime worker thread");
    }
    return *m_pApplicationCoroutineRuntime;
}

void iCAX::Application::CApplicationRuntime::WorkerMain()
{
    {
        std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
        m_WorkThreadID = std::this_thread::get_id();
    }
    NotifyEvent(EApplicationRuntimeEventCode::Starting);

    try
    {
        SetPhase(EApplicationRuntimePhase::Loading);
        Load();
        InitializeApplicationCoroutinesOnWorker();

        bool _bShouldRun = !m_bStopRequested.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            m_State = _bShouldRun ? EApplicationRuntimeState::Running : EApplicationRuntimeState::Stopping;
            m_Phase = _bShouldRun ? EApplicationRuntimePhase::Running : EApplicationRuntimePhase::Stopping;
            if (!_bShouldRun)
            {
                m_StopReason = EApplicationRuntimeStopReason::Requested;
            }
            m_bStartupCompleted = true;
        }
        m_StartupCondition.notify_all();

        if (_bShouldRun)
        {
            NotifyEvent(EApplicationRuntimeEventCode::Started);
        }

        const uint32_t _nFrameIntervalMilliseconds = m_Config.nFrameIntervalMilliseconds == 0
            ? 1
            : m_Config.nFrameIntervalMilliseconds;
        const auto _FrameInterval = std::chrono::milliseconds(_nFrameIntervalMilliseconds);
        const auto _LoopStartTime = std::chrono::steady_clock::now();
        auto _PreviousFrameTime = _LoopStartTime;
        auto _NextFrameTime = _LoopStartTime;

        while (!m_bStopRequested.load(std::memory_order_acquire))
        {
            const auto _FrameTime = std::chrono::steady_clock::now();
            const auto _DeltaTime = std::chrono::duration<double>(_FrameTime - _PreviousFrameTime).count();
            const auto _TotalTime = std::chrono::duration<double>(_FrameTime - _LoopStartTime).count();
            _PreviousFrameTime = _FrameTime;

            SetPhase(EApplicationRuntimePhase::Running);
            iCAX::Tasks::CurrentTaskSchedulerScope _SchedulerScope(m_pApplicationTaskScheduler);
            m_pApplicationTaskScheduler->RunAll();
            MainLoop();
            RequireApplicationCoroutineRuntimeOnWorker().Tick(_DeltaTime, _TotalTime);
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
                // 运行时主循环不追赶积压帧，避免长耗时调用导致空转补帧。
                _NextFrameTime = std::chrono::steady_clock::now();
            }
        }

        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            m_State = EApplicationRuntimeState::Stopping;
            m_StopReason = EApplicationRuntimeStopReason::Requested;
            m_Phase = EApplicationRuntimePhase::Stopping;
        }
        ShutdownApplicationCoroutinesOnWorker();
        NotifyEvent(EApplicationRuntimeEventCode::Stopping);

        SetPhase(EApplicationRuntimePhase::Unloading);
        Unload();

        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            m_State = EApplicationRuntimeState::Stopped;
            m_Phase = EApplicationRuntimePhase::None;
        }
        NotifyEvent(EApplicationRuntimeEventCode::Stopped);
    }
    catch (...)
    {
        auto _Exception = std::current_exception();
        EApplicationRuntimePhase _FaultPhase = EApplicationRuntimePhase::None;
        {
            std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
            _FaultPhase = m_Phase;
        }
        const auto _StopReason = _FaultPhase == EApplicationRuntimePhase::Loading || _FaultPhase == EApplicationRuntimePhase::Starting
            ? EApplicationRuntimeStopReason::StartFailed
            : EApplicationRuntimeStopReason::Faulted;
        const auto _Message = std::format(
            "ApplicationRuntime faulted during phase {}: {}",
            static_cast<int>(_FaultPhase),
            GetExceptionMessage(_Exception));

        ShutdownApplicationCoroutinesOnWorker();
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
            m_State = EApplicationRuntimeState::Faulted;
            m_Phase = _FaultPhase;
            m_bStartupCompleted = true;
            m_pWorkerException = _Exception;
        }
        m_StartupCondition.notify_all();
        NotifyEvent(EApplicationRuntimeEventCode::Faulted, _Message, _Exception);
    }
}

void iCAX::Application::CApplicationRuntime::Load()
{
    m_pFacadeChannelRegistry = std::make_shared<iCAX::Interaction::CFacadeChannelRegistry>();
    if (!m_pFacadeChannelRegistry->CreateChannel(m_ApplicationChannelID))
    {
        throw std::runtime_error("Application Facade channel already exists");
    }
    m_pApplicationContext = CreateApplicationContext();

    if (m_Config.StartupProductID)
    {
        (void)StartProduct(*m_Config.StartupProductID);
    }
}

void iCAX::Application::CApplicationRuntime::MainLoop()
{
    DispatchApplicationFacadeFrames();
}

void iCAX::Application::CApplicationRuntime::Unload()
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

    if (m_pFacadeChannelRegistry && !m_ApplicationChannelID.is_nil())
    {
        (void)m_pFacadeChannelRegistry->RemoveChannel(m_ApplicationChannelID);
    }
    m_pFacadeChannelRegistry.reset();
    m_pApplicationContext.reset();
}

void iCAX::Application::CApplicationRuntime::NotifyEvent(
    IN EApplicationRuntimeEventCode Code_,
    IN const std::string& strMessage_,
    IN std::exception_ptr pException_)
{
    std::vector<ApplicationRuntimeEventHandler> _Handlers;
    {
        std::lock_guard<std::mutex> _Lock(m_EventMutex);
        _Handlers.reserve(m_mapEventHandlers.size());
        for (const auto& _Pair : m_mapEventHandlers)
        {
            _Handlers.push_back(_Pair.second);
        }
    }

    ApplicationRuntimeEvent _Event;
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

void iCAX::Application::CApplicationRuntime::SetPhase(IN EApplicationRuntimePhase Phase_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_Phase = Phase_;
}

void iCAX::Application::CApplicationRuntime::RecordFault(
    IN EApplicationRuntimePhase Phase_,
    IN const std::string& strMessage_,
    IN std::exception_ptr pException_)
{
    std::lock_guard<std::mutex> _Lock(m_LifecycleMutex);
    m_LastFault = ApplicationRuntimeFault{ Phase_, strMessage_, pException_ };
}

std::string iCAX::Application::CApplicationRuntime::GetExceptionMessage(IN std::exception_ptr pException_)
{
    if (!pException_)
    {
        return "ApplicationRuntime exception pointer is empty";
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
        return "ApplicationRuntime caught a non-standard exception";
    }
}

void iCAX::Application::CApplicationRuntime::DispatchApplicationFacadeFrames()
{
    if (!m_pFacadeInvoker)
    {
        return;
    }

    if (!m_pFacadeChannelRegistry || !m_pFacadeChannelRegistry->HasChannel(m_ApplicationChannelID))
    {
        return;
    }

    auto _Endpoint = m_pFacadeChannelRegistry->GetBackendEndpoint(m_ApplicationChannelID);
    if (!_Endpoint.IsValid())
    {
        return;
    }

    m_pFacadeInvoker->DispatchAvailableFrames(
        _Endpoint,
        *m_pApplicationContext,
        nullptr,
        nullptr,
        nullptr);
}

std::shared_ptr<iCAX::Application::CApplicationContext> iCAX::Application::CApplicationRuntime::CreateApplicationContext() const
{
    return std::make_shared<iCAX::Application::CApplicationContext>(
        m_Config.Descriptor,
        m_Config.Paths,
        std::make_shared<iCAX::Application::CFileApplicationConfigStore>(),
        m_Config.strApplicationSettingsPath);
}

void iCAX::Application::CApplicationRuntime::RegisterBuiltInApplicationFacades()
{
    std::vector<CStaticApplicationFacade::MethodRecord> _Methods;
    _Methods.emplace_back(
        kAppGetStateName,
        [this](
            IN const iCAX::Interaction::CInvocation& Request_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleGetState(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Methods.emplace_back(
        kAppListProductsName,
        [this](
            IN const iCAX::Interaction::CInvocation& Request_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleListProducts(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Methods.emplace_back(
        kAppStartProductName,
        [this](
            IN const iCAX::Interaction::CInvocation& Request_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleStartProduct(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Methods.emplace_back(
        kAppStopProductName,
        [this](
            IN const iCAX::Interaction::CInvocation& Request_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleStopProduct(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Methods.emplace_back(
        kAppResolveProjectFileName,
        [this](
            IN const iCAX::Interaction::CInvocation& Request_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleResolveProjectFile(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });
    _Methods.emplace_back(
        kAppOpenProjectFileName,
        [this](
            IN const iCAX::Interaction::CInvocation& Request_,
            IN const iCAX::Application::IApplicationContext& ApplicationContext_,
            IN iCAX::Product::IProductContext* pProductContext_,
            IN iCAX::Project::IProjectContext* pProjectContext_,
            IN iCAX::Project::ISceneContext* pSceneContext_) {
            return HandleOpenProjectFile(Request_, ApplicationContext_, pProductContext_, pProjectContext_, pSceneContext_);
        });

    auto _pFacade = std::make_shared<CStaticApplicationFacade>(kAppFacadeName, std::move(_Methods));
    if (!m_pFacadeRegistry->Register(_pFacade))
    {
        throw std::runtime_error("Built-in application facade is already registered: " + _pFacade->GetName());
    }
}

iCAX::Interaction::CInvocationResult iCAX::Application::CApplicationRuntime::HandleGetState(
    IN const iCAX::Interaction::CInvocation&,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireApplicationFacadeContext(pProductContext_, pProjectContext_, pSceneContext_);
    return _MakeApplicationPayloadResponse(BuildApplicationStatePayload());
}

iCAX::Interaction::CInvocationResult iCAX::Application::CApplicationRuntime::HandleListProducts(
    IN const iCAX::Interaction::CInvocation&,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireApplicationFacadeContext(pProductContext_, pProjectContext_, pSceneContext_);
    return _MakeApplicationPayloadResponse(BuildApplicationStatePayload());
}

iCAX::Interaction::CInvocationResult iCAX::Application::CApplicationRuntime::HandleStartProduct(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireApplicationFacadeContext(pProductContext_, pProjectContext_, pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _pRuntime = StartProduct(_GetOptionalString(_Payload, "productId"));

    iCAX::Data::ObjectMap _Response;
    _Response["applicationChannelId"] = m_ApplicationChannelID;
    _Response["product"] = BuildProductPayload(_pRuntime->GetDefinition(), _pRuntime);
    _Response["state"] = BuildApplicationStatePayload();
    return _MakeApplicationPayloadResponse(iCAX::Data::Variant(_Response));
}

iCAX::Interaction::CInvocationResult iCAX::Application::CApplicationRuntime::HandleStopProduct(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireApplicationFacadeContext(pProductContext_, pProjectContext_, pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    const auto _ProductID = _GetOptionalString(_Payload, "productId");
    if (_ProductID.empty())
    {
        throw std::invalid_argument("ApplicationRuntime payload field is required: productId");
    }
    (void)StopProduct(_ProductID);
    return _MakeApplicationPayloadResponse(BuildApplicationStatePayload());
}

iCAX::Interaction::CInvocationResult iCAX::Application::CApplicationRuntime::HandleResolveProjectFile(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireApplicationFacadeContext(pProductContext_, pProjectContext_, pSceneContext_);
    auto _Payload = _DecodeObjectPayload(Request_);
    auto _ResolveResult = ResolveProjectFileProduct(_GetRequiredString(_Payload, "projectPath"));

    iCAX::Data::ObjectMap _Response;
    _Response["applicationChannelId"] = m_ApplicationChannelID;
    _Response["resolve"] = BuildProductFileResolvePayload(_ResolveResult);
    return _MakeApplicationPayloadResponse(iCAX::Data::Variant(_Response));
}

iCAX::Interaction::CInvocationResult iCAX::Application::CApplicationRuntime::HandleOpenProjectFile(
    IN const iCAX::Interaction::CInvocation& Request_,
    IN const iCAX::Application::IApplicationContext&,
    IN iCAX::Product::IProductContext* pProductContext_,
    IN iCAX::Project::IProjectContext* pProjectContext_,
    IN iCAX::Project::ISceneContext* pSceneContext_)
{
    _RequireApplicationFacadeContext(pProductContext_, pProjectContext_, pSceneContext_);
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

iCAX::Data::Variant iCAX::Application::CApplicationRuntime::BuildApplicationStatePayload() const
{
    iCAX::Data::ObjectMap _Root;
    _Root["applicationChannelId"] = m_ApplicationChannelID;
    _Root["state"] = _ToApplicationRuntimeStateText(GetState());
    _Root["phase"] = _ToApplicationRuntimePhaseText(GetPhase());

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

iCAX::Data::Variant iCAX::Application::CApplicationRuntime::BuildProductFileResolvePayload(
    IN const iCAX::Application::CProductFileResolveResult& Result_) const
{
    iCAX::Data::ObjectMap _Resolve;
    _Resolve["projectPath"] = Result_.ProjectPath;
    _Resolve["status"] = _ToProductFileResolveStatusText(Result_.Status);
    _Resolve["productId"] = Result_.ProductID;
    _Resolve["candidateProductIds"] = _MakeStringArray(Result_.CandidateProductIDs);
    _Resolve["matchedByMagic"] = Result_.bMatchedByMagic;
    return iCAX::Data::Variant(_Resolve);
}

iCAX::Data::Variant iCAX::Application::CApplicationRuntime::BuildProductPayload(
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

iCAX::Product::CProductDefinition iCAX::Application::CApplicationRuntime::FindProductDefinition(
    IN const std::string& strProductID_) const
{
    std::string _ProductID = strProductID_;
    if (_ProductID.empty())
    {
        if (m_Config.Products.size() != 1)
        {
            throw std::invalid_argument("productId is required when ApplicationRuntime has zero or multiple products");
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

std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> iCAX::Application::CApplicationRuntime::SnapshotProductRuntimes() const
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

std::vector<iCAX::Product::CProductDefinition> iCAX::Application::CApplicationRuntime::GetProductDefinitions() const
{
    return m_Config.Products;
}

std::vector<std::shared_ptr<iCAX::Product::CProductRuntime>> iCAX::Application::CApplicationRuntime::GetProductRuntimes() const
{
    return SnapshotProductRuntimes();
}

std::shared_ptr<iCAX::Product::CProductRuntime> iCAX::Application::CApplicationRuntime::FindProductRuntime(
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

std::shared_ptr<iCAX::Project::CProjectCatalog> iCAX::Application::CApplicationRuntime::OpenProjectFile(
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

iCAX::Application::CProductFileResolveResult iCAX::Application::CApplicationRuntime::ResolveProjectFileProduct(
    IN const std::string& strProjectPath_) const
{
    return CProductFileResolver::Resolve(strProjectPath_, m_Config.Products);
}

std::shared_ptr<iCAX::Product::CProductRuntime> iCAX::Application::CApplicationRuntime::StartProduct(
    IN const std::string& strProductID_)
{
    auto _Definition = FindProductDefinition(strProductID_);
    {
        std::unique_lock<std::mutex> _Lock(m_ProductRuntimeMutex);
        for (;;)
        {
            {
                std::lock_guard<std::mutex> _LifecycleLock(m_LifecycleMutex);
                if (m_State == EApplicationRuntimeState::Stopping
                    || m_State == EApplicationRuntimeState::Stopped
                    || m_State == EApplicationRuntimeState::Faulted
                    || m_Phase == EApplicationRuntimePhase::Unloading)
                {
                    throw std::logic_error("ApplicationRuntime is not accepting product starts");
                }
            }
            if (!m_pApplicationContext
                || !m_pFacadeChannelRegistry)
            {
                throw std::logic_error("ApplicationRuntime is not loaded");
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
            m_pFacadeChannelRegistry,
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

bool iCAX::Application::CApplicationRuntime::StopProduct(IN const std::string& strProductID_)
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

iCAX::Interaction::CFacadeEndpoint iCAX::Application::CApplicationRuntime::GetApplicationFrontendFacadeEndpoint() const
{
    if (!m_pFacadeChannelRegistry)
    {
        throw std::logic_error("ApplicationRuntime is not loaded");
    }
    return m_pFacadeChannelRegistry->GetFrontendEndpoint(m_ApplicationChannelID);
}

void iCAX::Application::CApplicationRuntime::SendFrontendEvent(
    IN uint64_t nMethodCode_,
    IN const std::string& strPayloadText_)
{
    if (!m_pFacadeChannelRegistry || m_ApplicationChannelID.is_nil())
    {
        throw std::logic_error("ApplicationRuntime Facade channel is not available");
    }

    m_pFacadeChannelRegistry->GetBackendEndpoint(m_ApplicationChannelID).SendText(
        0,
        nMethodCode_,
        iCAX::Interaction::EFacadeFrameKind::Event,
        strPayloadText_);
}

iCAX::Interaction::CFacadeEndpoint iCAX::Application::CApplicationRuntime::GetProductFrontendFacadeEndpoint(
    IN const std::string& strProductID_) const
{
    auto _pRuntime = FindProductRuntime(strProductID_);
    if (!_pRuntime)
    {
        throw std::runtime_error("ProductRuntime not found");
    }
    return _pRuntime->GetProductFrontendFacadeEndpoint();
}

iCAX::Interaction::CFacadeEndpoint iCAX::Application::CApplicationRuntime::GetSceneFrontendFacadeEndpoint(
    IN const iCAX::Data::uuid& ProjectID_,
    IN const iCAX::Data::uuid& SceneID_) const
{
    auto _Runtimes = SnapshotProductRuntimes();
    for (const auto& _pRuntime : _Runtimes)
    {
        if (_pRuntime && _pRuntime->FindProjectCatalogByProjectID(ProjectID_))
        {
            return _pRuntime->GetSceneFrontendFacadeEndpoint(ProjectID_, SceneID_);
        }
    }
    throw std::runtime_error("Project not found");
}

const iCAX::Data::uuid& iCAX::Application::CApplicationRuntime::GetApplicationChannelID() const
{
    return m_ApplicationChannelID;
}

