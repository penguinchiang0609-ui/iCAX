#include "pch.h"
#include "CefUIContainer.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/cef_display_handler.h"
#include "include/cef_frame.h"
#include "include/cef_parser.h"
#include "include/cef_request_handler.h"
#include "include/cef_task.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"

#include "PDO/PDOLease.h"
#include "PDO/SharedPDOArena.h"

#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include <commdlg.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")

namespace
{
    namespace json = boost::json;

    std::atomic_bool g_bCefRuntimeInitialized = false;
    std::atomic_bool g_bAllowFileAccessFromFiles = true;
    std::atomic_bool g_bDisableGPU = false;
    std::mutex g_CefRuntimeMutex;
    constexpr int g_nMinWindowWidthDIP = 1024;
    constexpr int g_nMinWindowHeightDIP = 680;
    constexpr wchar_t g_szOriginalWindowProcProperty[] = L"iCAX.CefUIContainer.OriginalWindowProc";

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

    std::wstring _UTF8ToWide(IN const std::string& Text_)
    {
        if (Text_.empty())
        {
            return {};
        }

        const int _Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text_.data(), static_cast<int>(Text_.size()), nullptr, 0);
        if (_Length <= 0)
        {
            throw std::runtime_error("Invalid UTF-8 text in CEF UI container config");
        }

        std::wstring _Result(static_cast<size_t>(_Length), L'\0');
        const int _Written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Text_.data(), static_cast<int>(Text_.size()), _Result.data(), _Length);
        if (_Written != _Length)
        {
            throw std::runtime_error("Failed to convert CEF UI container config text");
        }
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

    std::string _ResolveStartURL(IN const iCAX::Frontend::CUIContainerConfig& Config_)
    {
        if (!Config_.StartURL.empty())
        {
            return Config_.StartURL;
        }

        if (Config_.WebPageRoot.empty())
        {
            throw std::invalid_argument("WebPageRoot or StartURL is required");
        }

        auto _IndexPath = std::filesystem::path(Config_.WebPageRoot) / "index.html";
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

    iCAX::Frontend::CFrontendMailEnvelope _ParseEnvelope(IN const json::object& Object_)
    {
        iCAX::Frontend::CFrontendMailEnvelope _Envelope;
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

    std::wstring _MakeDialogFilterPattern(IN const json::array& Extensions_)
    {
        std::wstring _Pattern;
        for (const auto& _ExtensionValue : Extensions_)
        {
            auto _Extension = _AsString(_ExtensionValue, "extension");
            if (_Extension.empty())
            {
                continue;
            }
            if (!_Pattern.empty())
            {
                _Pattern += L';';
            }
            if (_Extension == "*")
            {
                _Pattern += L"*.*";
                continue;
            }
            if (_Extension.front() == '.')
            {
                _Extension.erase(_Extension.begin());
            }
            _Pattern += L"*.";
            _Pattern += _UTF8ToWide(_Extension);
        }
        return _Pattern.empty() ? L"*.*" : _Pattern;
    }

    std::wstring _BuildDialogFilterSpec(IN const json::object& Payload_)
    {
        std::wstring _Filter;
        const auto _FiltersIter = Payload_.find("filters");
        if (_FiltersIter != Payload_.end() && _FiltersIter->value().is_array())
        {
            for (const auto& _FilterValue : _FiltersIter->value().as_array())
            {
                if (!_FilterValue.is_object())
                {
                    continue;
                }

                const auto& _FilterObject = _FilterValue.as_object();
                const auto _NameIter = _FilterObject.find("name");
                const auto _ExtIter = _FilterObject.find("extensions");
                if (_ExtIter == _FilterObject.end() || !_ExtIter->value().is_array())
                {
                    continue;
                }

                const auto _Pattern = _MakeDialogFilterPattern(_ExtIter->value().as_array());
                const auto _Name = _NameIter != _FilterObject.end() && _NameIter->value().is_string()
                    ? _UTF8ToWide(_AsString(_NameIter->value(), "name"))
                    : std::wstring(L"Files");
                _Filter += _Name + L" (" + _Pattern + L")";
                _Filter.push_back(L'\0');
                _Filter += _Pattern;
                _Filter.push_back(L'\0');
            }
        }

        _Filter += L"All Files (*.*)";
        _Filter.push_back(L'\0');
        _Filter += L"*.*";
        _Filter.push_back(L'\0');
        _Filter.push_back(L'\0');
        return _Filter;
    }

    std::optional<std::string> _OpenFileDialog(
        IN CefRefPtr<CefBrowser> Browser_,
        IN const json::object& Payload_)
    {
        std::vector<wchar_t> _FileBuffer(32768, L'\0');
        const auto _Filter = _BuildDialogFilterSpec(Payload_);

        std::wstring _Title;
        const auto _TitleIter = Payload_.find("title");
        if (_TitleIter != Payload_.end() && _TitleIter->value().is_string())
        {
            _Title = _UTF8ToWide(_AsString(_TitleIter->value(), "title"));
        }

        HWND _Owner = nullptr;
        if (Browser_ && Browser_->GetHost())
        {
            _Owner = Browser_->GetHost()->GetWindowHandle();
        }

        OPENFILENAMEW _OpenFileName{};
        _OpenFileName.lStructSize = sizeof(_OpenFileName);
        _OpenFileName.hwndOwner = _Owner;
        _OpenFileName.lpstrFilter = _Filter.c_str();
        _OpenFileName.lpstrFile = _FileBuffer.data();
        _OpenFileName.nMaxFile = static_cast<DWORD>(_FileBuffer.size());
        _OpenFileName.nFilterIndex = 1;
        _OpenFileName.lpstrTitle = _Title.empty() ? nullptr : _Title.c_str();
        _OpenFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (::GetOpenFileNameW(&_OpenFileName))
        {
            return _ToUtf8(std::wstring(_FileBuffer.data()));
        }

        const auto _Error = ::CommDlgExtendedError();
        if (_Error != 0)
        {
            throw std::runtime_error("Windows open file dialog failed: " + std::to_string(_Error));
        }
        return std::nullopt;
    }

    HWND _GetBrowserWindowHandle(IN CefRefPtr<CefBrowser> Browser_)
    {
        if (!Browser_ || !Browser_->GetHost())
        {
            return nullptr;
        }
        return Browser_->GetHost()->GetWindowHandle();
    }

    HWND _GetTopLevelBrowserWindowHandle(IN CefRefPtr<CefBrowser> Browser_)
    {
        HWND _hWindow = _GetBrowserWindowHandle(Browser_);
        if (!_hWindow)
        {
            return nullptr;
        }

        HWND _hRootWindow = ::GetAncestor(_hWindow, GA_ROOT);
        return _hRootWindow ? _hRootWindow : _hWindow;
    }

    UINT _GetWindowDpiOrDefault(IN HWND hWindow_)
    {
        const UINT _nDpi = hWindow_ ? ::GetDpiForWindow(hWindow_) : 0;
        return _nDpi == 0 ? USER_DEFAULT_SCREEN_DPI : _nDpi;
    }

    int _ScaleByDpi(IN int nValue_, IN UINT nDpi_)
    {
        return ::MulDiv(nValue_, static_cast<int>(nDpi_), USER_DEFAULT_SCREEN_DPI);
    }

    SIZE _GetMinimumWindowSize(IN HWND hWindow_)
    {
        const UINT _nDpi = _GetWindowDpiOrDefault(hWindow_);
        return SIZE{
            _ScaleByDpi(g_nMinWindowWidthDIP, _nDpi),
            _ScaleByDpi(g_nMinWindowHeightDIP, _nDpi)
        };
    }

    LRESULT CALLBACK _BorderlessWindowProc(
        IN HWND hWindow_,
        IN UINT nMessage_,
        IN WPARAM wParam_,
        IN LPARAM lParam_)
    {
        auto _pOriginalProc = reinterpret_cast<WNDPROC>(::GetPropW(hWindow_, g_szOriginalWindowProcProperty));

        if (nMessage_ == WM_GETMINMAXINFO)
        {
            auto* _pInfo = reinterpret_cast<MINMAXINFO*>(lParam_);
            const SIZE _MinSize = _GetMinimumWindowSize(hWindow_);
            _pInfo->ptMinTrackSize.x = std::max(_pInfo->ptMinTrackSize.x, static_cast<LONG>(_MinSize.cx));
            _pInfo->ptMinTrackSize.y = std::max(_pInfo->ptMinTrackSize.y, static_cast<LONG>(_MinSize.cy));
            return 0;
        }

        if (nMessage_ == WM_WINDOWPOSCHANGING)
        {
            auto* _pWindowPos = reinterpret_cast<WINDOWPOS*>(lParam_);
            if (_pWindowPos && !(_pWindowPos->flags & SWP_NOSIZE) && !(_pWindowPos->flags & SWP_HIDEWINDOW) && !::IsIconic(hWindow_))
            {
                const SIZE _MinSize = _GetMinimumWindowSize(hWindow_);
                _pWindowPos->cx = std::max(_pWindowPos->cx, static_cast<int>(_MinSize.cx));
                _pWindowPos->cy = std::max(_pWindowPos->cy, static_cast<int>(_MinSize.cy));
            }
        }

        if (nMessage_ == WM_NCDESTROY && _pOriginalProc)
        {
            ::RemovePropW(hWindow_, g_szOriginalWindowProcProperty);
            ::SetWindowLongPtrW(hWindow_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_pOriginalProc));
            return ::CallWindowProcW(_pOriginalProc, hWindow_, nMessage_, wParam_, lParam_);
        }

        return _pOriginalProc
            ? ::CallWindowProcW(_pOriginalProc, hWindow_, nMessage_, wParam_, lParam_)
            : ::DefWindowProcW(hWindow_, nMessage_, wParam_, lParam_);
    }

    void _InstallBorderlessWindowProc(IN CefRefPtr<CefBrowser> Browser_)
    {
        HWND _hWindow = _GetTopLevelBrowserWindowHandle(Browser_);
        if (!_hWindow || ::GetPropW(_hWindow, g_szOriginalWindowProcProperty))
        {
            return;
        }

        auto _pOriginalProc = reinterpret_cast<WNDPROC>(
            ::SetWindowLongPtrW(_hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&_BorderlessWindowProc)));
        if (!_pOriginalProc)
        {
            return;
        }

        if (!::SetPropW(_hWindow, g_szOriginalWindowProcProperty, reinterpret_cast<HANDLE>(_pOriginalProc)))
        {
            ::SetWindowLongPtrW(_hWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(_pOriginalProc));
        }
    }

    void _ExecuteWindowCommand(IN CefRefPtr<CefBrowser> Browser_, IN const std::string& strCommand_)
    {
        HWND _hWindow = _GetTopLevelBrowserWindowHandle(Browser_);
        if (!_hWindow)
        {
            throw std::runtime_error("CEF browser window is not available");
        }

        if (strCommand_ == "minimize")
        {
            ::ShowWindow(_hWindow, SW_MINIMIZE);
            return;
        }

        if (strCommand_ == "maximize")
        {
            ::ShowWindow(_hWindow, ::IsZoomed(_hWindow) ? SW_RESTORE : SW_MAXIMIZE);
            return;
        }

        if (strCommand_ == "close")
        {
            Browser_->GetHost()->CloseBrowser(false);
            return;
        }

        throw std::invalid_argument("Unsupported window command: " + strCommand_);
    }

    void _BeginWindowDrag(IN CefRefPtr<CefBrowser> Browser_)
    {
        HWND _hWindow = _GetTopLevelBrowserWindowHandle(Browser_);
        if (!_hWindow)
        {
            throw std::runtime_error("CEF browser window is not available");
        }

        ::SetForegroundWindow(_hWindow);
        ::ReleaseCapture();
        ::SendMessageW(_hWindow, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
    }

    CefRect _MakeInitialWindowBounds()
    {
        RECT _WorkArea{};
        if (!::SystemParametersInfoW(SPI_GETWORKAREA, 0, &_WorkArea, 0))
        {
            _WorkArea = { 0, 0, 1280, 820 };
        }

        const int _WorkWidth = std::max(1L, _WorkArea.right - _WorkArea.left);
        const int _WorkHeight = std::max(1L, _WorkArea.bottom - _WorkArea.top);
        const int _Width = std::min(1320, _WorkWidth);
        const int _Height = std::min(860, _WorkHeight);
        const int _Left = _WorkArea.left + (_WorkWidth - _Width) / 2;
        const int _Top = _WorkArea.top + (_WorkHeight - _Height) / 2;
        return CefRect(_Left, _Top, _Width, _Height);
    }

    void _ShowInitialBrowserWindow(IN CefRefPtr<CefBrowser> Browser_)
    {
        _InstallBorderlessWindowProc(Browser_);

        HWND _hWindow = _GetTopLevelBrowserWindowHandle(Browser_);
        if (!_hWindow)
        {
            return;
        }

        ::ShowWindow(_hWindow, SW_MAXIMIZE);
        ::UpdateWindow(_hWindow);
    }

    json::object _ToJsonEnvelope(IN const iCAX::Frontend::CFrontendMailEnvelope& Envelope_)
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

    registerSceneChannel(projectId, sceneId) {
      return queryNative("registerSceneChannel", { projectId, sceneId });
    },

    postMail(mail) {
      return queryNative("postMail", { mail }).then(() => undefined);
    },

    openFileDialog(options) {
      return queryNative("openFileDialog", options || {});
    },

    windowCommand(command) {
      return queryNative("windowCommand", { command }).then(() => undefined);
    },

    beginWindowDrag() {
      return queryNative("beginWindowDrag").then(() => undefined);
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

    pdo: {
      withRead(descriptor, reader) {
        if (typeof window.__icaxPDOWithRead !== "function") {
          throw new Error("CEF PDO bridge is not available");
        }
        return window.__icaxPDOWithRead(descriptor, reader);
      },

      withWrite(descriptor, writer) {
        if (typeof window.__icaxPDOWithWrite !== "function") {
          throw new Error("CEF PDO bridge is not available");
        }
        return window.__icaxPDOWithWrite(descriptor, writer);
      }
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

    CefRefPtr<CefV8Value> _RequireV8ObjectField(
        IN CefRefPtr<CefV8Value> Object_,
        IN const char* szName_)
    {
        if (!Object_ || !Object_->IsObject())
        {
            throw std::invalid_argument("PDO descriptor must be an object");
        }

        auto _Value = Object_->GetValue(szName_);
        if (!_Value || _Value->IsUndefined() || _Value->IsNull())
        {
            throw std::invalid_argument(std::string("PDO descriptor field is required: ") + szName_);
        }
        return _Value;
    }

    std::string _V8ToString(IN CefRefPtr<CefV8Value> Value_, IN const char* szName_)
    {
        if (Value_ && Value_->IsString())
        {
            return Value_->GetStringValue().ToString();
        }
        if (Value_ && Value_->IsUInt())
        {
            return std::to_string(Value_->GetUIntValue());
        }
        if (Value_ && Value_->IsInt())
        {
            const auto _Value = Value_->GetIntValue();
            if (_Value < 0)
            {
                throw std::invalid_argument(std::string(szName_) + " must be unsigned");
            }
            return std::to_string(_Value);
        }
        throw std::invalid_argument(std::string(szName_) + " must be a string");
    }

    uint64_t _V8ToUInt64(IN CefRefPtr<CefV8Value> Value_, IN const char* szName_)
    {
        if (Value_ && Value_->IsString())
        {
            return std::stoull(Value_->GetStringValue().ToString());
        }
        if (Value_ && Value_->IsUInt())
        {
            return Value_->GetUIntValue();
        }
        if (Value_ && Value_->IsInt())
        {
            const auto _Value = Value_->GetIntValue();
            if (_Value < 0)
            {
                throw std::invalid_argument(std::string(szName_) + " must be unsigned");
            }
            return static_cast<uint64_t>(_Value);
        }
        if (Value_ && Value_->IsDouble())
        {
            const auto _Value = Value_->GetDoubleValue();
            if (_Value < 0)
            {
                throw std::invalid_argument(std::string(szName_) + " must be unsigned");
            }
            return static_cast<uint64_t>(_Value);
        }
        throw std::invalid_argument(std::string(szName_) + " must be an unsigned integer");
    }

    uint32_t _V8ToUInt32(IN CefRefPtr<CefV8Value> Value_, IN const char* szName_)
    {
        const auto _Value = _V8ToUInt64(Value_, szName_);
        if (_Value > UINT32_MAX)
        {
            throw std::out_of_range(std::string(szName_) + " exceeds uint32 range");
        }
        return static_cast<uint32_t>(_Value);
    }

    struct CPDOReadDescriptor final
    {
        std::wstring ArenaName;
        iCAX::PDO::PDOID nID = 0;
        uint32_t nVersion = 0;
        uint32_t nPayloadSize = 0;
    };

    struct CPDOWriteDescriptor final
    {
        std::wstring ArenaName;
        iCAX::PDO::PDOID nID = 0;
        uint32_t nVersion = 0;
        uint32_t nPayloadSize = 0;
        uint64_t nDataVersion = 0;
    };

    CPDOReadDescriptor _ParsePDOReadDescriptor(IN CefRefPtr<CefV8Value> Descriptor_)
    {
        CPDOReadDescriptor _Descriptor;
        _Descriptor.ArenaName = _UTF8ToWide(_V8ToString(_RequireV8ObjectField(Descriptor_, "arenaName"), "arenaName"));
        _Descriptor.nID = _V8ToUInt64(_RequireV8ObjectField(Descriptor_, "id"), "id");
        _Descriptor.nVersion = _V8ToUInt32(_RequireV8ObjectField(Descriptor_, "version"), "version");
        _Descriptor.nPayloadSize = _V8ToUInt32(_RequireV8ObjectField(Descriptor_, "payloadSize"), "payloadSize");

        if (_Descriptor.ArenaName.empty())
        {
            throw std::invalid_argument("PDO arenaName cannot be empty");
        }
        if (_Descriptor.nID == 0)
        {
            throw std::invalid_argument("PDO id cannot be zero");
        }
        if (_Descriptor.nVersion == 0)
        {
            throw std::invalid_argument("PDO version cannot be zero");
        }
        if (_Descriptor.nPayloadSize == 0)
        {
            throw std::invalid_argument("PDO payloadSize cannot be zero");
        }
        return _Descriptor;
    }

    CPDOWriteDescriptor _ParsePDOWriteDescriptor(IN CefRefPtr<CefV8Value> Descriptor_)
    {
        CPDOWriteDescriptor _Descriptor;
        _Descriptor.ArenaName = _UTF8ToWide(_V8ToString(_RequireV8ObjectField(Descriptor_, "arenaName"), "arenaName"));
        _Descriptor.nID = _V8ToUInt64(_RequireV8ObjectField(Descriptor_, "id"), "id");
        _Descriptor.nVersion = _V8ToUInt32(_RequireV8ObjectField(Descriptor_, "version"), "version");
        _Descriptor.nPayloadSize = _V8ToUInt32(_RequireV8ObjectField(Descriptor_, "payloadSize"), "payloadSize");
        _Descriptor.nDataVersion = _V8ToUInt64(_RequireV8ObjectField(Descriptor_, "dataVersion"), "dataVersion");

        if (_Descriptor.ArenaName.empty())
        {
            throw std::invalid_argument("PDO arenaName cannot be empty");
        }
        if (_Descriptor.nID == 0)
        {
            throw std::invalid_argument("PDO id cannot be zero");
        }
        if (_Descriptor.nVersion == 0)
        {
            throw std::invalid_argument("PDO version cannot be zero");
        }
        if (_Descriptor.nPayloadSize == 0)
        {
            throw std::invalid_argument("PDO payloadSize cannot be zero");
        }
        if (_Descriptor.nDataVersion == 0)
        {
            throw std::invalid_argument("PDO dataVersion cannot be zero");
        }
        return _Descriptor;
    }

    CefRefPtr<CefV8Value> _MakePDOReadMeta(
        IN const CPDOReadDescriptor& Descriptor_,
        IN const iCAX::PDO::CPDOReadLease& Lease_)
    {
        auto _Meta = CefV8Value::CreateObject(nullptr, nullptr);
        _Meta->SetValue("id", CefV8Value::CreateString(std::to_string(Descriptor_.nID)), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("version", CefV8Value::CreateUInt(Descriptor_.nVersion), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("payloadSize", CefV8Value::CreateUInt(Descriptor_.nPayloadSize), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("sequence", CefV8Value::CreateUInt(Lease_.Sequence()), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("bufferIndex", CefV8Value::CreateUInt(Lease_.BufferIndex()), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("dataVersion", CefV8Value::CreateString(std::to_string(Lease_.DataVersion())), V8_PROPERTY_ATTRIBUTE_READONLY);
        return _Meta;
    }

    CefRefPtr<CefV8Value> _MakePDOWriteMeta(IN const CPDOWriteDescriptor& Descriptor_)
    {
        auto _Meta = CefV8Value::CreateObject(nullptr, nullptr);
        _Meta->SetValue("id", CefV8Value::CreateString(std::to_string(Descriptor_.nID)), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("version", CefV8Value::CreateUInt(Descriptor_.nVersion), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("payloadSize", CefV8Value::CreateUInt(Descriptor_.nPayloadSize), V8_PROPERTY_ATTRIBUTE_READONLY);
        _Meta->SetValue("dataVersion", CefV8Value::CreateString(std::to_string(Descriptor_.nDataVersion)), V8_PROPERTY_ATTRIBUTE_READONLY);
        return _Meta;
    }

    class CPDOV8BridgeHandler final : public CefV8Handler
    {
    public:
        bool Execute(
            const CefString& Name_,
            CefRefPtr<CefV8Value>,
            const CefV8ValueList& Arguments_,
            CefRefPtr<CefV8Value>& ReturnValue_,
            CefString& Exception_) override
        {
            try
            {
                const auto _FunctionName = Name_.ToString();
                if (_FunctionName == "__icaxPDOWithRead")
                {
                    ReturnValue_ = ExecuteRead(Arguments_);
                    return true;
                }
                if (_FunctionName == "__icaxPDOWithWrite")
                {
                    ReturnValue_ = ExecuteWrite(Arguments_);
                    return true;
                }
                return false;
            }
            catch (const std::exception& _Error)
            {
                Exception_ = _Error.what();
                return true;
            }
        }

    private:
        CefRefPtr<CefV8Value> ExecuteRead(IN const CefV8ValueList& Arguments_)
        {
            if (Arguments_.size() != 2)
            {
                throw std::invalid_argument("PDO withRead expects descriptor and reader arguments");
            }
            if (!Arguments_[1] || !Arguments_[1]->IsFunction())
            {
                throw std::invalid_argument("PDO reader must be a function");
            }

            const auto _Descriptor = _ParsePDOReadDescriptor(Arguments_[0]);
            auto _pArena = OpenArena(_Descriptor.ArenaName);
            auto _Slot = _pArena->GetSlot(_Descriptor.nID);
            ValidateSlot(_Slot, _Descriptor, iCAX::PDO::kDirection2External);

            iCAX::PDO::CPDOReadLease _Lease(_Slot);
            // Official CEF binaries enable the V8 sandbox. V8 therefore cannot wrap
            // arbitrary process memory as an external ArrayBuffer; copy the current
            // shared-memory snapshot into V8-owned backing storage.
            auto _ArrayBuffer = CefV8Value::CreateArrayBufferWithCopy(
                const_cast<void*>(_Lease.Data()),
                _Descriptor.nPayloadSize);
            if (!_ArrayBuffer)
            {
                throw std::runtime_error("Failed to create PDO ArrayBuffer");
            }
            auto _Meta = _MakePDOReadMeta(_Descriptor, _Lease);

            CefV8ValueList _ReaderArguments;
            _ReaderArguments.push_back(_ArrayBuffer);
            _ReaderArguments.push_back(_Meta);
            auto _Result = Arguments_[1]->ExecuteFunction(nullptr, _ReaderArguments);

            if (!_Result)
            {
                throw std::runtime_error("PDO reader callback failed");
            }
            if (_Result->IsPromise())
            {
                throw std::logic_error("PDO reader callback must be synchronous");
            }
            return _Result;
        }

        CefRefPtr<CefV8Value> ExecuteWrite(IN const CefV8ValueList& Arguments_)
        {
            if (Arguments_.size() != 2)
            {
                throw std::invalid_argument("PDO withWrite expects descriptor and writer arguments");
            }
            if (!Arguments_[1] || !Arguments_[1]->IsFunction())
            {
                throw std::invalid_argument("PDO writer must be a function");
            }

            const auto _Descriptor = _ParsePDOWriteDescriptor(Arguments_[0]);
            auto _pArena = OpenArena(_Descriptor.ArenaName);
            auto _Slot = _pArena->GetSlot(_Descriptor.nID);
            ValidateSlot(_Slot, _Descriptor, iCAX::PDO::kDirection2Inner);

            std::vector<uint8_t> _Scratch(_Descriptor.nPayloadSize, 0);
            auto _ArrayBuffer = CefV8Value::CreateArrayBufferWithCopy(_Scratch.data(), _Scratch.size());
            if (!_ArrayBuffer)
            {
                throw std::runtime_error("Failed to create PDO ArrayBuffer");
            }

            CefV8ValueList _WriterArguments;
            _WriterArguments.push_back(_ArrayBuffer);
            _WriterArguments.push_back(_MakePDOWriteMeta(_Descriptor));
            auto _Result = Arguments_[1]->ExecuteFunction(nullptr, _WriterArguments);
            if (!_Result)
            {
                throw std::runtime_error("PDO writer callback failed");
            }
            if (_Result->IsPromise())
            {
                throw std::logic_error("PDO writer callback must be synchronous");
            }
            if (!_ArrayBuffer->IsArrayBuffer() || _ArrayBuffer->GetArrayBufferByteLength() != _Descriptor.nPayloadSize)
            {
                throw std::logic_error("PDO writer ArrayBuffer size changed");
            }

            auto _WriteLease = iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(_Slot, _Descriptor.nDataVersion);
            if (!_WriteLease)
            {
                return CefV8Value::CreateBool(false);
            }

            const auto _pData = _ArrayBuffer->GetArrayBufferData();
            if (!_pData)
            {
                throw std::runtime_error("PDO writer ArrayBuffer has no backing data");
            }
            std::memcpy(_WriteLease->Data(), _pData, _Descriptor.nPayloadSize);
            _WriteLease->Commit();
            return CefV8Value::CreateBool(true);
        }

        std::shared_ptr<iCAX::PDO::CSharedPDOArena> OpenArena(IN const std::wstring& ArenaName_)
        {
            std::lock_guard<std::mutex> _Lock(m_Mutex);
            auto _Iter = m_Arenas.find(ArenaName_);
            if (_Iter != m_Arenas.end())
            {
                return _Iter->second;
            }

            auto _pArena = iCAX::PDO::CSharedPDOArena::Open(ArenaName_);
            m_Arenas.emplace(ArenaName_, _pArena);
            return _pArena;
        }

        template <typename TDescriptor>
        static void ValidateSlot(
            IN const iCAX::PDO::CSharedPDOSlot& Slot_,
            IN const TDescriptor& Descriptor_,
            IN iCAX::PDO::PDODirection eExpectedDirection_)
        {
            const auto& _Decl = Slot_.GetHeader();
            if (_Decl.nID != Descriptor_.nID)
            {
                throw std::logic_error("PDO slot id does not match descriptor");
            }
            if (_Decl.nVersion != Descriptor_.nVersion)
            {
                throw std::logic_error("PDO slot version does not match descriptor");
            }
            if (_Decl.nPayloadSize != Descriptor_.nPayloadSize)
            {
                throw std::logic_error("PDO slot payload size does not match descriptor");
            }
            if (_Decl.eDirection != eExpectedDirection_)
            {
                throw std::logic_error("PDO slot direction does not match bridge access mode");
            }
        }

    private:
        std::mutex m_Mutex;
        std::unordered_map<std::wstring, std::shared_ptr<iCAX::PDO::CSharedPDOArena>> m_Arenas;

        IMPLEMENT_REFCOUNTING(CPDOV8BridgeHandler);
    };

    void _InstallPDOV8Bridge(IN CefRefPtr<CefV8Context> Context_)
    {
        if (!Context_)
        {
            return;
        }

        auto _Global = Context_->GetGlobal();
        if (!_Global)
        {
            return;
        }

        auto _Handler = new CPDOV8BridgeHandler();
        auto _ReadFunction = CefV8Value::CreateFunction("__icaxPDOWithRead", _Handler);
        auto _WriteFunction = CefV8Value::CreateFunction("__icaxPDOWithWrite", _Handler);
        _Global->SetValue("__icaxPDOWithRead", _ReadFunction, V8_PROPERTY_ATTRIBUTE_DONTENUM);
        _Global->SetValue("__icaxPDOWithWrite", _WriteFunction, V8_PROPERTY_ATTRIBUTE_DONTENUM);
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

        void OnBeforeCommandLineProcessing(
            const CefString&,
            CefRefPtr<CefCommandLine> CommandLine_) override
        {
            if (g_bAllowFileAccessFromFiles.load())
            {
                CommandLine_->AppendSwitch("allow-file-access-from-files");
            }
            if (g_bDisableGPU.load())
            {
                CommandLine_->AppendSwitch("disable-gpu");
                CommandLine_->AppendSwitch("disable-gpu-compositing");
            }
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
                _InstallPDOV8Bridge(Context_);
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
        explicit CInternalBridgeQueryHandler(IN iCAX::Frontend::IFrontendBridge* pBridge_)
            : m_pBridge(pBridge_)
        {
        }

        bool OnQuery(
            CefRefPtr<CefBrowser> Browser_,
            CefRefPtr<CefFrame>,
            int64_t,
            const CefString& Request_,
            bool,
            CefRefPtr<Callback> Callback_) override
        {
            try
            {
                if (!m_pBridge)
                {
                    throw std::runtime_error("FrontendBridge is not available");
                }

                const auto _RootValue = json::parse(Request_.ToString());
                const auto& _Root = _AsObject(_RootValue, "request");
                const auto _Method = _AsString(_RequireField(_Root, "method"), "method");
                const auto& _Payload = _AsObject(_RequireField(_Root, "payload"), "payload");

                if (_Method == "getApplicationChannelId")
                {
                    Callback_->Success(json::serialize(_ToJsonValue(m_pBridge->GetApplicationChannelIDText())));
                    return true;
                }

                if (_Method == "registerProductChannel")
                {
                    const auto _ProductID = _AsString(_RequireField(_Payload, "productId"), "productId");
                    Callback_->Success(json::serialize(_ToJsonValue(m_pBridge->RegisterProductChannel(_ProductID))));
                    return true;
                }

                if (_Method == "registerSceneChannel")
                {
                    const auto _ProjectID = _AsString(_RequireField(_Payload, "projectId"), "projectId");
                    const auto _SceneID = _AsString(_RequireField(_Payload, "sceneId"), "sceneId");
                    Callback_->Success(json::serialize(_ToJsonValue(m_pBridge->RegisterSceneChannel(_ProjectID, _SceneID))));
                    return true;
                }

                if (_Method == "postMail")
                {
                    const auto& _Mail = _AsObject(_RequireField(_Payload, "mail"), "mail");
                    m_pBridge->PostMail(_ParseEnvelope(_Mail));
                    Callback_->Success("null");
                    return true;
                }

                if (_Method == "openFileDialog")
                {
                    const auto _Path = _OpenFileDialog(Browser_, _Payload);
                    if (_Path.has_value())
                    {
                        Callback_->Success(json::serialize(_ToJsonValue(*_Path)));
                    }
                    else
                    {
                        Callback_->Success("null");
                    }
                    return true;
                }

                if (_Method == "windowCommand")
                {
                    const auto _Command = _AsString(_RequireField(_Payload, "command"), "command");
                    _ExecuteWindowCommand(Browser_, _Command);
                    Callback_->Success("null");
                    return true;
                }

                if (_Method == "beginWindowDrag")
                {
                    _BeginWindowDrag(Browser_);
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
        iCAX::Frontend::IFrontendBridge* m_pBridge = nullptr;
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

    class CCloseBrowserTask final : public CefTask
    {
    public:
        explicit CCloseBrowserTask(CefRefPtr<CInternalCefClient> pClient_)
            : m_pClient(std::move(pClient_))
        {
        }

        void Execute() override;

    private:
        CefRefPtr<CInternalCefClient> m_pClient;

        IMPLEMENT_REFCOUNTING(CCloseBrowserTask);
    };

    class CInternalCefClient final :
        public CefClient,
        public CefDisplayHandler,
        public CefLifeSpanHandler,
        public CefLoadHandler,
        public CefRequestHandler
    {
    public:
        explicit CInternalCefClient(
            IN iCAX::Frontend::IFrontendBridge* pBridge_,
            IN std::function<void()> OnBrowserClosed_)
            : m_OnBrowserClosed(std::move(OnBrowserClosed_))
        {
            m_pBridgeQueryHandler = std::make_unique<CInternalBridgeQueryHandler>(pBridge_);
        }

        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
        {
            return this;
        }

        CefRefPtr<CefDisplayHandler> GetDisplayHandler() override
        {
            return this;
        }

        CefRefPtr<CefLoadHandler> GetLoadHandler() override
        {
            return this;
        }

        CefRefPtr<CefRequestHandler> GetRequestHandler() override
        {
            return this;
        }

        void OnAfterCreated(CefRefPtr<CefBrowser> Browser_) override
        {
            CEF_REQUIRE_UI_THREAD();
            EnsureMessageRouterOnUI();
            m_pBrowser = Browser_;
            _ShowInitialBrowserWindow(Browser_);
        }

        void OnTitleChange(CefRefPtr<CefBrowser> Browser_, const CefString& Title_) override
        {
            CEF_REQUIRE_UI_THREAD();
            if (Browser_ && Browser_->GetHost())
            {
                HWND _hWindow = Browser_->GetHost()->GetWindowHandle();
                if (_hWindow)
                {
                    SetWindowTextW(_hWindow, Title_.ToWString().c_str());
                }
            }
        }

        void OnBeforeClose(CefRefPtr<CefBrowser> Browser_) override
        {
            CEF_REQUIRE_UI_THREAD();
            EnsureMessageRouterOnUI();
            if (m_pMessageRouter)
            {
                m_pMessageRouter->OnBeforeClose(Browser_);
            }
            m_pBrowser = nullptr;
            if (m_OnBrowserClosed)
            {
                m_OnBrowserClosed();
            }
        }

        bool OnBeforeBrowse(
            CefRefPtr<CefBrowser> Browser_,
            CefRefPtr<CefFrame> Frame_,
            CefRefPtr<CefRequest>,
            bool,
            bool) override
        {
            CEF_REQUIRE_UI_THREAD();
            EnsureMessageRouterOnUI();
            if (m_pMessageRouter)
            {
                m_pMessageRouter->OnBeforeBrowse(Browser_, Frame_);
            }
            return false;
        }

        void OnRenderProcessTerminated(
            CefRefPtr<CefBrowser> Browser_,
            TerminationStatus,
            int,
            const CefString&) override
        {
            CEF_REQUIRE_UI_THREAD();
            EnsureMessageRouterOnUI();
            if (m_pMessageRouter)
            {
                m_pMessageRouter->OnRenderProcessTerminated(Browser_);
            }
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
            EnsureMessageRouterOnUI();
            return m_pMessageRouter
                && m_pMessageRouter->OnProcessMessageReceived(Browser_, Frame_, SourceProcess_, Message_);
        }

        void CloseBrowser()
        {
            auto _TaskRunner = CefTaskRunner::GetForThread(TID_UI);
            if (!_TaskRunner)
            {
                return;
            }
            _TaskRunner->PostTask(new CCloseBrowserTask(this));
        }

        void CloseBrowserOnUI()
        {
            CEF_REQUIRE_UI_THREAD();
            if (m_pBrowser && m_pBrowser->GetHost())
            {
                m_pBrowser->GetHost()->CloseBrowser(false);
            }
        }

        void DispatchMail(IN const iCAX::Frontend::CFrontendMailEnvelope& Envelope_)
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
        void EnsureMessageRouterOnUI()
        {
            CEF_REQUIRE_UI_THREAD();
            if (m_pMessageRouter)
            {
                return;
            }

            CefMessageRouterConfig _Config;
            m_pMessageRouter = CefMessageRouterBrowserSide::Create(_Config);
            m_pMessageRouter->AddHandler(m_pBridgeQueryHandler.get(), false);
        }

    private:
        CefRefPtr<CefBrowser> m_pBrowser;
        CefRefPtr<CefMessageRouterBrowserSide> m_pMessageRouter;
        std::unique_ptr<CInternalBridgeQueryHandler> m_pBridgeQueryHandler;
        std::function<void()> m_OnBrowserClosed;

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

    void CCloseBrowserTask::Execute()
    {
        if (m_pClient)
        {
            m_pClient->CloseBrowserOnUI();
        }
    }

    CefRefPtr<CInternalCefApp> _CreateCefApp()
    {
        return new CInternalCefApp();
    }

    std::string _GetProperty(
        IN const iCAX::Frontend::CUIContainerConfig& Config_,
        IN const std::string& strName_,
        IN const std::string& strDefaultValue_)
    {
        for (const auto& _Property : Config_.Properties)
        {
            if (_Property.first == strName_)
            {
                return _Property.second;
            }
        }
        return strDefaultValue_;
    }

    int _GetIntProperty(
        IN const iCAX::Frontend::CUIContainerConfig& Config_,
        IN const std::string& strName_,
        IN int nDefaultValue_)
    {
        const auto _Text = _GetProperty(Config_, strName_, {});
        if (_Text.empty())
        {
            return nDefaultValue_;
        }
        return std::stoi(_Text);
    }

    std::string _ToLowerAscii(IN std::string Text_)
    {
        std::transform(Text_.begin(), Text_.end(), Text_.begin(), [](IN unsigned char Ch_) {
            return static_cast<char>(std::tolower(Ch_));
        });
        return Text_;
    }

    bool _GetBoolProperty(
        IN const iCAX::Frontend::CUIContainerConfig& Config_,
        IN const std::string& strName_,
        IN bool bDefaultValue_)
    {
        auto _Text = _ToLowerAscii(_GetProperty(Config_, strName_, {}));
        if (_Text.empty())
        {
            return bDefaultValue_;
        }
        return _Text == "1" || _Text == "true" || _Text == "yes" || _Text == "on";
    }

    std::wstring _GetWideProperty(
        IN const iCAX::Frontend::CUIContainerConfig& Config_,
        IN const std::string& strName_)
    {
        return _UTF8ToWide(_GetProperty(Config_, strName_, {}));
    }

    int __cdecl _ExecuteCefUISubProcess(IN void* pNativeInstanceHandle_)
    {
        return iCAX::Frontend::Cef::CCefUIContainer::ExecuteSubProcess(static_cast<HINSTANCE>(pNativeInstanceHandle_));
    }
}

class iCAX::Frontend::Cef::CCefUIContainer::Impl final
{
public:
    iCAX::Frontend::CUIContainerConfig Config;
    iCAX::Frontend::IFrontendBridge* pBridge = nullptr;
    CefRefPtr<CInternalCefClient> Client;
    std::thread PollThread;
    std::mutex ExitMutex;
    std::condition_variable ExitCondition;
    std::atomic_bool bStopPolling = true;
    bool bExitRequested = false;
    bool bStarted = false;

public:
    void ResetExitState()
    {
        std::lock_guard<std::mutex> _Lock(ExitMutex);
        bExitRequested = false;
    }

    void NotifyExit()
    {
        {
            std::lock_guard<std::mutex> _Lock(ExitMutex);
            bExitRequested = true;
        }
        ExitCondition.notify_all();
    }

    void WaitForExit()
    {
        std::unique_lock<std::mutex> _Lock(ExitMutex);
        ExitCondition.wait(_Lock, [this]() { return bExitRequested || !bStarted; });
    }

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

        auto _Mails = pBridge->PollMails();
        for (const auto& _Mail : _Mails)
        {
            Client->DispatchMail(_Mail);
        }
    }
};

iCAX::Frontend::Cef::CCefUIContainer::CCefUIContainer()
    : m_pImpl(std::make_unique<Impl>())
{
}

iCAX::Frontend::Cef::CCefUIContainer::~CCefUIContainer()
{
    if (IsRunning())
    {
        Stop();
    }
}

int iCAX::Frontend::Cef::CCefUIContainer::ExecuteSubProcess(IN HINSTANCE hInstance_)
{
    CefMainArgs _MainArgs(hInstance_);
    return CefExecuteProcess(_MainArgs, _CreateCefApp(), nullptr);
}

void iCAX::Frontend::Cef::CCefUIContainer::InitializeRuntime(IN const CCefRuntimeConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(g_CefRuntimeMutex);
    if (g_bCefRuntimeInitialized.load())
    {
        return;
    }

    CefMainArgs _MainArgs(Config_.hInstance);
    CefSettings _Settings;
    g_bAllowFileAccessFromFiles = Config_.bAllowFileAccessFromFiles;
    g_bDisableGPU = Config_.bDisableGPU;
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
        CefString(&_Settings.root_cache_path).FromWString(Config_.UserDataPath);
    }
    if (!Config_.LogFile.empty())
    {
        CefString(&_Settings.log_file).FromWString(Config_.LogFile);
    }
    if (Config_.nRemoteDebuggingPort > 0)
    {
        _Settings.remote_debugging_port = Config_.nRemoteDebuggingPort;
    }

    if (!CefInitialize(_MainArgs, _Settings, _CreateCefApp(), nullptr))
    {
        throw std::runtime_error("CefInitialize failed");
    }

    g_bCefRuntimeInitialized = true;
}

void iCAX::Frontend::Cef::CCefUIContainer::ShutdownRuntime()
{
    std::lock_guard<std::mutex> _Lock(g_CefRuntimeMutex);
    if (!g_bCefRuntimeInitialized.load())
    {
        return;
    }

    CefShutdown();
    g_bCefRuntimeInitialized = false;
}

void iCAX::Frontend::Cef::CCefUIContainer::SetConfig(IN const CUIContainerConfig& Config_)
{
    if (m_pImpl->bStarted)
    {
        throw std::logic_error("CefUIContainer config cannot be changed after start");
    }
    m_pImpl->Config = Config_;
    m_pImpl->pBridge = Config_.pFrontendBridge;
}

void iCAX::Frontend::Cef::CCefUIContainer::Start()
{
    if (m_pImpl->bStarted)
    {
        return;
    }
    if (!m_pImpl->pBridge)
    {
        throw std::logic_error("CefUIContainer requires a FrontendBridge");
    }
    if (!m_pImpl->pBridge->IsAttached())
    {
        throw std::logic_error("CefUIContainer requires an attached FrontendBridge");
    }

    if (!g_bCefRuntimeInitialized.load())
    {
        CCefRuntimeConfig _RuntimeConfig;
        _RuntimeConfig.hInstance = GetModuleHandleW(nullptr);
        _RuntimeConfig.BrowserSubprocessPath = _GetWideProperty(m_pImpl->Config, "browserSubprocessPath");
        _RuntimeConfig.CachePath = _GetWideProperty(m_pImpl->Config, "cachePath");
        _RuntimeConfig.UserDataPath = _GetWideProperty(m_pImpl->Config, "userDataPath");
        _RuntimeConfig.LogFile = _GetWideProperty(m_pImpl->Config, "logFile");
        _RuntimeConfig.nRemoteDebuggingPort = _GetIntProperty(m_pImpl->Config, "remoteDebuggingPort", 0);
        _RuntimeConfig.bAllowFileAccessFromFiles = _GetBoolProperty(m_pImpl->Config, "allowFileAccessFromFiles", true);
        _RuntimeConfig.bDisableGPU = _GetBoolProperty(m_pImpl->Config, "disableGpu", false);
        InitializeRuntime(_RuntimeConfig);
    }

    m_pImpl->ResetExitState();
    m_pImpl->Client = new CInternalCefClient(m_pImpl->pBridge, [this]() {
        m_pImpl->NotifyExit();
    });

    CefWindowInfo _WindowInfo;
    _WindowInfo.SetAsPopup(nullptr, L"iCAX");
    _WindowInfo.style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    _WindowInfo.ex_style = WS_EX_APPWINDOW;
    _WindowInfo.bounds = _MakeInitialWindowBounds();
    _WindowInfo.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    CefBrowserSettings _BrowserSettings;
    const auto _StartURL = _ResolveStartURL(m_pImpl->Config);
    if (!CefBrowserHost::CreateBrowser(_WindowInfo, m_pImpl->Client, _StartURL, _BrowserSettings, nullptr, nullptr))
    {
        m_pImpl->Client = nullptr;
        throw std::runtime_error("CefBrowserHost::CreateBrowser failed");
    }

    m_pImpl->StartPolling(_GetIntProperty(m_pImpl->Config, "mailPollIntervalMS", 16));
    m_pImpl->bStarted = true;
}

void iCAX::Frontend::Cef::CCefUIContainer::Stop()
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

    m_pImpl->bStarted = false;
    m_pImpl->NotifyExit();
}

void iCAX::Frontend::Cef::CCefUIContainer::WaitForExit()
{
    m_pImpl->WaitForExit();
}

bool iCAX::Frontend::Cef::CCefUIContainer::IsRunning() const
{
    return m_pImpl->bStarted;
}

void iCAX::Frontend::Cef::CCefUIContainer::PollMails()
{
    m_pImpl->PollMails();
}

ICAX_REGISTER_UI_CONTAINER_WITH_SUBPROCESS("cef", iCAX::Frontend::Cef::CCefUIContainer, &_ExecuteCefUISubProcess)
