#include "pch.h"
#include "CefWebPageHost.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_frame.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"

#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace
{
    namespace json = boost::json;

    std::atomic_bool g_bCefRuntimeInitialized = false;
    std::mutex g_CefRuntimeMutex;

    std::string _ToUtf8(IN const std::wstring& Text_)
    {
        if (Text_.empty())
        {
            return {};
        }

        const int _nSize = WideCharToMultiByte(CP_UTF8, 0, Text_.c_str(), static_cast<int>(Text_.size()), nullptr, 0, nullptr, nullptr);
        if (_nSize <= 0)
        {
            throw std::runtime_error("Failed to convert wide string to UTF-8");
        }

        std::string _Result(static_cast<size_t>(_nSize), '\0');
        WideCharToMultiByte(CP_UTF8, 0, Text_.c_str(), static_cast<int>(Text_.size()), _Result.data(), _nSize, nullptr, nullptr);
        return _Result;
    }

    std::string _ToFileURL(IN const std::string& strPath_)
    {
        auto _Path = std::filesystem::absolute(std::filesystem::path(strPath_));
        std::string _Text = _Path.generic_string();
        if (!_Text.empty() && _Text.front() != '/')
        {
            _Text.insert(_Text.begin(), '/');
        }
        return "file://" + _Text;
    }

    std::string _ResolveStartURL(IN const iCAX::Frontend::Cef::CCefWebPageHostConfig& Config_)
    {
        if (!Config_.StartURL.empty())
        {
            return _ToUtf8(Config_.StartURL);
        }

        if (Config_.CoreConfig.WebPageRoot.empty())
        {
            throw std::invalid_argument("WebPageRoot or StartURL is required");
        }

        auto _IndexPath = std::filesystem::path(Config_.CoreConfig.WebPageRoot) / "index.html";
        return _ToFileURL(_IndexPath.string());
    }

    const json::object& _AsObject(IN const json::value& Value_, IN const char* szName_)
    {
        if (!Value_.is_object())
        {
            throw std::invalid_argument(std::string(szName_) + " must be an object");
        }
        return Value_.as_object();
    }

    const json::value& _RequireField(IN const json::object& Object_, IN const char* szName_)
    {
        const auto _Iter = Object_.find(szName_);
        if (_Iter == Object_.end())
        {
            throw std::invalid_argument(std::string("CEF bridge field is required: ") + szName_);
        }
        return _Iter->value();
    }

    std::string _AsString(IN const json::value& Value_, IN const char* szName_)
    {
        if (Value_.is_string())
        {
            return std::string(Value_.as_string().c_str());
        }
        if (Value_.is_int64())
        {
            return std::to_string(Value_.as_int64());
        }
        if (Value_.is_uint64())
        {
            return std::to_string(Value_.as_uint64());
        }
        throw std::invalid_argument(std::string(szName_) + " must be a string");
    }

    uint64_t _AsUInt64(IN const json::value& Value_, IN const char* szName_)
    {
        if (Value_.is_uint64())
        {
            return Value_.as_uint64();
        }
        if (Value_.is_int64())
        {
            const auto _nValue = Value_.as_int64();
            if (_nValue < 0)
            {
                throw std::invalid_argument(std::string(szName_) + " must be unsigned");
            }
            return static_cast<uint64_t>(_nValue);
        }
        if (Value_.is_double())
        {
            const auto _nValue = Value_.as_double();
            if (_nValue < 0)
            {
                throw std::invalid_argument(std::string(szName_) + " must be unsigned");
            }
            return static_cast<uint64_t>(_nValue);
        }
        if (Value_.is_string())
        {
            return std::stoull(std::string(Value_.as_string().c_str()));
        }
        throw std::invalid_argument(std::string(szName_) + " must be an unsigned integer");
    }

    iCAX::Frontend::CWebPageMailEnvelope _ParseEnvelope(IN const json::object& Object_)
    {
        iCAX::Frontend::CWebPageMailEnvelope _Envelope;
        _Envelope.ChannelID = _AsString(_RequireField(Object_, "channelId"), "channelId");
        _Envelope.nID = _AsUInt64(_RequireField(Object_, "id"), "id");
        _Envelope.nOriginID = _AsUInt64(_RequireField(Object_, "originId"), "originId");
        _Envelope.nTypeCode = _AsUInt64(_RequireField(Object_, "typeCode"), "typeCode");
        _Envelope.nStamp = static_cast<uint16_t>(_AsUInt64(_RequireField(Object_, "stamp"), "stamp"));
        _Envelope.PayloadText = _AsString(_RequireField(Object_, "payloadText"), "payloadText");
        return _Envelope;
    }

    json::value _ToJsonValue(IN const std::string& strValue_)
    {
        return json::value(strValue_);
    }

    json::object _ToJsonEnvelope(IN const iCAX::Frontend::CWebPageMailEnvelope& Envelope_)
    {
        json::object _Object;
        _Object["channelId"] = Envelope_.ChannelID;
        _Object["id"] = Envelope_.nID;
        _Object["originId"] = Envelope_.nOriginID;
        _Object["typeCode"] = std::to_string(Envelope_.nTypeCode);
        _Object["stamp"] = Envelope_.nStamp;
        _Object["payloadText"] = Envelope_.PayloadText;
        return _Object;
    }

    std::string _BuildBridgeScript()
    {
        return R"JS(
(function () {
  if (window.icax && window.icax.__icaxCefBridge) {
    return;
  }

  const subscribers = new Map();

  function queryNative(method, payload) {
    return new Promise((resolve, reject) => {
      if (typeof window.cefQuery !== "function") {
        reject(new Error("CEF message router is not available"));
        return;
      }

      window.cefQuery({
        request: JSON.stringify({ method, payload: payload || {} }),
        onSuccess(responseText) {
          if (!responseText) {
            resolve(null);
            return;
          }
          resolve(JSON.parse(responseText));
        },
        onFailure(code, message) {
          reject(new Error(message || ("CEF bridge query failed: " + code)));
        }
      });
    });
  }

  function ensureSubscriberSet(channelId) {
    const key = String(channelId || "");
    if (!subscribers.has(key)) {
      subscribers.set(key, new Set());
    }
    return subscribers.get(key);
  }

  window.icax = {
    __icaxCefBridge: true,

    getApplicationChannelId() {
      return queryNative("getApplicationChannelId");
    },

    registerProductChannel(productId) {
      return queryNative("registerProductChannel", { productId });
    },

    registerProjectChannel(projectId) {
      return queryNative("registerProjectChannel", { projectId });
    },

    postMail(mail) {
      return queryNative("postMail", { mail }).then(() => undefined);
    },

    subscribeMail(channelId, handler) {
      if (typeof handler !== "function") {
        throw new TypeError("handler must be a function");
      }

      const handlers = ensureSubscriberSet(channelId);
      handlers.add(handler);
      return function unsubscribe() {
        handlers.delete(handler);
      };
    },

    __dispatchMail(mail) {
      const channelId = String(mail && mail.channelId || "");
      const handlers = subscribers.get(channelId);
      if (!handlers) {
        return;
      }
      for (const handler of Array.from(handlers)) {
        handler(mail);
      }
    }
  };
})();
)JS";
    }

    class CInternalCefApp final :
        public CefApp,
        public CefBrowserProcessHandler,
        public CefRenderProcessHandler
    {
    public:
        CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override
        {
            return this;
        }

        CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
        {
            return this;
        }

        void OnContextCreated(
            CefRefPtr<CefBrowser> Browser_,
            CefRefPtr<CefFrame> Frame_,
            CefRefPtr<CefV8Context> Context_) override
        {
            CEF_REQUIRE_RENDERER_THREAD();

            if (!m_pRenderMessageRouter)
            {
                CefMessageRouterConfig _Config;
                m_pRenderMessageRouter = CefMessageRouterRendererSide::Create(_Config);
            }

            m_pRenderMessageRouter->OnContextCreated(Browser_, Frame_, Context_);
            if (Frame_ && Frame_->IsMain())
            {
                Frame_->ExecuteJavaScript(_BuildBridgeScript(), Frame_->GetURL(), 0);
            }
        }

        void OnContextReleased(
            CefRefPtr<CefBrowser> Browser_,
            CefRefPtr<CefFrame> Frame_,
            CefRefPtr<CefV8Context> Context_) override
        {
            CEF_REQUIRE_RENDERER_THREAD();

            if (m_pRenderMessageRouter)
            {
                m_pRenderMessageRouter->OnContextReleased(Browser_, Frame_, Context_);
            }
        }

        bool OnProcessMessageReceived(
            CefRefPtr<CefBrowser> Browser_,
            CefRefPtr<CefFrame> Frame_,
            CefProcessId SourceProcess_,
            CefRefPtr<CefProcessMessage> Message_) override
        {
            CEF_REQUIRE_RENDERER_THREAD();

            return m_pRenderMessageRouter
                && m_pRenderMessageRouter->OnProcessMessageReceived(Browser_, Frame_, SourceProcess_, Message_);
        }

    private:
        CefRefPtr<CefMessageRouterRendererSide> m_pRenderMessageRouter;

        IMPLEMENT_REFCOUNTING(CInternalCefApp);
    };

    class CInternalBridgeQueryHandler final : public CefMessageRouterBrowserSide::Handler
    {
    public:
        explicit CInternalBridgeQueryHandler(IN iCAX::Frontend::CWebPageHost* pHost_)
            : m_pHost(pHost_)
        {
        }

        bool OnQuery(
            CefRefPtr<CefBrowser>,
            CefRefPtr<CefFrame>,
            int64_t,
            const CefString& Request_,
            bool,
            CefRefPtr<Callback> Callback_) override
        {
            try
            {
                if (!m_pHost)
                {
                    throw std::runtime_error("CWebPageHost is not available");
                }

                const auto _RootValue = json::parse(Request_.ToString());
                const auto& _Root = _AsObject(_RootValue, "request");
                const auto _Method = _AsString(_RequireField(_Root, "method"), "method");
                const auto& _Payload = _AsObject(_RequireField(_Root, "payload"), "payload");

                if (_Method == "getApplicationChannelId")
                {
                    Callback_->Success(json::serialize(_ToJsonValue(m_pHost->GetApplicationChannelIDText())));
                    return true;
                }

                if (_Method == "registerProductChannel")
                {
                    const auto _ProductID = _AsString(_RequireField(_Payload, "productId"), "productId");
                    Callback_->Success(json::serialize(_ToJsonValue(m_pHost->RegisterProductChannel(_ProductID))));
                    return true;
                }

                if (_Method == "registerProjectChannel")
                {
                    const auto _ProjectID = _AsString(_RequireField(_Payload, "projectId"), "projectId");
                    Callback_->Success(json::serialize(_ToJsonValue(m_pHost->RegisterProjectChannel(_ProjectID))));
                    return true;
                }

                if (_Method == "postMail")
                {
                    const auto& _Mail = _AsObject(_RequireField(_Payload, "mail"), "mail");
                    m_pHost->PostMail(_ParseEnvelope(_Mail));
                    Callback_->Success("null");
                    return true;
                }

                throw std::invalid_argument("Unsupported CEF bridge method: " + _Method);
            }
            catch (const std::exception& _Error)
            {
                Callback_->Failure(1, _Error.what());
                return true;
            }
        }

    private:
        iCAX::Frontend::CWebPageHost* m_pHost = nullptr;

        IMPLEMENT_REFCOUNTING(CInternalBridgeQueryHandler);
    };

    class CInternalCefClient;

    class CDispatchMailTask final : public CefTask
    {
    public:
        CDispatchMailTask(CefRefPtr<CInternalCefClient> pClient_, std::string strEnvelopeJson_);

        void Execute() override;

    private:
        CefRefPtr<CInternalCefClient> m_pClient;
        std::string m_strEnvelopeJson;

        IMPLEMENT_REFCOUNTING(CDispatchMailTask);
    };

    class CInternalCefClient final :
        public CefClient,
        public CefLifeSpanHandler,
        public CefLoadHandler
    {
    public:
        explicit CInternalCefClient(IN iCAX::Frontend::CWebPageHost* pHost_)
        {
            CefMessageRouterConfig _Config;
            m_pMessageRouter = CefMessageRouterBrowserSide::Create(_Config);
            m_pBridgeQueryHandler = new CInternalBridgeQueryHandler(pHost_);
            m_pMessageRouter->AddHandler(m_pBridgeQueryHandler, false);
        }

        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
        {
            return this;
        }

        CefRefPtr<CefLoadHandler> GetLoadHandler() override
        {
            return this;
        }

        void OnAfterCreated(CefRefPtr<CefBrowser> Browser_) override
        {
            CEF_REQUIRE_UI_THREAD();
            m_pBrowser = Browser_;
        }

        void OnBeforeClose(CefRefPtr<CefBrowser>) override
        {
            CEF_REQUIRE_UI_THREAD();
            m_pBrowser = nullptr;
        }

        void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> Frame_, int) override
        {
            CEF_REQUIRE_UI_THREAD();
            if (Frame_ && Frame_->IsMain())
            {
                Frame_->ExecuteJavaScript(_BuildBridgeScript(), Frame_->GetURL(), 0);
            }
        }

        bool OnProcessMessageReceived(
            CefRefPtr<CefBrowser> Browser_,
            CefRefPtr<CefFrame> Frame_,
            CefProcessId SourceProcess_,
            CefRefPtr<CefProcessMessage> Message_) override
        {
            CEF_REQUIRE_UI_THREAD();
            return m_pMessageRouter
                && m_pMessageRouter->OnProcessMessageReceived(Browser_, Frame_, SourceProcess_, Message_);
        }

        void CloseBrowser()
        {
            if (m_pBrowser)
            {
                m_pBrowser->GetHost()->CloseBrowser(false);
            }
        }

        void DispatchMail(IN const iCAX::Frontend::CWebPageMailEnvelope& Envelope_)
        {
            const auto _EnvelopeJson = json::serialize(_ToJsonEnvelope(Envelope_));
            auto _TaskRunner = CefTaskRunner::GetForThread(TID_UI);
            if (!_TaskRunner)
            {
                return;
            }
            _TaskRunner->PostTask(new CDispatchMailTask(this, _EnvelopeJson));
        }

        void DispatchMailOnUI(IN const std::string& strEnvelopeJson_)
        {
            CEF_REQUIRE_UI_THREAD();
            if (!m_pBrowser || !m_pBrowser->GetMainFrame())
            {
                return;
            }

            const std::string _Script = "window.icax && window.icax.__dispatchMail && window.icax.__dispatchMail("
                + strEnvelopeJson_ + ");";
            m_pBrowser->GetMainFrame()->ExecuteJavaScript(_Script, m_pBrowser->GetMainFrame()->GetURL(), 0);
        }

    private:
        CefRefPtr<CefBrowser> m_pBrowser;
        CefRefPtr<CefMessageRouterBrowserSide> m_pMessageRouter;
        CefRefPtr<CInternalBridgeQueryHandler> m_pBridgeQueryHandler;

        IMPLEMENT_REFCOUNTING(CInternalCefClient);
    };

    CDispatchMailTask::CDispatchMailTask(CefRefPtr<CInternalCefClient> pClient_, std::string strEnvelopeJson_)
        : m_pClient(std::move(pClient_))
        , m_strEnvelopeJson(std::move(strEnvelopeJson_))
    {
    }

    void CDispatchMailTask::Execute()
    {
        if (m_pClient)
        {
            m_pClient->DispatchMailOnUI(m_strEnvelopeJson);
        }
    }

    CefRefPtr<CInternalCefApp> _CreateCefApp()
    {
        return new CInternalCefApp();
    }
}

class iCAX::Frontend::Cef::CCefWebPageHost::Impl final
{
public:
    iCAX::Frontend::CWebPageHost CoreHost;
    CefRefPtr<CInternalCefClient> Client;
    std::thread PollThread;
    std::atomic_bool bStopPolling = true;
    bool bStarted = false;

public:
    void StartPolling(IN int nIntervalMS_)
    {
        bStopPolling = false;
        PollThread = std::thread([this, nIntervalMS_]()
        {
            const auto _Interval = std::chrono::milliseconds(std::max(1, nIntervalMS_));
            while (!bStopPolling.load())
            {
                PollMails();
                std::this_thread::sleep_for(_Interval);
            }
        });
    }

    void StopPolling()
    {
        bStopPolling = true;
        if (PollThread.joinable())
        {
            PollThread.join();
        }
    }

    void PollMails()
    {
        if (!Client)
        {
            return;
        }

        auto _Mails = CoreHost.PollMails();
        for (const auto& _Mail : _Mails)
        {
            Client->DispatchMail(_Mail);
        }
    }
};

iCAX::Frontend::Cef::CCefWebPageHost::CCefWebPageHost()
    : m_pImpl(std::make_unique<Impl>())
{
}

iCAX::Frontend::Cef::CCefWebPageHost::~CCefWebPageHost()
{
    if (IsRunning())
    {
        Stop();
    }
}

int iCAX::Frontend::Cef::CCefWebPageHost::ExecuteSubProcess(IN HINSTANCE hInstance_)
{
    CefMainArgs _MainArgs(hInstance_);
    return CefExecuteProcess(_MainArgs, _CreateCefApp(), nullptr);
}

void iCAX::Frontend::Cef::CCefWebPageHost::InitializeRuntime(IN const CCefRuntimeConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(g_CefRuntimeMutex);
    if (g_bCefRuntimeInitialized.load())
    {
        return;
    }

    CefEnableHighDPISupport();

    CefMainArgs _MainArgs(Config_.hInstance);
    CefSettings _Settings;
    _Settings.no_sandbox = true;
    _Settings.multi_threaded_message_loop = Config_.bMultiThreadedMessageLoop;
    _Settings.windowless_rendering_enabled = Config_.bWindowlessRenderingEnabled;

    if (!Config_.BrowserSubprocessPath.empty())
    {
        CefString(&_Settings.browser_subprocess_path).FromWString(Config_.BrowserSubprocessPath);
    }
    if (!Config_.CachePath.empty())
    {
        CefString(&_Settings.cache_path).FromWString(Config_.CachePath);
    }
    if (!Config_.UserDataPath.empty())
    {
        CefString(&_Settings.user_data_path).FromWString(Config_.UserDataPath);
    }

    if (!CefInitialize(_MainArgs, _Settings, _CreateCefApp(), nullptr))
    {
        throw std::runtime_error("CefInitialize failed");
    }

    g_bCefRuntimeInitialized = true;
}

void iCAX::Frontend::Cef::CCefWebPageHost::ShutdownRuntime()
{
    std::lock_guard<std::mutex> _Lock(g_CefRuntimeMutex);
    if (!g_bCefRuntimeInitialized.load())
    {
        return;
    }

    CefShutdown();
    g_bCefRuntimeInitialized = false;
}

void iCAX::Frontend::Cef::CCefWebPageHost::Start(IN const CCefWebPageHostConfig& Config_)
{
    if (m_pImpl->bStarted)
    {
        return;
    }

    if (!g_bCefRuntimeInitialized.load())
    {
        CCefRuntimeConfig _RuntimeConfig;
        _RuntimeConfig.hInstance = Config_.hInstance;
        InitializeRuntime(_RuntimeConfig);
    }

    m_pImpl->CoreHost.SetConfig(Config_.CoreConfig);
    m_pImpl->CoreHost.Start();
    m_pImpl->Client = new CInternalCefClient(&m_pImpl->CoreHost);

    CefWindowInfo _WindowInfo;
    if (Config_.hParentWnd)
    {
        CefRect _Bounds(Config_.nX, Config_.nY, Config_.nWidth, Config_.nHeight);
        _WindowInfo.SetAsChild(Config_.hParentWnd, _Bounds);
    }
    else
    {
        _WindowInfo.SetAsPopup(nullptr, L"iCAX");
    }

    CefBrowserSettings _BrowserSettings;
    const auto _StartURL = _ResolveStartURL(Config_);
    if (!CefBrowserHost::CreateBrowser(_WindowInfo, m_pImpl->Client, _StartURL, _BrowserSettings, nullptr, nullptr))
    {
        m_pImpl->Client = nullptr;
        m_pImpl->CoreHost.Stop();
        throw std::runtime_error("CefBrowserHost::CreateBrowser failed");
    }

    m_pImpl->StartPolling(Config_.nMailPollIntervalMS);
    m_pImpl->bStarted = true;
}

void iCAX::Frontend::Cef::CCefWebPageHost::Stop()
{
    if (!m_pImpl->bStarted)
    {
        return;
    }

    m_pImpl->StopPolling();

    if (m_pImpl->Client)
    {
        m_pImpl->Client->CloseBrowser();
        m_pImpl->Client = nullptr;
    }

    m_pImpl->CoreHost.Stop();
    m_pImpl->bStarted = false;
}

bool iCAX::Frontend::Cef::CCefWebPageHost::IsRunning() const
{
    return m_pImpl->bStarted;
}

void iCAX::Frontend::Cef::CCefWebPageHost::PollMails()
{
    m_pImpl->PollMails();
}

iCAX::Frontend::CWebPageHost& iCAX::Frontend::Cef::CCefWebPageHost::CoreHost()
{
    return m_pImpl->CoreHost;
}
